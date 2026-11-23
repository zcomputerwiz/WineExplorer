/*
 * The elevation broker's standard-user path (lab only): the token and SID checks its units share (token_checks.c).
 *
 * Every check fails closed. A TokenUser (or TokenIntegrityLevel) answer is read at the size the token reports, never
 * into a fixed buffer, and its SID must lie wholly inside the bytes the call wrote; an absent or empty expected SID
 * never matches; a thread that cannot end its impersonation ends the process. The layout checks are pure C, so the
 * native unit tests run them; the calls on tokens are Win32.
 *
 * The shipping broker (main.c alone) keeps its own token_user_sid, which applies the same layout rule; it moves to
 * these functions when the standard-user path ships and token_checks.c joins the shipping build.
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

#ifndef WEXELEVATE_TOKEN_CHECKS_H
#define WEXELEVATE_TOKEN_CHECKS_H

#include <stddef.h>
#include <stdint.h>
#include "wexelevate_request.h"

/* The shape GetTokenInformation writes for TokenUser and TokenIntegrityLevel: one SID_AND_ATTRIBUTES whose SID
 * pointer points into the same buffer. Natively a struct of the same layout, for the unit tests. */
#ifdef _WIN32
typedef SID_AND_ATTRIBUTES wex_sid_and_attributes;
#else
typedef struct
{
    void    *Sid;
    uint32_t Attributes;
} wex_sid_and_attributes;
#endif

#define WEX_SID_HEADER_SIZE 8    /* revision, subauthority count, identifier authority */
#define WEX_SID_MAX_SUBAUTH 15

/* A well-formed SID of exactly len bytes: revision 1, at most 15 subauthorities, 8 + 4 * count bytes. */
BOOL wex_sid_well_formed( const BYTE *sid, DWORD len );

/* buf holds the len bytes GetTokenInformation wrote into a buffer of size bytes, one SID_AND_ATTRIBUTES first.
 * TRUE when len covers the header, len <= size, and the whole SID it points to lies inside the len bytes and is
 * well formed; *sid and *sid_len then name it. */
BOOL wex_sid_and_attributes_layout( const void *buf, DWORD len, DWORD size, const BYTE **sid, DWORD *sid_len );

/* TRUE only when both SIDs are present, well formed and equal: an empty or absent expected SID never matches. */
BOOL wex_sid_equal( const BYTE *sid, DWORD sid_len, const BYTE *expected, DWORD expected_len );

#ifdef _WIN32
/* The token's user SID, queried at the size the token reports and checked as above, copied into sid. */
BOOL wex_token_user( HANDLE token, BYTE sid[SECURITY_MAX_SID_SIZE], DWORD *sid_len );
/* The token's user is exactly the expected SID (present and non-empty). */
BOOL wex_token_user_is( HANDLE token, const BYTE *expected, DWORD expected_len );
/* The token's mandatory label has at least one subauthority and its last one is at least rid. */
BOOL wex_token_integrity_at_least( HANDLE token, DWORD rid );
/* End the thread's impersonation. A thread that cannot revert would go on with the client's identity: the process
 * ends instead (fail closed); this function returns only after a successful revert. */
void wex_revert_to_self( void );
#endif

#endif /* WEXELEVATE_TOKEN_CHECKS_H */
