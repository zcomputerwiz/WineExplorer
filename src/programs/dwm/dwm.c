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

/* change log: a monotonically increasing revision and a ring of the last LOG_SIZE changes,
 * so clients can poll for attribute changes (DWM_OP_POLL) */
#define LOG_SIZE 1024

struct log_entry { uint64_t revision; uint32_t hwnd; uint32_t attribute; uint32_t value; };

static uint64_t revision = 1;
static struct log_entry change_log[LOG_SIZE];
static unsigned int log_head;   /* index of the oldest entry */
static unsigned int log_count;  /* number of valid entries, up to LOG_SIZE */

static void log_change( uint32_t hwnd, uint32_t attribute, uint32_t value )
{
    unsigned int idx = (log_head + log_count) % LOG_SIZE;

    revision++;
    change_log[idx].revision  = revision;
    change_log[idx].hwnd      = hwnd;
    change_log[idx].attribute = attribute;
    change_log[idx].value     = value;
    if (log_count == LOG_SIZE) log_head = (log_head + 1) % LOG_SIZE;
    else log_count++;
}

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
        else
        {
            log_change( attrs[i].hwnd, 0, 0 );  /* attribute 0, value 0: the window is gone */
            attrs[i] = attrs[--attr_count];
        }
    }
}

/* live thumbnails (docs/DWM_ARCHITECTURE.md, P8): keyed by the server-stamped sender process and the
 * HTHUMBNAIL value (handles are per process, so two processes both have handle 1); entries of destroyed
 * windows are dropped by the sweep, which also covers a process that exited */
struct thumb_entry { uint32_t pid; uint32_t handle; struct dwm_thumb thumb; BOOL used; };

static struct thumb_entry thumbs[DWM_THUMB_MAX];
static uint64_t thumb_revision = 1;

static struct thumb_entry *find_thumb( uint32_t pid, uint32_t handle )
{
    unsigned int i;

    for (i = 0; i < DWM_THUMB_MAX; i++)
        if (thumbs[i].used && thumbs[i].pid == pid && thumbs[i].handle == handle) return &thumbs[i];
    return NULL;
}

static void sweep_thumbs(void)
{
    unsigned int i;
    BOOL dropped = FALSE;

    for (i = 0; i < DWM_THUMB_MAX; i++)
    {
        if (!thumbs[i].used) continue;
        if (IsWindow( UlongToHandle( thumbs[i].thumb.dest ) ) &&
            IsWindow( UlongToHandle( thumbs[i].thumb.src ) )) continue;
        thumbs[i].used = FALSE;
        dropped = TRUE;
    }
    if (dropped) thumb_revision++;
}

static BOOL valid_attribute( uint32_t attribute )
{
    return attribute != 0 && (attribute < DWMWA_LAST || attribute == DWMWA_MICA_EFFECT ||
                              attribute == DWM_ATTR_WINE_BLUR);
}

