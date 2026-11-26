/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * wex-accountsd: the accounts helper (docs/LOCAL_USERS_DESIGN.md, "Draft 3: the helper wex-accountsd and its link").
 *
 * A native program under its own system account (wexaccounts, no supplementary groups, not in the system group). It
 * makes the AccountsService and systemd D-Bus calls that Local Users and Groups needs, for the wineserver of session 0
 * and nobody else, and it is the one place that decides account policy. polkit names only this account.
 *
 *   - Socket activation: the SEQPACKET socket (accounts.sock) is a systemd socket unit's, root-owned, 0660, group of
 *     session 0's account only. One connection is served at a time (one request at a time, start to end, so the last
 *     administrator check and its change cannot race).
 *   - The peer check runs at accept, before anything is read: the pid from SO_PEERCRED, pinned with SO_PEERPIDFD (before
 *     Linux 6.5: the process start time read before and after), must be the MainPID of the unit of session 0's server
 *     (read from systemd at each connect), its uid the session 0 account's, and the process non-dumpable (its
 *     /proc/<pid>/stat file is owned by root). Anything else is dropped at once.
 *   - The server's verdict (none, user, elevated administrator) allows a class of operation and never more. The helper
 *     checks every rule it can itself: the uid range of /etc/login.defs, never root, system accounts, the grammar and
 *     collisions of names, the hash form, the last administrator, the built-in administrator, the caller's own
 *     account, logged-in users.
 *   - The password is only ever a crypt(3) SHA-512 hash made by the client, or the empty string for a blank password
 *     (docs/BLANK_PASSWORDS.md: an empty shadow field that only the greeter accepts); it is never logged.
 *
 * Usage: wex-accountsd [--config FILE]. Lab options (only with WEX_ACCOUNTSD_TEST=1 in the environment): --bus ADDRESS
 * (a private bus), --listen PATH (make and listen on this socket), --login-defs FILE, --admin-members a,b (instead of
 * the admin group), --mainpid PID or --mainpid-file FILE (instead of systemd's answer, the file read at each connect),
 * --any-dumpable (skip the non-dumpable check), --no-idle-exit.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <systemd/sd-bus.h>
#include <systemd/sd-daemon.h>

#include "../../src/include/wexaccounts_proto.h"

#ifndef SO_PEERPIDFD
#define SO_PEERPIDFD 77
#endif

#define ACC_DEST "org.freedesktop.Accounts"
#define ACC_PATH "/org/freedesktop/Accounts"
#define ACC_IFACE "org.freedesktop.Accounts"
#define ACC_USER "org.freedesktop.Accounts.User"
#define SD_DEST "org.freedesktop.systemd1"
#define LOGIN_DEST "org.freedesktop.login1"

static struct
{
    char s0user[64], unit[128], admin_group[64], builtin[64], sync_unit[128], helper_user[64], blank_unit[128];
} cfg;

/* the group that Debian's greeter stacks let in without a password (pam_succeed_if user ingroup nopasswdlogin) */
#define BLANK_GROUP "nopasswdlogin"

/* The lab options below bypass the peer check (a private bus, a socket of their own, the MainPID from a file): they exist
 * only in a build made with -DWEX_ACCOUNTSD_LAB_TEST (scripts/test_accountsd.py builds one). The shipped binary has none of
 * them: it refuses the options, and scripts/test_accountsd_units.py checks that. */
#ifdef WEX_ACCOUNTSD_LAB_TEST
static int test_mode, any_dumpable, no_idle_exit;
static const char *test_bus, *test_listen, *test_login_defs, *test_admin_members, *test_mainpid_file;
static pid_t test_mainpid = -1;
static const char *group_path = "/etc/group", *passwd_path = "/etc/passwd", *common_auth_path = "/etc/pam.d/common-auth";
#else
#define common_auth_path "/etc/pam.d/common-auth"
#define group_path "/etc/group"
#define passwd_path "/etc/passwd"
#define test_mode 0
#define any_dumpable 0
#define no_idle_exit 0
#define test_bus ((const char *)NULL)
#define test_login_defs ((const char *)NULL)
#define test_admin_members ((const char *)NULL)
#endif
#define BUS_TIMEOUT_USEC (10 * 1000000ULL)   /* below the server's read timeout (15 s), so that 1722 never hides a change that completes */
static sd_bus *bus;
static unsigned int uid_min = 1000, uid_max = 60000;

static void logf_(const char *fmt, ...)
{
    va_list ap;
    va_start( ap, fmt );
    fprintf( stderr, "wex-accountsd: " );
    vfprintf( stderr, fmt, ap );
    fputc( '\n', stderr );
    va_end( ap );
}

/* ------------------------------------------------------------------------------------------- configuration */

static int valid_ident( const char *s, size_t max )
{
    size_t i, n = strlen( s );

    if (!n || n > max) return 0;
    for (i = 0; i < n; i++)
    {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
              c == '.' || c == '@'))
            return 0;
    }
    return 1;
}

static int load_config( const char *path )
{
    char line[512];
    FILE *f = fopen( path, "re" );

    strcpy( cfg.admin_group, "sudo" );
    strcpy( cfg.sync_unit, "wex-accounts-sync.service" );
    strcpy( cfg.blank_unit, "wex-accounts-blank.service" );
    if (!f) { logf_( "cannot read %s: %s", path, strerror( errno ) ); return 0; }
    while (fgets( line, sizeof(line), f ))
    {
        char *eq, *nl = strchr( line, '\n' );

        if (nl) *nl = 0;
        if (!line[0] || line[0] == '#') continue;
        if (!(eq = strchr( line, '=' ))) { logf_( "bad line in %s", path ); fclose( f ); return 0; }
        *eq++ = 0;
        if (!strcmp( line, "session0_user" ) && valid_ident( eq, 63 )) strcpy( cfg.s0user, eq );
        else if (!strcmp( line, "session0_unit" ) && valid_ident( eq, 127 )) strcpy( cfg.unit, eq );
        else if (!strcmp( line, "admin_group" ) && valid_ident( eq, 63 )) strcpy( cfg.admin_group, eq );
        else if (!strcmp( line, "builtin_administrator" ) && (!*eq || valid_ident( eq, 63 ))) strcpy( cfg.builtin, eq );
        else if (!strcmp( line, "sync_unit" ) && valid_ident( eq, 127 )) strcpy( cfg.sync_unit, eq );
        else if (!strcmp( line, "blank_unit" ) && valid_ident( eq, 127 )) strcpy( cfg.blank_unit, eq );
        else if (!strcmp( line, "helper_user" ) && valid_ident( eq, 63 )) strcpy( cfg.helper_user, eq );   /* the server reads it too */
        else { logf_( "unknown or bad key %s in %s", line, path ); fclose( f ); return 0; }
    }
    fclose( f );
    if (!cfg.s0user[0] || !cfg.unit[0]) { logf_( "%s needs session0_user and session0_unit", path ); return 0; }
    return 1;
}

