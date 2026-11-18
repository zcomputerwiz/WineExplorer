/*
 * wexjoin: the domain join engine's socket, unix side
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

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <stdarg.h>
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winerror.h"
#include "winternl.h"
#include "lmerr.h"

#include "wexjoin_unix.h"
#include "wexjoin.h"

static DWORD connect_error( int e )
{
    switch (e)
    {
    case ENOENT:
    case ECONNREFUSED: return NERR_WkstaNotStarted;      /* the engine's socket is not there: the workstation service is not started */
    case EACCES:
    case EPERM:        return ERROR_ACCESS_DENIED;        /* not a process of the system group: not an administrator's */
    default:           return ERROR_IO_DEVICE;
    }
}

static long long now_ms(void)
{
    struct timespec t;

    clock_gettime( CLOCK_MONOTONIC, &t );
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* The request to the engine and its answer (until the engine closes the connection). The engine may take minutes: the
 * wait is bounded by WEX_JOIN_WAIT_SECONDS for the whole call. */
static NTSTATUS join_request( void *args )
{
    struct request_params *params = args;
    const char *request = (const char *)(ULONG_PTR)params->request;
    char *reply = (char *)(ULONG_PTR)params->reply;
    struct sockaddr_un addr;
    size_t sent = 0, got = 0;
    long long deadline = now_ms() + WEX_JOIN_WAIT_SECONDS * 1000LL;
    int fd;

    params->reply_length = 0;
    params->err = ERROR_SUCCESS;
    if (!params->reply_size || params->request_length > WEX_JOIN_REQUEST_MAX) { params->err = ERROR_INVALID_PARAMETER; return STATUS_SUCCESS; }
    if ((fd = socket( AF_UNIX, SOCK_STREAM, 0 )) < 0) { params->err = ERROR_IO_DEVICE; return STATUS_SUCCESS; }
    memset( &addr, 0, sizeof(addr) );
    addr.sun_family = AF_UNIX;
    strcpy( addr.sun_path, WEX_JOIN_SOCKET );
    if (connect( fd, (struct sockaddr *)&addr, sizeof(addr) ))
    {
        params->err = connect_error( errno );
        close( fd );
        return STATUS_SUCCESS;
    }
    while (sent < params->request_length)
    {
        ssize_t n = send( fd, request + sent, params->request_length - sent, MSG_NOSIGNAL );

        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { params->err = ERROR_IO_DEVICE; close( fd ); return STATUS_SUCCESS; }
        sent += n;
    }
    shutdown( fd, SHUT_WR );
    for (;;)
    {
        struct pollfd pfd = { fd, POLLIN, 0 };
        long long left = deadline - now_ms();
        ssize_t n;

        if (left <= 0) { params->err = ERROR_TIMEOUT; break; }
        if (poll( &pfd, 1, left > 1000 ? 1000 : (int)left ) < 0 && errno != EINTR) { params->err = ERROR_IO_DEVICE; break; }
        if (!(pfd.revents & (POLLIN | POLLHUP | POLLERR))) continue;
        if (got + 1 >= params->reply_size) break;       /* the answer is longer than the buffer: what fits is the answer */
        n = read( fd, reply + got, params->reply_size - 1 - got );
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        got += n;
    }
    close( fd );
    reply[got] = 0;
    params->reply_length = got;
    return STATUS_SUCCESS;
}

const unixlib_entry_t __wine_unix_call_funcs[] =
{
    join_request,
};

C_ASSERT( ARRAYSIZE(__wine_unix_call_funcs) == unix_funcs_count );

#ifdef _WIN64
const unixlib_entry_t __wine_unix_call_wow64_funcs[] =
{
    join_request,
};

C_ASSERT( ARRAYSIZE(__wine_unix_call_wow64_funcs) == unix_funcs_count );
#endif
