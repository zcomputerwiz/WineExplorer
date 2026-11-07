/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Root-only account lifecycle maintenance. Never setuid/setgid.
 */
#define _GNU_SOURCE
#define WEX_ACCOUNTS_STANDALONE
#define WEX_ACCOUNTS_TOOL
#include "../../wine/server/accounts.c"
#include <sys/file.h>
#include <dirent.h>

static int mark_initialized( int dir );

static int random_bytes( void *data, size_t size )
{
    int fd = open( "/dev/urandom", O_RDONLY | O_CLOEXEC );
    unsigned char *p = data;
    ssize_t n;
    if (fd == -1) return 0;
    while (size)
    {
        n = read( fd, p, size );
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { close( fd ); return 0; }
        p += n;
        size -= n;
    }
    close( fd );
    return 1;
}

static int new_record( struct account_map *map, const struct account_native *u, int legacy )
{
    static const char hex[] = "0123456789abcdef";
    struct account_record *r;
    unsigned char raw[16];
    unsigned int i, j, attempt;
    if (map->count == ACCOUNT_LIMIT || (!legacy && map->next_rid > U32_MAX_VALUE)) return 0;
    r = &map->records[map->count];
    memset( r, 0, sizeof(*r) );
    for (attempt = 0; attempt < 8; attempt++)
    {
        if (!random_bytes( raw, sizeof(raw) )) return 0;
        for (i = 0; i < 16; i++) { r->key[2*i] = hex[raw[i] >> 4]; r->key[2*i+1] = hex[raw[i] & 15]; }
        if (!strcmp( r->key, "00000000000000000000000000000000" )) continue;
        for (j = 0; j < map->count; j++) if (!strcmp( r->key, map->records[j].key )) break;
        if (j == map->count) break;
    }
    if (attempt == 8) return 0;
    strcpy( r->name, u->name );
    r->uid = u->uid;
    r->active = 1;
    r->legacy = legacy;
    r->rid = legacy ? 1000 : map->next_rid++;
    map->count++;
    return 1;
}

static int save_map( int dir, const struct account_map *map, gid_t group, int fresh )
{
    char name[64];
    unsigned char random[16];
    unsigned int i;
    int fd = -1, ok = 0;
    FILE *f;
    if (fresh) strcpy( name, ".accounts.pending" );
    else
    {
        if (!random_bytes( random, sizeof(random) )) return 0;
        strcpy( name, ".accounts-" );
        for (i = 0; i < 16; i++) sprintf( name + 10 + 2*i, "%02x", random[i] );
    }
    if ((fd = openat( dir, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600 )) == -1) return 0;
    /* fchmod constrains inherited default ACL masks; no group may write. */
    if (fchown( fd, 0, group ) || fchmod( fd, 0640 ) || !no_access_acl( fd )) goto done;
    if (!(f = fdopen( fd, "w" ))) goto done;
    fprintf( f, "WEXACCOUNTS 1 %llu %llu %u %u %u\n",
             (unsigned long long)map->generation, (unsigned long long)map->next_rid,
             map->machine[0], map->machine[1], map->machine[2] );
    for (i = 0; i < map->count; i++)
    {
        const struct account_record *r = &map->records[i];
        fprintf( f, "%s %u %s %s 21 %u %u %u %u\n", r->name, r->uid, r->key,
                 r->active ? "active" : "tombstone", r->legacy ? 0 : map->machine[0],
                 r->legacy ? 0 : map->machine[1], r->legacy ? 0 : map->machine[2], r->rid );
    }
    ok = !ferror( f ) && !fflush( f ) && ftell( f ) >= 0 && ftell( f ) <= MAP_LIMIT && !fsync( fd );
    if (fclose( f )) ok = 0;
    fd = -1;
    /* The durable pending map precedes the guard. After the guard, recovery
     * publishes these exact bytes; it must never invent a new identity. */
    if (ok && fresh) ok = !fsync( dir ) && mark_initialized( dir );
    if (ok) ok = !renameat( dir, name, dir, "accounts.map" ) && !fsync( dir );
done:
    if (fd != -1) close( fd );
    if (!fresh) unlinkat( dir, name, 0 );
    return ok;
}

static int checked_lock( int dir )
{
    struct stat st;
    int fd = openat( dir, ".accounts.lock", O_RDWR | O_CREAT | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600 );
    if (fd == -1)
    { protection_error( ".accounts.lock", strerror(errno), -1, (gid_t)-1 ); return -1; }
    if (fstat( fd, &st ) || !S_ISREG( st.st_mode ) || st.st_uid || (st.st_mode & 0077) || st.st_nlink != 1 || !no_access_acl( fd ))
    {
        protection_error( ".accounts.lock", "unsafe lock metadata or ACL", fd, (gid_t)-1 );
        close( fd ); return -1;
    }
    if (flock( fd, LOCK_EX | LOCK_NB ))
    {
        fprintf( stderr, "wex-accounts: lock acquisition failed: %s\n", strerror(errno) );
        close( fd ); return -1;
    }
    return fd;
}

