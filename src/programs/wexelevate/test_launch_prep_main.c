/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Native C unit test harness for wex_prepare_launch (E1-S15).
 */

#include "launch_prep.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

static void fill_valid_params( struct wex_launch_prep_params *params,
                               int prefix_fd, int profile_fd,
                               uid_t bob_uid, gid_t bob_gid )
{
    memset( params, 0, sizeof(*params) );
    params->token.token_id = 0x1234;
    params->token.auth_id = 0x5678;
    params->token.elevation_type = 2;   /* Full */
    params->token.integrity_level = 0x3000; /* High */

    strcpy( params->profile.account_name, "alice" );
    strcpy( params->profile.sid, "S-1-5-21-11-22-33-1001" );
    params->profile.map_generation = 1;
    params->profile.storage_generation = 2;
    strcpy( params->profile.windows_path, "C:\\users\\.e1s\\S-1-5-21-11-22-33-1001\\00000000000000010000000000000002-alice" );
    strcpy( params->profile.unix_relative_path, "drive_c/users/.e1s/S-1-5-21-11-22-33-1001/00000000000000010000000000000002-alice" );

    params->descriptors.prefix_fd = prefix_fd;
    params->descriptors.profile_dir_fd = profile_fd;
    params->descriptors.cwd_dir_fd = -1;

    params->bob_uid = bob_uid;
    params->bob_gid = bob_gid;
    params->requested_uid = bob_uid;
    params->requested_gid = bob_gid;
}

#if defined(WEX_E1S15_LAUNCH_PREP_TEST)
/* The Windows environment block is UTF-16LE (double-NUL terminated). Keys are
 * ASCII, so they compare byte-for-byte as code units; the value is converted
 * back to UTF-8 for comparison. */
static int get_win_env_var( const char *env, size_t size, const char *key, char *out, size_t out_size )
{
    const uint16_t *p = (const uint16_t *)env;
    const uint16_t *end = (const uint16_t *)(env + size);
    size_t klen = strlen( key );

    while (p < end && *p)
    {
        size_t i;
        int match = 1;
        for (i = 0; i < klen; i++)
        {
            if (p[i] != (uint16_t)(unsigned char)key[i]) { match = 0; break; }
        }
        if (match && p[klen] == '=')
        {
            if (wex_utf16le_to_utf8( p + klen + 1, out, out_size ) == 0)
                return 1;
            return 0;
        }
        while (*p) p++;
        p++; /* skip the entry's NUL */
    }
    return 0;
}

static int has_unix_env_prefix( char **env, size_t count, const char *prefix )
{
    size_t i, plen = strlen( prefix );
    for (i = 0; i < count && env[i]; i++)
    {
        if (strncmp( env[i], prefix, plen ) == 0) return 1;
    }
    return 0;
}

static const char *get_unix_env_var( char **env, size_t count, const char *key )
{
    size_t i, klen = strlen( key );
    for (i = 0; i < count && env[i]; i++)
    {
        if (strncmp( env[i], key, klen ) == 0 && env[i][klen] == '=')
            return env[i] + klen + 1;
    }
    return NULL;
}
#endif