static void load_login_defs( void )
{
    char line[256];
    FILE *f = fopen( test_login_defs ? test_login_defs : "/etc/login.defs", "re" );

    if (!f) return;
    while (fgets( line, sizeof(line), f ))
    {
        unsigned int v;
        if (sscanf( line, "UID_MIN %u", &v ) == 1) uid_min = v;
        else if (sscanf( line, "UID_MAX %u", &v ) == 1) uid_max = v;
    }
    fclose( f );
    if (uid_min < 1000 || uid_max < uid_min || uid_max >= 65534) { uid_min = 1000; uid_max = 60000; }
}

/* ------------------------------------------------------------------------------------------------ peer check */

static unsigned long long proc_start( pid_t pid )
{
    char path[64], buf[4096], *p;
    unsigned long long start = 0;
    ssize_t n;
    int fd, i;

    snprintf( path, sizeof(path), "/proc/%d/stat", (int)pid );
    if ((fd = open( path, O_RDONLY | O_CLOEXEC )) == -1) return 0;
    n = read( fd, buf, sizeof(buf) - 1 );
    close( fd );
    if (n <= 0) return 0;
    buf[n] = 0;
    if (!(p = strrchr( buf, ')' ))) return 0;   /* the name may hold spaces and parentheses: after the last one */
    p += 2;
    for (i = 3; i < 22 && p; i++) p = strchr( p, ' ' ) ? strchr( p, ' ' ) + 1 : NULL;
    if (!p) return 0;
    start = strtoull( p, NULL, 10 );
    return start;
}

static int unit_main_pid( pid_t *pid )
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    const char *path = NULL;
    uint32_t main_pid = 0;
    int r;

#ifdef WEX_ACCOUNTSD_LAB_TEST
    if (test_mainpid_file)
    {
        FILE *f = fopen( test_mainpid_file, "re" );
        int v = 0;

        if (f && fscanf( f, "%d", &v ) == 1 && v > 0) test_mainpid = v;
        else test_mainpid = -1;
        if (f) fclose( f );
        if (test_mainpid == -1) return 0;
    }
    if (test_mainpid != -1) { *pid = test_mainpid; return 1; }
#endif
    if (!bus) return 0;
    r = sd_bus_call_method( bus, SD_DEST, "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager", "GetUnit", &err,
                            &reply, "s", cfg.unit );
    if (r < 0) { sd_bus_error_free( &err ); return 0; }
    r = sd_bus_message_read( reply, "o", &path );
    if (r >= 0 && path)
    {
        char path_copy[256];
        snprintf( path_copy, sizeof(path_copy), "%s", path );
        sd_bus_message_unref( reply );
        r = sd_bus_get_property_trivial( bus, SD_DEST, path_copy, "org.freedesktop.systemd1.Service", "MainPID", &err, 'u',
                                         &main_pid );
        sd_bus_error_free( &err );
        if (r < 0 || !main_pid) return 0;
        *pid = (pid_t)main_pid;
        return 1;
    }
    sd_bus_message_unref( reply );
    return 0;
}

/* Is the peer of the connected socket the main process of session 0's server unit? */
static int peer_ok( int fd )
{
    struct ucred cred;
    socklen_t len = sizeof(cred);
    struct passwd *pw;
    struct pollfd pfd;
    struct stat st;
    char path[64];
    unsigned long long before, after;
    pid_t main_pid;
    int pin = -1, ok = 0;

    if (getsockopt( fd, SOL_SOCKET, SO_PEERCRED, &cred, &len ) || cred.pid <= 0) return 0;
    if (!(pw = getpwnam( cfg.s0user )) || pw->pw_uid != cred.uid) return 0;
    len = sizeof(pin);
    if (getsockopt( fd, SOL_SOCKET, SO_PEERPIDFD, &pin, &len )) pin = -1;
    before = proc_start( cred.pid );
    if (!before) goto done;
    if (!unit_main_pid( &main_pid ) || main_pid != cred.pid) goto done;
    /* the files of a non-dumpable process in /proc belong to root (the directory itself does not: the kernel gives that one
     * to the process's user whatever it is); those of a dumpable one (a program the server runs) belong to its user */
    snprintf( path, sizeof(path), "/proc/%d/stat", (int)cred.pid );
    if (stat( path, &st ) || (!any_dumpable && st.st_uid != 0)) goto done;
    after = proc_start( cred.pid );
    if (after != before) goto done;
    if (pin != -1)
    {
        pfd.fd = pin;
        pfd.events = POLLIN;   /* a pidfd is readable once its process has exited */
        pfd.revents = 0;
        if (poll( &pfd, 1, 0 ) != 0) goto done;
    }
    ok = 1;
done:
    if (pin != -1) close( pin );
    return ok;
}

/* ------------------------------------------------------------------------------------------------ the bus */

static int open_bus( void )
{
    int r;

    if (bus) return 1;
    if (test_bus)
    {
        if (sd_bus_new( &bus ) < 0) return 0;
        sd_bus_set_address( bus, test_bus );
        sd_bus_set_bus_client( bus, 1 );
        if ((r = sd_bus_start( bus )) < 0) { sd_bus_unref( bus ); bus = NULL; return 0; }
    }
    else if ((r = sd_bus_open_system( &bus )) < 0) { bus = NULL; return 0; }
    /* F6: every call has the same bounded wait */
    sd_bus_set_method_call_timeout( bus, BUS_TIMEOUT_USEC );
    return 1;
}

static void drop_bus( void )
{
    if (bus) sd_bus_flush_close_unref( bus );
    bus = NULL;
}

/* an sd-bus failure as a status of the link */
static int map_error( int r, const sd_bus_error *err )
{
    if (err && err->name)
    {
        if (strstr( err->name, "PermissionDenied" ) || strstr( err->name, "PolicyKit" ) || strstr( err->name, "AccessDenied" ))
            return WEXA_DENIED;
        if (strstr( err->name, "UserExists" )) return WEXA_EXISTS;
        if (strstr( err->name, "UserDoesntExist" ) || strstr( err->name, "NoSuchUser" )) return WEXA_NOT_FOUND;
        if (strstr( err->name, "Disconnected" ) || strstr( err->name, "NoReply" ) || strstr( err->name, "ServiceUnknown" ))
            return WEXA_UNAVAILABLE;
        return WEXA_BUSY;
    }
    return (r == -ENOENT || r == -ECONNREFUSED || r == -ENOTCONN || r == -EPIPE || r == -ECONNRESET) ? WEXA_UNAVAILABLE : WEXA_BUSY;
}

