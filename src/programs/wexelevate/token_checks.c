/*
 * The elevation broker's standard-user path (lab only): token and SID checks (token_checks.h).
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

/* A lab unit (broker_internal.h): empty in a shipping Wine build. */
#if defined(WEXELEVATE_E1S18) || !defined(__WINE_PE_BUILD)

#include <stdlib.h>
#include <string.h>
#include "token_checks.h"

BOOL wex_sid_well_formed( const BYTE *sid, DWORD len )
{
    if (!sid || len < WEX_SID_HEADER_SIZE || len > SECURITY_MAX_SID_SIZE) return FALSE;
    if (sid[0] != 1 || sid[1] > WEX_SID_MAX_SUBAUTH) return FALSE;
    if (len != WEX_SID_HEADER_SIZE + 4u * sid[1]) return FALSE;
#ifdef _WIN32
    if (!IsValidSid( (PSID)sid ) || GetLengthSid( (PSID)sid ) != len) return FALSE;
#endif
    return TRUE;
}

BOOL wex_sid_and_attributes_layout( const void *buf, DWORD len, DWORD size, const BYTE **sid, DWORD *sid_len )
{
    const BYTE *base = buf, *p;
    void *ptr;
    size_t offset, avail;
    DWORD need;

    if (sid) *sid = NULL;
    if (sid_len) *sid_len = 0;
    if (!buf || !sid || !sid_len || len > size || len < sizeof(wex_sid_and_attributes)) return FALSE;
    memcpy( &ptr, base + offsetof( wex_sid_and_attributes, Sid ), sizeof(ptr) );
    p = ptr;
    if ((uintptr_t)p < (uintptr_t)base) return FALSE;
    offset = (uintptr_t)p - (uintptr_t)base;
    if (offset < sizeof(wex_sid_and_attributes) || offset > len) return FALSE;   /* not over the header */
    avail = len - offset;
    if (avail < WEX_SID_HEADER_SIZE || p[1] > WEX_SID_MAX_SUBAUTH) return FALSE;
    need = WEX_SID_HEADER_SIZE + 4u * p[1];
    if (avail < need || !wex_sid_well_formed( p, need )) return FALSE;
    *sid = p;
    *sid_len = need;
    return TRUE;
}

BOOL wex_sid_equal( const BYTE *sid, DWORD sid_len, const BYTE *expected, DWORD expected_len )
{
    if (!wex_sid_well_formed( sid, sid_len ) || !wex_sid_well_formed( expected, expected_len )) return FALSE;
    return sid_len == expected_len && !memcmp( sid, expected, sid_len );
}

#ifdef _WIN32

/* One SID_AND_ATTRIBUTES class at the size the token reports (bounded), checked; the SID is copied out. */
static BOOL token_sid_class( HANDLE token, TOKEN_INFORMATION_CLASS class, BYTE out[SECURITY_MAX_SID_SIZE],
                             DWORD *out_len )
{
    const BYTE *sid;
    DWORD need = 0, len = 0, sid_len;
    BYTE *buf;
    BOOL ok = FALSE;

    *out_len = 0;
    if (GetTokenInformation( token, class, NULL, 0, &need ) || GetLastError() != ERROR_INSUFFICIENT_BUFFER ||
        need < sizeof(wex_sid_and_attributes) || need > 4096) return FALSE;
    if (!(buf = calloc( 1, need ))) return FALSE;
    if (GetTokenInformation( token, class, buf, need, &len ) &&
        wex_sid_and_attributes_layout( buf, len, need, &sid, &sid_len ))
    {
        memcpy( out, sid, sid_len );
        *out_len = sid_len;
        ok = TRUE;
    }
    free( buf );
    return ok;
}

BOOL wex_token_user( HANDLE token, BYTE sid[SECURITY_MAX_SID_SIZE], DWORD *sid_len )
{
    return token_sid_class( token, TokenUser, sid, sid_len );
}

BOOL wex_token_user_is( HANDLE token, const BYTE *expected, DWORD expected_len )
{
    BYTE sid[SECURITY_MAX_SID_SIZE];
    DWORD len;

    if (!wex_sid_well_formed( expected, expected_len )) return FALSE;
    return wex_token_user( token, sid, &len ) && wex_sid_equal( sid, len, expected, expected_len );
}

BOOL wex_token_integrity_at_least( HANDLE token, DWORD rid )
{
    BYTE sid[SECURITY_MAX_SID_SIZE];
    DWORD len, last;

    if (!token_sid_class( token, TokenIntegrityLevel, sid, &len ) || sid[1] < 1) return FALSE;
    memcpy( &last, sid + len - 4, 4 );   /* the last subauthority */
    return last >= rid;
}

void wex_revert_to_self( void )
{
    if (RevertToSelf()) return;
    TerminateProcess( GetCurrentProcess(), ERROR_ACCESS_DENIED );
    for (;;) Sleep( INFINITE );   /* never go on as the client */
}

#endif /* _WIN32 */

#else
typedef int wexelevate_lab_unit_empty;   /* ISO C: no empty translation unit */
#endif /* WEXELEVATE_E1S18 || !__WINE_PE_BUILD */
