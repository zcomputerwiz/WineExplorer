/*
 * WineExplorer host services - Unix side: read leases on the image of an elevated program
 * (docs/ELEVATION_CONSENT_WORKORDER.md, Part A).
 *
 * The elevation broker holds a Linux read lease (fcntl F_SETLEASE) on the frozen image file from before the hash
 * check until the elevated process exits. Anyone who then opens the file for writing or truncates it blocks and
 * makes the lease break; a thread here sees the break (F_GETLEASE reports F_UNLCK while a lease is breaking),
 * ends the elevated process by its Unix pid, and only then gives the lease up, so the write never reaches a
 * running elevated process. The watching must not depend on the wineserver: a Wine program that writes the
 * file makes the wineserver itself block in open() until the lease is released.
 *
 * The signal that the kernel sends on a lease break is redirected to SIGURG, which is ignored by default and
 * unused by Wine (the default, SIGIO, would end the broker).
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/syscall.h>

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winbase.h>
#include <winerror.h>
#include <winternl.h>

#include "wine/server.h"
#include "wexhost_unix.h"

#ifndef F_SETLEASE
# define F_SETLEASE 1024
# define F_GETLEASE 1025
#endif
#ifndef F_SETSIG
# define F_SETSIG 10
#endif
#ifndef SYS_pidfd_open
# define SYS_pidfd_open 434
#endif
#ifndef SYS_pidfd_send_signal
# define SYS_pidfd_send_signal 424
#endif

#define MAX_LEASES   64
#define POLL_MS      10
#define KILL_WAIT_MS 30000

struct lease
{
    int fd, stop_pipe[2], pidfd;
    int phase;        /* 0: waiting for approval, nothing runs; 1: launching or running */
    int broken;       /* the kernel broke the lease */
    int released;     /* the lease was given up (F_UNLCK) */
    pthread_mutex_t lock;
};

static struct lease *leases[MAX_LEASES];
static pthread_mutex_t table_lock = PTHREAD_MUTEX_INITIALIZER;

static struct lease *lease_get( UINT64 id )
{
    struct lease *l = NULL;

    pthread_mutex_lock( &table_lock );
    if (id && id <= MAX_LEASES) l = leases[id - 1];
    pthread_mutex_unlock( &table_lock );
    return l;
}

/* the Unix pid the wineserver recorded for a Windows process (credentials of its connection, not the process's claim) */
static int unix_pid_of( DWORD pid )
{
    unsigned int size = 0x10000, count = 0, i, j;
    char *buf = NULL;
    int ret = -1;
    NTSTATUS status;

    for (;;)
    {
        free( buf );
        if (!(buf = malloc( size ))) return -1;
        SERVER_START_REQ( list_processes )
        {
            wine_server_set_reply( req, buf, size );
            status = wine_server_call( req );
            count = reply->process_count;
        }
        SERVER_END_REQ;
        if (status != STATUS_INFO_LENGTH_MISMATCH || size > 0x1000000) break;
        size *= 4;
    }
    if (!status)
    {
        unsigned int pos = 0;
        for (i = 0; i < count; i++)
        {
            const struct process_info *p;

            pos = (pos + 7) & ~7;
            p = (const struct process_info *)(buf + pos);
            if (p->pid == pid) ret = p->unix_pid;
            pos += sizeof(*p) + p->name_len;
            pos = (pos + 7) & ~7;
            for (j = 0; j < (unsigned int)p->thread_count; j++) pos += sizeof(struct thread_info);
        }
    }
    free( buf );
    return ret;
}

/* give the lease up; the writer that waits for it goes on */
static void drop_lease( struct lease *l )
{
    if (!l->released)
    {
        fcntl( l->fd, F_SETLEASE, F_UNLCK );
        l->released = 1;
    }
}

/* Runs on a Wine thread of the caller (wexhost.c) for the life of the lease, and never calls the server: it
 * must keep working while the wineserver is blocked in open() on behalf of a writer. Returns when the lease is
 * released by the owner or after a break has been handled (the process ended and the lease given up). */