static int mark_initialized( int dir )
{
    struct stat st;
    int fd = openat( dir, ".accounts.initialized", O_WRONLY | O_CREAT | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600 );
    int ok;
    if (fd == -1) return 0;
    ok = !fstat( fd, &st ) && S_ISREG( st.st_mode ) && !st.st_uid && !(st.st_mode & 0077) && st.st_nlink == 1 && no_access_acl( fd ) &&
         !fsync( fd ) && !fsync( dir );
    close( fd );
    return ok;
}

/* The lock excludes live writers. Only the exact random temporary-file name
 * is ours; never follow or remove unrelated files or the pending map. */
static int cleanup_temps( int dir )
{
    DIR *stream;
    struct dirent *entry;
    int copy = openat( dir, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC );
    int ok = 1;
    if (copy == -1) return 0;
    if (!(stream = fdopendir( copy ))) { close( copy ); return 0; }
    errno = 0;
    while ((entry = readdir( stream )))
    {
        const char *n = entry->d_name;
        if (strlen( n ) == 42 && !strncmp( n, ".accounts-", 10 ) &&
            strspn( n + 10, "0123456789abcdef" ) == 32 && unlinkat( dir, n, 0 ) && errno != ENOENT)
        { ok = 0; break; }
        errno = 0;
    }
    if (errno) ok = 0;
    if (closedir( stream )) ok = 0;
    return ok;
}