/* ------------------------------------------------------------------------------- the pieces of the policy */

static const char *reserved_names[] = { "root", "sudo", "wexsys", "wexaccounts", "daemon", "bin", "sys", "nobody", "admin", NULL };

static int valid_user_name( const char *name )
{
    size_t i, n = strlen( name );
    unsigned int k;

    if (!n || n > 32) return 0;
    if (!((name[0] >= 'a' && name[0] <= 'z') || name[0] == '_')) return 0;
    for (i = 0; i < n; i++)
    {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0;
    }
    for (k = 0; reserved_names[k]; k++) if (!strcmp( name, reserved_names[k] )) return 0;
    if (!strcmp( name, cfg.admin_group ) || !strcmp( name, cfg.s0user )) return 0;
    return 1;
}

/* a name for a user that nothing else uses: no user and no group has it */
static int name_is_free( const char *name )
{
    return !getpwnam( name ) && !getgrnam( name );
}

static int valid_full_name( const char *s )
{
    const unsigned char *p;

    for (p = (const unsigned char *)s; *p; p++)
        if (*p < 0x20 || *p == 0x7f || *p == ':' || *p == ',') return 0;
    return 1;
}

static int is_hash_char( int c )
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '/';
}

/* "$6$" ["rounds=N$"] salt "$" 86 characters of the crypt alphabet, nothing else */
static int valid_hash( const char *h )
{
    size_t i, n = strlen( h );
    const char *p = h + 3;
    size_t salt;

    if (n < 20 || n > 200 || strncmp( h, "$6$", 3 )) return 0;
    if (!strncmp( p, "rounds=", 7 ))
    {
        char *end;
        unsigned long rounds;

        p += 7;
        if (*p < '0' || *p > '9') return 0;
        rounds = strtoul( p, &end, 10 );
        if (*end != '$' || rounds < 1000 || rounds > 999999999 || end - p > 9) return 0;
        p = end + 1;
    }
    for (salt = 0; p[salt] && p[salt] != '$'; salt++) if (!is_hash_char( (unsigned char)p[salt] )) return 0;
    if (!salt || salt > 16 || p[salt] != '$') return 0;
    p += salt + 1;
    if (strlen( p ) != 86) return 0;
    for (i = 0; i < 86; i++) if (!is_hash_char( (unsigned char)p[i] )) return 0;
    return 1;
}

/* What a password field of a request may be: a crypt(3) hash of the form above, or the empty string, which is a blank
 * password (docs/BLANK_PASSWORDS.md). The empty string is never crypt("") (a real password that every stack would take):
 * it means an empty shadow field, which only the greeter lets in. 0 refuses, 1 a hash, 2 blank. */
static int password_form( const char *h )
{
    if (!h[0]) return 2;
    return valid_hash( h ) ? 1 : 0;
}

static int uid_in_range( uid_t uid )
{
    return uid != 0 && uid != 65534 && uid >= uid_min && uid <= uid_max;
}

struct user_info
{
    char path[256], name[64], full[256];
    uint32_t uid, type, locked;
    int system_account;
};

static int prop_string( const char *path, const char *name, char *out, size_t size )
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    char *value = NULL;
    int r = sd_bus_get_property_string( bus, ACC_DEST, path, ACC_USER, name, &err, &value );

    sd_bus_error_free( &err );
    if (r < 0) return r;
    snprintf( out, size, "%s", value ? value : "" );
    free( value );
    return 0;
}

/* the properties of the user object at u->path (and u->name), into u; the status when they cannot be had */
static int load_props( struct user_info *u )
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int32_t type = 0;
    int locked = 0, system = 0, r;
    uint64_t uid = 0;

    if (prop_string( u->path, "RealName", u->full, sizeof(u->full) ) < 0) return WEXA_BUSY;
    if (sd_bus_get_property_trivial( bus, ACC_DEST, u->path, ACC_USER, "Uid", &err, 't', &uid ) < 0 ||
        sd_bus_get_property_trivial( bus, ACC_DEST, u->path, ACC_USER, "AccountType", &err, 'i', &type ) < 0 ||
        sd_bus_get_property_trivial( bus, ACC_DEST, u->path, ACC_USER, "Locked", &err, 'b', &locked ) < 0 ||
        sd_bus_get_property_trivial( bus, ACC_DEST, u->path, ACC_USER, "SystemAccount", &err, 'b', &system ) < 0)
    {
        r = map_error( -EIO, &err );
        sd_bus_error_free( &err );
        return r;
    }
    sd_bus_error_free( &err );
    u->uid = (uint32_t)uid;
    u->type = type == 1 ? 1 : 0;
    u->locked = locked ? 1 : 0;
    u->system_account = system;
    return WEXA_OK;
}

/* the user of a name from AccountsService; the status when it cannot be had */
static int find_user( const char *name, struct user_info *u )
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    const char *path = NULL;
    int r;

    memset( u, 0, sizeof(*u) );
    r = sd_bus_call_method( bus, ACC_DEST, ACC_PATH, ACC_IFACE, "FindUserByName", &err, &reply, "s", name );
    if (r < 0)
    {
        r = map_error( r, &err );
        sd_bus_error_free( &err );
        return r == WEXA_BUSY ? WEXA_NOT_FOUND : r;
    }
    sd_bus_error_free( &err );
    if (sd_bus_message_read( reply, "o", &path ) < 0 || !path) { sd_bus_message_unref( reply ); return WEXA_BUSY; }
    snprintf( u->path, sizeof(u->path), "%s", path );
    sd_bus_message_unref( reply );
    snprintf( u->name, sizeof(u->name), "%s", name );
    return load_props( u );
}

#define MAX_ADMINS 1024

static int add_member( char names[][64], unsigned int *n, unsigned int max, const char *name )
{
    unsigned int i;

    if (!name[0] || strlen( name ) > 63) return 1;
    for (i = 0; i < *n; i++) if (!strcmp( names[i], name )) return 1;
    if (*n >= max) return 0;
    snprintf( names[(*n)++], 64, "%s", name );
    return 1;
}

/* The members of the administrator group: those the group file lists and the users whose primary group it is, read from
 * /etc/group and /etc/passwd themselves (not through getgrnam: nscd or sssd may answer from a cache that has not yet
 * seen a change that AccountsService just made, which the last-administrator rule must not trust). The admin group is a
 * local one. The lab's list stands in for both. */
