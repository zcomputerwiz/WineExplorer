/*
 * Desktop Window Manager broker (docs/DWM_ARCHITECTURE.md sections 2.0 and 3).
 *
 * One instance per ordinary desktop, started by explorer's desktop manager with
 *   dwm.exe /parent=<explorer pid>
 * on the desktop it serves. It creates that desktop's pipe (dwm_pipe_name; it exits when another process holds the
 * name) and answers requests, one thread per client. Every message's window is checked here: it must be on this
 * desktop, and only its owner may change it. It owns no display connection and draws nothing. It ends when the
 * parent exits.
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
#include "ntsecapi.h"
#include "sddl.h"
#include "dwmapi.h"
#include "wine/dwm_protocol.h"
#include "wine/wex_pipe.h"

/* the broker's state: attributes, change log, thumbnails; the client threads and the sweep take it */
static CRITICAL_SECTION state_cs;

/* a pipe client; the pid of a pipe's client is fixed for the connection (0 for a client bridged from elsewhere) */
struct client
{
    HANDLE pipe;
    ULONG pid;
    uint32_t id;
    BOOL has_window;   /* seen owning a window on this desktop (the read rule under enforcement) */
};

static uint64_t generation;

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

/* The shell's bridge waits on this per-desktop event before reading changes
 * (DWM_OP_POLL, DWM_OP_THUMB_LIST). Revisions preserve changes when signals coalesce. */
static HANDLE change_event;

static void notify_change(void)
{
    if (!change_event)
    {
        WCHAR name[MAX_PATH + 64];

        if (!dwm_desktop_object_name( GetThreadDesktop( GetCurrentThreadId() ), DWM_CHANGE_EVENT_NAME, name,
                                      ARRAY_SIZE(name) )) return;
        if (!(change_event = CreateEventW( NULL, FALSE, FALSE, name ))) return;
    }
    SetEvent( change_event );
}
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
    notify_change();
}

static struct attr_entry *find_attr( uint32_t hwnd, uint32_t attribute )
{
    unsigned int i;

    for (i = 0; i < attr_count; i++)
        if (attrs[i].hwnd == hwnd && attrs[i].attribute == attribute) return &attrs[i];
    return NULL;
}

/* A dead window's entries become holes (hwnd 0), reused by later entries: no entry moves, so a snapshot paged by
 * index (DWM_POLL_SNAPSHOT) neither skips nor repeats one. attr_count is the high-water mark. */
static void sweep_attrs(void)
{
    unsigned int i;

    for (i = 0; i < attr_count; i++)
    {
        if (!attrs[i].hwnd || IsWindow( UlongToHandle( attrs[i].hwnd ) )) continue;
        log_change( attrs[i].hwnd, 0, 0 );  /* attribute 0, value 0: the window is gone */
        memset( &attrs[i], 0, sizeof(attrs[i]) );
    }
    while (attr_count && !attrs[attr_count - 1].hwnd) attr_count--;
}

static struct attr_entry *new_attr(void)
{
    unsigned int i;

    for (i = 0; i < attr_count; i++) if (!attrs[i].hwnd) return &attrs[i];
    if (attr_count == MAX_ATTRS) return NULL;
    return &attrs[attr_count++];
}

/* live thumbnails (docs/DWM_ARCHITECTURE.md, P8): keyed by the connection that set them and the HTHUMBNAIL value
 * (handles are per process, so two processes both have handle 1); a closed connection drops its thumbnails, the
 * sweep those of destroyed windows */
struct thumb_entry { uint32_t client; uint32_t handle; struct dwm_thumb thumb; BOOL used; };

static struct thumb_entry thumbs[DWM_THUMB_MAX];
static uint64_t thumb_revision = 1;

static struct thumb_entry *find_thumb( uint32_t client, uint32_t handle )
{
    unsigned int i;

    for (i = 0; i < DWM_THUMB_MAX; i++)
        if (thumbs[i].used && thumbs[i].client == client && thumbs[i].handle == handle) return &thumbs[i];
    return NULL;
}

