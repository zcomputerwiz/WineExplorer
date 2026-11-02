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
#include "dwmapi.h"
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

#ifndef DWMWA_MICA_EFFECT
#define DWMWA_MICA_EFFECT 1029
#endif

/* Scalar window attributes, the canonical copy (docs/DWM_ARCHITECTURE.md section 5). The server has already
 * checked that the window belongs to this desktop; entries of destroyed windows are dropped by the sweep. */
#define MAX_ATTRS 4096

struct attr_entry { uint32_t hwnd, attribute, value; };

static struct attr_entry attrs[MAX_ATTRS];
static unsigned int attr_count;

static struct attr_entry *find_attr( uint32_t hwnd, uint32_t attribute )
{
    unsigned int i;

    for (i = 0; i < attr_count; i++)
        if (attrs[i].hwnd == hwnd && attrs[i].attribute == attribute) return &attrs[i];
    return NULL;
}

static void sweep_attrs(void)
{
    unsigned int i = 0;

    while (i < attr_count)
    {
        if (IsWindow( UlongToHandle( attrs[i].hwnd ) )) i++;
        else attrs[i] = attrs[--attr_count];
    }
}

static BOOL valid_attribute( uint32_t attribute )
{
    return attribute != 0 && (attribute < DWMWA_LAST || attribute == DWMWA_MICA_EFFECT);
}

/* buf holds a request (header and payload); on success it holds the reply and *reply_size its size */
static NTSTATUS handle_request( unsigned char *buf, uint32_t size, uint32_t *reply_size )
{
    struct dwm_msg_header *hdr = (struct dwm_msg_header *)buf;
    unsigned char *payload = buf + sizeof(*hdr);
    struct dwm_attr_reply *rep;
    struct attr_entry *entry;

    if (size < sizeof(*hdr) || hdr->length != size - sizeof(*hdr)) return STATUS_INVALID_PARAMETER;

    switch (hdr->opcode)
    {
    case DWM_OP_ECHO:
        *reply_size = size;
        return STATUS_SUCCESS;

    case DWM_OP_SET_ATTR:
    {
        struct dwm_attr_set set;

        if (hdr->length != sizeof(set)) return STATUS_INVALID_PARAMETER;
        memcpy( &set, payload, sizeof(set) );
        if (!hdr->window_token || !valid_attribute( set.attribute )) return STATUS_INVALID_PARAMETER;
        if (!(entry = find_attr( hdr->window_token, set.attribute )))
        {
            if (attr_count == MAX_ATTRS) sweep_attrs();
            if (attr_count == MAX_ATTRS) return STATUS_QUOTA_EXCEEDED;
            entry = &attrs[attr_count++];
            entry->hwnd      = hdr->window_token;
            entry->attribute = set.attribute;
        }
        entry->value = set.value;
        rep = (struct dwm_attr_reply *)payload;
        rep->found = 1;
        rep->value = set.value;
        break;
    }

    case DWM_OP_GET_ATTR:
    {
        struct dwm_attr_get get;

        if (hdr->length != sizeof(get)) return STATUS_INVALID_PARAMETER;
        memcpy( &get, payload, sizeof(get) );
        entry = hdr->window_token ? find_attr( hdr->window_token, get.attribute ) : NULL;
        rep = (struct dwm_attr_reply *)payload;
        rep->found = entry != NULL;
        rep->value = entry ? entry->value : 0;
        break;
    }

    default:
        return STATUS_NOT_IMPLEMENTED;
    }

    hdr->length = sizeof(*rep);
    *reply_size = sizeof(*hdr) + sizeof(*rep);
    return STATUS_SUCCESS;
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
            DWORD wait = WaitForMultipleObjects( count, objs, FALSE, 5000 );
            if (wait == WAIT_OBJECT_0 + 1) return;
            if (wait == WAIT_TIMEOUT) sweep_attrs();
            continue;
        }
        if (status != STATUS_SUCCESS) return;  /* revoked, closed or the desktop is gone */
        {
            uint32_t id = call.request_id, size = call.out_len, reply_size = 0;
            NTSTATUS result = handle_request( buf, size, &reply_size );

            memset( &call, 0, sizeof(call) );
            call.op         = DWM_CHANNEL_OP_REPLY;
            call.handle     = HandleToUlong( broker );
            call.request_id = id;
            call.status     = result;
            call.in_ptr     = result ? 0 : (uintptr_t)buf;
            call.in_len     = result ? 0 : reply_size;
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