static unsigned int admin_members( char names[][64], unsigned int max )
{
    unsigned int n = 0;
    char *line = NULL, *save;
    size_t cap = 0;
    long gid = -1;
    FILE *f;

#ifdef WEX_ACCOUNTSD_LAB_TEST
    if (test_admin_members)
    {
        char *copy = strdup( test_admin_members ), *tok;
        for (tok = strtok_r( copy, ",", &save ); tok; tok = strtok_r( NULL, ",", &save )) add_member( names, &n, max, tok );
        free( copy );
        return n;
    }
#endif
    if ((f = fopen( group_path, "re" )))
    {
        while (getline( &line, &cap, f ) > 0)
        {
            char *name, *pw, *gids, *members, *m;

            line[strcspn( line, "\n" )] = 0;
            name = strtok_r( line, ":", &save );
            if (!name || strcmp( name, cfg.admin_group )) continue;
            pw = strtok_r( NULL, ":", &save );
            gids = strtok_r( NULL, ":", &save );
            members = strtok_r( NULL, ":", &save );
            (void)pw;
            if (gids) gid = strtol( gids, NULL, 10 );
            if (members)
                for (m = strtok_r( members, ",", &save ); m; m = strtok_r( NULL, ",", &save )) add_member( names, &n, max, m );
            break;
        }
        fclose( f );
    }
    if (gid >= 0 && (f = fopen( passwd_path, "re" )))
    {
        while (getline( &line, &cap, f ) > 0)
        {
            char *name, *pw, *uid, *pgid;

            line[strcspn( line, "\n" )] = 0;
            name = strtok_r( line, ":", &save );
            pw = strtok_r( NULL, ":", &save );
            uid = strtok_r( NULL, ":", &save );
            pgid = strtok_r( NULL, ":", &save );
            (void)pw; (void)uid;
            if (name && pgid && strtol( pgid, NULL, 10 ) == gid) add_member( names, &n, max, name );
        }
        fclose( f );
    }
    free( line );
    return n;
}

static int is_admin_name( const char *name )
{
    static char names[MAX_ADMINS][64];
    unsigned int i, n = admin_members( names, MAX_ADMINS );

    for (i = 0; i < n; i++) if (!strcmp( names[i], name )) return 1;
    return 0;
}

/* how many administrators other than this account can still be used: members that exist and are not locked */
static unsigned int other_active_admins( const char *name )
{
    static char names[MAX_ADMINS][64];
    unsigned int i, n = admin_members( names, MAX_ADMINS ), count = 0;

    for (i = 0; i < n; i++)
    {
        struct user_info u;

        if (!strcmp( names[i], name )) continue;
        if (find_user( names[i], &u ) == WEXA_OK && !u.locked) count++;
    }
    return count;
}

static int is_logged_in( uid_t uid )
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    int r = sd_bus_call_method( bus, LOGIN_DEST, "/org/freedesktop/login1", "org.freedesktop.login1.Manager", "GetUser", &err,
                                &reply, "u", (uint32_t)uid );

    if (r >= 0) { sd_bus_message_unref( reply ); return 1; }
    r = err.name && (strstr( err.name, "NoSuchUser" ) != NULL) ? 0 : 1;   /* an answer we cannot read: assume yes */
    sd_bus_error_free( &err );
    return r;
}

/* the checks that every change of an existing account shares */
static int check_target( const struct user_info *u, uint32_t caller_uid )
{
    if (!uid_in_range( u->uid ) || u->system_account) return WEXA_DENIED;
    if (cfg.builtin[0] && !strcmp( u->name, cfg.builtin )) return WEXA_DENIED;
    (void)caller_uid;
    return WEXA_OK;
}

/* ------------------------------------------------------------------------------------- the message bodies */

struct reader { const uint8_t *p, *end; int bad; };

static uint32_t rd_u32( struct reader *r )
{
    uint32_t v;

    if (r->end - r->p < 4) { r->bad = 1; return 0; }
    memcpy( &v, r->p, 4 );
    r->p += 4;
    return v;
}

static const char *rd_str( struct reader *r, char *out, size_t size )
{
    uint16_t len;
    size_t i;

    if (r->end - r->p < 2) { r->bad = 1; return ""; }
    memcpy( &len, r->p, 2 );
    r->p += 2;
    if (len > WEXA_MAX_STR || len >= size || r->end - r->p < len) { r->bad = 1; return ""; }
    for (i = 0; i < len; i++) if (!r->p[i]) { r->bad = 1; return ""; }
    memcpy( out, r->p, len );
    out[len] = 0;
    r->p += len;
    return out;
}

struct writer { uint8_t *p; size_t used, size; int bad; };

static void wr_u32( struct writer *w, uint32_t v )
{
    if (w->size - w->used < 4) { w->bad = 1; return; }
    memcpy( w->p + w->used, &v, 4 );
    w->used += 4;
}

static void wr_str( struct writer *w, const char *s )
{
    size_t len = strlen( s );
    uint16_t l16 = (uint16_t)(len > WEXA_MAX_STR ? WEXA_MAX_STR : len);

    if (w->size - w->used < 2u + l16) { w->bad = 1; return; }
    memcpy( w->p + w->used, &l16, 2 );
    memcpy( w->p + w->used + 2, s, l16 );
    w->used += 2u + l16;
}

static void wr_record( struct writer *w, const struct user_info *u )
{
    wr_str( w, u->name );
    wr_str( w, u->full );
    wr_u32( w, u->uid );
    wr_u32( w, u->type );
    wr_u32( w, u->locked );
}

/* ------------------------------------------------------------------------------------------ the operations */

static int call_user_method( const struct user_info *u, const char *method, const char *sig, ... )
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *m = NULL;
    va_list ap;
    int r;

    r = sd_bus_message_new_method_call( bus, &m, ACC_DEST, u->path, ACC_USER, method );
    if (r < 0) return WEXA_BUSY;
    va_start( ap, sig );
    r = sd_bus_message_appendv( m, sig, ap );
    va_end( ap );
    if (r >= 0) r = sd_bus_call( bus, m, 0, &err, NULL );
    sd_bus_message_unref( m );
    if (r < 0)
    {
        r = map_error( r, &err );
        sd_bus_error_free( &err );
        return r;
    }
    return WEXA_OK;
}

#define MAX_LIST 4096   /* users the list can hold (a page is what fits in one frame; the rest comes by start index) */

static size_t record_size( const struct user_info *u )
{
    size_t n = strlen( u->name ), f = strlen( u->full );
    return 2 + (n > WEXA_MAX_STR ? WEXA_MAX_STR : n) + 2 + (f > WEXA_MAX_STR ? WEXA_MAX_STR : f) + 12;
}

/* F1: a page of the users. The reply is u32 total, u32 first (the start asked for), u32 count in this page, then the
 * records; the page stops before a record that does not fit, and the client asks again from first + count. */
