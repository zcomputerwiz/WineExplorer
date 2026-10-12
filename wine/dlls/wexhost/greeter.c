/*
 * The LightDM greeter protocol, for logonui.exe (docs/WINLOGON_AND_DWM.md, phase W2).
 *
 * LightDM starts the greeter with two pipes, named by LIGHTDM_TO_SERVER_FD and LIGHTDM_FROM_SERVER_FD.  Every message
 * is a header of two big-endian 32-bit integers (message id, payload length) and a payload of big-endian 32-bit
 * integers and strings (a 32-bit length, then the bytes without a terminator).  This is our own implementation of
 * that format; liblightdm-gobject is not used (LGPL-3+, see the plan).
 *
 * Events reach the PE side as text, one event per call:
 *   connected | reset              then "key=value" lines: LightDM's hints (lock-screen, select-user, ...)
 *   prompt <seq> <user>            then "<style>\t<text>" lines: 1 secret, 2 visible, 3 error, 4 information
 *   end <seq> <result> <user>      authentication finished; result 0 is success (a PAM code otherwise)
 *   session <result>               answer to start session: 0 started, 1 refused
 *   idle
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

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include "wine/debug.h"
#include "wexhost_unix.h"

WINE_DEFAULT_DEBUG_CHANNEL(wexhost);

enum { MSG_CONNECT = 0, MSG_AUTHENTICATE = 1, MSG_CONTINUE_AUTHENTICATION = 3, MSG_START_SESSION = 4,
       MSG_CANCEL_AUTHENTICATION = 5 };
enum { SRV_CONNECTED = 0, SRV_PROMPT_AUTHENTICATION = 1, SRV_END_AUTHENTICATION = 2, SRV_SESSION_RESULT = 3,
       SRV_SHARED_DIR_RESULT = 4, SRV_IDLE = 5, SRV_RESET = 6, SRV_CONNECTED_V2 = 7 };

#define API_VERSION 1
#define MAX_PAYLOAD 65536

static int to_server = -1, from_server = -1;
static unsigned int sequence;

/* ---------------------------------------------------------------- writing */

struct msg { unsigned char data[1024]; size_t len; BOOL overflow; };

static void put_int( struct msg *m, unsigned int value )
{
    if (m->len + 4 > sizeof(m->data)) { m->overflow = TRUE; return; }
    m->data[m->len++] = value >> 24;
    m->data[m->len++] = value >> 16;
    m->data[m->len++] = value >> 8;
    m->data[m->len++] = value;
}

static void put_string( struct msg *m, const char *s )
{
    size_t n = s ? strlen( s ) : 0;
    put_int( m, n );
    if (m->len + n > sizeof(m->data)) { m->overflow = TRUE; return; }
    memcpy( m->data + m->len, s, n );
    m->len += n;
}

/* the header goes in front once the payload is complete */
static DWORD send_msg( unsigned int id, struct msg *payload )
{
    unsigned char header[8];
    size_t done;

    if (to_server < 0) return ERROR_NOT_CONNECTED;
    if (payload->overflow) return ERROR_BUFFER_OVERFLOW;
    header[0] = id >> 24; header[1] = id >> 16; header[2] = id >> 8; header[3] = id;
    header[4] = payload->len >> 24; header[5] = payload->len >> 16; header[6] = payload->len >> 8; header[7] = payload->len;
    for (done = 0; done < sizeof(header) + payload->len;)
    {
        const unsigned char *p = done < sizeof(header) ? header + done : payload->data + done - sizeof(header);
        size_t n = done < sizeof(header) ? sizeof(header) - done : payload->len - (done - sizeof(header));
        ssize_t r = write( to_server, p, n );
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return ERROR_BROKEN_PIPE;
        done += r;
    }
    return ERROR_SUCCESS;
}

/* ---------------------------------------------------------------- reading */

static BOOL read_all( unsigned char *buf, size_t size )
{
    size_t done = 0;
    while (done < size)
    {
        ssize_t r = read( from_server, buf + done, size - done );
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return FALSE;
        done += r;
    }
    return TRUE;
}