/* buf holds a request (header and payload); on success it holds the reply and *reply_size its size */
static NTSTATUS handle_request( unsigned char *buf, uint32_t size, uint32_t sender_pid, uint32_t *reply_size )
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
            entry->value     = set.value;
            log_change( hdr->window_token, set.attribute, set.value );
        }
        else if (entry->value != set.value)
        {
            entry->value = set.value;
            log_change( hdr->window_token, set.attribute, set.value );
        }
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

    case DWM_OP_POLL:
    {
        struct dwm_poll_req poll;
        struct dwm_poll_reply *prep;
        struct dwm_change *changes;
        uint32_t count = 0, flags = 0, i;
        uint64_t reply_revision;

        if (hdr->length != sizeof(poll)) return STATUS_INVALID_PARAMETER;
        memcpy( &poll, payload, sizeof(poll) );
        prep    = (struct dwm_poll_reply *)payload;
        changes = (struct dwm_change *)(payload + sizeof(*prep));

        if (poll.since_revision >= revision)
        {
            reply_revision = revision;
        }
        else if (poll.since_revision == 0 || (log_count && poll.since_revision < change_log[log_head].revision))
        {
            /* the log overflowed, or the client wants the whole table: page through the attribute table */
            uint32_t start = poll.cursor < attr_count ? poll.cursor : attr_count;

            while (count < DWM_POLL_MAX_CHANGES && start + count < attr_count)
            {
                changes[count].hwnd      = attrs[start + count].hwnd;
                changes[count].attribute = attrs[start + count].attribute;
                changes[count].value     = attrs[start + count].value;
                changes[count].pad       = 0;
                count++;
            }
            flags |= DWM_POLL_SNAPSHOT;
            if (start + count < attr_count) flags |= DWM_POLL_MORE;
            reply_revision = revision;
        }
        else
        {
            /* page through the change log, oldest first */
            uint32_t total = 0, n = 0;
            uint64_t last = 0;

            for (i = 0; i < log_count; i++)
                if (change_log[(log_head + i) % LOG_SIZE].revision > poll.since_revision) total++;
            count = total < DWM_POLL_MAX_CHANGES ? total : DWM_POLL_MAX_CHANGES;
            for (i = 0; i < log_count && n < count; i++)
            {
                const struct log_entry *e = &change_log[(log_head + i) % LOG_SIZE];

                if (e->revision <= poll.since_revision) continue;
                changes[n].hwnd      = e->hwnd;
                changes[n].attribute = e->attribute;
                changes[n].value     = e->value;
                changes[n].pad       = 0;
                last = e->revision;
                n++;
            }
            if (total > count)
            {
                flags |= DWM_POLL_MORE;
                reply_revision = last;
            }
            else
                reply_revision = revision;
        }

        prep->revision = reply_revision;
        prep->count    = count;
        prep->flags    = flags;
        hdr->length    = sizeof(*prep) + count * sizeof(*changes);
        *reply_size    = sizeof(*hdr) + hdr->length;
        return STATUS_SUCCESS;
    }

    case DWM_OP_THUMB_SET:
    {
        struct dwm_thumb_set set;
        struct thumb_entry *tentry;
        unsigned int i;

        if (hdr->length != sizeof(set)) return STATUS_INVALID_PARAMETER;
        memcpy( &set, payload, sizeof(set) );
        if (!hdr->window_token || !set.src) return STATUS_INVALID_PARAMETER;
        if (set.flags & ~(DWM_THUMB_VISIBLE | DWM_THUMB_CLIENT_ONLY | DWM_THUMB_HAS_SOURCE))
            return STATUS_INVALID_PARAMETER;
        if (set.opacity > 255) return STATUS_INVALID_PARAMETER;
        if (set.dest[2] < set.dest[0] || set.dest[3] < set.dest[1]) return STATUS_INVALID_PARAMETER;
        if (!(tentry = find_thumb( sender_pid, set.handle )))
        {
            for (i = 0; i < DWM_THUMB_MAX; i++)
                if (!thumbs[i].used) break;
            if (i == DWM_THUMB_MAX) return STATUS_QUOTA_EXCEEDED;
            tentry = &thumbs[i];
            tentry->pid    = sender_pid;
            tentry->handle = set.handle;
            tentry->used   = TRUE;
        }
        tentry->thumb.dest    = (uint32_t)hdr->window_token;
        tentry->thumb.src     = set.src;
        tentry->thumb.flags   = set.flags;
        tentry->thumb.opacity = set.opacity;
        memcpy( tentry->thumb.dest_rect, set.dest, sizeof(set.dest) );
        memcpy( tentry->thumb.source, set.source, sizeof(set.source) );
        thumb_revision++;
        hdr->length  = 0;
        *reply_size  = sizeof(*hdr);
        return STATUS_SUCCESS;
    }

    case DWM_OP_THUMB_DEL:
    {
        struct dwm_thumb_del del;
        struct thumb_entry *tentry;

        if (hdr->length != sizeof(del)) return STATUS_INVALID_PARAMETER;
        memcpy( &del, payload, sizeof(del) );
        if (!(tentry = find_thumb( sender_pid, del.handle ))) return STATUS_NOT_FOUND;
        tentry->used = FALSE;
        thumb_revision++;
        hdr->length  = 0;
        *reply_size  = sizeof(*hdr);
        return STATUS_SUCCESS;
    }

    case DWM_OP_THUMB_LIST:
    {
        struct dwm_thumb_list_req list;
        struct dwm_thumb_list_reply *lrep;
        struct dwm_thumb *entries;
        uint32_t count = 0, i;

        if (hdr->length != sizeof(list)) return STATUS_INVALID_PARAMETER;
        memcpy( &list, payload, sizeof(list) );
        lrep    = (struct dwm_thumb_list_reply *)payload;
        entries = (struct dwm_thumb *)(payload + sizeof(*lrep));

        if (list.since_revision != thumb_revision)
            for (i = 0; i < DWM_THUMB_MAX; i++)
                if (thumbs[i].used)
                {
                    entries[count] = thumbs[i].thumb;
                    count++;
                }
        lrep->revision = thumb_revision;
        lrep->count    = count;
        lrep->pad      = 0;
        hdr->length    = sizeof(*lrep) + count * sizeof(*entries);
        *reply_size    = sizeof(*hdr) + hdr->length;
        return STATUS_SUCCESS;
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
    DWORD last_sweep = GetTickCount();

    for (;;)
    {
        /* by elapsed time: the shell's bridge polls several times a second, so the broker is rarely idle */
        if (GetTickCount() - last_sweep >= 5000)
        {
            sweep_attrs();
            sweep_thumbs();
            last_sweep = GetTickCount();
        }
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
            continue;
        }
        if (status != STATUS_SUCCESS) return;  /* revoked, closed or the desktop is gone */
        {
            uint32_t id = call.request_id, size = call.out_len, reply_size = 0;
            NTSTATUS result = handle_request( buf, size, call.sender_pid, &reply_size );

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