static void drop_client_thumbs( uint32_t client )
{
    unsigned int i;
    BOOL dropped = FALSE;

    for (i = 0; i < DWM_THUMB_MAX; i++)
    {
        if (!thumbs[i].used || thumbs[i].client != client) continue;
        thumbs[i].used = FALSE;
        dropped = TRUE;
    }
    if (dropped)
    {
        thumb_revision++;
        notify_change();
    }
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
    if (dropped)
    {
        thumb_revision++;
        notify_change();
    }
}

static BOOL valid_attribute( uint32_t attribute )
{
    return attribute != 0 && (attribute < DWMWA_LAST || attribute == DWMWA_MICA_EFFECT ||
                              (attribute >= DWM_ATTR_WINE_BLUR && attribute <= DWM_ATTR_WINE_LAST));
}

/* The window lies on this broker's desktop: the parent of its root window is this desktop's window. */
static BOOL on_this_desktop( uint32_t window )
{
    HWND hwnd = UlongToHandle( window ), root;

    if (!window || !(root = GetAncestor( hwnd, GA_ROOT ))) return FALSE;
    return GetAncestor( root, GA_PARENT ) == GetDesktopWindow();
}

/* Only a window's own process changes its attributes and thumbnails; a client without a pid owns nothing. */
static BOOL owned_by( uint32_t window, const struct client *client )
{
    DWORD pid = 0;

    if (!client->pid || !GetWindowThreadProcessId( UlongToHandle( window ), &pid )) return FALSE;
    return pid == client->pid;
}

static BOOL CALLBACK find_client_window( HWND hwnd, LPARAM param )
{
    struct client *client = (struct client *)param;
    DWORD pid = 0;

    GetWindowThreadProcessId( hwnd, &pid );
    if (pid != client->pid) return TRUE;
    client->has_window = TRUE;
    return FALSE;
}

/* May the client read this desktop's state? Only a process with a window here: the pipe's name is not scoped to a
 * desktop, and the server's desktop separation (sysrun) cannot be seen from user mode, so the rule always applies.
 * A process without windows cannot read other processes' DWM attributes (on Windows it can; none is known to).
 * A yes is kept for the connection, a no is checked again. */
static BOOL may_read( struct client *client )
{
    if (client->has_window) return TRUE;
    if (client->pid) EnumWindows( find_client_window, (LPARAM)client );
    return client->has_window;
}

/* buf holds a request (header and payload); on success it holds the reply's payload after the reply header's
 * place and *reply_length its length. Called with state_cs held. */
