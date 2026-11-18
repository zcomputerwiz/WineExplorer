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
#define WEX_JOIN_WAIT_SECONDS  300     /* a join takes a minute or two: the controller, the account, winbind */

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

#endif