static int op_list( struct writer *w, uint32_t start )
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    const char *path;
    struct user_info *all = malloc( MAX_LIST * sizeof(*all) );
    uint32_t total = 0, count = 0, i;
    size_t count_at;
    int r;

    if (!all) return WEXA_BUSY;
    r = sd_bus_call_method( bus, ACC_DEST, ACC_PATH, ACC_IFACE, "ListCachedUsers", &err, &reply, "" );
    if (r < 0) { r = map_error( r, &err ); sd_bus_error_free( &err ); free( all ); return r; }
    if (sd_bus_message_enter_container( reply, 'a', "o" ) < 0) { sd_bus_message_unref( reply ); free( all ); return WEXA_BUSY; }
    while (total < MAX_LIST && sd_bus_message_read( reply, "o", &path ) > 0)
    {
        struct user_info u;
        char name[64], pathcopy[256];

        snprintf( pathcopy, sizeof(pathcopy), "%s", path );
        /* the name is a property of the user object */
        if (prop_string( pathcopy, "UserName", name, sizeof(name) ) < 0 || find_user( name, &u ) != WEXA_OK) continue;
        if (!uid_in_range( u.uid ) || u.system_account) continue;
        all[total++] = u;
    }
    sd_bus_message_unref( reply );
    wr_u32( w, total );
    wr_u32( w, start );
    count_at = w->used;
    wr_u32( w, 0 );
    for (i = start; i < total; i++)
    {
        if (w->size - w->used < record_size( &all[i] )) break;
        wr_record( w, &all[i] );
        count++;
    }
    memcpy( w->p + count_at, &count, 4 );
    free( all );
    return w->bad ? WEXA_BAD_ARGUMENT : WEXA_OK;
}

/* the same paging for the members of the administrator group: u32 total, u32 first, u32 count, the names */
static int op_admin_members( struct writer *w, uint32_t start )
{
    static char names[MAX_ADMINS][64];
    unsigned int i, n = admin_members( names, MAX_ADMINS ), count = 0;
    size_t count_at;

    wr_u32( w, n );
    wr_u32( w, start );
    count_at = w->used;
    wr_u32( w, 0 );
    for (i = start; i < n; i++)
    {
        if (w->size - w->used < 2u + strlen( names[i] )) break;
        wr_str( w, names[i] );
        count++;
    }
    memcpy( w->p + count_at, &count, 4 );
    return w->bad ? WEXA_BAD_ARGUMENT : WEXA_OK;
}

static int op_start_sync( void )
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int r = sd_bus_call_method( bus, SD_DEST, "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager", "StartUnit", &err,
                                NULL, "ss", cfg.sync_unit, "replace" );

    if (r < 0) { r = map_error( r, &err ); sd_bus_error_free( &err ); return r; }
    return WEXA_OK;
}

/* A created, deleted or renamed user reaches the account map when the sync unit has run: the helper asks for it itself (the server
 * forwards one client message once, so the Windows-side service has no request of its own to send), and the service watches the map's
 * generation. A refusal of the unit is only logged: the change was made, and the timer syncs anyway. */
static void sync_after_change( void )
{
    int r = op_start_sync();

    if (r != WEXA_OK) logf_( "the sync unit could not be started (status %d)", r );
}

/* F5: only a name that cannot carry a control character or a newline goes into the journal; anything else is "<invalid>" */
static void set_logname( char *out, const char *name )
{
    size_t i, n = strlen( name );

    if (!n || n > 63) { strcpy( out, "<invalid>" ); return; }
    for (i = 0; i < n; i++)
    {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' ||
              c == '@'))
        {
            strcpy( out, "<invalid>" );
            return;
        }
    }
    memcpy( out, name, n + 1 );
}

/* F2: a user that was made and then could not be finished is deleted again (files are never removed) */
static void rollback_create( uint32_t uid )
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int r = sd_bus_call_method( bus, ACC_DEST, ACC_PATH, ACC_IFACE, "DeleteUser", &err, NULL, "xb", (int64_t)uid, 0 );

    if (r < 0) logf_( "rollback of a created user (uid %u) failed: %s", uid, err.name ? err.name : "error" );
    sd_bus_error_free( &err );
}

/* ------------------------------------------------------------------------------------- blank passwords */

/* Is this name a member of the greeter's passwordless group? Read from the group file itself, as admin_members does. */
static int in_blank_group( const char *name )
{
    char *line = NULL, *save;
    size_t cap = 0;
    int found = 0;
    FILE *f = fopen( group_path, "re" );

    if (!f) return 0;
    while (!found && getline( &line, &cap, f ) > 0)
    {
        char *g, *members, *m;

        line[strcspn( line, "\n" )] = 0;
        g = strtok_r( line, ":", &save );
        if (!g || strcmp( g, BLANK_GROUP )) continue;
        strtok_r( NULL, ":", &save );
        strtok_r( NULL, ":", &save );
        members = strtok_r( NULL, ":", &save );
        if (members)
            for (m = strtok_r( members, ",", &save ); m; m = strtok_r( NULL, ",", &save )) if (!strcmp( m, name )) found = 1;
        break;
    }
    free( line );
    fclose( f );
    return found;
}

/* Fail closed: a blank password is made only while the common PAM stack refuses an empty password, that is, while no pam_unix
 * line of /etc/pam.d/common-auth has nullok. A failed `wex-blank-pam.sh apply`, a later pam-auth-update or a libpam-runtime
 * change can bring nullok back, and then an empty field would be taken by su and sudo. A stack that cannot be read counts as
 * accepting. */
static int pam_refuses_empty( void )
{
    char *line = NULL;
    size_t cap = 0;
    int ok = 1;
    FILE *f = fopen( common_auth_path, "re" );

    if (!f) return 0;
    while (getline( &line, &cap, f ) > 0)
    {
        const char *p = line + strspn( line, " \t" );

        if (*p == '#') continue;
        if (strstr( p, "pam_unix" ) && strstr( p, "nullok" )) ok = 0;
    }
    free( line );
    fclose( f );
    return ok;
}

/* AccountsService on Debian does not keep nopasswdlogin (it runs passwd -d and nothing else), so a root-run unit does: it
 * makes the group's members exactly the accounts whose shadow field is empty (installer/blank/wex-accounts-blank-reconcile).
 * The helper only asks for it, through the one polkit grant, and checks the result in the group file. */
static int start_blank_reconcile( void )
{
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int r = sd_bus_call_method( bus, SD_DEST, "/org/freedesktop/systemd1", "org.freedesktop.systemd1.Manager", "StartUnit", &err,
                                NULL, "ss", cfg.blank_unit, "replace" );

    if (r < 0) { r = map_error( r, &err ); sd_bus_error_free( &err ); return r; }
    return WEXA_OK;
}