struct reader { const unsigned char *p, *end; BOOL bad; };

static unsigned int get_int( struct reader *r )
{
    unsigned int v;
    if (r->end - r->p < 4) { r->bad = TRUE; r->p = r->end; return 0; }
    v = (r->p[0] << 24) | (r->p[1] << 16) | (r->p[2] << 8) | r->p[3];
    r->p += 4;
    return v;
}

/* copies a string into out with tabs and line breaks replaced, so it stays one field of one line */
static void get_string( struct reader *r, char *out, size_t size )
{
    unsigned int n = get_int( r ), i, k = 0;
    if (r->bad || n > (size_t)(r->end - r->p)) { r->bad = TRUE; r->p = r->end; if (size) *out = 0; return; }
    for (i = 0; i < n; i++)
    {
        char c = r->p[i];
        if (k + 1 < size) out[k++] = (c == '\t' || c == '\n' || c == '\r') ? ' ' : c;
    }
    if (size) out[k] = 0;
    r->p += n;
}

struct text { char *buf; size_t size, len; BOOL overflow; };

static void add( struct text *t, const char *fmt, ... )
{
    va_list args;
    int n;
    va_start( args, fmt );
    n = vsnprintf( t->buf + t->len, t->len < t->size ? t->size - t->len : 0, fmt, args );
    va_end( args );
    if (n < 0 || t->len + n >= t->size) t->overflow = TRUE;
    else t->len += n;
}

static void add_hints( struct text *t, struct reader *r, unsigned int count )
{
    char key[256], value[1024];
    while (count-- && !r->bad && r->p < r->end)
    {
        get_string( r, key, sizeof(key) );
        get_string( r, value, sizeof(value) );
        if (!r->bad) add( t, "%s=%s\n", key, value );
    }
}

/* one server message as text; FALSE when the message is not one we report */
static BOOL format_event( unsigned int id, struct reader *r, struct text *t )
{
    char user[256], s[1024];
    unsigned int seq, n, style, result;

    switch (id)
    {
    case SRV_CONNECTED:            /* version, then hints up to the end */
        get_string( r, s, sizeof(s) );
        add( t, "connected\n" );
        add_hints( t, r, ~0u );
        return TRUE;
    case SRV_CONNECTED_V2:         /* api version, version, hint count, hints */
        get_int( r );
        get_string( r, s, sizeof(s) );
        n = get_int( r );
        add( t, "connected\n" );
        add_hints( t, r, n );
        return TRUE;
    case SRV_RESET:
        add( t, "reset\n" );
        add_hints( t, r, ~0u );
        return TRUE;
    case SRV_PROMPT_AUTHENTICATION:
        seq = get_int( r );
        get_string( r, user, sizeof(user) );
        n = get_int( r );
        add( t, "prompt %u %s\n", seq, user );
        while (n-- && !r->bad)
        {
            style = get_int( r );
            get_string( r, s, sizeof(s) );
            add( t, "%u\t%s\n", style, s );
        }
        return TRUE;
    case SRV_END_AUTHENTICATION:
        seq = get_int( r );
        get_string( r, user, sizeof(user) );
        result = get_int( r );
        add( t, "end %u %u %s\n", seq, result, user );
        return TRUE;
    case SRV_SESSION_RESULT:
        add( t, "session %u\n", get_int( r ) );
        return TRUE;
    case SRV_IDLE:
        add( t, "idle\n" );
        return TRUE;
    }
    return FALSE;
}

/* ---------------------------------------------------------------- calls */

static BOOL open_pipes( void )
{
    const char *to = getenv( "LIGHTDM_TO_SERVER_FD" ), *from = getenv( "LIGHTDM_FROM_SERVER_FD" );
    if (to_server >= 0) return TRUE;
    if (!to || !from) return FALSE;
    to_server = atoi( to );
    from_server = atoi( from );
    if (fcntl( to_server, F_GETFD ) < 0 || fcntl( from_server, F_GETFD ) < 0)
    {
        to_server = from_server = -1;
        return FALSE;
    }
    return TRUE;
}

