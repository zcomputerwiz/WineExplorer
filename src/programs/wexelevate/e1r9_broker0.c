/*
 * WineExplorer elevation: the broker in session 0 (E1-S18e R9, lab only)
 *
 * Copyright 2026 WineExplorer contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/*
 * docs/E1_S18E_R9_SPEC.md. In session 0 wexelevate is B0: it decides. It serves \pipe\wexelevate.r9 (SYSTEM only;
 * the sessions' agents reach it through their server's bridge), applies the policy and the attempt count, makes
 * each attempt's tuple and every frame for the dialog, checks every frame of the dialog, talks to the verifier
 * from START to RELEASE on its own S6 channel and gives the grant. It never touches an object of a session: the
 * agent A creates the dialog, relays the frames, asks its server for the rechecks, redeems and launches.
 *
 * Included at the end of main.c, after e1s18_broker.c, whose helpers it uses.
 *
 * Limits of this version (stated in the spec, section 12):
 *  - one request is served at a time; a second session's request gets DECISION deny (busy);
 *  - the attempt count is per request (3); the per-account count across sessions is the verifier's lockout policy;
 *  - while B0 waits for the verifier it does not read the agent's pipe: the agent's cancellation reaches it through
 *    the verifier (the agent's channel loss ends the attempt there, and the verifier tells B0).
 */

#if defined(_WIN32) && defined(WEXELEVATE_E1S18)

#define R9B_PIPE_NAME   L"\\\\.\\pipe\\wexelevate.r9"
#define R9B_INSTANCES   8
#define R9B_FRAME_MAX   2400    /* above every A <-> B0 message (TO_D is the largest, 2143) */
#define R9B_IO_TIMEOUT  10000   /* a write to the agent, and its first message */
#define R9B_SPAWN_TIMEOUT 30000 /* the agent creates the dialog (and the secure desktop) */
#define R9B_LAUNCH_TIMEOUT 120000

static LONG r9b_busy;       /* the one request in flight */
static LONG r9b_threads;

struct r9b
{
    HANDLE pipe;
    struct request vreq;            /* only pipe and deadline: what the S6 transport helpers read */
    struct e1r8_tuple t;            /* the attempt's tuple; sealed once the agent sent the dialog id */
    struct e1r8_request rq;         /* the agent's REQUEST; its strings point into request[] */
    uint8_t request[1792];
    char owner[256];                /* the verifier's bus name, for INIT */
    BOOL channel_open;
    BOOL started;                   /* START was sent for this attempt: the verifier holds a worker */
};

static void r9b_trace( const char *step )
{
    char text[96];
    snprintf( text, sizeof(text), "r9b: %s", step );
    r8_trace_line( text );
}

static void r9b_trace_err( const char *step, DWORD err )
{
    char text[96];
    snprintf( text, sizeof(text), "r9b: %s failed %lu", step, (unsigned long)err );
    r8_trace_line( text );
}

/* Milliseconds left until the request's deadline, at most the given bound. */
static DWORD r9b_left( const struct r9b *b, DWORD bound )
{
    DWORD left = r8_remaining( &b->vreq );
    return left < bound ? left : bound;
}

static DWORD r9b_send( struct r9b *b, uint8_t *frame, size_t len )
{
    DWORD done = 0, err = pipe_io( b->pipe, frame, (DWORD)len, TRUE, R9B_IO_TIMEOUT, &done );
    if (!err && done != len) err = ERROR_BROKEN_PIPE;
    return err;
}

/* One message from the agent. Its CANCEL, at any point, ends the request. */
static DWORD r9b_recv( struct r9b *b, uint8_t frame[R9B_FRAME_MAX], size_t *len, DWORD timeout )
{
    DWORD done = 0, err;

    if (!timeout) return ERROR_TIMEOUT;
    if ((err = pipe_io( b->pipe, frame, R9B_FRAME_MAX, FALSE, timeout, &done ))) return err;
    if (done < E1R8_HEADER_SIZE) return ERROR_INVALID_DATA;
    *len = done;
    if (!memcmp( frame, "E1R8", 4 ) && e1r8_get_u16( frame + 6 ) == E1R8_TYPE_CANCEL_R9)
    {
        r9b_trace( "agent cancelled" );
        return ERROR_CANCELLED;
    }
    return ERROR_SUCCESS;
}