int main( int argc, char **argv )
{
    const char *mode = (argc > 1) ? argv[1] : "all";
    const char *prefix_dir = (argc > 2) ? argv[2] : ".";
    const char *profile_dir = (argc > 3) ? argv[3] : ".";
    uid_t bob_uid = (argc > 4) ? (uid_t)atoi( argv[4] ) : getuid();
    gid_t bob_gid = (argc > 5) ? (gid_t)atoi( argv[5] ) : getgid();

    int prefix_fd = open( prefix_dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC );
    int profile_fd = open( profile_dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC );
    struct wex_launch_prep_params params;
    struct wex_launch_prep_result result;
    enum wex_launch_prep_status status;

    if (strcmp( mode, "test_disabled" ) == 0)
    {
        fill_valid_params( &params, prefix_fd, profile_fd, bob_uid, bob_gid );
        status = wex_prepare_launch( &params, &result );
        if (status != WEX_LAUNCH_PREP_ERR_DISABLED)
        {
            fprintf( stderr, "FAIL: expected DISABLED got %d\n", status );
            return 1;
        }
        printf( "PASS test_disabled\n" );
        return 0;
    }

#if !defined(WEX_E1S15_LAUNCH_PREP_TEST)
    fprintf( stderr, "Harness built without WEX_E1S15_LAUNCH_PREP_TEST\n" );
    return 1;
#else

    if (prefix_fd < 0 || profile_fd < 0)
    {
        fprintf( stderr, "FAIL: unable to open fixture descriptors\n" );
        return 1;
    }

    if (strcmp( mode, "test_clean_win_env" ) == 0 || strcmp( mode, "all" ) == 0)
    {
        char val[512];
        fill_valid_params( &params, prefix_fd, profile_fd, bob_uid, bob_gid );
        status = wex_prepare_launch( &params, &result );
        if (status != WEX_LAUNCH_PREP_OK)
        {
            fprintf( stderr, "FAIL: clean win env status=%d\n", status );
            return 1;
        }

        /* Verify all 9 Alice variables */
        if (!get_win_env_var( result.win_env, result.win_env_size, "USERNAME", val, sizeof(val) ) ||
            strcmp( val, "alice" ) != 0)
        {
            fprintf( stderr, "FAIL: USERNAME=%s\n", val );
            return 1;
        }
        if (!get_win_env_var( result.win_env, result.win_env_size, "USERPROFILE", val, sizeof(val) ) ||
            strcmp( val, params.profile.windows_path ) != 0)
        {
            fprintf( stderr, "FAIL: USERPROFILE=%s\n", val );
            return 1;
        }
        if (!get_win_env_var( result.win_env, result.win_env_size, "HOMEDRIVE", val, sizeof(val) ) ||
            strcmp( val, "C:" ) != 0)
        {
            fprintf( stderr, "FAIL: HOMEDRIVE=%s\n", val );
            return 1;
        }
        if (!get_win_env_var( result.win_env, result.win_env_size, "HOMEPATH", val, sizeof(val) ) ||
            strcmp( val, params.profile.windows_path + 2 ) != 0)
        {
            fprintf( stderr, "FAIL: HOMEPATH=%s\n", val );
            return 1;
        }
        if (!get_win_env_var( result.win_env, result.win_env_size, "USERDOMAIN", val, sizeof(val) ) ||
            strcmp( val, "WINEHOST" ) != 0)
        {
            fprintf( stderr, "FAIL: USERDOMAIN=%s\n", val );
            return 1;
        }

        snprintf( val, sizeof(val), "%s\\AppData\\Roaming", params.profile.windows_path );
        char act[512];
        if (!get_win_env_var( result.win_env, result.win_env_size, "APPDATA", act, sizeof(act) ) ||
            strcmp( act, val ) != 0)
        {
            fprintf( stderr, "FAIL: APPDATA=%s (expected %s)\n", act, val );
            return 1;
        }

        snprintf( val, sizeof(val), "%s\\AppData\\Local", params.profile.windows_path );
        if (!get_win_env_var( result.win_env, result.win_env_size, "LOCALAPPDATA", act, sizeof(act) ) ||
            strcmp( act, val ) != 0)
        {
            fprintf( stderr, "FAIL: LOCALAPPDATA=%s\n", act );
            return 1;
        }

        snprintf( val, sizeof(val), "%s\\AppData\\Local\\Temp", params.profile.windows_path );
        if (!get_win_env_var( result.win_env, result.win_env_size, "TEMP", act, sizeof(act) ) ||
            strcmp( act, val ) != 0)
        {
            fprintf( stderr, "FAIL: TEMP=%s\n", act );
            return 1;
        }
        if (!get_win_env_var( result.win_env, result.win_env_size, "TMP", act, sizeof(act) ) ||
            strcmp( act, val ) != 0)
        {
            fprintf( stderr, "FAIL: TMP=%s\n", act );
            return 1;
        }

        /* Verify standard system variables present */
        if (!get_win_env_var( result.win_env, result.win_env_size, "SystemRoot", val, sizeof(val) ) ||
            strcmp( val, "C:\\windows" ) != 0)
        {
            fprintf( stderr, "FAIL: SystemRoot=%s\n", val );
            return 1;
        }
        if (!get_win_env_var( result.win_env, result.win_env_size, "ProgramData", val, sizeof(val) ) ||
            strcmp( val, "C:\\ProgramData" ) != 0)
        {
            fprintf( stderr, "FAIL: ProgramData=%s\n", val );
            return 1;
        }

        wex_launch_prep_result_free( &result );
        printf( "PASS test_clean_win_env\n" );
    }

    if (strcmp( mode, "test_inherit_rejected" ) == 0 || strcmp( mode, "all" ) == 0)
    {
        fill_valid_params( &params, prefix_fd, profile_fd, bob_uid, bob_gid );
        params.flags |= WEX_LAUNCH_PREP_FLAG_INHERIT_ENV;
        status = wex_prepare_launch( &params, &result );
        if (status != WEX_LAUNCH_PREP_ERR_INHERIT_REJECTED)
        {
            fprintf( stderr, "FAIL: expected INHERIT_REJECTED got %d\n", status );
            return 1;
        }
        wex_launch_prep_result_free( &result );
        printf( "PASS test_inherit_rejected\n" );
    }

    if (strcmp( mode, "test_protected_override" ) == 0 || strcmp( mode, "all" ) == 0)
    {
        static const char *overrides[] = {
            "USERNAME=evil\0",
            "USERPROFILE=C:\\evil\0",
            "HOMEDRIVE=D:\0",
            "HOMEPATH=\\evil\0",
            "APPDATA=C:\\evil\0",
            "LOCALAPPDATA=C:\\evil\0",
            "TEMP=C:\\evil\0",
            "TMP=C:\\evil\0",
            "USERDOMAIN=EVILDOMAIN\0",
            "SystemRoot=D:\\windows\0"
        };
        size_t i;
        for (i = 0; i < sizeof(overrides) / sizeof(overrides[0]); i++)
        {
            fill_valid_params( &params, prefix_fd, profile_fd, bob_uid, bob_gid );
            params.caller_win_env = overrides[i];
            params.caller_win_env_size = strlen( overrides[i] ) + 2;
            status = wex_prepare_launch( &params, &result );
            if (status != WEX_LAUNCH_PREP_ERR_PROTECTED_OVERRIDE)
            {
                fprintf( stderr, "FAIL: override %s got %d, expected PROTECTED_OVERRIDE\n",
                         overrides[i], status );
                return 1;
            }
            wex_launch_prep_result_free( &result );
        }
        printf( "PASS test_protected_override\n" );
    }

    if (strcmp( mode, "test_real_uid_rejected" ) == 0 || strcmp( mode, "all" ) == 0)
    {
        fill_valid_params( &params, prefix_fd, profile_fd, bob_uid, bob_gid );
        params.requested_uid = bob_uid + 100; /* Try switching real UID */
        status = wex_prepare_launch( &params, &result );
        if (status != WEX_LAUNCH_PREP_ERR_REAL_UID_UNSUPPORTED)
        {
            fprintf( stderr, "FAIL: expected REAL_UID_UNSUPPORTED got %d\n", status );
            return 1;
        }
        wex_launch_prep_result_free( &result );

        /* Real UID 0 (root) attempt */
        fill_valid_params( &params, prefix_fd, profile_fd, bob_uid, bob_gid );
        params.requested_uid = 0;
        status = wex_prepare_launch( &params, &result );
        if (status != WEX_LAUNCH_PREP_ERR_REAL_UID_UNSUPPORTED)
        {
            fprintf( stderr, "FAIL: root uid attempt got %d\n", status );
            return 1;
        }
        wex_launch_prep_result_free( &result );
        printf( "PASS test_real_uid_rejected\n" );
    }

    if (strcmp( mode, "test_sanitized_unix_env" ) == 0 || strcmp( mode, "all" ) == 0)
    {
        const char *caller_env[] = {
            "SSH_AUTH_SOCK=/run/user/1000/ssh.socket",
            "SSH_AGENT_PID=12345",
            "LD_PRELOAD=/tmp/evil.so",
            "LD_LIBRARY_PATH=/tmp/lib",
            "WINEPREFIX=/home/bob/.wine",
            "WINEDEBUG=+all",
            "KRB5CCNAME=FILE:/tmp/krb5",
            "GPG_AGENT_INFO=/tmp/gpg:123:1",
            "DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus",
            "TERM=xterm-256color",
            "LANG=en_US.UTF-8",
            "LC_CTYPE=en_US.UTF-8",
            "DISPLAY=:0.0",
            "WAYLAND_DISPLAY=wayland-0",
            NULL
        };

        fill_valid_params( &params, prefix_fd, profile_fd, bob_uid, bob_gid );
        params.caller_unix_env = caller_env;
        status = wex_prepare_launch( &params, &result );
        if (status != WEX_LAUNCH_PREP_OK)
        {
            fprintf( stderr, "FAIL: unix env sanitization status=%d\n", status );
            return 1;
        }

        /* Execution identities must be Bob's */
        if (result.exec_uid != bob_uid || result.exec_gid != bob_gid)
        {
            fprintf( stderr, "FAIL: exec_uid/gid mismatch\n" );
            return 1;
        }

        /* Check that Bob's identity variables are set */
        const char *val;
        val = get_unix_env_var( result.unix_env, result.unix_env_count, "PATH" );
        if (!val || strcmp( val, "/usr/local/bin:/usr/bin:/bin" ) != 0)
        {
            fprintf( stderr, "FAIL: PATH=%s\n", val ? val : "(null)" );
            return 1;
        }
        val = get_unix_env_var( result.unix_env, result.unix_env_count, "SHELL" );
        if (!val || strcmp( val, "/bin/bash" ) != 0)
        {
            fprintf( stderr, "FAIL: SHELL=%s\n", val ? val : "(null)" );
            return 1;
        }
        val = get_unix_env_var( result.unix_env, result.unix_env_count, "TERM" );
        if (!val || strcmp( val, "xterm-256color" ) != 0)
        {
            fprintf( stderr, "FAIL: TERM=%s\n", val ? val : "(null)" );
            return 1;
        }
        val = get_unix_env_var( result.unix_env, result.unix_env_count, "DISPLAY" );
        if (!val || strcmp( val, ":0.0" ) != 0)
        {
            fprintf( stderr, "FAIL: DISPLAY=%s\n", val ? val : "(null)" );
            return 1;
        }
        val = get_unix_env_var( result.unix_env, result.unix_env_count, "WAYLAND_DISPLAY" );
        if (!val || strcmp( val, "wayland-0" ) != 0)
        {
            fprintf( stderr, "FAIL: WAYLAND_DISPLAY=%s\n", val ? val : "(null)" );
            return 1;
        }

        /* Check that ALL forbidden variables were stripped */
        if (has_unix_env_prefix( result.unix_env, result.unix_env_count, "SSH_" ))
        {
            fprintf( stderr, "FAIL: leaked SSH_* variable\n" );
            return 1;
        }
        if (has_unix_env_prefix( result.unix_env, result.unix_env_count, "LD_" ))
        {
            fprintf( stderr, "FAIL: leaked LD_* variable\n" );
            return 1;
        }
        if (has_unix_env_prefix( result.unix_env, result.unix_env_count, "WINE" ))
        {
            fprintf( stderr, "FAIL: leaked WINE* variable\n" );
            return 1;
        }
        if (has_unix_env_prefix( result.unix_env, result.unix_env_count, "KRB5" ))
        {
            fprintf( stderr, "FAIL: leaked KRB5* variable\n" );
            return 1;
        }
        if (has_unix_env_prefix( result.unix_env, result.unix_env_count, "GPG_" ))
        {
            fprintf( stderr, "FAIL: leaked GPG_* variable\n" );
            return 1;
        }
        if (has_unix_env_prefix( result.unix_env, result.unix_env_count, "DBUS_" ))
        {
            fprintf( stderr, "FAIL: leaked DBUS_* variable\n" );
            return 1;
        }

        wex_launch_prep_result_free( &result );
        printf( "PASS test_sanitized_unix_env\n" );
    }

    if (strcmp( mode, "test_cwd_checks" ) == 0 || strcmp( mode, "all" ) == 0)
    {
        /* 1. Default CWD (inside Alice profile) */
        fill_valid_params( &params, prefix_fd, profile_fd, bob_uid, bob_gid );
        params.requested_cwd = NULL;
        status = wex_prepare_launch( &params, &result );
        if (status != WEX_LAUNCH_PREP_OK || result.verified_cwd_fd < 0)
        {
            fprintf( stderr, "FAIL: default cwd status=%d fd=%d\n", status, result.verified_cwd_fd );
            return 1;
        }
        wex_launch_prep_result_free( &result );

        /* 2. Subdirectory inside profile */
        fill_valid_params( &params, prefix_fd, profile_fd, bob_uid, bob_gid );
        params.requested_cwd = "AppData/Local";
        status = wex_prepare_launch( &params, &result );
        if (status != WEX_LAUNCH_PREP_OK || result.verified_cwd_fd < 0)
        {
            fprintf( stderr, "FAIL: subpath AppData/Local status=%d\n", status );
            return 1;
        }
        wex_launch_prep_result_free( &result );

        /* 3. Allowed system directory: C:\windows\system32 */
        fill_valid_params( &params, prefix_fd, profile_fd, bob_uid, bob_gid );
        params.requested_cwd = "C:\\windows\\system32";
        status = wex_prepare_launch( &params, &result );
        if (status != WEX_LAUNCH_PREP_OK || result.verified_cwd_fd < 0)
        {
            fprintf( stderr, "FAIL: system32 status=%d\n", status );
            return 1;
        }
        wex_launch_prep_result_free( &result );

        /* 4. Dot-dot traversal attempt */
        fill_valid_params( &params, prefix_fd, profile_fd, bob_uid, bob_gid );
        params.requested_cwd = "AppData/../../..";
        status = wex_prepare_launch( &params, &result );
        if (status != WEX_LAUNCH_PREP_ERR_CWD_TRAVERSAL)
        {
            fprintf( stderr, "FAIL: dot-dot traversal got %d\n", status );
            return 1;
        }
        wex_launch_prep_result_free( &result );

        /* 5. Disallowed path outside profile and system dirs */
        fill_valid_params( &params, prefix_fd, profile_fd, bob_uid, bob_gid );
        params.requested_cwd = "C:\\users\\bob";
        status = wex_prepare_launch( &params, &result );
        if (status != WEX_LAUNCH_PREP_ERR_CWD_NOT_ALLOWED)
        {
            fprintf( stderr, "FAIL: C:\\users\\bob got %d, expected CWD_NOT_ALLOWED\n", status );
            return 1;
        }
        wex_launch_prep_result_free( &result );

        /* 6. Another account profile attempt */
        fill_valid_params( &params, prefix_fd, profile_fd, bob_uid, bob_gid );
        params.requested_cwd = "C:\\users\\.e1s\\S-1-5-21-99-99-99-9999\\1-mallory";
        status = wex_prepare_launch( &params, &result );
        if (status != WEX_LAUNCH_PREP_ERR_CWD_NOT_ALLOWED)
        {
            fprintf( stderr, "FAIL: foreign profile got %d\n", status );
            return 1;
        }
        wex_launch_prep_result_free( &result );

        /* 7. Symlink traversal rejection */
        fill_valid_params( &params, prefix_fd, profile_fd, bob_uid, bob_gid );
        params.requested_cwd = "AppData/symlink_escape";
        status = wex_prepare_launch( &params, &result );
        if (status != WEX_LAUNCH_PREP_ERR_CWD_TRAVERSAL)
        {
            fprintf( stderr, "FAIL: symlink traversal got %d, expected CWD_TRAVERSAL\n", status );
            return 1;
        }
        wex_launch_prep_result_free( &result );

        /* 8. Non-existent path */
        fill_valid_params( &params, prefix_fd, profile_fd, bob_uid, bob_gid );
        params.requested_cwd = "NonExistentDir";
        status = wex_prepare_launch( &params, &result );
        if (status != WEX_LAUNCH_PREP_ERR_CWD_NOT_DIR)
        {
            fprintf( stderr, "FAIL: non-existent dir got %d, expected CWD_NOT_DIR\n", status );
            return 1;
        }
        wex_launch_prep_result_free( &result );

        printf( "PASS test_cwd_checks\n" );
    }

    if (strcmp( mode, "test_lookup_failure" ) == 0 || strcmp( mode, "all" ) == 0)
    {
        /* A host UID with no passwd entry must fail closed, not invent a home. */
        fill_valid_params( &params, prefix_fd, profile_fd, 62002, 62002 );
        status = wex_prepare_launch( &params, &result );
        if (status != WEX_LAUNCH_PREP_ERR_ACCOUNT_LOOKUP)
        {
            fprintf( stderr, "FAIL: lookup failure got %d, expected ACCOUNT_LOOKUP\n", status );
            return 1;
        }
        wex_launch_prep_result_free( &result );
        printf( "PASS test_lookup_failure\n" );
    }

    if (strcmp( mode, "test_nonascii_env" ) == 0 || strcmp( mode, "all" ) == 0)
    {
        /* "alicé-中" in UTF-8: a l i c \xc3\xa9 - \xe4\xb8\xad */
        static const char nonascii_name[] = "alic\xc3\xa9-\xe4\xb8\xad";
        char val[512];

        fill_valid_params( &params, prefix_fd, profile_fd, bob_uid, bob_gid );
        strcpy( params.profile.account_name, nonascii_name );
        status = wex_prepare_launch( &params, &result );
        if (status != WEX_LAUNCH_PREP_OK)
        {
            fprintf( stderr, "FAIL: non-ASCII env status=%d\n", status );
            return 1;
        }

        /* The non-ASCII account name must round-trip into the UTF-16LE block. */
        if (!get_win_env_var( result.win_env, result.win_env_size, "USERNAME", val, sizeof(val) ) ||
            strcmp( val, nonascii_name ) != 0)
        {
            fprintf( stderr, "FAIL: non-ASCII USERNAME round-trip (got %s)\n", val );
            return 1;
        }

        /* The block must end with two NUL code units (four zero bytes). */
        if (result.win_env_size < 4 ||
            result.win_env[result.win_env_size - 4] != 0 ||
            result.win_env[result.win_env_size - 3] != 0 ||
            result.win_env[result.win_env_size - 2] != 0 ||
            result.win_env[result.win_env_size - 1] != 0)
        {
            fprintf( stderr, "FAIL: UTF-16LE block does not end with two NUL code units\n" );
            return 1;
        }

        wex_launch_prep_result_free( &result );
        printf( "PASS test_nonascii_env\n" );
    }

    if (strcmp( mode, "test_cwd_conversion" ) == 0 || strcmp( mode, "all" ) == 0)
    {
        char out[512];

        /* A non-ASCII cwd is converted exactly. "C:\dökument" (ö = U+00F6). */
        {
            static const uint16_t cwd16a[] = { 'C', ':', '\\', 'd', 0x00f6, 'k', 'u', 'm', 'e', 'n', 't', 0 };
            /* Expected UTF-8: "C:\d" + \xc3\xb6 + "kument" */
            static const char expected[] = "C:\\d\xc3\xb6kument";
            if (wex_utf16le_to_utf8( cwd16a, out, sizeof(out) ) != 0 ||
                strcmp( out, expected ) != 0)
            {
                fprintf( stderr, "FAIL: non-ASCII cwd conversion (got %s)\n", out );
                return 1;
            }
        }

        /* A surrogate pair (U+1D11E) is converted exactly. */
        {
            static const uint16_t cwd16b[] = { 'a', 0xd834, 0xdd1e, 'b', 0 };
            /* U+1D11E in UTF-8: \xf0\x9d\x84\x9e */
            static const char expected[] = "a\xf0\x9d\x84\x9e" "b";
            if (wex_utf16le_to_utf8( cwd16b, out, sizeof(out) ) != 0 ||
                strcmp( out, expected ) != 0)
            {
                fprintf( stderr, "FAIL: surrogate pair cwd conversion (got %s)\n", out );
                return 1;
            }
        }

        /* An overlong cwd that does not fit the buffer denies. */
        {
            static uint16_t long16[64];
            int i;
            for (i = 0; i < 63; i++) long16[i] = 'x';
            long16[63] = 0;
            if (wex_utf16le_to_utf8( long16, out, 10 ) == 0)
            {
                fprintf( stderr, "FAIL: overlong cwd must deny when it does not fit\n" );
                return 1;
            }
        }

        /* A lone high surrogate denies. */
        {
            static const uint16_t lone_hi[] = { 'a', 0xd800, 'b', 0 };
            if (wex_utf16le_to_utf8( lone_hi, out, sizeof(out) ) == 0)
            {
                fprintf( stderr, "FAIL: lone high surrogate must deny\n" );
                return 1;
            }
        }

        /* A lone low surrogate denies. */
        {
            static const uint16_t lone_lo[] = { 'a', 0xdc00, 'b', 0 };
            if (wex_utf16le_to_utf8( lone_lo, out, sizeof(out) ) == 0)
            {
                fprintf( stderr, "FAIL: lone low surrogate must deny\n" );
                return 1;
            }
        }

        /* A truncated pair (high surrogate at end of string) denies. */
        {
            static const uint16_t trunc16[] = { 'a', 0xd800, 0 };
            if (wex_utf16le_to_utf8( trunc16, out, sizeof(out) ) == 0)
            {
                fprintf( stderr, "FAIL: truncated surrogate pair must deny\n" );
                return 1;
            }
        }

        printf( "PASS test_cwd_conversion\n" );
    }

    close( prefix_fd );
    close( profile_fd );
    return 0;
#endif
}
