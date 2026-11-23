/*
 * The elevation broker's standard-user path (E1-S18, lab only): the one header its compilation units share.
 *
 * The units are main.c (the broker; the shipping build is main.c alone), standard.c (the standard-user request:
 * policy, S1 digests, dialog, redemption, token, profile, launch), broker0.c (B0, the broker in session 0, R9),
 * token_checks.c (the token and SID checks), cred_dialog.c (the prompt queue) and launch_prep.c (S15).
 *
 * Every unit but main.c is listed in Makefile.in and compiles to nothing in a shipping Wine build: each one starts
 * with "#if defined(WEXELEVATE_E1S18) || !defined(__WINE_PE_BUILD)", so its body exists only in a lab build or a
 * native unit test (and a plain MinGW compile, which the broker test uses).
 *
 * In a lab build, main.c gives the services below (stop_event, check_binding, ...) external linkage through
 * BROKER_SHARED; in the shipping build they stay static, so its object file does not change.
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

#ifndef WEXELEVATE_BROKER_INTERNAL_H
#define WEXELEVATE_BROKER_INTERNAL_H

#include "standard.h"
#include "cred_dialog.h"
#include "launch_prep.h"
#include "token_checks.h"
#include "e1s6_protocol.h"
#include "e1s_encode.h"
#include "e1s_digest.h"
#ifdef WEXELEVATE_E1S18
#include "e1r8_codec.h"
#endif

/* Wipe a secret; never elided. */
static inline void e1s18_wipe( void *ptr, size_t size )
{
    wex_secure_wipe( ptr, size );
}

/* ---- standard.c, for the other units ---------------------------------------------------------------------- */

/* Monotonic nanoseconds; 0 when the clock fails (every caller denies on 0). */
uint64_t e1s18_get_monotonic_time( void );
/* Random bytes from the system generator; FALSE on failure (deny). */
BOOL e1s18_fill_random( uint8_t *buf, size_t len );
BOOL e1s18_is_all_zero( const uint8_t *buf, size_t len );

/* The answers (request id, nonce) the broker has acted on, so that none is acted on twice. */
BOOL e1s18_is_answer_replayed( uint64_t request_id, const uint8_t nonce[32] );
void e1s18_record_answer( uint64_t request_id, const uint8_t nonce[32] );

void r8_trace_line( const char *msg );
void r8_trace_err( const char *step, unsigned long err );

#if defined(_WIN32) && defined(WEXELEVATE_E1S18)

#include <sddl.h>
#include <userenv.h>

/* The S6 channel of the attempt (R8) or of B0's request (R9); 0: none open. */
extern UINT64 s_r8_channel;

/* Milliseconds left until the request's admission deadline, at most 130 s; 0 when it has passed. */
DWORD r8_remaining( const struct request *req );
/* One E1R8 frame to or from the verifier on s_r8_channel. */
DWORD r8_v_send( struct request *req, uint8_t *frame, size_t len );
DWORD r8_v_recv( struct request *req, uint8_t frame[E1S6_R8_FRAME_MAX], size_t *len, DWORD timeout );

/* ---- main.c, for the lab units (BROKER_SHARED) ------------------------------------------------------------ */

extern HANDLE stop_event;
extern CRITICAL_SECTION table_cs;
extern DWORD (WINAPI *pLeaseArm)( UINT64, DWORD );
extern const WCHAR wexelevate_consent_exe[];   /* the consent program's file name in system32 */

DWORD pipe_io( HANDLE pipe, void *buf, DWORD size, BOOL write, DWORD timeout, DWORD *done );
DWORD check_binding( struct request *req, DWORD pid, HANDLE pipe_token, BOOL capture );
DWORD verify_image( struct request *req );
BOOL start_guard( struct request *req, HANDLE process );
DWORD secure_enter( void );
void secure_leave( void );
DWORD consent_timeout( void );
void report( DWORD state, DWORD error );

/* ---- broker0.c -------------------------------------------------------------------------------------------- */

BOOL e1r9_is_session0( void );
void e1r9_broker0_serve( void );

#endif /* _WIN32 && WEXELEVATE_E1S18 */

#endif /* WEXELEVATE_BROKER_INTERNAL_H */
