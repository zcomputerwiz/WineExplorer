/*
 * Desktop Window Manager broker (docs/DWM_ARCHITECTURE.md section 3, phase P2).
 *
 * One instance per ordinary desktop, started by explorer's desktop manager with
 *   dwm.exe /parent=<explorer pid>   (the one-use broker capability, 16 hex digits, arrives on stdin)
 * on the desktop it serves. It registers as the broker of that desktop's control channel and answers requests.
 * It owns no display connection and draws nothing. It ends when the channel is revoked or the parent exits.
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

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "winternl.h"
#include "wine/dwm_protocol.h"

static NTSTATUS channel_call( struct dwm_channel_call *call )
{
    return NtSetInformationProcess( GetCurrentProcess(), ProcessWineDwmChannel, call, sizeof(*call) );
}

/* the broker serves the desktop it was started on */
static BOOL use_startup_desktop(void)
{
    /* the process starts on the desktop named in STARTUPINFO, and so does its first thread */
    return GetThreadDesktop( GetCurrentThreadId() ) != NULL;
}

static void serve( HANDLE broker, HANDLE parent )
{
    unsigned char buf[sizeof(struct dwm_msg_header) + DWM_MAX_MESSAGE_SIZE];
    struct dwm_channel_call call;
    NTSTATUS status;

    for (;;)
    {
        memset( &call, 0, sizeof(call) );
        call.op      = DWM_CHANNEL_OP_RECV;
        call.handle  = HandleToUlong( broker );
        call.out_ptr = (uintptr_t)buf;
        call.out_len = sizeof(buf);
        status = channel_call( &call );
        if (status == STATUS_PENDING)
        {
            HANDLE objs[2] = { broker, parent };
            DWORD count = parent ? 2 : 1;
            if (WaitForMultipleObjects( count, objs, FALSE, INFINITE ) == WAIT_OBJECT_0 + 1) return;
            continue;
        }
        if (status != STATUS_SUCCESS) return;  /* revoked, closed or the desktop is gone */
        {
            const struct dwm_msg_header *hdr = (const struct dwm_msg_header *)buf;
            uint32_t id = call.request_id, size = call.out_len;
            BOOL known = size >= sizeof(*hdr) && hdr->opcode == DWM_OP_ECHO;

            memset( &call, 0, sizeof(call) );
            call.op         = DWM_CHANNEL_OP_REPLY;
            call.handle     = HandleToUlong( broker );
            call.request_id = id;
            call.status     = known ? STATUS_SUCCESS : STATUS_NOT_IMPLEMENTED;
            call.in_ptr     = known ? (uintptr_t)buf : 0;
            call.in_len     = known ? size : 0;
            channel_call( &call );
        }
    }
}

int __cdecl wmain( int argc, WCHAR **argv )
{
    struct dwm_channel_call call;
    HANDLE parent = NULL;
    uint64_t capability = 0;
    NTSTATUS status;
    int i;

    for (i = 1; i < argc; i++)
    {
        if (!wcsncmp( argv[i], L"/parent=", 8 ))
            parent = OpenProcess( SYNCHRONIZE, FALSE, wcstoul( argv[i] + 8, NULL, 10 ) );
    }
    {
        /* the capability comes through stdin so that it is not on a command line other processes can read */
        char text[17] = { 0 };
        DWORD read = 0;

        if (!ReadFile( GetStdHandle( STD_INPUT_HANDLE ), text, 16, &read, NULL ) || read != 16)
        {
            fprintf( stderr, "dwm: no capability on stdin (error %lu)\n", GetLastError() );
            return 2;
        }
        capability = strtoull( text, NULL, 16 );
    }
    if (!capability) return 2;

    if (!use_startup_desktop())
    {
        fprintf( stderr, "dwm: cannot use the startup desktop (error %lu)\n", GetLastError() );
        return 4;
    }

    memset( &call, 0, sizeof(call) );
    call.op         = DWM_CHANNEL_OP_BROKER_REGISTER;
    call.capability = capability;
    if ((status = channel_call( &call )))
    {
        fprintf( stderr, "dwm: broker register failed %08lx\n", status );
        return 3;
    }

    serve( UlongToHandle( call.handle ), parent );
    return 0;
}