/* the reconcile has run when the group file says so: wait (at most 8 s) until the user is, or is not, a member */
static int wait_membership( const char *name, int want )
{
    int i;

    for (i = 0; i < 80; i++)
    {
        if (in_blank_group( name ) == want) return 1;
        /* a start that comes while the unit is running is merged into that run, which may have read the shadow file before our
         * change: ask again every two seconds (the reconcile also re-reads until nothing changes) */
        if (i && i % 20 == 0) start_blank_reconcile();
        usleep( 100000 );
    }
    return 0;
}

/* The password of a user, in either form (password_form): a hash is SetPassword; a blank is SetPasswordMode(NONE), which
 * empties the shadow field. Invariant: a user is in nopasswdlogin if and only if the shadow field is empty. A blank is
 * finished when the reconcile has put the user in the group (until then the greeter refuses it: fail safe); a hash is
 * finished when the reconcile has taken the user out (the account is passwordless at the greeter until then, as it was).
 * An account that is locked has "!" in the field, so it is not blank and not a member. */
static int apply_password( const struct user_info *u, const char *hash, int will_be_locked )
{
    int status;

    if (!hash[0])
    {
        if (!pam_refuses_empty()) return WEXA_BLANK_REFUSED;
        if ((status = call_user_method( u, "SetPasswordMode", "i", 2 )) != WEXA_OK) return status;
        if (will_be_locked) return WEXA_OK;
        /* from here the account is blank whatever the status says: the old password is gone (a BUSY below means the greeter
         * does not take it yet, not that nothing changed) */
        if ((status = start_blank_reconcile()) != WEXA_OK) { logf_( "user '%s' is blank, but the reconcile could not be started", u->name ); return status; }
        if (wait_membership( u->name, 1 )) return WEXA_OK;
        logf_( "user '%s' is blank (the old password is gone), but is not in %s yet", u->name, BLANK_GROUP );
        return WEXA_BUSY;
    }
    if ((status = call_user_method( u, "SetPassword", "ss", hash, "" )) != WEXA_OK) return status;
    if (!in_blank_group( u->name )) return WEXA_OK;
    if ((status = start_blank_reconcile()) != WEXA_OK) return status;
    if (wait_membership( u->name, 0 )) return WEXA_OK;
    logf_( "user '%s' has a password but is still in %s", u->name, BLANK_GROUP );
    return WEXA_BUSY;
}

