/*
 * wexjoin: the domain join engine's request, as the PE side and the unix side share it
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

#ifndef __WEXJOIN_H
#define __WEXJOIN_H

/* docs/DOMAIN_JOIN.md, "How the API reaches root". The engine (wex-domain-join.sh serve) listens here; only the system
 * group can connect. A request is lines of key=value ended by an empty line, the answer status=N (a Win32 or network
 * error code) and message=TEXT. */
#define WEX_JOIN_SOCKET        "/run/wex-domain-join.sock"
#define WEX_JOIN_REQUEST_MAX   4096
#define WEX_JOIN_REPLY_MAX     2048
/* a join takes a minute or two (the controller, the account, winbind); the engine's unit runs at most 300 seconds
 * (RuntimeMaxSec) and then has 90 to put a rename back (TimeoutStopSec): the caller waits longer than both, so that
 * the answer it gets is the engine's (installer/systemd/wex-domain-join@.service) */
#define WEX_JOIN_WAIT_SECONDS  420

/* the NETSETUP_ flags of NetJoinDomain and NetUnjoinDomain (lmjoin.h of the Windows SDK; Wine's header lacks them) */
#define WEX_NETSETUP_JOIN_DOMAIN             0x00000001
#define WEX_NETSETUP_ACCT_CREATE             0x00000002
#define WEX_NETSETUP_ACCT_DELETE             0x00000004
#define WEX_NETSETUP_WIN9X_UPGRADE           0x00000010
#define WEX_NETSETUP_DOMAIN_JOIN_IF_JOINED   0x00000020
#define WEX_NETSETUP_JOIN_UNSECURE           0x00000040
#define WEX_NETSETUP_MACHINE_PWD_PASSED      0x00000080
#define WEX_NETSETUP_DEFER_SPN_SET           0x00000100
#define WEX_NETSETUP_JOIN_DC_ACCOUNT         0x00000200
#define WEX_NETSETUP_JOIN_WITH_NEW_NAME      0x00000400
#define WEX_NETSETUP_JOIN_READONLY           0x00000800
#define WEX_NETSETUP_INSTALL_INVOCATION      0x00040000
#define WEX_NETSETUP_IGNORE_UNSUPPORTED_FLAGS 0x10000000

/* the types of NetValidateName */
#define WEX_NETSETUP_NAME_UNKNOWN            0
#define WEX_NETSETUP_NAME_MACHINE            1
#define WEX_NETSETUP_NAME_WORKGROUP          2
#define WEX_NETSETUP_NAME_DOMAIN             3
#define WEX_NETSETUP_NAME_NONEXISTENT_DOMAIN 4
#define WEX_NETSETUP_NAME_DNS_MACHINE        5

/* the operations of WexJoinSecureChannel: their answer is WexJoinLastMessage's text */
#define WEX_JOIN_OP_VERIFY   1    /* netdom verify, nltest /sc_verify */
#define WEX_JOIN_OP_RESET    2    /* netdom reset, nltest /sc_reset */
#define WEX_JOIN_OP_DCLIST   3    /* the controllers DNS lists, separated by spaces */
#define WEX_JOIN_OP_DSGETDC  4    /* name=value fields separated by "||" */
#define WEX_JOIN_OP_SCQUERY  5    /* dc= and status= separated by "||" */

#if !defined(WINE_UNIX_LIB) && defined(WINAPI)

/* netapi32's join functions (forwarded to wexjoin.dll): Wine's lmjoin.h declares only NetGetJoinInformation */
DWORD WINAPI NetJoinDomain( const WCHAR *server, const WCHAR *domain, const WCHAR *ou, const WCHAR *account,
                            const WCHAR *password, DWORD flags );
DWORD WINAPI NetUnjoinDomain( const WCHAR *server, const WCHAR *account, const WCHAR *password, DWORD flags );
DWORD WINAPI NetValidateName( const WCHAR *server, const WCHAR *name, const WCHAR *account, const WCHAR *password,
                              DWORD type );
DWORD WINAPI NetRenameMachineInDomain( const WCHAR *server, const WCHAR *new_name, const WCHAR *account,
                                       const WCHAR *password, DWORD flags );

/* wexjoin's own exports, for the tools and the dialogs: wexjoin.dll is loaded when asked for, so that a program that
 * only shows the membership does not need it */
static inline DWORD wex_join_secure_channel( DWORD op )
{
    HMODULE module = LoadLibraryW( L"wexjoin.dll" );
    DWORD (WINAPI *channel)( DWORD ) = module ? (void *)GetProcAddress( module, "WexJoinSecureChannel" ) : NULL;

    return channel ? channel( op ) : ERROR_NOT_SUPPORTED;
}

/* the engine's message about the last call (Samba's reason, its own, or an operation's answer); empty when none */
static inline void wex_join_last_message( WCHAR *text, DWORD chars )
{
    HMODULE module = LoadLibraryW( L"wexjoin.dll" );
    DWORD (WINAPI *message)( WCHAR *, DWORD ) = module ? (void *)GetProcAddress( module, "WexJoinLastMessage" ) : NULL;

    if (!chars) return;
    text[0] = 0;
    if (message) message( text, chars );
}

#endif

#endif
