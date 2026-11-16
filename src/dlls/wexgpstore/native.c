/*
 * wexgpstore: the file work of the local Group Policy store, as session 0's account (native code, plain POSIX)
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

/* See native.h and docs/GROUP_POLICY.md, "Writing the local files: the spool". */

#if 0
#pragma makedep unix
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "native.h"

/* the requests the system group may leave, by kind; the caller never names a path */
static const char *const request_names[GPS_KIND_COUNT] =
    { "Machine.Registry.pol", "User.Registry.pol", "Machine.GptTmpl.inf" };

/* the store file of each kind: its directories below GroupPolicy (a NULL ends the list) and its name */
static const char *const store_dirs[GPS_KIND_COUNT][5] =
{
    { "Machine", NULL },
    { "User", NULL },
    { "Machine", "Microsoft", "Windows NT", "SecEdit", NULL },
};
static const char *const store_names[GPS_KIND_COUNT] = { "Registry.pol", "Registry.pol", "GptTmpl.inf" };

const char *gps_request_name( enum gps_kind kind )
{
    return kind < GPS_KIND_COUNT ? request_names[kind] : NULL;
}

/* a directory that belongs to this user and that the given classes of users may not use */
static int directory_ok( int fd, mode_t forbidden, int check_group )
{
    struct stat st;

    if (fstat( fd, &st ) || !S_ISDIR( st.st_mode ) || st.st_uid != geteuid()) return 0;
    if (check_group && st.st_gid != getegid()) return 0;
    return !(st.st_mode & forbidden);
}

int gps_open( const char *prefix, struct gps_store *store )
{
    static const char *const path[] = { "drive_c", "windows", "system32", "GroupPolicy" };
    int fd, next;
    unsigned int i;

    store->root_fd = store->spool_fd = -1;
    if (!prefix || prefix[0] != '/') return GPS_BAD_ARGUMENT;
    /* the prefix is session 0's own configured path; below it nothing may be a link */
    if ((fd = open( prefix, O_RDONLY | O_DIRECTORY | O_CLOEXEC )) < 0) return GPS_BAD_TREE;
    for (i = 0; i < sizeof(path) / sizeof(path[0]); i++)
    {
        next = openat( fd, path[i], O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC );
        close( fd );
        if (next < 0) return GPS_BAD_TREE;
        fd = next;
    }
    if (!directory_ok( fd, S_IWGRP | S_IWOTH, 1 ))
    {
        close( fd );
        return GPS_BAD_TREE;
    }
    next = openat( fd, "Spool", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC );
    if (next < 0 || !directory_ok( next, S_IRWXO, 1 ))
    {
        if (next >= 0) close( next );
        close( fd );
        return GPS_BAD_TREE;
    }
    store->root_fd = fd;
    store->spool_fd = next;
    return GPS_OK;
}

void gps_close( struct gps_store *store )
{
    if (store->spool_fd >= 0) close( store->spool_fd );
    if (store->root_fd >= 0) close( store->root_fd );
    store->root_fd = store->spool_fd = -1;
}

/* a directory of the store: opened without following links, from the descriptor of the one above; made when missing
 * (when create is set); it has to belong to this user and not be writable by group or others. Returns a new
 * descriptor, or -1 with *result set. */
static int open_store_dir( const struct gps_store *store, enum gps_kind kind, int create, int *result )
{
    const char *const *name;
    int fd = store->root_fd, next, owned = 0;

    for (name = store_dirs[kind]; *name; name++)
    {
        next = openat( fd, *name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC );
        if (next < 0 && errno == ENOENT && create && !mkdirat( fd, *name, 02755 ))
        {
            next = openat( fd, *name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC );
            if (next >= 0) fchmod( next, 02755 );   /* the umask does not decide */
        }
        if (owned) close( fd );
        if (next < 0)
        {
            *result = errno == ENOENT ? GPS_NONE : errno == ELOOP || errno == ENOTDIR ? GPS_BAD_TREE : GPS_IO_ERROR;
            return -1;
        }
        fd = next;
        owned = 1;
        if (!directory_ok( fd, S_IWGRP | S_IWOTH, 0 ))
        {
            close( fd );
            *result = GPS_BAD_TREE;
            return -1;
        }
    }
    if (!owned)
    {
        *result = GPS_BAD_ARGUMENT;
        return -1;
    }
    return fd;
}

/* exactly size bytes from fd; 0 or -1 */
static int read_exact( int fd, unsigned char *buffer, size_t size )
{
    size_t done = 0;

    while (done < size)
    {
        ssize_t n = read( fd, buffer + done, size - done );
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        done += n;
    }
    return 0;
}

static int write_all( int fd, const unsigned char *buffer, size_t size )
{
    size_t done = 0;

    while (done < size)
    {
        ssize_t n = write( fd, buffer + done, size - done );
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        done += n;
    }
    return 0;
}

/* the request that gps_spool_read last returned, by kind: spool_done removes that file, and not a newer one that the
 * writer put under the same name in between (that one stays for the next request) */
static struct { int valid; dev_t dev; ino_t ino; } seen[GPS_KIND_COUNT];

