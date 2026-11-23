/*
 * The elevation broker's standard-user path (lab only): the one R8 decision sequence and the one attempt loop.
 * See decision.h. Design review, finding 2: these steps existed twice (e1s18_spawn_consent_dialog and r9b_attempt)
 * and the attempt loop twice (default_prompt_dialog and r9b_request).
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef WEXELEVATE_E1S18   /* first: a shipping build compiles nothing of this unit */

#include "broker_internal.h"
#include <stdio.h>
#include "decision.h"

static void r8d_trace( const char *who, const char *what )
{
    char text[96];
    snprintf( text, sizeof(text), "%s: %s", who, what );
    r8_trace_line( text );
}

static void r8d_trace_err( const char *who, const char *step, DWORD err )
{
    char text[96];
    snprintf( text, sizeof(text), "%s: %s failed %lu", who, step, (unsigned long)err );
    r8_trace_line( text );
}

enum r8_decision r8_decide( struct r8_link *l, char account[64], uint8_t key[16], uint8_t ref[16],
                            BOOL *started, DWORD *error )
{
    static const uint8_t zero32[32];
    /* inner: one frame for the dialog (RESULT is the largest, 179); from: one frame of it (ANSWER, 264) */
    uint8_t inner[180], from[340], frame[E1S6_R8_FRAME_MAX], challenge[32], fresh_key[16], start[160], ready[76];
    struct e1r8_tuple *t = l->t;
    struct e1r8_select sel;
    struct e1r8_result_v vres;
    struct e1r8_answer ans;
    enum r8_decision result = R8_DECISION_FAILED;
    uint32_t len = 0, account_len;
    size_t flen = 0;
    const char *step;
    DWORD err;
    BOOL locked;

    memset( account, 0, 64 );
    memset( key, 0, 16 );
    memset( ref, 0, 16 );
    *started = FALSE;

    /* SELECT_OK / SELECT: the person names the account. */
    step = "select";
    err = ERROR_INVALID_DATA;
    if (e1r8_encode_select_ok( inner, 76, t )) goto fail;
    if ((err = l->to_d( l, inner, 76 ))) goto fail;
    if ((err = l->from_d( l, E1R8_TYPE_SELECT, from, sizeof(from), &len ))) goto fail;
    err = ERROR_ACCESS_DENIED;
    if (e1r8_decode_select( from, len, t, &sel ) || sel.account_len > 63) goto fail;
    account_len = sel.account_len;
    memcpy( account, sel.account, account_len );
    r8d_trace( l->who, "select ok" );
    /* An unknown name gets the zero key: the decoy, which no password authenticates. */
    e1s18_resolve_account_s4( account, key );
    r8d_trace( l->who, e1s18_is_all_zero( key, 16 ) ? "account decoy" : "account known" );

    /* START: the verifier makes the worker for the attempt the server endorsed. */
    step = "start";
    err = ERROR_INVALID_DATA;
    if (e1r8_encode_start( start, 96 + account_len, t, (const uint8_t *)account, account_len, key )) goto fail;
    *started = TRUE;
    r8d_trace( l->who, "start sent" );
    if (l->v_start( l, start, 96 + account_len, ready )) memset( ready, 0, sizeof(ready) );
    locked = !e1r8_get_header( ready, sizeof(ready), E1R8_TYPE_LOCKED, t, NULL );
    err = ERROR_ACCESS_DENIED;
    if (!locked && e1r8_decode_worker_ready( ready, sizeof(ready), t )) goto fail;
    if (locked)
    {
        /* The lockout policy closed the account: no worker. The dialog tells the person and leaves by itself. */
        r8d_trace( l->who, "account locked" );
        *started = FALSE;
        if (!e1r8_put_header( inner, 76, E1R8_TYPE_LOCKED, 76, t ) && !l->to_d( l, inner, 76 ))
            r8d_trace( l->who, "locked sent" );
        *error = ERROR_ACCOUNT_LOCKED_OUT;
        result = R8_DECISION_LOCKED;
        goto done;
    }
    r8d_trace( l->who, "worker ready" );
    err = ERROR_INVALID_DATA;
    if (e1r8_encode_attach_ok( inner, 76, t )) goto fail;
    if ((err = l->to_d( l, inner, 76 ))) goto fail;
    r8d_trace( l->who, "attach_ok sent" );

    /* DELEGATED: the endorsed dialog attached to the worker. READY after a recheck. */
    step = "delegated";
    err = ERROR_ACCESS_DENIED;
    if (l->v_recv( l, frame, &flen ) || e1r8_decode_delegated( frame, flen, t )) goto fail;
    r8d_trace( l->who, "delegated" );
    step = "recheck ready";
    if ((err = l->recheck( l, E1R8_RECHECK_STEP_READY ))) goto fail;
    err = ERROR_INVALID_DATA;
    if (e1r8_encode_ready( inner, 76, t )) goto fail;
    if ((err = l->to_d( l, inner, 76 ))) goto fail;
    r8d_trace( l->who, "ready sent" );

    /* The verifier's RESULT after the worker's PAM conversation. The password goes from the dialog to the worker on
     * their own socket; it never passes through a broker. */
    step = "verifier result";
    err = ERROR_ACCESS_DENIED;
    if (l->v_recv( l, frame, &flen ) || e1r8_decode_result_v( frame, flen, t, &vres )) goto fail;
    if (vres.status != E1R8_RESULT_AUTHENTICATED)
    {
        r8d_trace( l->who, "result refused" );
        step = "refused result";
        err = ERROR_INVALID_DATA;
        if (e1r8_encode_result( inner, 116, t, E1R8_RESULT_REFUSED, (const uint8_t *)"", 0, zero32 )) goto fail;
        /* Only a dialog that took the refusal leads to a further attempt. */
        if ((err = l->to_d( l, inner, 116 ))) goto fail;
        *error = ERROR_LOGON_FAILURE;
        result = R8_DECISION_REFUSED;
        goto done;
    }
    memcpy( ref, vres.evidence_ref, 16 );

    /* Section 8: the authenticated account is the selected one, resolved again. */
    step = "final account";
    if (vres.account_len != account_len || memcmp( vres.account, account, account_len ) ||
        e1s18_is_all_zero( key, 16 ) || memcmp( vres.account_key, key, 16 )) goto fail;
    e1s18_resolve_account_s4( account, fresh_key );
    if (memcmp( fresh_key, key, 16 )) goto fail;
    step = "recheck result";
    if ((err = l->recheck( l, E1R8_RECHECK_STEP_RESULT ))) goto fail;

    /* RESULT with a one-use challenge, then the dialog's Confirm for exactly that challenge. */
    step = "result";
    err = ERROR_ACCESS_DENIED;
    if (!e1s18_fill_random( challenge, 32 ) || e1s18_is_all_zero( challenge, 32 )) goto fail;
    if (e1r8_encode_result( inner, 116 + account_len, t, E1R8_RESULT_AUTHENTICATED,
                            (const uint8_t *)account, account_len, challenge )) goto fail;
    if ((err = l->to_d( l, inner, 116 + account_len ))) goto fail;
    r8d_trace( l->who, "result sent" );
    step = "confirm";
    if ((err = l->from_d( l, E1R8_TYPE_ANSWER, from, sizeof(from), &len ))) goto fail;
    err = ERROR_ACCESS_DENIED;
    if (e1r8_decode_answer( from, len, t, E1S1_ALL_REQUIRED_CAPS, &ans )) goto fail;
    if (ans.answer_kind != E1R8_S1_ANSWER_CONFIRM) { err = ERROR_CANCELLED; goto fail; }
    if (memcmp( ans.challenge, challenge, 32 ) || e1s18_is_answer_replayed( t->request_id, t->nonce ) ||
        !e1s18_record_answer( t->request_id, t->nonce )) goto fail;
    step = "recheck confirm";
    if ((err = l->recheck( l, E1R8_RECHECK_STEP_CONFIRM ))) goto fail;
    r8d_trace( l->who, "confirm ok" );
    *error = ERROR_SUCCESS;
    result = R8_DECISION_CONFIRMED;
    goto done;

fail:
    r8d_trace_err( l->who, step, err );
    *error = err ? err : ERROR_ACCESS_DENIED;
    e1s18_wipe( key, 16 );
    e1s18_wipe( ref, 16 );
done:
    e1s18_wipe( inner, sizeof(inner) );
    e1s18_wipe( from, sizeof(from) );
    e1s18_wipe( frame, sizeof(frame) );
    e1s18_wipe( challenge, sizeof(challenge) );
    e1s18_wipe( fresh_key, sizeof(fresh_key) );
    e1s18_wipe( start, sizeof(start) );
    e1s18_wipe( ready, sizeof(ready) );
    return result;
}

DWORD r8_attempt_loop( struct r8_attempts *a )
{
    uint8_t id[16];
    uint32_t n;
    DWORD err;

    for (n = 1; ; n++)
    {
        err = a->attempt( a, n );
        if (err != ERROR_LOGON_FAILURE || n >= 3 || a->remaining( a ) < 10000) break;
        if (!e1s18_fill_random( id, 16 ) || e1s18_is_all_zero( id, 16 )) break;
        if ((err = a->further( a, n + 1, id ))) break;
        r8d_trace( a->who, "further attempt" );
    }
    e1s18_wipe( id, sizeof(id) );
    return err;
}

#else
typedef int wexelevate_lab_unit_empty;   /* ISO C: no empty translation unit */
#endif /* WEXELEVATE_E1S18 */