NTSTATUS lease_monitor( void *args )
{
    struct lease_monitor_params *params = args;
    struct lease *l = lease_get( params->lease );
    struct pollfd p;

    params->broken = 0;
    params->err = ERROR_SUCCESS;
    if (!l) { params->err = ERROR_INVALID_HANDLE; return STATUS_SUCCESS; }
    p.fd = l->stop_pipe[0];
    p.events = POLLIN;
    p.revents = 0;

    for (;;)
    {
        if (poll( &p, 1, POLL_MS ) > 0) return STATUS_SUCCESS;       /* released by the owner */
        if (fcntl( l->fd, F_GETLEASE ) != F_RDLCK) break;             /* a writer is waiting */
    }

    pthread_mutex_lock( &l->lock );
    l->broken = 1;
    if (l->phase)
    {
        /* a process is being launched or runs: end it first, then give the lease up */
        while (l->pidfd < 0)
        {
            pthread_mutex_unlock( &l->lock );
            if (poll( &p, 1, POLL_MS ) > 0) return STATUS_SUCCESS;   /* the launch failed, nothing runs */
            pthread_mutex_lock( &l->lock );
        }
        syscall( SYS_pidfd_send_signal, l->pidfd, SIGKILL, NULL, 0 );
        {
            struct pollfd pf = { l->pidfd, POLLIN, 0 };
            poll( &pf, 1, KILL_WAIT_MS );
        }
    }
    drop_lease( l );
    pthread_mutex_unlock( &l->lock );
    params->broken = 1;
    return STATUS_SUCCESS;
}

/* Is the image safe from this user without a lease? Yes if the user cannot open it for writing anyway. */
static BOOL user_cannot_write( int fd )
{
    char path[64];

    snprintf( path, sizeof(path), "/proc/self/fd/%d", fd );
    return access( path, W_OK ) != 0;
}

/* Only these filesystem types are accepted for elevated images, even when no lease is needed. */
static BOOL image_filesystem_allowed( int fd )
{
    static const unsigned int allowed[] =
    {
        0xef53,      /* ext2/3/4 */
        0x58465342,  /* xfs */
        0x9123683e,  /* btrfs */
        0x01021994,  /* tmpfs */
        0x794c7630,  /* overlayfs */
        0x858458f6,  /* ramfs */
        0x4d44       /* vfat/msdos */
    };
    struct statfs fs;
    unsigned int i, type;

    if (fstatfs( fd, &fs ))
    {
        fprintf( stderr, "wexhost: cannot determine image filesystem: errno %d\n", errno );
        return FALSE;
    }
    type = (unsigned int)fs.f_type;
    for (i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++)
        if (type == allowed[i]) return TRUE;
    fprintf( stderr, "wexhost: denied image filesystem type 0x%08x\n", type );
    return FALSE;
}