static int handle_op( const struct wexa_request *req, const uint8_t *body, struct writer *w, char *logname, char *hash )
{
    struct reader r = { body, body + req->body_len, 0 };
    char name[WEXA_MAX_STR + 1], other[WEXA_MAX_STR + 1], full[WEXA_MAX_STR + 1];
    struct user_info u;
    int status;
    uint32_t value, locked;
    int change;

    if (req->op < WEXA_LIST_USERS || req->op >= WEXA_OP_LAST || req->flags) return WEXA_BAD_REQUEST;
    change = req->op >= WEXA_CREATE_USER;
    if (req->verdict == WEXA_VERDICT_NONE || req->verdict > WEXA_VERDICT_ELEVATED) return WEXA_DENIED;
    if (change && req->verdict != WEXA_VERDICT_ELEVATED) return WEXA_DENIED;
    if (!open_bus()) return WEXA_UNAVAILABLE;

    switch (req->op)
    {
    case WEXA_LIST_USERS:
    case WEXA_ADMIN_MEMBERS:
        /* an optional start index: the page asked for (F1) */
        value = 0;
        if (req->body_len) { value = rd_u32( &r ); if (r.bad || r.p != r.end) return WEXA_BAD_REQUEST; }
        return req->op == WEXA_LIST_USERS ? op_list( w, value ) : op_admin_members( w, value );

    case WEXA_START_SYNC:
        if (req->body_len) return WEXA_BAD_REQUEST;
        return op_start_sync();

    case WEXA_GET_USER:
        rd_str( &r, name, sizeof(name) );
        if (r.bad || r.p != r.end) return WEXA_BAD_REQUEST;
        set_logname( logname, name );
        if (!valid_user_name( name ) && !getpwnam( name )) return WEXA_NOT_FOUND;
        if ((status = find_user( name, &u )) != WEXA_OK) return status;
        if (!uid_in_range( u.uid ) || u.system_account) return WEXA_NOT_FOUND;
        wr_record( w, &u );
        return WEXA_OK;

    case WEXA_CREATE_USER:
        rd_str( &r, name, sizeof(name) );
        rd_str( &r, full, sizeof(full) );
        value = rd_u32( &r );
        rd_str( &r, hash, WEXA_MAX_STR + 1 );
        /* an optional trailing u32: create the account disabled, in the one operation (so that it is never enabled, even
         * briefly); the older form without it creates an enabled one */
        locked = 0;
        if (!r.bad && r.p != r.end) locked = rd_u32( &r );
        if (r.bad || r.p != r.end || locked > 1) return WEXA_BAD_REQUEST;
        set_logname( logname, name );
        if (!valid_user_name( name ) || !valid_full_name( full ) || value > 1 || !password_form( hash )) return WEXA_BAD_ARGUMENT;
        if (!name_is_free( name )) return WEXA_EXISTS;
        if (!hash[0] && !pam_refuses_empty()) { logf_( "a blank password is refused: PAM accepts empty passwords (nullok)" ); return WEXA_BLANK_REFUSED; }
        {
            sd_bus_error err = SD_BUS_ERROR_NULL;
            sd_bus_message *reply = NULL;
            const char *path = NULL;
            int rr = sd_bus_call_method( bus, ACC_DEST, ACC_PATH, ACC_IFACE, "CreateUser", &err, &reply, "ssi", name, full, 0 );

            if (rr < 0) { rr = map_error( rr, &err ); sd_bus_error_free( &err ); return rr; }
            memset( &u, 0, sizeof(u) );
            if (sd_bus_message_read( reply, "o", &path ) >= 0 && path) snprintf( u.path, sizeof(u.path), "%s", path );
            sd_bus_message_unref( reply );
            snprintf( u.name, sizeof(u.name), "%.63s", name );
        }
        /* from here on the user exists: any failure takes it away again and reports the original status (F2) */
        if (!u.path[0] || (status = load_props( &u )) != WEXA_OK)
        {
            if ((status = find_user( name, &u )) != WEXA_OK) return status;   /* it exists, but we cannot say more */
        }
        if (!uid_in_range( u.uid ) || u.system_account) { rollback_create( u.uid ); return WEXA_DENIED; }
        if ((status = apply_password( &u, hash, locked )) != WEXA_OK) { rollback_create( u.uid ); return status; }
        if (value == 1 && (status = call_user_method( &u, "SetAccountType", "i", 1 )) != WEXA_OK)
        {
            rollback_create( u.uid );
            return status;
        }
        if (locked && (status = call_user_method( &u, "SetLocked", "b", 1 )) != WEXA_OK)
        {
            rollback_create( u.uid );
            return status;
        }
        if (find_user( name, &u ) == WEXA_OK) wr_record( w, &u );
        sync_after_change();
        return WEXA_OK;

    case WEXA_DELETE_USER:
        rd_str( &r, name, sizeof(name) );
        if (r.bad || r.p != r.end) return WEXA_BAD_REQUEST;
        set_logname( logname, name );
        if ((status = find_user( name, &u )) != WEXA_OK) return status;
        if ((status = check_target( &u, req->caller_uid )) != WEXA_OK) return status;
        if (u.uid == req->caller_uid) return WEXA_DENIED;
        if (is_admin_name( name ) && !other_active_admins( name )) return WEXA_LAST_ADMIN;
        if (is_logged_in( u.uid )) return WEXA_LOGGED_IN;
        {
            sd_bus_error err = SD_BUS_ERROR_NULL;
            int rr = sd_bus_call_method( bus, ACC_DEST, ACC_PATH, ACC_IFACE, "DeleteUser", &err, NULL, "xb", (int64_t)u.uid, 0 );

            if (rr < 0) { rr = map_error( rr, &err ); sd_bus_error_free( &err ); return rr; }
        }
        sync_after_change();
        return WEXA_OK;

    case WEXA_RENAME_USER:
        rd_str( &r, name, sizeof(name) );
        rd_str( &r, other, sizeof(other) );
        if (r.bad || r.p != r.end) return WEXA_BAD_REQUEST;
        set_logname( logname, name );
        if (!valid_user_name( other )) return WEXA_BAD_ARGUMENT;
        if ((status = find_user( name, &u )) != WEXA_OK) return status;
        if ((status = check_target( &u, req->caller_uid )) != WEXA_OK) return status;
        if (!name_is_free( other )) return WEXA_EXISTS;
        if (is_logged_in( u.uid )) return WEXA_LOGGED_IN;
        status = call_user_method( &u, "SetUserName", "s", other );
        if (status == WEXA_OK) sync_after_change();
        return status;

    case WEXA_SET_FULLNAME:
        rd_str( &r, name, sizeof(name) );
        rd_str( &r, full, sizeof(full) );
        if (r.bad || r.p != r.end) return WEXA_BAD_REQUEST;
        set_logname( logname, name );
        if (!valid_full_name( full )) return WEXA_BAD_ARGUMENT;
        if ((status = find_user( name, &u )) != WEXA_OK) return status;
        if ((status = check_target( &u, req->caller_uid )) != WEXA_OK) return status;
        return call_user_method( &u, "SetRealName", "s", full );

    case WEXA_SET_LOCKED:
        rd_str( &r, name, sizeof(name) );
        value = rd_u32( &r );
        if (r.bad || r.p != r.end || value > 1) return WEXA_BAD_REQUEST;
        set_logname( logname, name );
        if ((status = find_user( name, &u )) != WEXA_OK) return status;
        if ((status = check_target( &u, req->caller_uid )) != WEXA_OK) return status;
        if (value && u.uid == req->caller_uid) return WEXA_DENIED;
        if (value && is_admin_name( name ) && !other_active_admins( name )) return WEXA_LAST_ADMIN;
        if ((status = call_user_method( &u, "SetLocked", "b", (int)value )) != WEXA_OK || value) return status;
        /* Enabling an account whose field is only "!" (a blank account that was disabled: usermod -L turned the empty field
         * into "!") does nothing on the real AccountsService: shadow's usermod -U refuses to leave a passwordless account and
         * AccountsService still answers success (the lab run of 2026-11-25). An account that is still locked after an unlock
         * has no password to lose, so the blank form is made again and the unlock repeated. */
        if (find_user( name, &u ) != WEXA_OK || !u.locked) return WEXA_OK;
        if (!pam_refuses_empty()) { logf_( "enabling a blank account is refused: PAM accepts empty passwords (nullok)" ); return WEXA_BLANK_REFUSED; }
        logf_( "user '%s' was still locked after an unlock: made blank again, then unlocked", logname );
        if ((status = apply_password( &u, "", 1 )) != WEXA_OK) return status;
        if ((status = call_user_method( &u, "SetLocked", "b", 0 )) != WEXA_OK) return status;
        if (find_user( name, &u ) != WEXA_OK || u.locked) return WEXA_BUSY;
        if ((status = start_blank_reconcile()) != WEXA_OK) return status;
        return wait_membership( u.name, 1 ) ? WEXA_OK : WEXA_BUSY;

    case WEXA_SET_TYPE:
        rd_str( &r, name, sizeof(name) );
        value = rd_u32( &r );
        if (r.bad || r.p != r.end || value > 1) return WEXA_BAD_REQUEST;
        set_logname( logname, name );
        if ((status = find_user( name, &u )) != WEXA_OK) return status;
        if ((status = check_target( &u, req->caller_uid )) != WEXA_OK) return status;
        if (!value && u.uid == req->caller_uid) return WEXA_DENIED;
        if (!value && is_admin_name( name ) && !other_active_admins( name )) return WEXA_LAST_ADMIN;
        return call_user_method( &u, "SetAccountType", "i", (int)value );

    case WEXA_SET_PASSWORD:
        rd_str( &r, name, sizeof(name) );
        rd_str( &r, hash, WEXA_MAX_STR + 1 );
        if (r.bad || r.p != r.end) return WEXA_BAD_REQUEST;
        set_logname( logname, name );
        if (!password_form( hash )) return WEXA_BAD_ARGUMENT;
        if ((status = find_user( name, &u )) != WEXA_OK) return status;
        if ((status = check_target( &u, req->caller_uid )) != WEXA_OK) return status;
        /* a blank password for a disabled account would leave it neither blank nor disabled in a way that can be undone
         * (the lock puts "!" in the field): the account is enabled first, or given a password */
        if (!hash[0] && u.locked) return WEXA_BAD_ARGUMENT;
        if (!hash[0] && !pam_refuses_empty()) { logf_( "a blank password is refused: PAM accepts empty passwords (nullok)" ); return WEXA_BLANK_REFUSED; }
        return apply_password( &u, hash, 0 );
    }
    return WEXA_BAD_REQUEST;
}

/* F4: the hash lives in a buffer of this function and is wiped on every way out */
static int handle( const struct wexa_request *req, const uint8_t *body, struct writer *w, char *logname )
{
    char hash[WEXA_MAX_STR + 1];
    int status;

    hash[0] = 0;
    status = handle_op( req, body, w, logname, hash );
    explicit_bzero( hash, sizeof(hash) );
    return status;
}

