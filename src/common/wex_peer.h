/*
 * WineExplorer: the peer check of a native socket server (SO_PEERCRED), shared by wexprefetchd and
 * wex-journal-bridge
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef WEX_PEER_H
#define WEX_PEER_H

#include <sys/socket.h>
#include <sys/types.h>

/* The kernel's word for who is at the other end of a connected AF_UNIX socket: its uid and pid at connect time.
 * A server that admits one account compares the uid, never a group (a group can be held by others: elevated
 * processes carry the system group). 0, or -1 when the kernel does not say. */
static inline int wex_peer_cred( int fd, uid_t *uid, pid_t *pid )
{
    struct ucred cred;
    socklen_t len = sizeof(cred);

    if (getsockopt( fd, SOL_SOCKET, SO_PEERCRED, &cred, &len ) < 0 || len != sizeof(cred)) return -1;
    if (uid) *uid = cred.uid;
    if (pid) *pid = cred.pid;
    return 0;
}

#endif /* WEX_PEER_H */
