/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Absolute paths opened edge by edge, shared by the lab driver and the
 * provisioning helper. Include after profile.c.
 */
#ifndef WEX_PROFILE_ANCHOR_H
#define WEX_PROFILE_ANCHOR_H

struct anchor { int fd; char name[256]; };
struct absolute_path { struct anchor edge[128]; size_t count; };

static void close_absolute( struct absolute_path *p )
{
    while (p->count) close( p->edge[--p->count].fd );
}

/* Retain every directory edge for a final substitution check; no
 * realpath/concatenated mutation, no symlink ancestors. A seed path must be
 * wholly root protected; prefix ancestors may also be Bob.
 */
static enum wex_profile_status open_absolute( const char *path, uid_t bob, int seed,
                                              struct absolute_path *out )
{
    char copy[4096], *part, *next;
    struct stat st;
    int fd;
    if (!path || path[0] != '/' || strlen( path ) >= sizeof(copy) || !path[1]) return WEX_PROFILE_COMPONENT;
    strcpy( copy, path + 1 );
    fd = open( "/", O_RDONLY | O_DIRECTORY | O_CLOEXEC );
    if (fd < 0) return WEX_PROFILE_IO;
    out->edge[out->count++].fd = fd;
    part = copy;
    for (;;)
    {
        if (out->count >= 128) return WEX_PROFILE_LIMIT;
        next = strchr( part, '/' );
        if (next) *next++ = 0;
        if (!*part || strlen(part) > 255 || !strcmp( part, "." ) || !strcmp( part, ".." )) return WEX_PROFILE_COMPONENT;
        if (fstat( fd, &st ) || !S_ISDIR( st.st_mode ) || (st.st_uid && (seed || st.st_uid != bob)) ||
            (st.st_mode & 0022) || !no_posix_acl( fd ) || !no_reparse( fd )) return WEX_PROFILE_MODE;
        fd = openat( fd, part, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK | (next || !seed ? O_DIRECTORY : 0) );
        if (fd < 0) return WEX_PROFILE_SUBSTITUTION;
        strcpy( out->edge[out->count].name, part );
        out->edge[out->count++].fd = fd;
        if (!next) return WEX_PROFILE_OK;
        part = next;
    }
}

static enum wex_profile_status check_absolute( const struct absolute_path *p )
{
    size_t i;
    for (i = 1; i < p->count; i++)
        if (same_edge( p->edge[i - 1].fd, p->edge[i].name, p->edge[i].fd )) return WEX_PROFILE_SUBSTITUTION;
    return WEX_PROFILE_OK;
}

#endif
