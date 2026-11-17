/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Root-only account lifecycle maintenance. Never setuid/setgid.
 */
#define _GNU_SOURCE
#define WEX_ACCOUNTS_STANDALONE
#define WEX_ACCOUNTS_TOOL
#include "../../wine/server/accounts.c"
#include <sys/file.h>
#include <sys/wait.h>
#include <time.h>
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

/* fixed_rid: a domain record's RID, which takes no counter RID; 0 for a local record */
static int new_record( struct account_map *map, const struct account_native *u, int legacy, int builtin,
                       uint32_t fixed_rid )
{
    static const char hex[] = "0123456789abcdef";
    struct account_record *r;
    unsigned char raw[16];
    unsigned int i, j, attempt;
    if (map->count == ACCOUNT_LIMIT || (!legacy && !fixed_rid && map->next_rid > U32_MAX_VALUE)) return 0;
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
    r->domain = fixed_rid != 0;
    for (i = 0; i < 3; i++) r->sid[i] = legacy ? 0 : r->domain ? map->domain[i] : map->machine[i];
    r->rid = fixed_rid ? fixed_rid : legacy ? 1000 : builtin ? BUILTIN_ADMIN_RID : map->next_rid++;
    map->count++;
    return 1;
}

/* a domain account admitted at logon (docs/DOMAIN_LOGON.md): the header's domain SID and the domain's RID */
static int new_domain_record( struct account_map *map, const char *name, uid_t uid, uint32_t rid )
{
    struct account_native u;

    memset( &u, 0, sizeof(u) );
    strcpy( u.name, name );
    u.uid = uid;
    return new_record( map, &u, 0, 0, rid );
}

/* Winbind, asked by running wbinfo from a fixed path with a clean environment (no shell); its standard output
 * and error in out (on success the error stream is empty). The exit status, or -1 when it cannot run, times out,
 * fails to read, fills the buffer or contains NUL bytes. */
#ifdef WEX_ACCOUNTS_TEST
static const char *wbinfo_path = "/usr/bin/wbinfo";
#else
#define wbinfo_path "/usr/bin/wbinfo"
#endif
static int run_wbinfo( char *out, size_t size, const char *arg1, const char *arg2 )
{
    char *argv[] = { (char *)"wbinfo", (char *)arg1, (char *)arg2, NULL };
    char *envp[] = { (char *)"PATH=/usr/bin:/bin", (char *)"LC_ALL=C", NULL };
    size_t used = 0;
    int pipefd[2], status, waited, read_error = 0;
    pid_t pid;
    ssize_t n;

    out[0] = 0;
    if (pipe2( pipefd, O_CLOEXEC )) return -1;
    if ((pid = fork()) == -1) { close( pipefd[0] ); close( pipefd[1] ); return -1; }
    if (!pid)
    {
        int null = open( "/dev/null", O_RDWR );
        dup2( pipefd[1], 1 );
        dup2( pipefd[1], 2 );
        if (null != -1) dup2( null, 0 );
        alarm( 15 );
        execve( wbinfo_path, argv, envp );
        _exit( 127 );
    }
    close( pipefd[1] );
    while (used + 1 < size && (n = read( pipefd[0], out + used, size - 1 - used )) != 0)
    {
        if (n < 0) { if (errno == EINTR) continue; read_error = 1; break; }
        used += n;
    }
    out[used] = 0;
    close( pipefd[0] );
    while ((waited = waitpid( pid, &status, 0 )) == -1 && errno == EINTR) ;
    if (waited != pid || !WIFEXITED( status ) || read_error || used == size - 1 || memchr( out, 0, used )) return -1;
    return WEXITSTATUS( status );
}

