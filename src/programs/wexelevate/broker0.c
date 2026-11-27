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
 * A unit of its own (broker_internal.h); it uses standard.c's R8 helpers, main.c's services, and the decision
 * sequence and attempt loop it shares with the R8 broker (decision.c).
 *
 * Limits of this version (stated in the spec, section 12):
 *  - one request is served at a time; a second session's request gets DECISION deny (busy);
 *  - the attempt count is per request (3); the per-account count across sessions is the verifier's lockout policy;
 *  - while B0 waits for the verifier it does not read the agent's pipe: the agent's cancellation reaches it through
 *    the verifier (the agent's channel loss ends the attempt there, and the verifier tells B0).
 */

#if defined(_WIN32) && defined(WEXELEVATE_E1S18)   /* first: a shipping build compiles nothing of this unit */

#include "broker_internal.h"

#include <stdio.h>
#include <sddl.h>
#include "wine/wex_pipe.h"

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
    struct r8_link link;            /* the shared decision sequence's link: frames through the agent */
    struct r8_attempts attempts;    /* the shared attempt loop */
    char account[64];               /* a granted attempt's account, key and evidence reference */
    uint8_t key[16], ref[16];
};

#define R9B_OF(ptr, member) ((struct r9b *)((char *)(ptr) - offsetof(struct r9b, member)))

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

/* B132: session 0 maps a session's system role to the account whose uid runs it, so its token no longer says SYSTEM;
 * session 0's server says what it is, as a caller class (WEX_CALLER_SESSION_SYSTEM, wine/wex_caller.h), as the
 * EventLog and wexmsiserver gates ask it. The request is in caller_class.c (wine/server.h's protocol header defines a
 * tag "request" that the broker's struct request collides with); here only the class constants. */
#define WEX_CALLER_CONSTANTS_ONLY
#include "wine/wex_caller.h"
extern unsigned int r9b_caller_class( HANDLE pipe );

/* The client is a session's system role (the bridge described its token; session 0 bound it to the peer server's
 * account and, since B132, mapped it to that account: WEX_CALLER_SESSION_SYSTEM), and session 0's server names the
 * session it came from, which binds the request (the tuple's caller_session): an agent acts on its own session's
 * request only. Development mode, where session 0 maps nothing, still describes SYSTEM itself. Every refusal is
 * logged with the class. */
static BOOL r9b_client_ok( HANDLE pipe, DWORD *session )
{
    PSID system_sid = NULL;
    HANDLE token = NULL;
    unsigned int class = r9b_caller_class( pipe );
    BOOL ok = class == WEX_CALLER_SESSION_SYSTEM;
    char trace[64];

    *session = 0;
    if (!ok && class == WEX_CALLER_USER)
    {
        if (!ImpersonateNamedPipeClient( pipe )) return FALSE;
        if (!OpenThreadToken( GetCurrentThread(), TOKEN_QUERY, TRUE, &token )) token = NULL;
        wex_revert_to_self();   /* returns only when this thread is itself again; else the process ends */
        if (token && ConvertStringSidToSidW( L"S-1-5-18", &system_sid ))
        {
            ok = wex_token_user_is( token, system_sid, GetLengthSid( system_sid ) );
            LocalFree( system_sid );
        }
        if (token) CloseHandle( token );
    }
    if (!ok)
    {
        snprintf( trace, sizeof(trace), "client refused: caller class %u", class );
        r9b_trace( trace );
    }
    if (ok && (!GetNamedPipeClientSessionId( pipe, session ) || !*session)) ok = FALSE;
    return ok;
}

/* Design review, finding 2: B0's link for the shared decision sequence (decision.c). Every frame for the dialog goes
 * through the agent, every server recheck is asked of it, and the verifier is on B0's own S6 channel. */
static DWORD r9b_link_to_d( struct r8_link *link, const uint8_t *frame, uint32_t len )
{
    return r9b_to_d( R9B_OF( link, link ), link->t, frame, len );
}

static DWORD r9b_link_from_d( struct r8_link *link, uint16_t type, uint8_t *frame, uint32_t max, uint32_t *len )
{
    struct r9b *b = R9B_OF( link, link );
    if (max < 340) return ERROR_INVALID_PARAMETER;
    return r9b_from_d( b, type, frame, len, r9b_left( b, consent_timeout() ) );
}

static DWORD r9b_link_recheck( struct r8_link *link, uint32_t step )
{
    return r9b_recheck( R9B_OF( link, link ), step );
}

static DWORD r9b_link_v_start( struct r8_link *link, const uint8_t *start, size_t len, uint8_t ready[76] )
{
    struct r9b *b = R9B_OF( link, link );
    const struct e1s18_ops *ops = e1s18_get_ops();
    if (!ops->r8_start) return ERROR_NOT_SUPPORTED;
    return ops->r8_start( &b->vreq, s_r8_channel, start, len, ready );
}