/* one datagram from the server: the request, the check of its frame, the reply */
static int serve_datagram( int fd, uint8_t *buf )
{
    static uint8_t out[WEXA_MAX_FRAME];
    struct wexa_request req;
    struct wexa_reply rep = { WEXA_MAGIC, WEXA_VERSION, 0, 0, 0 };
    struct writer w = { out + sizeof(rep), 0, sizeof(out) - sizeof(rep), 0 };
    char logname[64] = "";
    ssize_t n = recv( fd, buf, WEXA_MAX_FRAME + 1, MSG_TRUNC );

    if (n <= 0) return 0;
    if ((size_t)n < sizeof(req) || n > WEXA_MAX_FRAME) { logf_( "bad frame size %zd: connection closed", n ); return 0; }
    memcpy( &req, buf, sizeof(req) );
    if (req.magic != WEXA_MAGIC || req.version != WEXA_VERSION || (size_t)n != sizeof(req) + req.body_len)
    {
        logf_( "bad frame header: connection closed" );
        return 0;
    }
    rep.request_id = req.request_id;
    rep.status = (uint16_t)handle( &req, buf + sizeof(req), &w, logname );
    explicit_bzero( buf, (size_t)n );   /* the request may hold a hash: it does not stay in the receive buffer (F4) */
    if (rep.status != WEXA_OK) w.used = 0;
    if (rep.status == WEXA_UNAVAILABLE) drop_bus();
    rep.body_len = (uint32_t)w.used;
    memcpy( out, &rep, sizeof(rep) );
    /* a line per request: the operation, the kind of verdict, the account named and the result; never the hash */
    logf_( "op %u verdict %u caller %u user '%s': status %u", req.op, req.verdict, req.caller_uid, logname, rep.status );
    n = send( fd, out, sizeof(rep) + w.used, MSG_NOSIGNAL );
    explicit_bzero( out, sizeof(rep) + w.used );
    return n >= 0;
}

static int blank_checked;

static void serve_connection( int fd )
{
    static uint8_t buf[WEXA_MAX_FRAME + 1];
    struct timeval tv = { 5, 0 };

    /* the peer check first, before anything is read (it asks systemd over the bus: no answer, no accept) */
    open_bus();
    if (!peer_ok( fd )) { logf_( "peer refused" ); return; }
    setsockopt( fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv) );
    /* once per start of the helper: the root-run reconcile repairs the passwordless group (a member whose field is not
     * empty is removed, an empty field's account is added), so that a change made outside of us never stands */
    if (!blank_checked && bus)
    {
        blank_checked = 1;
        if (start_blank_reconcile() != WEXA_OK) logf_( "the blank-password reconcile could not be started" );
    }
    while (serve_datagram( fd, buf )) {}
}

#ifdef WEX_ACCOUNTSD_LAB_TEST
static int make_listener( const char *path )
{
    struct sockaddr_un sa;
    int fd = socket( AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0 );

    if (fd < 0) return -1;
    memset( &sa, 0, sizeof(sa) );
    sa.sun_family = AF_UNIX;
    snprintf( sa.sun_path, sizeof(sa.sun_path), "%s", path );
    unlink( path );
    if (bind( fd, (struct sockaddr *)&sa, sizeof(sa) ) || chmod( path, 0600 ) || listen( fd, 4 )) { close( fd ); return -1; }
    return fd;
}
#endif

int main( int argc, char **argv )
{
    const char *config = WEXA_CONFIG;
    struct rlimit nocore = { 0, 0 };
    int i, listener;

#ifdef WEX_ACCOUNTSD_LAB_TEST
    test_mode = getenv( "WEX_ACCOUNTSD_TEST" ) && !strcmp( getenv( "WEX_ACCOUNTSD_TEST" ), "1" );
#endif
    for (i = 1; i < argc; i++)
    {
        if (!strcmp( argv[i], "--config" ) && i + 1 < argc) config = argv[++i];
#ifdef WEX_ACCOUNTSD_LAB_TEST
        else if (test_mode && !strcmp( argv[i], "--bus" ) && i + 1 < argc) test_bus = argv[++i];
        else if (test_mode && !strcmp( argv[i], "--listen" ) && i + 1 < argc) test_listen = argv[++i];
        else if (test_mode && !strcmp( argv[i], "--login-defs" ) && i + 1 < argc) test_login_defs = argv[++i];
        else if (test_mode && !strcmp( argv[i], "--admin-members" ) && i + 1 < argc) test_admin_members = argv[++i];
        else if (test_mode && !strcmp( argv[i], "--mainpid" ) && i + 1 < argc) test_mainpid = (pid_t)atoi( argv[++i] );
        else if (test_mode && !strcmp( argv[i], "--mainpid-file" ) && i + 1 < argc) test_mainpid_file = argv[++i];
        else if (test_mode && !strcmp( argv[i], "--group-file" ) && i + 1 < argc) group_path = argv[++i];
        else if (test_mode && !strcmp( argv[i], "--passwd-file" ) && i + 1 < argc) passwd_path = argv[++i];
        else if (test_mode && !strcmp( argv[i], "--common-auth" ) && i + 1 < argc) common_auth_path = argv[++i];
        else if (test_mode && !strcmp( argv[i], "--any-dumpable" )) any_dumpable = 1;
        else if (test_mode && !strcmp( argv[i], "--no-idle-exit" )) no_idle_exit = 1;
#endif
        else { fprintf( stderr, "usage: wex-accountsd [--config FILE]\n" ); return 2; }
    }
    prctl( PR_SET_DUMPABLE, 0 );
    setrlimit( RLIMIT_CORE, &nocore );
    if (!load_config( config )) return 1;
    load_login_defs();
#ifdef WEX_ACCOUNTSD_LAB_TEST
    if (test_listen) listener = make_listener( test_listen );
    else
#endif
    if (sd_listen_fds( 0 ) == 1) listener = SD_LISTEN_FDS_START;
    else { logf_( "no socket from systemd" ); return 1; }
    if (listener < 0) { logf_( "cannot listen: %s", strerror( errno ) ); return 1; }
    for (;;)
    {
        struct pollfd pfd = { listener, POLLIN, 0 };
        int fd, r = poll( &pfd, 1, (test_mode || no_idle_exit) ? -1 : 60000 );

        if (r == 0) return 0;   /* idle: socket activation starts us again */
        if (r < 0) { if (errno == EINTR) continue; return 1; }
        if ((fd = accept4( listener, NULL, NULL, SOCK_CLOEXEC )) < 0) continue;
        serve_connection( fd );
        close( fd );
    }
}