/* "S-1-5-21-a-b-c" (rid NULL) or "S-1-5-21-a-b-c-rid" at the start of s, ended by a space, a newline or the end */
static int parse_sid( const char *s, uint32_t sub[3], uint32_t *rid )
{
    char part[16];
    unsigned int i, count = rid ? 4 : 3;
    uint64_t n;

    if (strncmp( s, "S-1-5-21-", 9 )) return 0;
    s += 9;
    for (i = 0; i < count; i++)
    {
        size_t len = strspn( s, "0123456789" );
        if (!len || len >= sizeof(part)) return 0;
        memcpy( part, s, len );
        part[len] = 0;
        if (!parse_number( part, U32_MAX_VALUE, &n )) return 0;
        if (i < 3) sub[i] = n; else *rid = n;
        s += len;
        if (i + 1 < count) { if (*s != '-') return 0; s++; }
    }
    return !*s || *s == ' ' || *s == '\n';
}

/* the joined domain as Winbind gives it: flat name and SID */
static int winbind_domain( char flat[16], uint32_t sid[3] )
{
    char out[4096], *p;
    size_t len;

    if (run_wbinfo( out, sizeof(out), "--own-domain", NULL )) return 0;
    len = strcspn( out, "\n" );
    if (!len || len > 15) return 0;
    memcpy( flat, out, len );
    flat[len] = 0;
    if (!valid_domain_name( flat ) || run_wbinfo( out, sizeof(out), "-D", flat )) return 0;
    /* "SID               : S-1-5-21-..." */
    for (p = out; p && *p; p = strchr( p, '\n' ) ? strchr( p, '\n' ) + 1 : NULL)
    {
        if (strncmp( p, "SID", 3 ) || p[3] != ' ') continue;
        if (!(p = strstr( p, ": " ))) return 0;
        return parse_sid( p + 2, sid, NULL );
    }
    return 0;
}

/* Successful name lookups include the numeric SID_NAME_USE in parentheses. Ignore the display label: neither
 * it nor a failed lookup's diagnostic is an API. Unknown types and failed lookups are inconclusive.
 * The standalone package has no libwbclient dependency; missing records are checked by the successful,
 * numeric --lookup-sids result below instead of linking Samba into every build of this tool. */
enum winbind_answer { WB_USER, WB_NONE, WB_ERROR };

static enum winbind_answer winbind_user_sid( const char *name, uint32_t sid[3], uint32_t *rid )
{
    char out[4096], *p, *end;
    uint64_t type;
    size_t len;

    if (run_wbinfo( out, sizeof(out), "--name-to-sid", name ) || !parse_sid( out, sid, rid )) return WB_ERROR;
    len = strlen( out );
    if (!len || out[len - 1] != '\n' || strchr( out, '\n' ) != out + len - 1 ||
        !(p = strrchr( out, '(' )) || p == out || p[-1] != ' ' || !(end = strchr( p, ')' )) ||
        end != out + len - 2) return WB_ERROR;
    *end = 0;
    if (!parse_number( p + 1, U32_MAX_VALUE, &type )) return WB_ERROR;
    switch (type)
    {
    case 1: return WB_USER;  /* SidTypeUser */
    case 2: case 3: case 4: case 5: case 6: case 9: return WB_NONE; /* known non-user or deleted account */
    default: return WB_ERROR;
    }
}

/* --lookup-sids uses wbcLookupSids: errors fail the command, whereas a successful answer includes the queried
 * SID and a numeric SID_NAME_USE. Only SidTypeUnknown (8), on exactly that SID and one complete line, proves
 * it is unmapped. Unsupported options, diagnostics, truncation, malformed data and all nonzero exits KEEP.
 * A failed name lookup alone proves nothing, even if a previous --ping-dc succeeded. */
static int winbind_record_missing( const struct account_record *r )
{
    char out[4096], sid[64], *type;
    size_t len, sidlen;

    snprintf( sid, sizeof(sid), "S-1-5-21-%u-%u-%u-%u", r->sid[0], r->sid[1], r->sid[2], r->rid );
    if (run_wbinfo( out, sizeof(out), "--lookup-sids", sid )) return 0;
    len = strlen( out );
    sidlen = strlen( sid );
    if (len <= sidlen + 7 || strncmp( out, sid, sidlen ) || strncmp( out + sidlen, " -> ", 4 ) ||
        out[len - 1] != '\n' || strchr( out, '\n' ) != out + len - 1 ||
        !(type = strrchr( out, ' ' )) || type <= out + sidlen + 4) return 0;
    return !strcmp( type, " 8\n" );
}