int gps_spool_read( struct gps_store *store, enum gps_kind kind, unsigned char **data, size_t *size )
{
    struct stat st;
    size_t limit;
    unsigned char *buffer;
    int fd;

    *data = NULL;
    *size = 0;
    if (kind >= GPS_KIND_COUNT || store->spool_fd < 0) return GPS_BAD_ARGUMENT;
    seen[kind].valid = 0;
    limit = kind == GPS_MACHINE_TEMPLATE ? GPS_TEMPLATE_LIMIT : GPS_REQUEST_LIMIT;
    /* O_NONBLOCK: a FIFO that was put in the place of the request must not hold this up */
    fd = openat( store->spool_fd, request_names[kind], O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC );
    if (fd < 0) return errno == ENOENT ? GPS_NONE : errno == ELOOP ? GPS_REFUSED : GPS_IO_ERROR;
    if (fstat( fd, &st ))
    {
        close( fd );
        return GPS_IO_ERROR;
    }
    seen[kind].valid = 1;
    seen[kind].dev = st.st_dev;
    seen[kind].ino = st.st_ino;
    /* a regular file with one link: a second name for a file that this account can read and the writer cannot
     * must not be installed as a store file that everyone can read */
    if (!S_ISREG( st.st_mode ) || st.st_nlink != 1)
    {
        close( fd );
        return GPS_REFUSED;
    }
    if (st.st_size < 0 || (unsigned long long)st.st_size > limit)
    {
        close( fd );
        return GPS_TOO_LARGE;
    }
    if (!st.st_size)
    {
        close( fd );
        return GPS_OK;   /* *size 0, no data: remove the store file */
    }
    /* exactly st_size bytes, into the buffer that is checked afterwards; the file is not opened again */
    if (!(buffer = malloc( st.st_size )))
    {
        close( fd );
        return GPS_NO_MEMORY;
    }
    if (read_exact( fd, buffer, st.st_size ))
    {
        free( buffer );
        close( fd );
        return GPS_IO_ERROR;
    }
    close( fd );
    *data = buffer;
    *size = st.st_size;
    return GPS_OK;
}

int gps_spool_done( struct gps_store *store, enum gps_kind kind, const char *error )
{
    char error_name[64];
    size_t length;
    int fd, result = GPS_OK;

    if (kind >= GPS_KIND_COUNT || store->spool_fd < 0) return GPS_BAD_ARGUMENT;
    snprintf( error_name, sizeof(error_name), "%s.error", request_names[kind] );
    if (seen[kind].valid)
    {
        struct stat now;

        seen[kind].valid = 0;
        if (!fstatat( store->spool_fd, request_names[kind], &now, AT_SYMLINK_NOFOLLOW ) &&
            (now.st_dev != seen[kind].dev || now.st_ino != seen[kind].ino))
            goto keep_request;   /* a newer request under the same name, not read yet: it stays */
    }
    if (unlinkat( store->spool_fd, request_names[kind], 0 ) && errno != ENOENT) result = GPS_IO_ERROR;
keep_request:
    /* whatever is at NAME.error goes (a link is removed, not followed), then a new file or none */
    if (unlinkat( store->spool_fd, error_name, 0 ) && errno != ENOENT) return GPS_IO_ERROR;
    if (!error) return result;
    length = strlen( error );
    if (length > GPS_ERROR_LIMIT) length = GPS_ERROR_LIMIT;
    fd = openat( store->spool_fd, error_name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0640 );
    if (fd < 0) return GPS_IO_ERROR;
    fchmod( fd, 0640 );
    if (write_all( fd, (const unsigned char *)error, length )) result = GPS_IO_ERROR;
    if (close( fd )) result = GPS_IO_ERROR;
    return result;
}

int gps_store_write( struct gps_store *store, enum gps_kind kind, const unsigned char *data, size_t size )
{
    char temp[64];
    int dir, fd, result = GPS_OK;

    if (kind >= GPS_KIND_COUNT || store->root_fd < 0 || (size && !data)) return GPS_BAD_ARGUMENT;
    if ((dir = open_store_dir( store, kind, 1, &result )) < 0) return result;
    snprintf( temp, sizeof(temp), "%s.new", store_names[kind] );
    /* an old STORE.new, or a link put there, is removed (not followed) before the file is made new */
    if (unlinkat( dir, temp, 0 ) && errno != ENOENT)
    {
        close( dir );
        return GPS_IO_ERROR;
    }
    if (!size)
    {
        if (unlinkat( dir, store_names[kind], 0 ) && errno != ENOENT) result = GPS_IO_ERROR;
        close( dir );
        return result;
    }
    fd = openat( dir, temp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644 );
    if (fd < 0)
    {
        close( dir );
        return GPS_IO_ERROR;
    }
    fchmod( fd, 0644 );   /* readable by everyone, whatever the umask is */
    if (write_all( fd, data, size ) || fsync( fd )) result = GPS_IO_ERROR;
    if (close( fd )) result = GPS_IO_ERROR;
    if (result == GPS_OK && renameat( dir, temp, dir, store_names[kind] )) result = GPS_IO_ERROR;
    if (result != GPS_OK) unlinkat( dir, temp, 0 );
    close( dir );
    return result;
}

void gps_clean( struct gps_store *store )
{
    char temp[64];
    int kind, dir, result;

    for (kind = 0; kind < GPS_KIND_COUNT; kind++)
    {
        if ((dir = open_store_dir( store, kind, 0, &result )) < 0) continue;
        snprintf( temp, sizeof(temp), "%s.new", store_names[kind] );
        unlinkat( dir, temp, 0 );
        close( dir );
    }
}
