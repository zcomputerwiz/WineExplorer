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

/*
 * docs/GROUP_POLICY.md, "Writing the local files: the spool". The store (GroupPolicy and below, in session 0's root)
 * belongs to session 0's account alone; the spool is where the system group leaves requests. Nothing here builds a
 * path string below GroupPolicy and opens it: everything is relative to directory descriptors, with O_NOFOLLOW, so a
 * link that the system group puts in the way is never followed. No Wine headers: tested natively
 * (scripts/test_gpstore_native.sh).
 */

#ifndef WEXGPSTORE_NATIVE_H
#define WEXGPSTORE_NATIVE_H

#include <stddef.h>
#include <sys/types.h>

#define GPS_REQUEST_LIMIT   (4u * 1024 * 1024)
#define GPS_TEMPLATE_LIMIT  (1024u * 1024)
#define GPS_ERROR_LIMIT     200

enum gps_kind { GPS_MACHINE_POL, GPS_USER_POL, GPS_MACHINE_TEMPLATE, GPS_KIND_COUNT };

enum gps_result
{
    GPS_OK = 0,
    GPS_NONE,        /* no request of that name */
    GPS_REFUSED,     /* a link, a FIFO or another file that is not a regular file with one link */
    GPS_TOO_LARGE,
    GPS_IO_ERROR,
    GPS_BAD_TREE,    /* the store or the spool is not as setup makes it (owner, group, mode, a link) */
    GPS_NO_MEMORY,
    GPS_BAD_ARGUMENT
};

struct gps_store
{
    int root_fd;     /* GroupPolicy */
    int spool_fd;    /* GroupPolicy/Spool */
};

/* GroupPolicy below the machine root (the Wine prefix of session 0, an absolute path): the components below the
 * prefix are opened without following links; GroupPolicy has to belong to the calling user and group and not be
 * writable by group or others, Spool to the same user and group and closed to others. */
int gps_open( const char *prefix, struct gps_store *store );
void gps_close( struct gps_store *store );

/* the name of a request in the spool ("Machine.Registry.pol", ...) */
const char *gps_request_name( enum gps_kind kind );

/* a request, if there is one: *data is malloc'd and holds exactly the file's size, *size 0 asks to remove the
 * store file. A link, a FIFO, a file with another link or one over the limit is refused, and nothing hangs. */
int gps_spool_read( struct gps_store *store, enum gps_kind kind, unsigned char **data, size_t *size );

/* the request is handled: remove it, and write NAME.error with the error (at most GPS_ERROR_LIMIT bytes), or remove
 * an old NAME.error when error is NULL */
int gps_spool_done( struct gps_store *store, enum gps_kind kind, const char *error );

/* the store file of a kind: written as STORE.new and renamed (size 0: removed). The directories below GroupPolicy
 * are made when missing and have to belong to the calling user and not be writable by group or others. */
int gps_store_write( struct gps_store *store, enum gps_kind kind, const unsigned char *data, size_t size );

/* stale STORE.new files of an earlier run are removed */
void gps_clean( struct gps_store *store );

#endif