/* Domain records that the domain no longer has become tombstones, but only when the controller answers: a user
 * must be able to log on while it is away (docs/DOMAIN_LOGON.md, condition 6). A different joined domain
 * tombstones all of the old domain's records. 1 when the map changed. */
static int check_domain_records( struct account_map *map )
{
    char out[256], flat[16];
    uint32_t sid[3], rid;
    unsigned int i;
    int changed = 0, any = 0;

    for (i = 0; i < map->count; i++) if (map->records[i].domain && map->records[i].active) any = 1;
    if (map->version != 2 || !any) return 0;
    if (run_wbinfo( out, sizeof(out), "--ping-dc", NULL ) || !winbind_domain( flat, sid )) return 0;
    if (memcmp( sid, map->domain, sizeof(sid) ) || strcmp( flat, map->domain_name ))
    {
        for (i = 0; i < map->count; i++)
            if (map->records[i].domain && map->records[i].active) { map->records[i].active = 0; changed = 1; }
        memcpy( map->domain, sid, sizeof(sid) );
        strcpy( map->domain_name, flat );
        return changed;
    }
    for (i = 0; i < map->count; i++)
    {
        struct account_record *r = &map->records[i];
        struct account_native u;

        if (!r->domain || !r->active) continue;
        /* a definitive answer only: no such user, or another SID, RID or (NSS answering) UID; an error keeps the
         * record for the next sync */
        switch (winbind_user_sid( r->name, sid, &rid ))
        {
        case WB_ERROR:
            if (!winbind_record_missing( r )) continue;
            break;
        case WB_NONE: break;
        case WB_USER:
            if (!memcmp( sid, r->sid, sizeof(sid) ) && rid == r->rid &&
                !(native_passwd( r->name, 0, &u ) && u.uid != r->uid)) continue;
            break;
        }
        r->active = 0;
        changed = 1;
    }
    return changed;
}

struct domain_check
{
    char *original;
    size_t length;
    struct account_map *map;
    int changed;
};

/* Sync's potentially slow Winbind/NSS probes never hold the map lock. Publication is atomic, so a protected
 * snapshot can be read without it. After locking, apply these answers only to the exact bytes probed: a
 * concurrent admit, restore or other writer invalidates the whole batch, leaving domain records for next sync.
 * Missing/invalid maps are handled by the normal locked validation and recovery, never by this preflight. */