/* One R8 frame for the dialog, through the agent. */
static DWORD r9b_to_d( struct r9b *b, const struct e1r8_tuple *t, const uint8_t *inner, uint32_t inner_len )
{
    uint8_t frame[R9B_FRAME_MAX];
    DWORD err;

    if (E1R8_HEADER_SIZE + inner_len > sizeof(frame) ||
        e1r8_encode_to_d( frame, E1R8_HEADER_SIZE + inner_len, t, inner, inner_len )) return ERROR_INVALID_DATA;
    err = r9b_send( b, frame, E1R8_HEADER_SIZE + inner_len );
    e1s18_wipe( frame, sizeof(frame) );
    return err;
}

/* One R8 frame of the dialog, as the agent read it; the caller applies R8's checks to it. */
static DWORD r9b_from_d( struct r9b *b, uint16_t type, uint8_t inner[340], uint32_t *inner_len, DWORD timeout )
{
    uint8_t frame[R9B_FRAME_MAX];
    struct e1r8_from_d from;
    size_t len = 0;
    DWORD err;

    if ((err = r9b_recv( b, frame, &len, timeout ))) return err;
    if (e1r8_decode_from_d( frame, len, &b->t, &from ) || from.type != type || from.frame_len > 340)
        err = ERROR_INVALID_DATA;
    else
    {
        memcpy( inner, from.frame, from.frame_len );
        *inner_len = from.frame_len;
    }
    e1s18_wipe( frame, sizeof(frame) );
    return err;
}

/* The agent asks its server to recheck the dialog; nothing goes on without its ok for exactly this step. */
static DWORD r9b_recheck( struct r9b *b, uint32_t step )
{
    uint8_t frame[R9B_FRAME_MAX];
    struct e1r8_rechecked done;
    size_t len = 0;
    DWORD err;

    if (e1r8_encode_recheck( frame, 80, &b->t, step )) return ERROR_INVALID_DATA;
    if ((err = r9b_send( b, frame, 80 ))) return err;
    if ((err = r9b_recv( b, frame, &len, r9b_left( b, R9B_IO_TIMEOUT ) ))) return err;
    if (e1r8_decode_rechecked( frame, len, &b->t, &done ) || done.step != step) return ERROR_INVALID_DATA;
    return done.status == E1R8_RECHECKED_OK ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
}

static void r9b_end( struct r9b *b, const struct e1r8_tuple *t, uint32_t outcome )
{
    uint8_t frame[80];
    if (!e1r8_encode_end( frame, 80, t, outcome )) r9b_send( b, frame, 80 );
}

/* B0's own S6 channel: session 0's server endorses this process (it has no caller to bind), the verifier calls
 * the channel a B0 channel. Kept open for the request, as the R8 broker keeps its channel for the attempt. */
static DWORD r9b_open_channel( struct r9b *b )
{
    const struct e1s18_ops *ops = e1s18_get_ops();
    unsigned char reg[E1S6_OPEN_SIZE];
    UINT64 channel = 0;
    DWORD err;

    if (!ops || !ops->r8_open || !ops->r8_owner || !ops->r8_close) return ERROR_NOT_SUPPORTED;
    memset( reg, 0, sizeof(reg) );
    if ((err = ops->r8_open( &b->vreq, reg, &channel ))) return err;
    if (!channel) return ERROR_ACCESS_DENIED;
    memset( b->owner, 0, sizeof(b->owner) );
    if (!(err = ops->r8_owner( &b->vreq, channel, b->owner )) &&
        !e1r8_valid_verifier_owner( (const uint8_t *)b->owner, strlen( b->owner ) )) err = ERROR_ACCESS_DENIED;
    if (err)
    {
        ops->r8_close( &b->vreq, channel );
        return err;
    }
    s_r8_channel = channel;
    b->channel_open = TRUE;
    return ERROR_SUCCESS;
}