static DWORD r9b_link_v_recv( struct r8_link *link, uint8_t frame[E1S6_R8_FRAME_MAX], size_t *len )
{
    struct r9b *b = R9B_OF( link, link );
    return r8_v_recv( &b->vreq, frame, len, r9b_left( b, 130000 ) );
}

/*
 * One attempt, from the agent's ENDORSED_LOCAL to the grant: the dialog shown through the agent (ENDORSED_LOCAL,
 * INIT, ACK), the shared decision sequence, then the grant.
 * ERROR_SUCCESS: b->account, key and ref are set and the verifier has granted.
 * ERROR_LOGON_FAILURE: the password was refused, the dialog has RESULT 2 (not yet DONE) and the verifier's attempt is
 * released; the attempt loop decides on a further attempt.
 * ERROR_ACCOUNT_LOCKED_OUT: the verifier's lockout closed the account; the dialog has the notice.
 */
static DWORD r9b_attempt( struct r9b *b )
{
    uint8_t frame[E1S6_R8_FRAME_MAX], msg[R9B_FRAME_MAX], inner[2067], from[340];
    struct e1r8_endorsed_local endorsed;
    struct e1r8_ack ack;
    enum r8_decision decision;
    uint32_t inner_len = 0, vo_len, total;
    size_t len = 0;
    const char *step;
    DWORD err = ERROR_ACCESS_DENIED;

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

    /* SELECT to the dialog's confirmed answer: the shared sequence (decision.c). */
    b->link.t = &b->t;
    decision = r8_decide( &b->link, b->account, b->key, b->ref, &b->started, &err );
    if (decision == R8_DECISION_REFUSED)
    {
        /* Only a dialog that took the refusal leads to a further attempt. */
        r9b_release( b, E1R8_RELEASE_CANCELLED );
        goto done;
    }
    if (decision != R8_DECISION_CONFIRMED) goto done;   /* locked, or failed (traced by the sequence) */

    /* The grant: this is the decision to elevate. The verifier lets the agent redeem only after it. */
    step = "grant";
    err = ERROR_ACCESS_DENIED;
    if (e1r8_encode_grant( frame, 108, &b->t, b->key, b->ref ) || r8_v_send( &b->vreq, frame, 108 ) ||
        r8_v_recv( &b->vreq, frame, &len, r9b_left( b, R9B_IO_TIMEOUT ) ) ||
        e1r8_decode_granted( frame, len, &b->t )) goto fail;
    r9b_trace( "granted" );
    err = ERROR_SUCCESS;
    goto done;

fail:
    r9b_trace_err( step, err );
    if (!err) err = ERROR_ACCESS_DENIED;
done:
    e1s18_wipe( frame, sizeof(frame) );
    e1s18_wipe( msg, sizeof(msg) );
    e1s18_wipe( inner, sizeof(inner) );
    e1s18_wipe( from, sizeof(from) );
    if (err)
    {
        e1s18_wipe( b->key, sizeof(b->key) );
        e1s18_wipe( b->ref, sizeof(b->ref) );
    }
    return err;
}

/* Design review, finding 2: B0's attempts for the shared attempt loop. */
static DWORD r9b_attempts_attempt( struct r8_attempts *a, uint32_t n )
{
    (void)n;   /* b->t.attempt already says n: r9b_attempts_further set it */
    return r9b_attempt( R9B_OF( a, attempts ) );
}

/* A further attempt: NEXT_ATTEMPT goes first, so the agent expects the old dialog's exit; then that dialog's DONE,
 * still under the old attempt's header. */
static DWORD r9b_attempts_further( struct r8_attempts *a, uint32_t n, const uint8_t channel_id[16] )
{
    struct r9b *b = R9B_OF( a, attempts );
    struct e1r8_tuple old = b->t;
    uint8_t frame[R9B_FRAME_MAX], inner[80];
    DWORD err = ERROR_SUCCESS;

    b->t.attempt = n;
    memcpy( b->t.channel_id, channel_id, 16 );
    memset( b->t.dialog_id, 0, 16 );
    if (e1r8_encode_next_attempt( frame, 280, &b->t ) || r9b_send( b, frame, 280 ) ||
        e1r8_encode_done( inner, 80, &old, E1R8_DONE_CANCELLED ) || r9b_to_d( b, &old, inner, 80 ))
        err = ERROR_BROKEN_PIPE;
    e1s18_wipe( frame, sizeof(frame) );
    return err;
}

static DWORD r9b_attempts_remaining( struct r8_attempts *a )
{
    return r8_remaining( &R9B_OF( a, attempts )->vreq );
}

/* A fresh channel id for an attempt (16 random bytes, never zero), as the R8 broker makes it. */
static BOOL r9b_new_token( uint8_t token[16] )
{
    return e1s18_fill_random( token, 16 ) && !e1s18_is_all_zero( token, 16 );
}