static NTSTATUS handle_request( unsigned char *buf, uint32_t size, struct client *client, uint32_t *reply_length )
{
    struct dwm_request req;
    unsigned char *payload = buf + sizeof(req);
    struct dwm_attr_reply *rep;
    struct attr_entry *entry;

    if (size < sizeof(req)) return STATUS_INVALID_PARAMETER;
    memcpy( &req, buf, sizeof(req) );
    if (req.length != size - sizeof(req)) return STATUS_INVALID_PARAMETER;
    if (req.version != DWM_PROTOCOL_VERSION) return STATUS_REVISION_MISMATCH;
    *reply_length = 0;

    switch (req.opcode)
    {
    case DWM_OP_ECHO:
        *reply_length = req.length;
        return STATUS_SUCCESS;

    case DWM_OP_SET_ATTR:
    {
        struct dwm_attr_set set;

        if (req.length != sizeof(set)) return STATUS_INVALID_PARAMETER;
        memcpy( &set, payload, sizeof(set) );
        if (!req.window || !valid_attribute( set.attribute )) return STATUS_INVALID_PARAMETER;
        if (!on_this_desktop( req.window ) || !owned_by( req.window, client )) return STATUS_ACCESS_DENIED;
        if (!(entry = find_attr( req.window, set.attribute )))
        {
            if (!(entry = new_attr()))
            {
                sweep_attrs();
                if (!(entry = new_attr())) return STATUS_QUOTA_EXCEEDED;
            }
            entry->hwnd      = req.window;
            entry->attribute = set.attribute;
            entry->value     = set.value;
            log_change( req.window, set.attribute, set.value );
        }
        else if (entry->value != set.value)
        {
            entry->value = set.value;
            log_change( req.window, set.attribute, set.value );
        }
        rep = (struct dwm_attr_reply *)payload;
        rep->found = 1;
        rep->value = set.value;
        *reply_length = sizeof(*rep);
        return STATUS_SUCCESS;
    }

    case DWM_OP_GET_ATTR:
    {
        struct dwm_attr_get get;

        if (req.length != sizeof(get)) return STATUS_INVALID_PARAMETER;
        memcpy( &get, payload, sizeof(get) );
        if (!on_this_desktop( req.window ) || !may_read( client )) return STATUS_ACCESS_DENIED;
        entry = find_attr( req.window, get.attribute );
        rep = (struct dwm_attr_reply *)payload;
        rep->found = entry != NULL;
        rep->value = entry ? entry->value : 0;
        *reply_length = sizeof(*rep);
        return STATUS_SUCCESS;
    }

    case DWM_OP_POLL:
    {
        struct dwm_poll_req poll;
        struct dwm_poll_reply *prep;
        struct dwm_change *changes;
        uint32_t count = 0, flags = 0, i;
        uint64_t reply_revision;

        if (req.length != sizeof(poll)) return STATUS_INVALID_PARAMETER;
        if (!may_read( client )) return STATUS_ACCESS_DENIED;
        memcpy( &poll, payload, sizeof(poll) );
        prep    = (struct dwm_poll_reply *)payload;
        changes = (struct dwm_change *)(payload + sizeof(*prep));

        if (poll.since_revision >= revision)
        {
            reply_revision = revision;
        }
        else if (poll.since_revision == 0 || (log_count && poll.since_revision < change_log[log_head].revision))
        {
            /* the log overflowed, or the client wants the whole table: page through the attribute table, holes
             * included (attribute 0 records, which the client ignores) so that the client's cursor stays an index.
             * The client resumes the log from the first page's revision, which covers changes made meanwhile. */
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
        *reply_length  = sizeof(*prep) + count * sizeof(*changes);
        return STATUS_SUCCESS;
    }

    case DWM_OP_THUMB_SET:
    {
        struct dwm_thumb_set set;
        struct thumb_entry *tentry;
        unsigned int i;

        if (req.length != sizeof(set)) return STATUS_INVALID_PARAMETER;
        memcpy( &set, payload, sizeof(set) );
        if (!req.window || !set.src) return STATUS_INVALID_PARAMETER;
        if (set.flags & ~(DWM_THUMB_VISIBLE | DWM_THUMB_CLIENT_ONLY | DWM_THUMB_HAS_SOURCE))
            return STATUS_INVALID_PARAMETER;
        if (set.opacity > 255) return STATUS_INVALID_PARAMETER;
        if (set.dest[2] < set.dest[0] || set.dest[3] < set.dest[1]) return STATUS_INVALID_PARAMETER;
        /* the destination is the caller's; the source may be another process's window, of this desktop too */
        if (!on_this_desktop( req.window ) || !owned_by( req.window, client ) || !on_this_desktop( set.src ))
            return STATUS_ACCESS_DENIED;
        if (!(tentry = find_thumb( client->id, set.handle )))
        {
            for (i = 0; i < DWM_THUMB_MAX; i++)
                if (!thumbs[i].used) break;
            if (i == DWM_THUMB_MAX) return STATUS_QUOTA_EXCEEDED;
            tentry = &thumbs[i];
            tentry->client = client->id;
            tentry->handle = set.handle;
            tentry->used   = TRUE;
        }
        tentry->thumb.dest    = req.window;
        tentry->thumb.src     = set.src;
        tentry->thumb.flags   = set.flags;
        tentry->thumb.opacity = set.opacity;
        memcpy( tentry->thumb.dest_rect, set.dest, sizeof(set.dest) );
        memcpy( tentry->thumb.source, set.source, sizeof(set.source) );
        thumb_revision++;
        notify_change();
        return STATUS_SUCCESS;
    }

    case DWM_OP_THUMB_DEL:
    {
        struct dwm_thumb_del del;
        struct thumb_entry *tentry;

        if (req.length != sizeof(del)) return STATUS_INVALID_PARAMETER;
        memcpy( &del, payload, sizeof(del) );
        if (!(tentry = find_thumb( client->id, del.handle ))) return STATUS_NOT_FOUND;
        tentry->used = FALSE;
        thumb_revision++;
        notify_change();
        return STATUS_SUCCESS;
    }

    case DWM_OP_THUMB_LIST:
    {
        struct dwm_thumb_list_req list;
        struct dwm_thumb_list_reply *lrep;
        struct dwm_thumb *entries;
        uint32_t count = 0, i;

        if (req.length != sizeof(list)) return STATUS_INVALID_PARAMETER;
        if (!may_read( client )) return STATUS_ACCESS_DENIED;
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
        *reply_length  = sizeof(*lrep) + count * sizeof(*entries);
        return STATUS_SUCCESS;
    }

    default:
        return STATUS_NOT_IMPLEMENTED;
    }
}

/* Connections hold a thread each, so they are capped: per process (a client with pid 0 has nothing to send: one)
 * and in total; over a cap the connection is closed at once. Under state_cs. */
#define MAX_CLIENTS          512
#define MAX_CLIENTS_PER_PID  8

static struct client *clients[MAX_CLIENTS];

static BOOL add_client( struct client *client )
{
    unsigned int i, same = 0, slot = MAX_CLIENTS;

    for (i = 0; i < MAX_CLIENTS; i++)
    {
        if (!clients[i]) { if (slot == MAX_CLIENTS) slot = i; }
        else if (clients[i]->pid == client->pid) same++;
    }
    if (slot == MAX_CLIENTS || same >= (client->pid ? MAX_CLIENTS_PER_PID : 1)) return FALSE;
    clients[slot] = client;
    return TRUE;
}

static void remove_client( struct client *client )
{
    unsigned int i;

    for (i = 0; i < MAX_CLIENTS; i++) if (clients[i] == client) clients[i] = NULL;
}

/* one thread per client: requests in, replies out, until the client goes away */
static DWORD WINAPI client_thread( void *arg )
{
    struct client *client = arg;
    unsigned char *buf = malloc( sizeof(struct dwm_request) + DWM_MAX_MESSAGE_SIZE );
    HANDLE event = CreateEventW( NULL, TRUE, FALSE, NULL );
    struct dwm_reply rep;
    DWORD size, done;

    while (buf && event &&
           !wex_pipe_io( client->pipe, FALSE, buf, sizeof(struct dwm_request) + DWM_MAX_MESSAGE_SIZE, &size, event,
                         INFINITE ))
    {
        uint32_t length = 0;

        EnterCriticalSection( &state_cs );
        rep.status = handle_request( buf, size, client, &length );
        LeaveCriticalSection( &state_cs );
        if (rep.status) length = 0;
        /* the payload stays where the request's was: both headers are 16 bytes */
        rep.length     = length;
        rep.generation = generation;
        memcpy( buf, &rep, sizeof(rep) );
        if (wex_pipe_io( client->pipe, TRUE, buf, sizeof(rep) + length, &done, event, 5000 ) ||
            done != sizeof(rep) + length) break;
    }
    EnterCriticalSection( &state_cs );
    drop_client_thumbs( client->id );
    remove_client( client );
    LeaveCriticalSection( &state_cs );
    if (event) CloseHandle( event );
    free( buf );
    DisconnectNamedPipe( client->pipe );
    CloseHandle( client->pipe );
    free( client );
    return 0;
}

/* the pipe's descriptor names this broker's user */
static BOOL make_sddl( WCHAR *sddl, DWORD size )
{
    BYTE buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    TOKEN_USER *user = (TOKEN_USER *)buffer;
    WCHAR *sid_text;
    HANDLE token;
    DWORD len;
    BOOL ret;

    if (!OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token )) return FALSE;
    ret = GetTokenInformation( token, TokenUser, buffer, sizeof(buffer), &len ) &&
          ConvertSidToStringSidW( user->User.Sid, &sid_text );
    CloseHandle( token );
    if (!ret) return FALSE;
    ret = _snwprintf( sddl, size, DWM_PIPE_SDDL_FMT, sid_text ) > 0;
    LocalFree( sid_text );
    return ret;
}

