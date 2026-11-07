/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Root-owned, never setuid/setgid. Normal build denies all operations.
 * Standalone LAB driver; no authenticated S4/S10 or Windows capability claim.
 */
#include "profile.c"

#ifdef WEX_E1S12_PROFILE_TEST
#include <sys/prctl.h>

struct anchor { int fd; char name[256]; };
struct absolute_path { struct anchor edge[128]; size_t count; };

static void close_absolute( struct absolute_path *p )
{
    while (p->count) close( p->edge[--p->count].fd );
}

/* CLI fixture bootstrap only. Retain every directory edge for a final
 * substitution check; no realpath/concatenated mutation, no symlink ancestors.
 * A seed path must be wholly root protected; prefix ancestors may also be Bob.
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

static int number( const char *text, uint64_t maximum, uint64_t *value )
{
    const char *p;
    uint64_t v = 0;
    if (!text[0] || (text[0] == '0' && text[1])) return 0;
    for (p = text; *p; p++)
    {
        unsigned int d = (unsigned char)*p - '0';
        if (d > 9 || d > maximum || v > (maximum - d) / 10) return 0;
        v = v * 10 + d;
    }
    *value = v;
    return 1;
}

static int key_bytes( const char *text, unsigned char *key )
{
    size_t i;
    if (strlen( text ) != 32) return 0;
    memset( key, 0, 16 );
    for (i = 0; i < 32; i++)
    {
        unsigned int c = (unsigned char)text[i], n;
        if (c >= '0' && c <= '9') n = c - '0';
        else if (c >= 'a' && c <= 'f') n = c - 'a' + 10;
        else return 0;
        key[i / 2] = (key[i / 2] << 4) | n;
    }
    return 1;
}

int main( int argc, char **argv )
{
    struct absolute_path prefix = {0}, seed = {0};
    struct wex_account a = {0};
    struct wex_profile_storage out;
    unsigned char zero[sizeof(struct wex_profile_lease_reserved)] = {0};
    struct wex_profile_layout candidate;
    struct stat st;
    uint64_t uid, gid, v;
    enum wex_profile_status s = WEX_PROFILE_COMPONENT;
    int pfd = -1, sfd = -1;
    unsigned int i;
    if (argc != 12 && argc != 13)
    {
        fputs( "lab usage: wex-profile PREFIX OFFLINE_SEED BOB_UID BOB_GID NAME KEY A B C RID MAP_GEN [STOP_STEP]\n", stderr );
        return WEX_PROFILE_COMPONENT;
    }
    if (geteuid() || getuid()) { s = WEX_PROFILE_OWNER; goto done; }
    if (!number( argv[3], UINT32_MAX - 1, &uid ) || !uid ||
        !number( argv[4], UINT32_MAX - 1, &gid ) || !gid || strlen(argv[5]) > WEX_ACCOUNT_NAME_MAX ||
        !key_bytes( argv[6], a.incarnation )) goto done;
    strcpy( a.name, argv[5] );
    a.uid = 1; /* explicitly synthetic account input; NEVER authentication */
    a.administrator = 1;
    a.sid[0] = 21;
    for (i = 1; i < 5; i++)
    {
        if (!number( argv[6 + i], UINT32_MAX, &v )) goto done;
        a.sid[i] = v;
    }
    if (!number( argv[11], UINT64_MAX, &a.generation )) goto done;
    if (argc == 13)
    {
        if (!number( argv[12], 5, &v )) goto done;
        test_stop_step = v;
    }
    if ((s = wex_profile_layout( &a, &candidate ))) goto done;
    if ((s = open_absolute( argv[1], uid, 0, &prefix )) ||
        (s = open_absolute( argv[2], uid, 1, &seed ))) goto done;
    pfd = prefix.edge[prefix.count - 1].fd;
    sfd = seed.edge[seed.count - 1].fd;
    if (fstat( pfd, &st )) { s = WEX_PROFILE_IO; goto done; }
    umask( 077 );
    /* Clear root and supplementary authority permanently BEFORE prefix I/O.
     * No helper/target process is launched; only this lab process changes UID.
     * Linux clears capabilities on this full root -> Bob transition. */
    if (setgroups( 0, NULL ) || setresgid( gid, gid, gid ) || setresuid( uid, uid, uid ) ||
        prctl( PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0 )) { s = WEX_PROFILE_OWNER; goto done; }
    s = wex_profile_prepare( pfd, st.st_dev, st.st_ino, uid, gid, sfd, &a, &out );
    if (!s && ((s = check_absolute( &prefix )) || (s = check_absolute( &seed ))))
    {
        close( out.profile_fd ); close( out.hive_fd );
        memset( &out, 0, sizeof(out) );
        out.profile_fd = out.hive_fd = -1;
    }
    if (!s)
    {
        printf( "status=0 reused=%d acl_pending=%d generation=%"PRIu64" reserved_zero=%d\nprofile=%s\nhive=%s\nwindows=%s\nintent=%s\n",
            out.reused, out.acl_pending, out.profile_generation, !memcmp( zero, &out.reserved, sizeof(zero) ),
            out.path.relative, out.path.hive, out.path.windows, out.acl_intent );
        close( out.profile_fd ); close( out.hive_fd );
    }
    else printf( "status=%u profile_fd=%d hive_fd=%d reserved_zero=%d\n", s, out.profile_fd,
                 out.hive_fd, !memcmp( zero, &out.reserved, sizeof(zero) ) );
done:
    if (s) fprintf( stderr, "DENIED=%u\n", s );
    close_absolute( &seed );
    close_absolute( &prefix );
    return s;
}
#else
int main( void )
{
    fputs( "DENIED=1 (E1-S12 storage disabled)\n", stderr );
    return WEX_PROFILE_DISABLED;
}
#endif