static void r9b_close_channel( struct r9b *b )
{
    if (b->channel_open) e1s18_r8_release( &b->vreq );   /* ends whatever the verifier still holds for it */
    b->channel_open = FALSE;
}

/* The verifier's attempt ends without a result to keep. Best effort: closing the channel does the same. */
static void r9b_release( struct r9b *b, uint32_t outcome )
{
    uint8_t frame[E1S6_R8_FRAME_MAX];
    size_t len = 0;

    if (!b->started) return;
    b->started = FALSE;
    if (!e1r8_encode_release( frame, 80, &b->t, outcome ) && !r8_v_send( &b->vreq, frame, 80 ) &&
        !r8_v_recv( &b->vreq, frame, &len, 5000 ) && !e1r8_decode_released( frame, len, &b->t ))
        r9b_trace( "released" );
}

/* The client is a session's SYSTEM (the bridge described its token; session 0 bound it to the peer server's
 * account), and session 0's server names the session it came from. */
static BOOL r9b_client_ok( HANDLE pipe, DWORD *session )
{
    BYTE user[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    PSID system_sid = NULL;
    HANDLE token = NULL;
    BOOL ok = FALSE;

    *session = 0;
    if (!ImpersonateNamedPipeClient( pipe )) return FALSE;
    if (!OpenThreadToken( GetCurrentThread(), TOKEN_QUERY, TRUE, &token )) token = NULL;
    RevertToSelf();
    if (!token) return FALSE;
    if (ConvertStringSidToSidW( L"S-1-5-18", &system_sid ))
    {
        ok = token_user_sid( token, user, sizeof(user) ) && EqualSid( ((TOKEN_USER *)user)->User.Sid, system_sid );
        LocalFree( system_sid );
    }
    CloseHandle( token );
    if (ok && (!GetNamedPipeClientSessionId( pipe, session ) || !*session)) ok = FALSE;
    return ok;
}

enum r9b_result { R9B_GRANTED, R9B_REFUSED, R9B_LOCKED, R9B_FAILED };

/*
 * One attempt, from the agent's ENDORSED_LOCAL to the grant: R8's broker steps (e1s18_spawn_consent_dialog), with
 * every frame for the dialog sent through the agent and every server recheck asked of it.
 * R9B_GRANTED: account, key and evidence reference are set and the verifier has granted.
 * R9B_REFUSED: the password was refused, the dialog has RESULT 2 (not yet DONE) and the verifier's attempt is
 * released; the caller decides on a further attempt.
 */
static enum r9b_result r9b_attempt( struct r9b *b, char account[64], uint8_t key[16], uint8_t ref[16], DWORD *error )
{
    static const uint8_t zero32[32];
    /* frame: one frame to or from the verifier; msg: one message of the agent (larger) */
    uint8_t frame[E1S6_R8_FRAME_MAX], msg[R9B_FRAME_MAX], inner[2067], from[340], challenge[32], fresh_key[16], ready[76];
    struct e1r8_endorsed_local endorsed;
    struct e1r8_ack ack;
    struct e1r8_select sel;
    struct e1r8_result_v vres;
    struct e1r8_answer ans;
    const struct e1s18_ops *ops = e1s18_get_ops();
    enum r9b_result result = R9B_FAILED;
    uint32_t inner_len = 0, vo_len, total;
    size_t len = 0, account_len = 0;
    const char *step;
    DWORD err = ERROR_ACCESS_DENIED;
    BOOL locked;

    /* The agent created the dialog for the tuple and its server endorsed it: the dialog id seals the tuple. */
    step = "endorsed";
    if ((err = r9b_recv( b, msg, &len, r9b_left( b, R9B_SPAWN_TIMEOUT ) ))) goto fail;
    err = ERROR_ACCESS_DENIED;
    if (e1r8_decode_endorsed_local( msg, len, &b->t, &endorsed ) ||
        endorsed.status != E1R8_ENDORSED_LOCAL_ENDORSED || e1r8_is_zero16( endorsed.dialog_id )) goto fail;
    memcpy( b->t.dialog_id, endorsed.dialog_id, 16 );
    r9b_trace( "endorsed" );

    /* INIT / ACK. The display strings are the agent's (it is trusted for them, spec section 1). */
    step = "init";
    vo_len = (uint32_t)strlen( b->owner );
    total = E1R8_HEADER_SIZE + E1R8_TAIL_SIZE + (4 + vo_len) + (4 + b->rq.requester_len) +
            (4 + b->rq.image_len) + (4 + b->rq.publisher_len);
    if (total > sizeof(inner) ||
        e1r8_encode_init( inner, total, &b->t, (const uint8_t *)b->owner, vo_len, b->rq.requester, b->rq.requester_len,
                          b->rq.image, b->rq.image_len, b->rq.publisher, b->rq.publisher_len )) goto fail;
    if ((err = r9b_to_d( b, &b->t, inner, total ))) goto fail;
    step = "ack";
    if ((err = r9b_from_d( b, E1R8_TYPE_ACK, from, &inner_len, r9b_left( b, consent_timeout() ) ))) goto fail;
    err = ERROR_ACCESS_DENIED;
    if (e1r8_decode_ack( from, inner_len, &b->t, &ack )) goto fail;
    step = "recheck attach";
    if ((err = r9b_recheck( b, E1R8_RECHECK_STEP_ATTACH ))) goto fail;
    r9b_trace( "ack ok" );

    /* SELECT_OK / SELECT: the person names the account. */
    step = "select";
    if (e1r8_encode_select_ok( inner, 76, &b->t )) { err = ERROR_INVALID_DATA; goto fail; }
    if ((err = r9b_to_d( b, &b->t, inner, 76 ))) goto fail;
    if ((err = r9b_from_d( b, E1R8_TYPE_SELECT, from, &inner_len, r9b_left( b, consent_timeout() ) ))) goto fail;
    err = ERROR_ACCESS_DENIED;
    if (e1r8_decode_select( from, inner_len, &b->t, &sel ) || sel.account_len > 63) goto fail;
    account_len = sel.account_len;
    memcpy( account, sel.account, account_len );
    account[account_len] = 0;
    /* An unknown name gets the zero key: the decoy, which no password authenticates. */
    e1s18_resolve_account_s4( account, key );
    r9b_trace( e1s18_is_all_zero( key, 16 ) ? "account decoy" : "account known" );

    /* START: the verifier makes the worker for the attempt the session's server endorsed on the agent's channel. */
    step = "start";
    if (e1r8_encode_start( frame, 96 + account_len, &b->t, (const uint8_t *)account, (uint32_t)account_len, key ))
    { err = ERROR_INVALID_DATA; goto fail; }
    b->started = TRUE;
    if (!ops->r8_start || ops->r8_start( &b->vreq, s_r8_channel, frame, 96 + account_len, ready ))
        memset( ready, 0, sizeof(ready) );
    locked = !e1r8_get_header( ready, sizeof(ready), E1R8_TYPE_LOCKED, &b->t, NULL );
    if (!locked && e1r8_decode_worker_ready( ready, sizeof(ready), &b->t )) goto fail;
    if (locked)
    {
        /* The lockout policy closed the account: no worker. The dialog tells the person and leaves by itself. */
        r9b_trace( "account locked" );
        b->started = FALSE;
        if (!e1r8_put_header( inner, 76, E1R8_TYPE_LOCKED, 76, &b->t )) r9b_to_d( b, &b->t, inner, 76 );
        *error = ERROR_ACCOUNT_LOCKED_OUT;
        result = R9B_LOCKED;
        goto done;
    }
    r9b_trace( "worker ready" );
    if (e1r8_encode_attach_ok( inner, 76, &b->t )) { err = ERROR_INVALID_DATA; goto fail; }
    if ((err = r9b_to_d( b, &b->t, inner, 76 ))) goto fail;

    /* DELEGATED: the endorsed dialog attached to the worker. READY after a recheck. */
    step = "delegated";
    err = ERROR_ACCESS_DENIED;
    if (r8_v_recv( &b->vreq, frame, &len, r9b_left( b, 130000 ) ) || e1r8_decode_delegated( frame, len, &b->t ))
        goto fail;
    step = "recheck ready";
    if ((err = r9b_recheck( b, E1R8_RECHECK_STEP_READY ))) goto fail;
    if (e1r8_encode_ready( inner, 76, &b->t )) { err = ERROR_INVALID_DATA; goto fail; }
    if ((err = r9b_to_d( b, &b->t, inner, 76 ))) goto fail;
    r9b_trace( "ready sent" );

    /* The verifier's RESULT after the worker's PAM conversation. The password goes from the dialog to the worker
     * on their own socket; neither the agent nor this process sees it. */
    step = "verifier result";
    err = ERROR_ACCESS_DENIED;
    if (r8_v_recv( &b->vreq, frame, &len, r9b_left( b, 130000 ) ) ||
        e1r8_decode_result_v( frame, len, &b->t, &vres )) goto fail;
    if (vres.status != E1R8_RESULT_AUTHENTICATED)
    {
        r9b_trace( "result refused" );
        step = "refused result";
        if (e1r8_encode_result( inner, 116, &b->t, E1R8_RESULT_REFUSED, (const uint8_t *)"", 0, zero32 ))
        { err = ERROR_INVALID_DATA; goto fail; }
        /* Only a dialog that took the refusal leads to a further attempt. */
        if ((err = r9b_to_d( b, &b->t, inner, 116 ))) goto fail;
        r9b_release( b, E1R8_RELEASE_CANCELLED );
        *error = ERROR_LOGON_FAILURE;
        result = R9B_REFUSED;
        goto done;
    }
    memcpy( ref, vres.evidence_ref, 16 );

    /* R8 section 8: the authenticated account is the selected one, resolved again. */
    step = "final account";
    if (vres.account_len != account_len || memcmp( vres.account, account, account_len ) ||
        e1s18_is_all_zero( key, 16 ) || memcmp( vres.account_key, key, 16 )) goto fail;
    e1s18_resolve_account_s4( account, fresh_key );
    if (memcmp( fresh_key, key, 16 )) goto fail;
    step = "recheck result";
    if ((err = r9b_recheck( b, E1R8_RECHECK_STEP_RESULT ))) goto fail;

    /* RESULT with a one-use challenge, then the dialog's Confirm for exactly that challenge. */
    step = "result";
    err = ERROR_ACCESS_DENIED;
    if (!e1s18_fill_random( challenge, 32 ) || e1s18_is_all_zero( challenge, 32 )) goto fail;
    if (e1r8_encode_result( inner, 116 + account_len, &b->t, E1R8_RESULT_AUTHENTICATED,
                            (const uint8_t *)account, (uint32_t)account_len, challenge )) goto fail;
    if ((err = r9b_to_d( b, &b->t, inner, (uint32_t)(116 + account_len) ))) goto fail;
    r9b_trace( "result sent" );
    step = "confirm";
    if ((err = r9b_from_d( b, E1R8_TYPE_ANSWER, from, &inner_len, r9b_left( b, consent_timeout() ) ))) goto fail;
    err = ERROR_ACCESS_DENIED;
    if (e1r8_decode_answer( from, inner_len, &b->t, E1S1_ALL_REQUIRED_CAPS, &ans )) goto fail;
    if (ans.answer_kind != E1R8_S1_ANSWER_CONFIRM) { err = ERROR_CANCELLED; goto fail; }
    if (memcmp( ans.challenge, challenge, 32 ) || e1s18_is_answer_replayed( b->t.request_id, b->t.nonce )) goto fail;
    e1s18_record_answer( b->t.request_id, b->t.nonce );
    step = "recheck confirm";
    if ((err = r9b_recheck( b, E1R8_RECHECK_STEP_CONFIRM ))) goto fail;
    r9b_trace( "confirm ok" );

    /* The grant: this is the decision to elevate. The verifier lets the agent redeem only after it. */
    step = "grant";
    err = ERROR_ACCESS_DENIED;
    if (e1r8_encode_grant( frame, 108, &b->t, key, ref ) || r8_v_send( &b->vreq, frame, 108 ) ||
        r8_v_recv( &b->vreq, frame, &len, r9b_left( b, R9B_IO_TIMEOUT ) ) ||
        e1r8_decode_granted( frame, len, &b->t )) goto fail;
    r9b_trace( "granted" );
    *error = ERROR_SUCCESS;
    result = R9B_GRANTED;
    goto done;

fail:
    r9b_trace_err( step, err );
    *error = err ? err : ERROR_ACCESS_DENIED;
done:
    e1s18_wipe( frame, sizeof(frame) );
    e1s18_wipe( msg, sizeof(msg) );
    e1s18_wipe( inner, sizeof(inner) );
    e1s18_wipe( from, sizeof(from) );
    e1s18_wipe( challenge, sizeof(challenge) );
    return result;
}

/* A fresh channel id for an attempt (16 random bytes, never zero), as the R8 broker makes it. */
static BOOL r9b_new_token( uint8_t token[16] )
{
    return e1s18_fill_random( token, 16 ) && !e1s18_is_all_zero( token, 16 );
}

/* One request of one session's agent, from REQUEST to END. */
static void r9b_request( HANDLE pipe, BOOL busy )
{
    struct r9b *b;
    struct e1r8_tuple head, old;
    struct e1r8_launched launched;
    uint8_t frame[R9B_FRAME_MAX], inner[80], key[16], ref[16];
    char account[64];
    size_t len = 0, account_len;
    DWORD session = 0, done = 0, err = ERROR_ACCESS_DENIED;
    uint64_t now;
    uint32_t outcome = E1R8_END_CANCELLED, reason = 0;
    enum r9b_result result;

    if (!(b = calloc( 1, sizeof(*b) ))) return;
    b->pipe = pipe;
    b->vreq.pipe = pipe;
    b->vreq.image_file = INVALID_HANDLE_VALUE;
    memset( &head, 0, sizeof(head) );

    /* REQUEST. The header names the request and its nonce; nothing else is known before it. */
    if (pipe_io( pipe, b->request, sizeof(b->request), FALSE, R9B_IO_TIMEOUT, &done ) ||
        done < E1R8_HEADER_SIZE) goto out;
    head.request_id = e1r8_get_u64( b->request + 12 );
    memcpy( head.nonce, b->request + 28, 32 );
    if (!head.request_id || e1r8_decode_request( b->request, done, &head, &b->rq )) goto out;
    if (!r9b_client_ok( pipe, &session ))
    {
        r9b_trace( "client refused" );
        goto out;
    }
    r9b_trace( "request" );

    /* Policy (session 0's registry) and the limits. No prompt on an insecure desktop in this version. */
    now = e1s18_get_monotonic_time();
    if (busy) reason = E1R8_DECISION_REASON_BUSY;
    else if (!e1s18_policy_enabled() || !b->rq.secure_desktop || !b->rq.image_len ||
             !now || b->rq.deadline <= now || !b->rq.root_gen || !b->rq.server_gen || !b->rq.broker_gen ||
             !b->rq.verifier_gen || !b->rq.map_gen || !b->rq.policy_gen || e1r8_is_zero16( b->rq.desktop_route ))
        reason = E1R8_DECISION_REASON_POLICY;
    if (reason)
    {
        r9b_trace( busy ? "deny busy" : "deny policy" );
        if (!e1r8_encode_decision( frame, 84, &head, E1R8_DECISION_DENY, reason )) r9b_send( b, frame, 84 );
        r9b_end( b, &head, E1R8_END_DENIED );
        goto out;
    }

    /* The first attempt's tuple: the agent's values unchanged (its server and the verifier check them), the
     * session as session 0's server names it, a deadline no later than the agent's. */
    b->t = head;
    b->t.attempt = 1;
    memcpy( b->t.binding_digest, b->rq.binding_digest, 32 );
    memcpy( b->t.request_digest, b->rq.request_digest, 32 );
    b->t.deadline = b->rq.deadline;
    b->t.root_gen = b->rq.root_gen;
    b->t.server_gen = b->rq.server_gen;
    b->t.broker_gen = b->rq.broker_gen;
    b->t.verifier_gen = b->rq.verifier_gen;
    b->t.map_gen = b->rq.map_gen;
    b->t.policy_gen = b->rq.policy_gen;
    b->t.caller_session = session;
    memcpy( b->t.desktop_route, b->rq.desktop_route, 16 );
    b->t.desktop_mode = E1R8_DESKTOP_MODE_SECURE;
    b->vreq.deadline = b->rq.deadline;
    if (!r9b_new_token( b->t.channel_id ) || (err = r9b_open_channel( b )))
    {
        r9b_trace_err( "channel", err );
        if (!e1r8_encode_decision( frame, 84, &head, E1R8_DECISION_DENY, E1R8_DECISION_REASON_POLICY ))
            r9b_send( b, frame, 84 );
        r9b_end( b, &head, E1R8_END_DENIED );
        goto out;
    }
    if (e1r8_encode_decision( frame, 284, &b->t, E1R8_DECISION_PROMPT, 0 ) || r9b_send( b, frame, 284 )) goto out;
    r9b_trace( "decision prompt" );

    for (;;)
    {
        result = r9b_attempt( b, account, key, ref, &err );
        if (result != R9B_REFUSED) break;
        /* A refused password: a further attempt (at most 3, same deadline, a fresh channel id) while there is
         * time for one. NEXT_ATTEMPT goes first, so the agent expects the old dialog's exit; then that dialog's
         * DONE, still under the old attempt's header. */
        old = b->t;
        if (b->t.attempt < 3 && r8_remaining( &b->vreq ) >= 10000 && r9b_new_token( b->t.channel_id ))
        {
            b->t.attempt++;
            memset( b->t.dialog_id, 0, 16 );
            if (e1r8_encode_next_attempt( frame, 280, &b->t ) || r9b_send( b, frame, 280 )) { result = R9B_FAILED; break; }
            if (e1r8_encode_done( inner, 80, &old, E1R8_DONE_CANCELLED ) || r9b_to_d( b, &old, inner, 80 ))
            { result = R9B_FAILED; break; }
            r9b_trace( "further attempt" );
            continue;
        }
        b->t = old;
        if (!e1r8_encode_done( inner, 80, &b->t, E1R8_DONE_CANCELLED )) r9b_to_d( b, &b->t, inner, 80 );
        break;
    }

    if (result == R9B_GRANTED)
    {
        /* DONE first (the dialog leaves, the secure desktop is free while the program starts), then LAUNCH. */
        account_len = strlen( account );
        if (e1r8_encode_done( inner, 80, &b->t, E1R8_DONE_COMPLETED ) || r9b_to_d( b, &b->t, inner, 80 ) ||
            e1r8_encode_launch( frame, 96 + account_len, &b->t, (const uint8_t *)account, (uint32_t)account_len, ref ) ||
            r9b_send( b, frame, 96 + account_len ))
            r9b_trace_err( "launch", ERROR_BROKEN_PIPE );
        else
        {
            r9b_trace( "launch sent" );
            /* The agent redeems on its own channel, has its server issue the token and starts the program. */
            if (!r9b_recv( b, frame, &len, R9B_LAUNCH_TIMEOUT ) && !e1r8_decode_launched( frame, len, &b->t, &launched ) &&
                launched.status == E1R8_LAUNCHED_COMMITTED)
            {
                r9b_trace( "launched" );
                outcome = E1R8_END_COMPLETED;
            }
            else r9b_trace( "not launched" );
        }
        r9b_release( b, outcome == E1R8_END_COMPLETED ? E1R8_RELEASE_COMPLETED : E1R8_RELEASE_CANCELLED );
    }
    else if (result == R9B_LOCKED) outcome = E1R8_END_DENIED;
    r9b_end( b, &b->t, outcome );
    r9b_trace( outcome == E1R8_END_COMPLETED ? "end completed" : outcome == E1R8_END_DENIED ? "end denied"
                                                                                           : "end cancelled" );

out:
    r9b_close_channel( b );
    e1s18_wipe( frame, sizeof(frame) );
    e1s18_wipe( key, sizeof(key) );
    e1s18_wipe( ref, sizeof(ref) );
    e1s18_wipe( b, sizeof(*b) );
    free( b );
}

static DWORD WINAPI r9b_client_thread( void *param )
{
    HANDLE pipe = param;
    BOOL busy = InterlockedCompareExchange( &r9b_busy, 1, 0 ) != 0;

    r9b_request( pipe, busy );
    if (!busy) InterlockedExchange( &r9b_busy, 0 );
    FlushFileBuffers( pipe );
    DisconnectNamedPipe( pipe );
    CloseHandle( pipe );
    InterlockedDecrement( &r9b_threads );
    return 0;
}

static HANDLE r9b_create_instance( BOOL first )
{
    SECURITY_ATTRIBUTES sa = { sizeof(sa) };
    HANDLE pipe;

    /* SYSTEM only: a session's agent (its token described over the bridge) and nothing else. */
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW( L"D:P(A;;GA;;;SY)", SDDL_REVISION_1,
                                                              &sa.lpSecurityDescriptor, NULL )) return INVALID_HANDLE_VALUE;
    pipe = CreateNamedPipeW( R9B_PIPE_NAME, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
                             (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
                             PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                             R9B_INSTANCES, R9B_FRAME_MAX, R9B_FRAME_MAX, 0, &sa );
    LocalFree( sa.lpSecurityDescriptor );
    return pipe;
}

/* The service's main loop in session 0, in place of serve(). */
static void e1r9_broker0_serve(void)
{
    HANDLE pipe = r9b_create_instance( TRUE );
    OVERLAPPED ov = { 0 };
    HANDLE events[2], thread;

    if (pipe == INVALID_HANDLE_VALUE)
    {
        report( SERVICE_STOPPED, GetLastError() ? GetLastError() : ERROR_ACCESS_DENIED );
        return;
    }
    report( SERVICE_RUNNING, 0 );
    r9b_trace( "serving" );
    ov.hEvent = CreateEventW( NULL, TRUE, FALSE, NULL );
    events[0] = stop_event;
    events[1] = ov.hEvent;
    for (;;)
    {
        DWORD err = ERROR_SUCCESS;

        ResetEvent( ov.hEvent );
        if (!ConnectNamedPipe( pipe, &ov ))
        {
            err = GetLastError();
            if (err == ERROR_IO_PENDING)
            {
                if (WaitForMultipleObjects( 2, events, FALSE, INFINITE ) == WAIT_OBJECT_0)
                {
                    CancelIo( pipe );
                    break;
                }
                err = ERROR_SUCCESS;
            }
            else if (err == ERROR_PIPE_CONNECTED) err = ERROR_SUCCESS;
        }
        if (!err && InterlockedIncrement( &r9b_threads ) <= R9B_INSTANCES - 1 &&
            (thread = CreateThread( NULL, 0, r9b_client_thread, pipe, 0, NULL )))
        {
            CloseHandle( thread );
            pipe = INVALID_HANDLE_VALUE;
        }
        else
        {
            if (!err) InterlockedDecrement( &r9b_threads );
            DisconnectNamedPipe( pipe );
        }
        while (pipe == INVALID_HANDLE_VALUE)
        {
            if ((pipe = r9b_create_instance( FALSE )) != INVALID_HANDLE_VALUE) break;
            if (WaitForSingleObject( stop_event, 1000 ) == WAIT_OBJECT_0) break;   /* every instance in use */
        }
        if (pipe == INVALID_HANDLE_VALUE) break;
    }
    CloseHandle( ov.hEvent );
    if (pipe != INVALID_HANDLE_VALUE) CloseHandle( pipe );
    SetEvent( stop_event );
    while (r9b_threads) Sleep( 50 );
}

/* B0 is the role of this image in session 0. */
static BOOL e1r9_is_session0(void)
{
    DWORD session = 1;
    return ProcessIdToSessionId( GetCurrentProcessId(), &session ) && !session;
}

#endif /* _WIN32 && WEXELEVATE_E1S18 */