static int serve( const WCHAR *name, const WCHAR *sddl, HANDLE parent )
{
    OVERLAPPED ov = { 0 };
    DWORD last_sweep = GetTickCount();
    uint32_t next_id = 1;
    BOOL first = TRUE;

    if (!(ov.hEvent = CreateEventW( NULL, TRUE, FALSE, NULL ))) return 1;
    for (;;)
    {
        HANDLE pipe = wex_pipe_create( name, sddl, first, sizeof(struct dwm_request) + DWM_MAX_MESSAGE_SIZE );
        HANDLE objs[2] = { ov.hEvent, parent };
        BOOL connected = FALSE;

        if (pipe == INVALID_HANDLE_VALUE)
        {
            /* the first instance could not be ours: another broker serves this desktop, or another process took the
             * name (clients check the server and refuse it) */
            fprintf( stderr, "dwm: cannot create %ls (error %lu)\n", name, GetLastError() );
            if (first) return 3;
            Sleep( 100 );
            continue;
        }
        first = FALSE;
        ResetEvent( ov.hEvent );
        if (ConnectNamedPipe( pipe, &ov )) connected = TRUE;
        else if (GetLastError() == ERROR_PIPE_CONNECTED) connected = TRUE;
        else if (GetLastError() != ERROR_IO_PENDING)
        {
            CloseHandle( pipe );
            continue;
        }
        while (!connected)
        {
            DWORD wait = WaitForMultipleObjects( parent ? 2 : 1, objs, FALSE, 5000 ), done;

            if (wait == WAIT_OBJECT_0 + 1)
            {
                CancelIoEx( pipe, &ov );
                CloseHandle( pipe );
                return 0;
            }
            if (wait == WAIT_OBJECT_0)
            {
                connected = GetOverlappedResult( pipe, &ov, &done, FALSE );
                break;
            }
            /* idle: drop the state of destroyed windows */
            if (GetTickCount() - last_sweep >= 5000)
            {
                EnterCriticalSection( &state_cs );
                sweep_attrs();
                sweep_thumbs();
                LeaveCriticalSection( &state_cs );
                last_sweep = GetTickCount();
            }
        }
        if (connected)
        {
            struct client *client = calloc( 1, sizeof(*client) );
            HANDLE thread = NULL;
            BOOL added = FALSE;

            if (client)
            {
                client->pipe = pipe;
                client->id = next_id++;
                if (!GetNamedPipeClientProcessId( pipe, &client->pid )) client->pid = 0;
                EnterCriticalSection( &state_cs );
                added = add_client( client );
                LeaveCriticalSection( &state_cs );
                if (added) thread = CreateThread( NULL, 0, client_thread, client, 0, NULL );
                else fprintf( stderr, "dwm: too many connections (pid %04lx)\n", client->pid );
            }
            if (thread) CloseHandle( thread );
            else
            {
                if (added)
                {
                    EnterCriticalSection( &state_cs );
                    remove_client( client );
                    LeaveCriticalSection( &state_cs );
                }
                free( client );
                DisconnectNamedPipe( pipe );
                CloseHandle( pipe );
            }
        }
        else CloseHandle( pipe );
        if (GetTickCount() - last_sweep >= 5000)
        {
            EnterCriticalSection( &state_cs );
            sweep_attrs();
            sweep_thumbs();
            LeaveCriticalSection( &state_cs );
            last_sweep = GetTickCount();
        }
    }
}

int __cdecl wmain( int argc, WCHAR **argv )
{
    WCHAR name[MAX_PATH * 2 + 64], sddl[512];
    HANDLE parent = NULL;
    int i;

    for (i = 1; i < argc; i++)
    {
        if (!wcsncmp( argv[i], L"/parent=", 8 ))
            parent = OpenProcess( SYNCHRONIZE, FALSE, wcstoul( argv[i] + 8, NULL, 10 ) );
    }
    if (!use_startup_desktop() || !dwm_pipe_name( GetThreadDesktop( GetCurrentThreadId() ), name, ARRAY_SIZE(name) ))
    {
        fprintf( stderr, "dwm: cannot use the startup desktop (error %lu)\n", GetLastError() );
        return 4;
    }
    if (!make_sddl( sddl, ARRAY_SIZE(sddl) ))
    {
        fprintf( stderr, "dwm: no pipe descriptor (error %lu)\n", GetLastError() );
        return 2;
    }
    /* a new generation tells clients that this broker holds none of their state yet */
    while (!generation) if (!RtlGenRandom( &generation, sizeof(generation) )) generation = GetTickCount64();
    InitializeCriticalSection( &state_cs );
    wex_pipe_grant_appcontainers();
    return serve( name, sddl, parent );
}
