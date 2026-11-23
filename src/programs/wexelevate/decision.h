/*
 * The elevation broker's standard-user path (lab only): the one R8 decision sequence and the one attempt loop
 * (decision.c), shared by the R8 broker (standard.c: its own dialog D on two local pipes) and B0 (broker0.c: the
 * dialog in a session, every frame through the agent). Design review, finding 2.
 *
 * A link carries the frames: to and from the dialog, the server's rechecks of the dialog, and the verifier's START
 * and frames on the attempt's S6 channel. The sequence itself is pure C over the link, so the native unit tests drive
 * it with a fake dialog and a fake verifier. What differs between the two brokers stays with them: making and
 * showing the dialog (INIT/ACK), and what follows a confirmed answer (the R8 broker redeems the evidence, B0 grants).
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

#ifndef WEXELEVATE_DECISION_H
#define WEXELEVATE_DECISION_H

#ifdef WEXELEVATE_E1S18

#include "standard.h"
#include "e1s6_protocol.h"
#include "e1r8_codec.h"

struct r8_link
{
    const char *who;              /* the trace prefix: "r8" or "r9b" */
    struct e1r8_tuple *t;         /* the attempt's sealed tuple (the dialog id is set) */
    /* One whole R8 frame to the dialog. */
    DWORD (*to_d)( struct r8_link *link, const uint8_t *frame, uint32_t len );
    /* One whole R8 frame of the dialog, of this type, at most max bytes; the caller decodes it. */
    DWORD (*from_d)( struct r8_link *link, uint16_t type, uint8_t *frame, uint32_t max, uint32_t *len );
    /* The server rechecks the dialog for this step (E1R8_RECHECK_STEP_*); anything but ERROR_SUCCESS stops. */
    DWORD (*recheck)( struct r8_link *link, uint32_t step );
    /* START to the verifier; its 76-byte answer (WORKER_READY or LOCKED) in ready. */
    DWORD (*v_start)( struct r8_link *link, const uint8_t *start, size_t len, uint8_t ready[76] );
    /* The verifier's next frame on the attempt's channel, within the request's deadline. */
    DWORD (*v_recv)( struct r8_link *link, uint8_t frame[E1S6_R8_FRAME_MAX], size_t *len );
};

enum r8_decision
{
    R8_DECISION_CONFIRMED,   /* account, key and evidence reference set; the answer is recorded as used */
    R8_DECISION_REFUSED,     /* the password was refused and the dialog has the refusal (not yet DONE) */
    R8_DECISION_LOCKED,      /* the verifier's lockout closed the account; the dialog has the notice */
    R8_DECISION_FAILED       /* anything else: *error says what; the caller ends the dialog */
};

/*
 * From SELECT_OK to the dialog's confirmed answer (docs/E1_S18E_R8_SPEC.md sections 6-8): the person names the
 * account, the verifier's worker takes the password, the authenticated account must be the selected one resolved
 * again, and the dialog confirms exactly the one-use challenge. *started tells whether the verifier holds a worker
 * for the attempt (the caller releases it). Every check fails closed.
 */
enum r8_decision r8_decide( struct r8_link *link, char account[64], uint8_t key[16], uint8_t ref[16],
                            BOOL *started, DWORD *error );

struct r8_attempts
{
    const char *who;
    /* Attempt n (1..3); ERROR_LOGON_FAILURE: the password was refused. */
    DWORD (*attempt)( struct r8_attempts *a, uint32_t n );
    /* Before attempt n > 1: take the fresh channel id (and tell the dialog's side). An error ends the request. */
    DWORD (*further)( struct r8_attempts *a, uint32_t n, const uint8_t channel_id[16] );
    /* Milliseconds left until the request's deadline. */
    DWORD (*remaining)( struct r8_attempts *a );
};

/*
 * Spec section 1: attempts 1..3 per request within its one deadline; only a refused password leads to a further
 * attempt, with a fresh channel id, while at least 10 s are left. Returns the last attempt's result.
 */
DWORD r8_attempt_loop( struct r8_attempts *a );

#endif /* WEXELEVATE_E1S18 */
#endif /* WEXELEVATE_DECISION_H */