int main( int argc, char **argv )
{
    struct account_map *map = NULL;
    struct account_native *users = NULL;
    struct account_policy policy;
    struct stat st;
    char *buf = NULL;
    const char *command = NULL, *legacy = NULL, *retire = NULL, *rename_old = NULL, *rename_new = NULL;
    unsigned int count, i, j;
    int dir = -1, lock = -1, initialize = 0, fresh = 0, changed = 0, ok = 0;
    size_t length;
    enum wex_account_status status = WEX_ACCOUNT_MAP_INVALID;

    umask( 0077 );
    if (getuid() || geteuid()) { fprintf( stderr, "wex-accounts: root required\n" ); return 1; }
    for (i = 1; i < (unsigned int)argc; i++)
    {
        if (!strcmp( argv[i], "--directory" ) && i + 1 < (unsigned int)argc) account_directory = argv[++i];
        else if (!strcmp( argv[i], "--initialize" )) initialize = 1;
        else if (!strcmp( argv[i], "--legacy-user" ) && i + 1 < (unsigned int)argc) legacy = argv[++i];
#ifdef WEX_ACCOUNTS_TEST
        else if (!strcmp( argv[i], "test-small-number" ))
        {
            uint64_t number;
            return parse_number( "9", 5, &number ) || !parse_number( "5", 5, &number );
        }
        else if (!strcmp( argv[i], "--test-passwd" ) && i + 1 < (unsigned int)argc) test_passwd = argv[++i];
        else if (!strcmp( argv[i], "--test-group" ) && i + 1 < (unsigned int)argc) test_group = argv[++i];
        else if (!strcmp( argv[i], "--test-server-uid" ) && i + 1 < (unsigned int)argc)
        {
            uint64_t number;
            if (!parse_number( argv[++i], U32_MAX_VALUE - 1, &number )) goto done;
            test_server_uid = number;
        }
        else if ((!strcmp( argv[i], "lookup" ) || !strcmp( argv[i], "lookup-no-cap" )) && i + 1 < (unsigned int)argc)
        {
            struct wex_account result;
            uint64_t number;
            uint64_t caps = !strcmp( argv[i], "lookup" ) ? WEX_ACCOUNT_MAP_V1 : 0;
            const char *name = argv[++i];
            uid_t uid = 0;
            if (name[0] == '#')
            {
                if (!parse_number( name + 1, U32_MAX_VALUE - 1, &number )) goto done;
                uid = number;
                name = NULL;
            }
            status = lookup_account( name, uid, caps, &result );
            printf( "status=%u name=%s uid=%u gid=%u admin=%d generation=%llu sid=S-1-5-%u-%u-%u-%u-%u\n",
                    (unsigned int)status, result.name, (unsigned int)result.uid, (unsigned int)result.primary_gid,
                    result.administrator, (unsigned long long)result.generation,
                    result.sid[0], result.sid[1], result.sid[2], result.sid[3], result.sid[4] );
            return status;
        }
#endif
        else if (!command && !strcmp( argv[i], "sync" )) command = "sync";
        else if (!command && !strcmp( argv[i], "retire" ) && i + 1 < (unsigned int)argc)
        { command = "retire"; retire = argv[++i]; }
        else if (!command && !strcmp( argv[i], "rename" ) && i + 2 < (unsigned int)argc)
        { command = "rename"; rename_old = argv[++i]; rename_new = argv[++i]; }
        else goto done;
    }
    if (!command || (initialize && strcmp( command, "sync" ))) goto done;
    if ((dir = protected_directory( account_directory, &status )) == -1) goto done;
    if ((status = read_policy( dir, &policy ))) goto done;
    status = WEX_ACCOUNT_MAP_UNPROTECTED;
    if ((lock = checked_lock( dir )) == -1) goto done;
    if (!cleanup_temps( dir )) goto done;
    status = WEX_ACCOUNT_MAP_UNREADABLE;
    if (!(map = calloc( 1, sizeof(*map) )) || !(users = malloc( ACCOUNT_LIMIT * sizeof(*users) ))) goto done;
    if (fstatat( dir, "accounts.map", &st, AT_SYMLINK_NOFOLLOW ))
    {
        int guarded;
        if (errno != ENOENT || strcmp( command, "sync" )) goto done;
        guarded = !fstatat( dir, ".accounts.initialized", &st, AT_SYMLINK_NOFOLLOW );
        if (!guarded && errno != ENOENT) goto done;
        if (guarded)
        {
            /* A missing pending file means a previously published map may
             * have been lost: never reset its history automatically. */
            if (!mark_initialized( dir ) ||
                (status = read_protected( dir, ".accounts.pending", policy.system_gid, &buf, &length )) ||
                (status = parse_map( buf, map )) ||
                renameat( dir, ".accounts.pending", dir, "accounts.map" ) || fsync( dir )) goto done;
            free( buf );
            buf = NULL;
            goto recovered;
        }
        /* An unguarded pending file was never published and can be retried. */
        if (unlinkat( dir, ".accounts.pending", 0 ) && errno != ENOENT) goto done;
        fresh = changed = 1;
        map->next_rid = 1001;
        for (i = 0; i < 3; i++)
            do { if (!random_bytes( &map->machine[i], sizeof(map->machine[i]) )) goto done; } while (!map->machine[i]);
    }
    else
    {
        if ((status = read_protected( dir, "accounts.map", policy.system_gid, &buf, &length )) ||
            (status = parse_map( buf, map ))) goto done;
    }
recovered:
    if ((status = native_accounts( users, &count ))) goto done;
    status = WEX_ACCOUNT_MAP_INVALID;
    if (legacy)
    {
        /* An explicit legacy binding is immutable on subsequent setup runs. */
        for (i = 0; i < map->count; i++) if (map->records[i].legacy) break;
        if (!fresh && (i == map->count || strcmp( map->records[i].name, legacy ))) goto done;
        for (j = 0; j < count; j++) if (!strcmp( users[j].name, legacy ) && users[j].supported) break;
        if (j == count) goto done;
        if (fresh && !new_record( map, &users[j], 1 )) goto done;
    }
    if (retire)
    {
        for (i = 0; i < map->count; i++) if (map->records[i].active && !strcmp( map->records[i].name, retire ))
        { map->records[i].active = 0; changed = 1; }
        /* Deliberately do not re-add a still-present or already recreated name. */
    }
    else
    {
        if (rename_old)
        {
            for (i = 0; i < map->count; i++) if (map->records[i].active && !strcmp( map->records[i].name, rename_old )) break;
            if (i == map->count) goto done;
            for (j = 0; j < count; j++) if (!strcmp( users[j].name, rename_old )) goto done;
            for (j = 0; j < count; j++) if (!strcmp( users[j].name, rename_new ) && users[j].uid == map->records[i].uid) break;
            if (j == count) goto done;
            for (j = 0; j < map->count; j++) if (map->records[j].active && !strcmp( map->records[j].name, rename_new )) goto done;
            strcpy( map->records[i].name, rename_new );
            changed = 1;
        }
        for (i = 0; i < map->count; i++) if (map->records[i].active)
        {
            for (j = 0; j < count; j++)
                if (map->records[i].uid == users[j].uid && !strcmp( map->records[i].name, users[j].name )) break;
            if (j == count) { map->records[i].active = 0; changed = 1; }
        }
        for (j = 0; j < count; j++)
        {
            for (i = 0; i < map->count; i++)
                if (map->records[i].active && map->records[i].uid == users[j].uid && !strcmp( map->records[i].name, users[j].name )) break;
            if (i == map->count)
            {
                if (!new_record( map, &users[j], 0 )) goto done;
                changed = 1;
            }
        }
    }
    status = WEX_ACCOUNT_MAP_UNREADABLE;
    if (changed)
    {
        if (map->generation == UINT64_MAX) goto done;
        map->generation++;
        if (!save_map( dir, map, policy.system_gid, fresh )) goto done;
    }
    if (!mark_initialized( dir )) goto done;
    ok = 1;
done:
    if (!ok) fprintf( stderr, "wex-accounts: FAIL status=%u (sync [--initialize] [--legacy-user NAME], retire NAME, rename OLD NEW)\n", (unsigned int)status );
    else printf( "wex-accounts: OK generation=%llu changed=%d\n", (unsigned long long)map->generation, changed );
    free( buf );
    free( users );
    free( map );
    if (lock != -1) close( lock );
    if (dir != -1) close( dir );
    return !ok;
}