static void prepare_domain_check( int dir, gid_t group, struct domain_check *check )
{
    struct stat st;
    char *parsed;

    if (fstatat( dir, "accounts.map", &st, AT_SYMLINK_NOFOLLOW ) ||
        read_protected( dir, "accounts.map", group, &check->original, &check->length )) return;
    if (!(check->map = calloc( 1, sizeof(*check->map) )) || !(parsed = strdup( check->original ))) return;
    /* parse_map splits its buffer in place; retain the original bytes for the locked comparison. */
    if (!parse_map( parsed, check->map )) check->changed = check_domain_records( check->map );
    free( parsed );
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
    if (map->version == 2)
        fprintf( f, "WEXACCOUNTS 2 %llu %llu %u %u %u %u %u %u %s\n",
                 (unsigned long long)map->generation, (unsigned long long)map->next_rid,
                 map->machine[0], map->machine[1], map->machine[2],
                 map->domain[0], map->domain[1], map->domain[2], map->domain_name );
    else
        fprintf( f, "WEXACCOUNTS 1 %llu %llu %u %u %u\n",
                 (unsigned long long)map->generation, (unsigned long long)map->next_rid,
                 map->machine[0], map->machine[1], map->machine[2] );
    for (i = 0; i < map->count; i++)
    {
        const struct account_record *r = &map->records[i];
        if (map->version == 2)
            fprintf( f, "%s %u %s %s %s 21 %u %u %u %u\n", r->name, r->uid, r->key,
                     r->active ? "active" : "tombstone", r->domain ? "domain" : "local",
                     r->sid[0], r->sid[1], r->sid[2], r->rid );
        else
            fprintf( f, "%s %u %s %s 21 %u %u %u %u\n", r->name, r->uid, r->key,
                     r->active ? "active" : "tombstone", r->sid[0], r->sid[1], r->sid[2], r->rid );
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
    /* another run holds the map (a second logon at the same time, or a sync): wait for it, 20 seconds at most,
     * without a blocking flock (an alarm is in use for wbinfo) */
    for (int tries = 0; flock( fd, LOCK_EX | LOCK_NB ); tries++)
    {
        const struct timespec pause = { 0, 50 * 1000 * 1000 };

        if ((errno != EWOULDBLOCK && errno != EINTR) || tries >= 400)
        {
            fprintf( stderr, "wex-accounts: lock acquisition failed: %s\n", strerror(errno) );
            close( fd ); return -1;
        }
        nanosleep( &pause, NULL );
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

/* Admit a domain user at logon (docs/DOMAIN_LOGON.md, design B and conditions 1, 2, 6): the name is PAM's, the
 * identity is NSS's and Winbind's, and anything that does not fit refuses the logon. users[] are the local
 * accounts of /etc/passwd. Under the map lock. */
static enum wex_account_status admit_domain_user( struct account_map *map, const struct account_native *users,
                                                  unsigned int count, const char *pam_user, int *changed,
                                                  char admitted[64] )
{
    struct account_native u;
    char flat[16];
    uint32_t domain[3], sid[3], rid;
    size_t len;
    unsigned int i;

    /* the account as NSS (Winbind) names it, both ways round, with a shell that can log on */
    if (!valid_domain_account( pam_user, NULL ) || !native_passwd( pam_user, 0, &u ) || !native_match( &u ))
        return WEX_ACCOUNT_NSS_MISMATCH;
    if (!valid_domain_account( u.name, &len ) || !u.supported) return WEX_ACCOUNT_UNSUPPORTED;
    /* not a local account: no UID below 1000 and none that /etc/passwd or a local record has */
    if (u.uid < 1000) return WEX_ACCOUNT_UNSUPPORTED;
    for (i = 0; i < count; i++) if (users[i].uid == u.uid) return WEX_ACCOUNT_UNSUPPORTED;
    for (i = 0; i < map->count; i++)
        if (!map->records[i].domain && map->records[i].uid == u.uid) return WEX_ACCOUNT_UNSUPPORTED;
    /* a user of the domain this machine is joined to */
    if (!winbind_domain( flat, domain )) return WEX_ACCOUNT_NSS_ERROR;
    if (strlen( flat ) != len || strncasecmp( u.name, flat, len )) return WEX_ACCOUNT_UNSUPPORTED;
    switch (winbind_user_sid( u.name, sid, &rid ))
    {
    case WB_USER: break;
    case WB_NONE: return WEX_ACCOUNT_UNKNOWN;
    case WB_ERROR: return WEX_ACCOUNT_NSS_ERROR;
    }
    if (memcmp( sid, domain, sizeof(sid) ) || rid < 500) return WEX_ACCOUNT_UNSUPPORTED;
    /* the name as the map keeps it: the domain's flat name, then the user's part as NSS gives it */
    memcpy( u.name, flat, len );
    strcpy( admitted, u.name );

    /* The map's domain is set at the first domain logon. A logon never changes it: Winbind giving another
     * domain at one logon refuses that logon, and only sync, with the controller answering, moves the map to a
     * new domain and retires the old one's records (review of WO 1, change 2). */
    if (map->version == 2 && (memcmp( map->domain, domain, sizeof(domain) ) || strcmp( map->domain_name, flat )))
        return WEX_ACCOUNT_NSS_ERROR;
    if (map->version == 1)
    {
        if (!memcmp( domain, map->machine, sizeof(domain) )) return WEX_ACCOUNT_MAP_INVALID;
        map->version = 2;
        memcpy( map->domain, domain, sizeof(domain) );
        strcpy( map->domain_name, flat );
        *changed = 1;
    }
    for (i = 0; i < map->count; i++)
    {
        const struct account_record *r = &map->records[i];

        if (r->active && !strcmp( r->name, u.name ))
        {
            /* known: nothing to do while it is the same identity; otherwise sync has to settle it first */
            if (r->domain && r->uid == u.uid && r->rid == rid && !memcmp( r->sid, domain, sizeof(domain) ))
                return WEX_ACCOUNT_OK;
            return WEX_ACCOUNT_NSS_MISMATCH;
        }
        if (r->active && r->uid == u.uid) return WEX_ACCOUNT_NSS_MISMATCH;
        /* a SID is never given twice, also not to the same account coming back (an administrator retires and
         * restores, not the logon) */
        if (!memcmp( r->sid, domain, sizeof(domain) ) && r->rid == rid) return WEX_ACCOUNT_SID_COLLISION;
    }
    if (!new_domain_record( map, u.name, u.uid, rid )) return WEX_ACCOUNT_MAP_INVALID;
    *changed = 1;
    return WEX_ACCOUNT_OK;
}

/* WO 2, condition 4: the domain groups of an admitted account, recorded at each logon by this root step from
 * Winbind (wbinfo --user-domgroups), never from anything the user's processes write. Winbind not answering keeps
 * the last recorded set (and never adds a group); Winbind answering but the file not written fails the logon,
 * since an old set could keep a user in Domain Admins after removal. Only lines of active domain records stay. */
static int record_domain_groups( int dir, const struct account_map *map, gid_t group, const char *name )
{
    const struct account_record *r = NULL;
    static char out[65536];
    char sidtext[64], *line, *next, *text = NULL, *keep = NULL;
    uint32_t rids[GROUPS_MAX], sid[3], rid;
    unsigned int count = 0, i, j;
    size_t length, used = 0, room;
    int fd = -1, ok = 0;
    FILE *f = NULL;

    for (i = 0; i < map->count; i++)
        if (map->records[i].active && map->records[i].domain && !strcmp( map->records[i].name, name ))
            r = &map->records[i];
    if (!r) return 0;
    snprintf( sidtext, sizeof(sidtext), "S-1-5-21-%u-%u-%u-%u", r->sid[0], r->sid[1], r->sid[2], r->rid );
    if (run_wbinfo( out, sizeof(out), "--user-domgroups", sidtext )) return 1;   /* not answering: keep */
    for (line = out; line && *line; line = next)
    {
        if ((next = strchr( line, '\n' ))) *next++ = 0;
        if (!*line) continue;
        if (!parse_sid( line, sid, &rid )) return 0;   /* an answer that does not parse is not taken */
        if (memcmp( sid, map->domain, sizeof(sid) ) || rid < 500) continue;   /* other domains: not recorded */
        /* Samba's answer includes the user's own SID (an identity of its own, as a group id); a token has the user
         * once, as the user */
        if (rid == r->rid) continue;
        for (j = 0; j < count; j++) if (rids[j] == rid) break;
        if (j < count) continue;
        if (count == GROUPS_MAX) return 0;
        rids[count++] = rid;
    }
    /* the other active domain records' lines stay as they are */
    room = sizeof(out);
    if (!(keep = malloc( room ))) return 0;
    keep[0] = 0;
    if (!read_protected( dir, "domain-groups", group, &text, &length ) && !strncmp( text, "WEXGROUPS 1\n", 12 ))
    {
        for (line = text + 12; *line; line = next)
        {
            /* a last line without its newline is a cut file: dropped, since a cut number is another group
             * ("5121" cut to "512" would be Domain Admins) */
            if (!(next = strchr( line, '\n' ))) break;
            *next++ = 0;
            if (strlen( line ) < 33 || !strncmp( line, r->key, 32 )) continue;
            for (j = 0; j < map->count; j++)
                if (map->records[j].active && map->records[j].domain && !strncmp( map->records[j].key, line, 32 )) break;
            if (j == map->count || used + strlen( line ) + 2 > room) continue;
            used += sprintf( keep + used, "%s\n", line );
        }
    }
    if ((fd = openat( dir, ".domain-groups.new", O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600 )) == -1)
        goto done;
    if (fchown( fd, 0, group ) || fchmod( fd, 0640 ) || !no_access_acl( fd ) || !(f = fdopen( fd, "w" ))) goto done;
    fd = -1;
    fprintf( f, "WEXGROUPS 1\n%s%s", keep, r->key );
    for (j = 0; j < count; j++) fprintf( f, " %u", rids[j] );
    fputc( '\n', f );
    ok = !ferror( f ) && !fflush( f ) && !fsync( fileno( f ) );
    if (fclose( f )) ok = 0;
    f = NULL;
    if (ok) ok = !renameat( dir, ".domain-groups.new", dir, "domain-groups" ) && !fsync( dir );
done:
    if (f) fclose( f );
    if (fd != -1) close( fd );
    if (!ok) unlinkat( dir, ".domain-groups.new", 0 );
    free( keep );
    free( text );
    return ok;
}

/* wex-accounts restore NAME (review of WO 1, change 3): a domain account wrongly retired comes back. The newest
 * tombstone of that name is made active again, with its key: the same SID is the same account, and the key is
 * what its managed profile is stored under, so a new key would part the account from its own profile. Only when
 * Winbind confirms the same SID and RID now (a positive answer; an error refuses), NSS gives the same UID both
 * ways, the record is of the map's domain, and no active record and no local account has the name or the UID. */
static enum wex_account_status restore_domain_user( struct account_map *map, const struct account_native *users,
                                                    unsigned int count, const char *name, int *changed )
{
    struct account_record *r = NULL;
    struct account_native u;
    uint32_t sid[3], rid;
    unsigned int i;

    if (!valid_domain_account( name, NULL ) || map->version != 2) return WEX_ACCOUNT_UNKNOWN;
    for (i = 0; i < map->count; i++)
    {
        if (map->records[i].active && !strcmp( map->records[i].name, name )) return WEX_ACCOUNT_NSS_MISMATCH;
        if (!map->records[i].active && map->records[i].domain && !strcmp( map->records[i].name, name ))
            r = &map->records[i];
    }
    if (!r) return WEX_ACCOUNT_UNKNOWN;
    if (memcmp( r->sid, map->domain, sizeof(r->sid) )) return WEX_ACCOUNT_UNSUPPORTED;
    for (i = 0; i < map->count; i++)
        if (map->records[i].active && map->records[i].uid == r->uid) return WEX_ACCOUNT_NSS_MISMATCH;
    for (i = 0; i < count; i++) if (users[i].uid == r->uid) return WEX_ACCOUNT_NSS_MISMATCH;
    if (!native_passwd( name, 0, &u ) || !native_match( &u ) || u.uid != r->uid) return WEX_ACCOUNT_NSS_MISMATCH;
    if (!u.supported) return WEX_ACCOUNT_UNSUPPORTED;
    switch (winbind_user_sid( name, sid, &rid ))
    {
    case WB_USER: break;
    case WB_NONE: return WEX_ACCOUNT_UNKNOWN;
    case WB_ERROR: return WEX_ACCOUNT_NSS_ERROR;
    }
    if (memcmp( sid, r->sid, sizeof(sid) ) || rid != r->rid) return WEX_ACCOUNT_SID_COLLISION;
    r->active = 1;
    *changed = 1;
    return WEX_ACCOUNT_OK;
}

int main( int argc, char **argv )
{
    struct account_map *map = NULL;
    struct account_native *users = NULL;
    const struct account_native *builtin = NULL;
    struct account_policy policy;
    struct domain_check domain_check = {0};
    struct stat st;
    char *buf = NULL;
    const char *command = NULL, *legacy = NULL, *retire = NULL, *rename_old = NULL, *rename_new = NULL;
    const char *admit_user = NULL, *restore_name = NULL;
    char admitted[64] = "";
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
        else if (!strcmp( argv[i], "--test-wbinfo" ) && i + 1 < (unsigned int)argc) wbinfo_path = argv[++i];
        else if (!strcmp( argv[i], "--test-winbind-passwd" ) && i + 1 < (unsigned int)argc) test_winbind_passwd = argv[++i];
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
        /* the user comes only from PAM_USER (condition 2 of docs/DOMAIN_LOGON.md), never from an argument */
        else if (!command && !strcmp( argv[i], "admit" )) command = "admit";
        else if (!command && !strcmp( argv[i], "restore" ) && i + 1 < (unsigned int)argc)
        { command = "restore"; restore_name = argv[++i]; }
        else if (!command && !strcmp( argv[i], "retire" ) && i + 1 < (unsigned int)argc)
        { command = "retire"; retire = argv[++i]; }
        else if (!command && !strcmp( argv[i], "rename" ) && i + 2 < (unsigned int)argc)
        { command = "rename"; rename_old = argv[++i]; rename_new = argv[++i]; }
        else goto done;
    }
    if (!command || (initialize && strcmp( command, "sync" ))) goto done;
    if (!strcmp( command, "admit" ))
    {
        /* a plain name is a local account: sync's business, nothing to admit */
        const char *pam_user = getenv( "PAM_USER" ), *pam_type = getenv( "PAM_TYPE" );
        if (!pam_type || (strcmp( pam_type, "open_session" ) && strcmp( pam_type, "account" ))) goto done;
        if (!pam_user || !*pam_user) goto done;
        if (!strchr( pam_user, '\\' ))
        {
            if (!accounts_valid_name( pam_user )) goto done;
            ok = 1;
            goto done;
        }
        admit_user = pam_user;
    }
    if ((dir = protected_directory( account_directory, &status )) == -1) goto done;
    if ((status = read_policy( dir, &policy ))) goto done;
    if (!strcmp( command, "sync" )) prepare_domain_check( dir, policy.system_gid, &domain_check );
    status = WEX_ACCOUNT_MAP_UNPROTECTED;
    if ((lock = checked_lock( dir )) == -1) goto done;
    if (!cleanup_temps( dir )) goto done;
    status = WEX_ACCOUNT_MAP_UNREADABLE;
    if (!(map = calloc( 1, sizeof(*map) )) || !(users = malloc( ACCOUNT_LIMIT * sizeof(*users) ))) goto done;
    if (fstatat( dir, "accounts.map", &st, AT_SYMLINK_NOFOLLOW ))
    {
        int guarded;
        /* admit never makes a map: a machine without one has no accounts yet */
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
        map->version = 1;
        map->next_rid = 1001;
        for (i = 0; i < 3; i++)
            do { if (!random_bytes( &map->machine[i], sizeof(map->machine[i]) )) goto done; } while (!map->machine[i]);
    }
    else
    {
        if ((status = read_protected( dir, "accounts.map", policy.system_gid, &buf, &length ))) goto done;
        if (domain_check.changed && length == domain_check.length &&
            !memcmp( buf, domain_check.original, length ))
        {
            memcpy( map, domain_check.map, sizeof(*map) );
            changed = 1;
        }
        else if ((status = parse_map( buf, map ))) goto done;
    }
recovered:
    if ((status = native_accounts( users, &count ))) goto done;
    if (admit_user)
    {
        if ((status = admit_domain_user( map, users, count, admit_user, &changed, admitted ))) goto done;
        goto save;
    }
    if (restore_name)
    {
        if ((status = restore_domain_user( map, users, count, restore_name, &changed ))) goto done;
        goto save;
    }
    status = WEX_ACCOUNT_MAP_INVALID;
    if (legacy)
    {
        /* An explicit legacy binding is immutable on subsequent setup runs. */
        for (i = 0; i < map->count; i++) if (map->records[i].legacy) break;
        if (!fresh && (i == map->count || strcmp( map->records[i].name, legacy ))) goto done;
        for (j = 0; j < count; j++) if (!strcmp( users[j].name, legacy ) && users[j].supported) break;
        if (j == count) goto done;
        if (fresh && !new_record( map, &users[j], 1, 0, 0 )) goto done;
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
        /* local records follow /etc/passwd; domain records follow Winbind (check_domain_records), except that a
         * local account always wins a UID */
        for (i = 0; i < map->count; i++) if (map->records[i].active && !map->records[i].domain)
        {
            for (j = 0; j < count; j++)
                if (map->records[i].uid == users[j].uid && !strcmp( map->records[i].name, users[j].name )) break;
            if (j == count) { map->records[i].active = 0; changed = 1; }
        }
        for (i = 0; i < map->count; i++) if (map->records[i].active && map->records[i].domain)
        {
            for (j = 0; j < count; j++) if (map->records[i].uid == users[j].uid) break;
            if (j < count) { map->records[i].active = 0; changed = 1; }
        }
        /* The built-in Administrator (sysrun.conf "builtin_administrator=NAME")
         * receives the reserved RID 500 the first time it is mapped. A counter-RID
         * record it already has is retired once and replaced (a new SID). RID 500
         * is never reassigned: once any record holds it, nothing changes here. The
         * account must be in the administrator group, else the sync fails. */
        if (policy.builtin_admin[0])
        {
            for (i = 0; i < map->count; i++)
                if (!map->records[i].legacy && !map->records[i].domain && map->records[i].rid == BUILTIN_ADMIN_RID) break;
            if (i == map->count)
            {
                for (j = 0; j < count; j++) if (!strcmp( users[j].name, policy.builtin_admin )) break;
                if (j < count)
                {
                    int member;
                    if ((status = administrator_member( &users[j], policy.admin_group, &member ))) goto done;
                    status = WEX_ACCOUNT_MAP_INVALID;
                    if (!member) goto done;
                    for (i = 0; i < map->count; i++)
                        if (map->records[i].active && !strcmp( map->records[i].name, users[j].name ))
                        {
                            if (map->records[i].legacy) goto done;
                            map->records[i].active = 0;
                            changed = 1;
                        }
                    builtin = &users[j];
                }
            }
        }
        for (j = 0; j < count; j++)
        {
            for (i = 0; i < map->count; i++)
                if (map->records[i].active && map->records[i].uid == users[j].uid && !strcmp( map->records[i].name, users[j].name )) break;
            if (i == map->count)
            {
                if (!new_record( map, &users[j], 0, &users[j] == builtin, 0 )) goto done;
                changed = 1;
            }
        }
    }
save:
    status = WEX_ACCOUNT_MAP_UNREADABLE;
    if (changed)
    {
        if (map->generation == UINT64_MAX) goto done;
        map->generation++;
        if (!save_map( dir, map, policy.system_gid, fresh )) goto done;
    }
    /* the admitted account's domain groups, after its record is in the map (WO 2) */
    status = WEX_ACCOUNT_NSS_ERROR;
    if (admitted[0] && !record_domain_groups( dir, map, policy.system_gid, admitted )) goto done;
    status = WEX_ACCOUNT_MAP_UNREADABLE;
    if (!mark_initialized( dir )) goto done;
    ok = 1;
done:
    if (!ok) fprintf( stderr, "wex-accounts: FAIL status=%u (sync [--initialize] [--legacy-user NAME], retire NAME, rename OLD NEW, admit, restore NAME)\n", (unsigned int)status );
    else printf( "wex-accounts: OK generation=%llu changed=%d\n", map ? (unsigned long long)map->generation : 0ULL, changed );
    free( buf );
    free( domain_check.original );
    free( domain_check.map );
    free( users );
    free( map );
    if (lock != -1) close( lock );
    if (dir != -1) close( dir );
    return !ok;
}