NTSTATUS greeter_connect( void *args )
{
    struct greeter_params *params = args;
    struct msg m = {0};

    if (!open_pipes()) { params->err = ERROR_NOT_SUPPORTED; return STATUS_SUCCESS; }
    put_string( &m, "wineexplorer-logonui" );
    put_int( &m, 0 );              /* not resettable: a new greeter process starts for every logon */
    put_int( &m, API_VERSION );
    params->err = send_msg( MSG_CONNECT, &m );
    return STATUS_SUCCESS;
}

NTSTATUS greeter_authenticate( void *args )
{
    struct greeter_params *params = args;
    struct msg m = {0};

    put_int( &m, ++sequence );
    put_string( &m, params->text[0] ? params->text : NULL );
    params->err = send_msg( MSG_AUTHENTICATE, &m );
    params->value = sequence;
    return STATUS_SUCCESS;
}

NTSTATUS greeter_respond( void *args )
{
    struct greeter_params *params = args;
    struct msg m = {0};

    put_int( &m, 1 );
    put_string( &m, params->text );
    params->err = send_msg( MSG_CONTINUE_AUTHENTICATION, &m );
    explicit_bzero( m.data, sizeof(m.data) );
    explicit_bzero( params->text, sizeof(params->text) );
    return STATUS_SUCCESS;
}

NTSTATUS greeter_cancel( void *args )
{
    struct greeter_params *params = args;
    struct msg m = {0};
    params->err = send_msg( MSG_CANCEL_AUTHENTICATION, &m );
    return STATUS_SUCCESS;
}

NTSTATUS greeter_start_session( void *args )
{
    struct greeter_params *params = args;
    struct msg m = {0};
    put_string( &m, params->text[0] ? params->text : NULL );   /* none: the user's default session */
    params->err = send_msg( MSG_START_SESSION, &m );
    return STATUS_SUCCESS;
}

/* waits up to value milliseconds (INFINITE: no limit) for the next event we report */
NTSTATUS greeter_read( void *args )
{
    struct greeter_read_params *params = args;
    char *buffer = (char *)(ULONG_PTR)params->buffer;
    unsigned char header[8], *payload;
    unsigned int id, len;

    params->err = ERROR_SUCCESS;
    if (from_server < 0) { params->err = ERROR_NOT_CONNECTED; return STATUS_SUCCESS; }
    for (;;)
    {
        struct pollfd pfd = { from_server, POLLIN, 0 };
        struct text t = { buffer, params->size, 0, FALSE };
        struct reader r;
        int ret = poll( &pfd, 1, params->timeout == INFINITE ? -1 : (int)params->timeout );

        if (ret < 0 && errno == EINTR) continue;
        if (ret == 0) { params->err = ERROR_TIMEOUT; return STATUS_SUCCESS; }
        if (ret < 0 || !read_all( header, sizeof(header) )) { params->err = ERROR_BROKEN_PIPE; return STATUS_SUCCESS; }
        id = (header[0] << 24) | (header[1] << 16) | (header[2] << 8) | header[3];
        len = (header[4] << 24) | (header[5] << 16) | (header[6] << 8) | header[7];
        if (len > MAX_PAYLOAD || !(payload = malloc( len + 1 ))) { params->err = ERROR_INVALID_DATA; return STATUS_SUCCESS; }
        if (!read_all( payload, len )) { free( payload ); params->err = ERROR_BROKEN_PIPE; return STATUS_SUCCESS; }
        r.p = payload; r.end = payload + len; r.bad = FALSE;
        if (params->size) buffer[0] = 0;
        if (!format_event( id, &r, &t ))
        {
            TRACE( "ignoring message %u\n", id );
            free( payload );
            continue;
        }
        free( payload );
        if (r.bad) WARN( "malformed message %u\n", id );
        if (t.overflow) params->err = ERROR_MORE_DATA;
        return STATUS_SUCCESS;
    }
}