NTSTATUS lease_take( void *args )
{
    struct lease_take_params *params = args;
    struct stat st;
    struct lease *l;
    unsigned int options, i;
    int fd = -1;

    params->lease = 0;
    params->err = ERROR_SUCCESS;
    if (wine_server_handle_to_fd( (HANDLE)(ULONG_PTR)params->file, FILE_READ_DATA, &fd, &options ))
    {
        params->err = ERROR_INVALID_HANDLE;
        return STATUS_SUCCESS;
    }
    if (fstat( fd, &st ) || !S_ISREG( st.st_mode ))
    {
        close( fd );
        params->err = ERROR_FILE_SYSTEM_LIMITATION;
        return STATUS_SUCCESS;
    }
    if (!image_filesystem_allowed( fd ))
    {
        close( fd );
        params->err = ERROR_NOT_SUPPORTED;
        return STATUS_SUCCESS;
    }
    /* a file that the user cannot write needs no lease (root's or the system group's files in the machine root) */
    if (st.st_uid != geteuid() && user_cannot_write( fd ))
    {
        close( fd );
        return STATUS_SUCCESS;
    }

    fcntl( fd, F_SETSIG, SIGURG );
    if (fcntl( fd, F_SETLEASE, F_RDLCK ))
    {
        /* EAGAIN: someone has the file open for writing. Anything else: no lease possible here (a file system
         * without lease support, or a file that the user can write but does not own); no fallback copy. */
        params->err = errno == EAGAIN ? ERROR_SHARING_VIOLATION : ERROR_FILE_SYSTEM_LIMITATION;
        close( fd );
        return STATUS_SUCCESS;
    }

    if (!(l = calloc( 1, sizeof(*l) )) || pipe( l->stop_pipe ))
    {
        if (l) free( l );
        fcntl( fd, F_SETLEASE, F_UNLCK );
        close( fd );
        params->err = ERROR_NOT_ENOUGH_MEMORY;
        return STATUS_SUCCESS;
    }
    l->fd = fd;
    l->pidfd = -1;
    pthread_mutex_init( &l->lock, NULL );

    pthread_mutex_lock( &table_lock );
    for (i = 0; i < MAX_LEASES; i++) if (!leases[i]) break;
    if (i < MAX_LEASES) leases[i] = l;
    pthread_mutex_unlock( &table_lock );
    if (i == MAX_LEASES)
    {
        fcntl( fd, F_SETLEASE, F_UNLCK );
        close( fd ); close( l->stop_pipe[0] ); close( l->stop_pipe[1] );
        free( l );
        params->err = ERROR_NOT_ENOUGH_MEMORY;
        return STATUS_SUCCESS;
    }
    params->lease = i + 1;
    return STATUS_SUCCESS;
}

/* pid 0: a process is about to be launched, the lease must be kept until it is known and ended.
 * pid != 0: that process runs; end it if the lease breaks. ERROR_FILE_CORRUPT if the lease has broken already. */
NTSTATUS lease_arm( void *args )
{
    struct lease_arm_params *params = args;
    struct lease *l = lease_get( params->lease );
    int unix_pid;

    params->err = ERROR_SUCCESS;
    if (!l) { params->err = ERROR_INVALID_HANDLE; return STATUS_SUCCESS; }
    if (params->pid)
    {
        int pidfd = -1;

        if ((unix_pid = unix_pid_of( params->pid )) > 0) pidfd = syscall( SYS_pidfd_open, unix_pid, 0 );
        if (pidfd < 0) { params->err = ERROR_NOT_FOUND; return STATUS_SUCCESS; }
        pthread_mutex_lock( &l->lock );
        l->pidfd = pidfd;
        l->phase = 1;
        params->err = l->broken ? ERROR_FILE_CORRUPT : ERROR_SUCCESS;
        pthread_mutex_unlock( &l->lock );
        return STATUS_SUCCESS;
    }
    pthread_mutex_lock( &l->lock );
    l->phase = 1;
    params->err = l->broken ? ERROR_FILE_CORRUPT : ERROR_SUCCESS;
    pthread_mutex_unlock( &l->lock );
    return STATUS_SUCCESS;
}

/* tell the monitor to return (the owner then waits for its thread and calls lease_release) */
NTSTATUS lease_stop( void *args )
{
    struct lease_release_params *params = args;
    struct lease *l = lease_get( params->lease );

    params->err = l ? ERROR_SUCCESS : ERROR_INVALID_HANDLE;
    if (l && write( l->stop_pipe[1], "x", 1 ) < 0) {}
    return STATUS_SUCCESS;
}

NTSTATUS lease_release( void *args )
{
    struct lease_release_params *params = args;
    struct lease *l = lease_get( params->lease );

    params->err = ERROR_SUCCESS;
    params->broken = 0;
    if (!l) { params->err = ERROR_INVALID_HANDLE; return STATUS_SUCCESS; }
    pthread_mutex_lock( &table_lock );
    leases[params->lease - 1] = NULL;
    pthread_mutex_unlock( &table_lock );

    params->broken = l->broken;
    drop_lease( l );
    close( l->fd );
    close( l->stop_pipe[0] );
    close( l->stop_pipe[1] );
    if (l->pidfd >= 0) close( l->pidfd );
    free( l );
    return STATUS_SUCCESS;
}
