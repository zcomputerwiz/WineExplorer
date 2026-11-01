/*
 * Test client for wexprefetchd.
 *
 * Usage: wexpf-client [-s SOCKET] [-n COUNT] FILE
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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#include "protocol.h"

/* Buffer for one SCM_RIGHTS control message; the union guarantees cmsghdr alignment. */
union cmsg_buf
{
    char buf[CMSG_SPACE(sizeof(int))];
    struct cmsghdr hdr;
};

static const char *status_name( int status )
{
    switch (status)
    {
    case WEXPF_OK:          return "WEXPF_OK";
    case WEXPF_BAD_REQUEST: return "WEXPF_BAD_REQUEST";
    case WEXPF_NOT_PE:      return "WEXPF_NOT_PE";
    case WEXPF_TOO_LARGE:   return "WEXPF_TOO_LARGE";
    case WEXPF_BUSY:        return "WEXPF_BUSY";
    case WEXPF_IO_ERROR:    return "WEXPF_IO_ERROR";
    case WEXPF_DISABLED:    return "WEXPF_DISABLED";
    case WEXPF_UNSUPPORTED: return "WEXPF_UNSUPPORTED";
    default:                return "WEXPF_UNKNOWN";
    }
}

static double now_ms( void )
{
    struct timespec ts;
    clock_gettime( CLOCK_MONOTONIC, &ts );
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/*
 * One request/reply round trip on a fresh connection.
 * Returns 0 on WEXPF_OK with a descriptor, 1 on a daemon error status, 2 on a local/protocol error.
 */
static int do_request( const char *socket_path, int file_fd, int verbose, int *status_out )
{
    struct wexpf_request req;
    struct wexpf_reply reply;
    struct sockaddr_un addr;
    struct msghdr msg;
    struct iovec iov;
    union cmsg_buf cmsgu;
    ssize_t n;
    int sock;
    int reply_fd = -1;

    if (status_out)
        *status_out = -1;

    sock = socket( AF_UNIX, SOCK_SEQPACKET, 0 );
    if (sock < 0)
    {
        perror( "socket" );
        return 2;
    }

    memset( &addr, 0, sizeof(addr) );
    addr.sun_family = AF_UNIX;
    if (strlen( socket_path ) >= sizeof(addr.sun_path))
    {
        fprintf( stderr, "socket path too long: %s\n", socket_path );
        close( sock );
        return 2;
    }
    strcpy( addr.sun_path, socket_path );

    if (connect( sock, (struct sockaddr *)&addr, sizeof(addr) ) < 0)
    {
        perror( "connect" );
        close( sock );
        return 2;
    }

    /* Build and send the request with the file descriptor attached. */
    memset( &req, 0, sizeof(req) );
    req.magic   = WEXPF_MAGIC;
    req.version = WEXPF_VERSION;
    req.op      = WEXPF_OP_GET_IMAGE;
    req.flags   = 0;

    memset( &msg, 0, sizeof(msg) );
    iov.iov_base = &req;
    iov.iov_len  = sizeof(req);
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = cmsgu.buf;
    msg.msg_controllen = CMSG_SPACE(sizeof(int));

    {
        struct cmsghdr *cmsg = CMSG_FIRSTHDR( &msg );
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type  = SCM_RIGHTS;
        cmsg->cmsg_len   = CMSG_LEN(sizeof(int));
        *(int *)CMSG_DATA( cmsg ) = file_fd;
    }

    if (sendmsg( sock, &msg, 0 ) < 0)
    {
        perror( "sendmsg" );
        close( sock );
        return 2;
    }

    /* Receive the reply and the optional descriptor. */
    memset( &msg, 0, sizeof(msg) );
    iov.iov_base = &reply;
    iov.iov_len  = sizeof(reply);
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = cmsgu.buf;
    msg.msg_controllen = sizeof(cmsgu.buf);

    n = recvmsg( sock, &msg, MSG_CMSG_CLOEXEC );
    if (n < 0)
    {
        perror( "recvmsg" );
        close( sock );
        return 2;
    }
    if (n != (ssize_t)sizeof(reply))
    {
        fprintf( stderr, "short reply: %zd bytes (expected %zu)\n", n, sizeof(reply) );
        close( sock );
        return 2;
    }

    if (reply.magic != WEXPF_MAGIC)
    {
        fprintf( stderr, "bad magic in reply: 0x%08x\n", reply.magic );
        close( sock );
        return 2;
    }
    if (reply.version != WEXPF_VERSION)
    {
        fprintf( stderr, "bad version in reply: %u\n", reply.version );
        close( sock );
        return 2;
    }

    if (status_out)
        *status_out = reply.status;

    /* Extract the descriptor, if the daemon attached one. */
    if (msg.msg_controllen >= CMSG_LEN(sizeof(int)))
    {
        struct cmsghdr *cmsg = CMSG_FIRSTHDR( &msg );
        if (cmsg && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS &&
            cmsg->cmsg_len >= CMSG_LEN(sizeof(int)))
        {
            reply_fd = *(int *)CMSG_DATA( cmsg );
        }
    }

    if (verbose)
    {
        printf( "status: %d (%s)\n", reply.status, status_name( reply.status ) );
        printf( "layout: %u\n", reply.layout );
        printf( "image_size: %llu\n", (unsigned long long)reply.image_size );
        printf( "sha256: %s\n", reply.sha256[0] ? reply.sha256 : "(none)" );

        if (reply_fd >= 0)
        {
            struct stat st;
            int fl;
            if (fstat( reply_fd, &st ) < 0)
            {
                perror( "fstat" );
                close( reply_fd );
                close( sock );
                return 2;
            }
            fl = fcntl( reply_fd, F_GETFL );
            printf( "descriptor: size=%llu blocks*512=%llu regular=%s read-only=%s\n",
                    (unsigned long long)st.st_size,
                    (unsigned long long)st.st_blocks * 512,
                    S_ISREG( st.st_mode ) ? "yes" : "no",
                    (fl & O_ACCMODE) == O_RDONLY ? "yes" : "no" );
            close( reply_fd );
        }
    }
    else if (reply_fd >= 0)
    {
        close( reply_fd );
    }

    close( sock );

    if (reply.status == WEXPF_OK && reply_fd >= 0)
        return 0;
    if (reply.status != WEXPF_OK)
        return 1;
    /* WEXPF_OK without a descriptor is a protocol error. */
    return 2;
}

int main( int argc, char **argv )
{
    const char *socket_path = NULL;
    const char *file = NULL;
    int count = 1;
    int i;
    int fd;

    for (i = 1; i < argc; i++)
    {
        if (!strcmp( argv[i], "-s" ) && i + 1 < argc)
        {
            socket_path = argv[++i];
        }
        else if (!strcmp( argv[i], "-n" ) && i + 1 < argc)
        {
            char *end;
            long n = strtol( argv[++i], &end, 10 );
            if (*end || n < 1)
            {
                fprintf( stderr, "invalid count: %s\n", argv[i] );
                return 2;
            }
            count = (int)n;
        }
        else if (!strcmp( argv[i], "-h" ) || !strcmp( argv[i], "--help" ))
        {
            printf( "usage: wexpf-client [-s SOCKET] [-n COUNT] FILE\n" );
            return 0;
        }
        else if (file == NULL)
        {
            file = argv[i];
        }
        else
        {
            fprintf( stderr, "unexpected argument: %s\n", argv[i] );
            return 2;
        }
    }

    if (file == NULL)
    {
        fprintf( stderr, "usage: wexpf-client [-s SOCKET] [-n COUNT] FILE\n" );
        return 2;
    }

    if (socket_path == NULL)
        socket_path = getenv( WEXPF_SOCKET_ENV );
    if (socket_path == NULL)
        socket_path = WEXPF_SOCKET_PATH;

    fd = open( file, O_RDONLY );
    if (fd < 0)
    {
        perror( file );
        return 2;
    }

    if (count == 1)
    {
        int rc = do_request( socket_path, fd, 1, NULL );
        close( fd );
        return rc;
    }

    {
        double total = 0;
        int worst = 0;
        for (i = 0; i < count; i++)
        {
            int status = -1;
            double t0 = now_ms();
            int r = do_request( socket_path, fd, 0, &status );
            double dt = now_ms() - t0;
            total += dt;
            printf( "round %d: status %d (%s), %.3f ms\n",
                    i + 1, status, status_name( status ), dt );
            if (r > worst)
                worst = r;
        }
        printf( "total: %.3f ms, average: %.3f ms over %d requests\n",
                total, total / count, count );
        close( fd );
        return worst;
    }
}