/* One request of one session's agent, from REQUEST to END. The one request in flight (r9b_busy) is taken only for
 * an admitted agent, so a caller that the descriptor lets connect but r9b_client_ok refuses never holds it; *held
 * says whether this request took it. */
static void r9b_request( HANDLE pipe, BOOL *held )
{
    BOOL busy;
    struct r9b *b;
    struct e1r8_tuple head;
    struct e1r8_launched launched;
    uint8_t frame[R9B_FRAME_MAX], inner[80];
    size_t len = 0, account_len;
    DWORD session = 0, done = 0, err = ERROR_ACCESS_DENIED;
    uint64_t now;
    uint32_t outcome = E1R8_END_CANCELLED, reason = 0;

    if (!(b = calloc( 1, sizeof(*b) ))) return;
    b->pipe = pipe;
    b->vreq.pipe = pipe;
    b->vreq.image_file = INVALID_HANDLE_VALUE;
    b->link.who = "r9b";
    b->link.to_d = r9b_link_to_d;
    b->link.from_d = r9b_link_from_d;
    b->link.recheck = r9b_link_recheck;
    b->link.v_start = r9b_link_v_start;
    b->link.v_recv = r9b_link_v_recv;
    b->attempts.who = "r9b";
    b->attempts.attempt = r9b_attempts_attempt;
    b->attempts.further = r9b_attempts_further;
    b->attempts.remaining = r9b_attempts_remaining;
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
    busy = InterlockedCompareExchange( &r9b_busy, 1, 0 ) != 0;
    *held = !busy;
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

    /* Spec section 1: the shared attempt loop (decision.c). The last refused attempt's dialog gets its DONE here. */
    err = r8_attempt_loop( &b->attempts );
    if (err == ERROR_LOGON_FAILURE && !e1r8_encode_done( inner, 80, &b->t, E1R8_DONE_CANCELLED ))
        r9b_to_d( b, &b->t, inner, 80 );

    if (err == ERROR_SUCCESS)
    {
        /* DONE first (the dialog leaves, the secure desktop is free while the program starts), then LAUNCH. */
        account_len = strlen( b->account );
        if (e1r8_encode_done( inner, 80, &b->t, E1R8_DONE_COMPLETED ) || r9b_to_d( b, &b->t, inner, 80 ) ||
            e1r8_encode_launch( frame, 96 + account_len, &b->t, (const uint8_t *)b->account, (uint32_t)account_len,
                                b->ref ) ||
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
    else if (err == ERROR_ACCOUNT_LOCKED_OUT) outcome = E1R8_END_DENIED;
    r9b_end( b, &b->t, outcome );
    r9b_trace( outcome == E1R8_END_COMPLETED ? "end completed" : outcome == E1R8_END_DENIED ? "end denied"
                                                                                           : "end cancelled" );

out:
    r9b_close_channel( b );
    e1s18_wipe( frame, sizeof(frame) );
    e1s18_wipe( b, sizeof(*b) );
    free( b );
}

static DWORD WINAPI r9b_client_thread( void *param )
{
    HANDLE pipe = param;
    BOOL held = FALSE;

    r9b_request( pipe, &held );
    if (held) InterlockedExchange( &r9b_busy, 0 );
    /* The agent reads END and closes: wait for that, bounded and ended by a service stop, so the last message is
     * not cut off by the disconnect. Never FlushFileBuffers here: a client that does not read would hold this
     * worker, and with it one of the broker's few connections, for good (review, 2026-11-13). */
    {
        uint8_t last[E1R8_HEADER_SIZE];
        DWORD done = 0;
        pipe_io( pipe, last, sizeof(last), FALSE, 2000, &done );
    }
    DisconnectNamedPipe( pipe );
    CloseHandle( pipe );
    InterlockedDecrement( &r9b_threads );
    return 0;
}

static HANDLE r9b_create_instance( BOOL first )
{
    /* A session's agent (its token described over the bridge) and nothing else. Since B132 that token is the agent's
     * account (an allow ACE for SY no longer matches it), so the descriptor lets authenticated callers connect and
     * r9b_client_ok admits only a session's system role, by the server's caller class; anyone else is refused at the
     * first read, and logged. */
    return wex_pipe_create_instances( R9B_PIPE_NAME, L"D:P(A;;GA;;;SY)(A;;GRGW;;;AU)", first, R9B_FRAME_MAX,
                                      R9B_INSTANCES );
}

/* The service's main loop in session 0, in place of serve(). */
void e1r9_broker0_serve( void )
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
BOOL e1r9_is_session0( void )
{
    DWORD session = 1;
    return ProcessIdToSessionId( GetCurrentProcessId(), &session ) && !session;
}

#else
typedef int wexelevate_lab_unit_empty;   /* ISO C: no empty translation unit */
#endif /* _WIN32 && WEXELEVATE_E1S18 */
