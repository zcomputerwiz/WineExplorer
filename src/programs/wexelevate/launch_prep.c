/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Pure, unit-testable launch preparation for E1-S15.
 * Validates same-server inputs, sanitizes Windows and Unix environments,
 * verifies working directory containment without symlink traversal,
 * and enforces Bob's host UID with hard rejection of real-UID transitions.
 * Does not launch any process; no credential launches enabled.
 */

#include "launch_prep.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifndef _WIN32
#include <pwd.h>
#endif

/* Convert a NUL-terminated UTF-16LE string to UTF-8. dst must have room for
 * dst_size bytes including the terminating NUL. Returns 0 on success, -1 on an
 * unpaired surrogate, a truncated input, or if the result does not fit. */
int wex_utf16le_to_utf8( const uint16_t *src, char *dst, size_t dst_size )
{
    size_t out = 0;

    if (!src || !dst || dst_size == 0) return -1;

    while (*src)
    {
        uint32_t cp;
        uint16_t w = *src++;

        if (w >= 0xd800 && w <= 0xdbff)
        {
            /* High surrogate: must be immediately followed by a low surrogate.
             * A NUL here means the input was truncated mid-pair. */
            uint16_t lo = *src;
            if (lo < 0xdc00 || lo > 0xdfff) return -1;
            cp = 0x10000 + ((uint32_t)(w - 0xd800) << 10) + (uint32_t)(lo - 0xdc00);
            src++;
        }
        else if (w >= 0xdc00 && w <= 0xdfff)
        {
            return -1; /* lone low surrogate */
        }
        else
        {
            cp = w;
        }

        if (cp < 0x80)
        {
            if (out + 1 >= dst_size) return -1;
            dst[out++] = (char)cp;
        }
        else if (cp < 0x800)
        {
            if (out + 2 >= dst_size) return -1;
            dst[out++] = (char)(0xc0 | (cp >> 6));
            dst[out++] = (char)(0x80 | (cp & 0x3f));
        }
        else if (cp < 0x10000)
        {
            if (out + 3 >= dst_size) return -1;
            dst[out++] = (char)(0xe0 | (cp >> 12));
            dst[out++] = (char)(0x80 | ((cp >> 6) & 0x3f));
            dst[out++] = (char)(0x80 | (cp & 0x3f));
        }
        else
        {
            if (out + 4 >= dst_size) return -1;
            dst[out++] = (char)(0xf0 | (cp >> 18));
            dst[out++] = (char)(0x80 | ((cp >> 12) & 0x3f));
            dst[out++] = (char)(0x80 | ((cp >> 6) & 0x3f));
            dst[out++] = (char)(0x80 | (cp & 0x3f));
        }
    }
    if (out >= dst_size) return -1;
    dst[out] = '\0';
    return 0;
}

#if !defined(WEX_E1S15_LAUNCH_PREP_TEST)

enum wex_launch_prep_status wex_prepare_launch(
    const struct wex_launch_prep_params *params,
    struct wex_launch_prep_result *result )
{
    (void)params;
    if (result)
    {
        memset( result, 0, sizeof(*result) );
        result->verified_cwd_fd = -1;
    }
    return WEX_LAUNCH_PREP_ERR_DISABLED;
}

void wex_launch_prep_result_free( struct wex_launch_prep_result *result )
{
    if (!result) return;
    if (result->verified_cwd_fd >= 0)
    {
        close( result->verified_cwd_fd );
        result->verified_cwd_fd = -1;
    }
    free( result->win_env );
    result->win_env = NULL;
    result->win_env_size = 0;
    if (result->unix_env)
    {
        size_t i;
        for (i = 0; i < result->unix_env_count; i++)
            free( result->unix_env[i] );
        free( result->unix_env );
        result->unix_env = NULL;
        result->unix_env_count = 0;
    }
}

#else

/* Protected Windows variables that callers cannot override */
static const char * const protected_win_vars[] = {
    "USERNAME",
    "USERPROFILE",
    "APPDATA",
    "LOCALAPPDATA",
    "TEMP",
    "TMP",
    "HOMEDRIVE",
    "HOMEPATH",
    "USERDOMAIN",
    "SystemRoot",
    "SystemDrive",
    "ProgramData",
    "ALLUSERSPROFILE",
    "PUBLIC",
    "COMPUTERNAME"
};

/* Forbidden Unix variables that must never leak or be inherited */
static const char * const forbidden_unix_prefixes[] = {
    "SSH_",
    "LD_",
    "WINE",
    "KRB5",
    "GPG_"
};

static const char * const forbidden_unix_exact[] = {
    "SSH_AUTH_SOCK",
    "SSH_AGENT_PID",
    "LD_PRELOAD",
    "LD_LIBRARY_PATH",
    "LD_AUDIT",
    "LD_DEBUG",
    "WINEPREFIX",
    "WINEDEBUG",
    "WINELOADER",
    "WINESERVER",
    "WINEARCH",
    "WINEUSERNAME",
    "KRB5CCNAME",
    "GPG_AGENT_INFO",
    "DBUS_SESSION_BUS_ADDRESS"
};

static int is_protected_win_var( const char *name, size_t len )
{
    size_t i;
    for (i = 0; i < sizeof(protected_win_vars) / sizeof(protected_win_vars[0]); i++)
    {
        if (strncasecmp( name, protected_win_vars[i], len ) == 0 &&
            protected_win_vars[i][len] == '\0')
            return 1;
    }
    return 0;
}

static int is_forbidden_unix_var( const char *entry )
{
    size_t i, name_len = 0;
    const char *eq = strchr( entry, '=' );
    if (!eq) return 1;
    name_len = (size_t)(eq - entry);

    for (i = 0; i < sizeof(forbidden_unix_exact) / sizeof(forbidden_unix_exact[0]); i++)
    {
        if (strncmp( entry, forbidden_unix_exact[i], name_len ) == 0 &&
            forbidden_unix_exact[i][name_len] == '\0')
            return 1;
    }

    for (i = 0; i < sizeof(forbidden_unix_prefixes) / sizeof(forbidden_unix_prefixes[0]); i++)
    {
        size_t plen = strlen( forbidden_unix_prefixes[i] );
        if (name_len >= plen && strncmp( entry, forbidden_unix_prefixes[i], plen ) == 0)
            return 1;
    }

    return 0;
}

static int is_safe_string( const char *s )
{
    if (!s) return 0;
    while (*s)
    {
        if ((unsigned char)*s < 0x20 || *s == 0x7f) return 0;
        s++;
    }
    return 1;
}

static int is_safe_display( const char *d )
{
    /* Format: :N or :N.N or host:N or host:N.N */
    size_t len;
    const char *colon;
    if (!d || !*d) return 0;
    len = strlen( d );
    if (len > 64) return 0;
    colon = strchr( d, ':' );
    if (!colon) return 0;
    colon++;
    if (!isdigit( (unsigned char)*colon )) return 0;
    while (*colon && isdigit( (unsigned char)*colon )) colon++;
    if (*colon == '.')
    {
        colon++;
        if (!isdigit( (unsigned char)*colon )) return 0;
        while (*colon && isdigit( (unsigned char)*colon )) colon++;
    }
    return (*colon == '\0');
}

static int is_safe_wayland_display( const char *w )
{
    size_t len;
    if (!w || !*w) return 0;
    len = strlen( w );
    if (len > 64) return 0;
    while (*w)
    {
        if (!isalnum( (unsigned char)*w ) && *w != '-' && *w != '_') return 0;
        w++;
    }
    return 1;
}

/* Convert a NUL-terminated UTF-8 string to UTF-16LE in dst (capacity
 * dst_size code units including the NUL). Returns the number of code units
 * written including the NUL, or (size_t)-1 on invalid UTF-8 or if it does not
 * fit. */
static size_t utf8_to_utf16le( const char *src, uint16_t *dst, size_t dst_size )
{
    size_t out = 0;
    size_t max_content = dst_size ? dst_size - 1 : 0;
    const unsigned char *p = (const unsigned char *)src;

    while (*p)
    {
        uint32_t cp;
        size_t units;
        unsigned char c = *p;

        if (c < 0x80)
        {
            cp = c;
            p += 1;
            units = 1;
        }
        else if ((c & 0xe0) == 0xc0)
        {
            if (p[1] < 0x80 || p[1] >= 0xc0) return (size_t)-1;
            cp = ((uint32_t)(c & 0x1f) << 6) | (p[1] & 0x3f);
            if (cp < 0x80) return (size_t)-1; /* overlong */
            p += 2;
            units = 1;
        }
        else if ((c & 0xf0) == 0xe0)
        {
            if (p[1] < 0x80 || p[1] >= 0xc0 || p[2] < 0x80 || p[2] >= 0xc0)
                return (size_t)-1;
            cp = ((uint32_t)(c & 0x0f) << 12) | ((uint32_t)(p[1] & 0x3f) << 6) | (p[2] & 0x3f);
            if (cp < 0x800) return (size_t)-1; /* overlong */
            if (cp >= 0xd800 && cp <= 0xdfff) return (size_t)-1; /* encoded surrogate */
            p += 3;
            units = 1;
        }
        else if ((c & 0xf8) == 0xf0)
        {
            if (p[1] < 0x80 || p[1] >= 0xc0 || p[2] < 0x80 || p[2] >= 0xc0 ||
                p[3] < 0x80 || p[3] >= 0xc0)
                return (size_t)-1;
            cp = ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(p[1] & 0x3f) << 12) |
                 ((uint32_t)(p[2] & 0x3f) << 6) | (p[3] & 0x3f);
            if (cp < 0x10000 || cp > 0x10ffff) return (size_t)-1; /* overlong / out of range */
            p += 4;
            units = 2; /* surrogate pair */
        }
        else
        {
            return (size_t)-1; /* invalid lead byte */
        }

        if (out + units > max_content) return (size_t)-1;
        if (units == 1)
        {
            dst[out++] = (uint16_t)cp;
        }
        else
        {
            cp -= 0x10000;
            dst[out++] = (uint16_t)(0xd800 + (cp >> 10));
            dst[out++] = (uint16_t)(0xdc00 + (cp & 0x3ff));
        }
    }
    dst[out] = 0;
    return out + 1;
}

/* Dynamic buffer builder for the double-NUL terminated UTF-16LE Windows
 * environment block. length and capacity are in code units. */
struct win_env_builder
{
    uint16_t *buf;
    size_t capacity;
    size_t length;
    int error;
};

static void win_env_add( struct win_env_builder *b, const char *key, const char *val )
{
    size_t klen, vlen;
    uint16_t *k16, *v16;
    size_t k16_len, v16_len;

    if (b->error) return;
    klen = strlen( key );
    vlen = strlen( val );

    k16 = malloc( (klen + 1) * sizeof(uint16_t) );
    v16 = malloc( (vlen + 1) * sizeof(uint16_t) );
    if (!k16 || !v16)
    {
        free( k16 );
        free( v16 );
        b->error = 1;
        return;
    }

    k16_len = utf8_to_utf16le( key, k16, klen + 1 );
    v16_len = utf8_to_utf16le( val, v16, vlen + 1 );
    if (k16_len == (size_t)-1 || v16_len == (size_t)-1)
    {
        free( k16 );
        free( v16 );
        b->error = 1;
        return;
    }

    {
        size_t add_units = (k16_len - 1) + 1 + (v16_len - 1) + 1 + 1; /* key '=' val NUL + final NUL */
        if (b->length + add_units > b->capacity)
        {
            size_t new_cap = b->capacity ? b->capacity * 2 : 1024;
            uint16_t *new_buf;
            while (new_cap < b->length + add_units) new_cap *= 2;
            new_buf = realloc( b->buf, new_cap * sizeof(uint16_t) );
            if (!new_buf)
            {
                free( k16 );
                free( v16 );
                b->error = 1;
                return;
            }
            b->buf = new_buf;
            b->capacity = new_cap;
        }
        memcpy( b->buf + b->length, k16, (k16_len - 1) * sizeof(uint16_t) );
        b->length += k16_len - 1;
        b->buf[b->length++] = '=';
        memcpy( b->buf + b->length, v16, (v16_len - 1) * sizeof(uint16_t) );
        b->length += v16_len - 1;
        b->buf[b->length++] = 0; /* NUL terminator for this entry */
        b->buf[b->length] = 0;   /* double NUL termination */
    }

    free( k16 );
    free( v16 );
}

/* Working directory traversal and verification */
static enum wex_launch_prep_status verify_and_open_cwd(
    int prefix_fd, int profile_dir_fd, const struct wex_profile_ref *profile,
    const char *requested_cwd, int *out_fd, char *out_path, size_t out_path_size )
{
    int anchor_fd = -1;
    char path_buf[512];
    char relative_subpath[512];
    char *part, *saveptr;
    struct stat st_anchor, st_cur;
    int cur_fd = -1;

    if (!requested_cwd || !*requested_cwd)
    {
        /* Default to Alice's profile directory */
        anchor_fd = profile_dir_fd;
        relative_subpath[0] = '\0';
        snprintf( out_path, out_path_size, "%s", profile->windows_path );
    }
    else
    {
        /* Clean and inspect requested_cwd */
        size_t cwd_len = strlen( requested_cwd );
        if (cwd_len >= 192) return WEX_LAUNCH_PREP_ERR_CWD_TRAVERSAL;
        strcpy( path_buf, requested_cwd );

        /* Check for dot-dot traversal */
        if (strstr( path_buf, ".." ) != NULL)
            return WEX_LAUNCH_PREP_ERR_CWD_TRAVERSAL;

        /* Windows path normalization: convert backslashes to slashes */
        {
            size_t i;
            for (i = 0; i < cwd_len; i++)
                if (path_buf[i] == '\\') path_buf[i] = '/';
        }

        /* If path starts with drive_c/, normalize to C:/ */
        if (strncasecmp( path_buf, "drive_c/", 8 ) == 0)
        {
            char tmp[512];
            snprintf( tmp, sizeof(tmp), "C:/%.192s", path_buf + 8 );
            strcpy( path_buf, tmp );
        }
        else if (strcasecmp( path_buf, "drive_c" ) == 0)
        {
            strcpy( path_buf, "C:/" );
        }

        /* Determine anchor: inside profile or allowed system dir */
        if (path_buf[0] == '/')
        {
            /* Absolute Unix path: check if it matches prefix / profile */
            return WEX_LAUNCH_PREP_ERR_CWD_NOT_ALLOWED;
        }
        else if (strncasecmp( path_buf, "C:/users/.e1s/", 14 ) == 0)
        {
            /* Subpath inside .e1s: must match this exact profile */
            size_t prefix_len = 0;
            char norm_prof[256];
            size_t np_len = strlen( profile->windows_path );
            size_t k;
            if (np_len >= sizeof(norm_prof)) return WEX_LAUNCH_PREP_ERR_CWD_TRAVERSAL;
            for (k = 0; k <= np_len; k++)
                norm_prof[k] = (profile->windows_path[k] == '\\') ? '/' : profile->windows_path[k];

            if (strncasecmp( path_buf, norm_prof, strlen(norm_prof) ) == 0)
            {
                prefix_len = strlen( norm_prof );
                if (path_buf[prefix_len] != '\0' && path_buf[prefix_len] != '/')
                    return WEX_LAUNCH_PREP_ERR_CWD_NOT_ALLOWED;
                anchor_fd = profile_dir_fd;
                if (path_buf[prefix_len] == '/') prefix_len++;
                snprintf( relative_subpath, sizeof(relative_subpath), "%.192s", path_buf + prefix_len );
                if (relative_subpath[0])
                    snprintf( out_path, out_path_size, "%.256s\\%.192s", profile->windows_path, relative_subpath );
                else
                    snprintf( out_path, out_path_size, "%.256s", profile->windows_path );
            }
            else
            {
                return WEX_LAUNCH_PREP_ERR_CWD_NOT_ALLOWED;
            }
        }
        else if (strncasecmp( path_buf, "C:/windows/system32", 19 ) == 0 &&
                 (path_buf[19] == '\0' || path_buf[19] == '/'))
        {
            const char *rest = path_buf + 19;
            if (*rest == '/') rest++;
            anchor_fd = prefix_fd;
            if (*rest)
            {
                snprintf( relative_subpath, sizeof(relative_subpath), "drive_c/windows/system32/%.192s", rest );
                snprintf( out_path, out_path_size, "C:\\windows\\system32\\%.192s", rest );
            }
            else
            {
                snprintf( relative_subpath, sizeof(relative_subpath), "drive_c/windows/system32" );
                snprintf( out_path, out_path_size, "C:\\windows\\system32" );
            }
        }
        else if (strncasecmp( path_buf, "C:/windows", 10 ) == 0 &&
                 (path_buf[10] == '\0' || path_buf[10] == '/'))
        {
            const char *rest = path_buf + 10;
            if (*rest == '/') rest++;
            anchor_fd = prefix_fd;
            if (*rest)
            {
                snprintf( relative_subpath, sizeof(relative_subpath), "drive_c/windows/%.192s", rest );
                snprintf( out_path, out_path_size, "C:\\windows\\%.192s", rest );
            }
            else
            {
                snprintf( relative_subpath, sizeof(relative_subpath), "drive_c/windows" );
                snprintf( out_path, out_path_size, "C:\\windows" );
            }
        }
        else if (strncasecmp( path_buf, "C:/Program Files", 16 ) == 0 &&
                 (path_buf[16] == '\0' || path_buf[16] == '/'))
        {
            const char *rest = path_buf + 16;
            if (*rest == '/') rest++;
            anchor_fd = prefix_fd;
            if (*rest)
            {
                snprintf( relative_subpath, sizeof(relative_subpath), "drive_c/Program Files/%.192s", rest );
                snprintf( out_path, out_path_size, "C:\\Program Files\\%.192s", rest );
            }
            else
            {
                snprintf( relative_subpath, sizeof(relative_subpath), "drive_c/Program Files" );
                snprintf( out_path, out_path_size, "C:\\Program Files" );
            }
        }
        else if (strcasecmp( path_buf, "C:" ) == 0 || strcasecmp( path_buf, "C:/" ) == 0)
        {
            anchor_fd = prefix_fd;
            snprintf( relative_subpath, sizeof(relative_subpath), "drive_c" );
            snprintf( out_path, out_path_size, "C:\\" );
        }
        else if (strncasecmp( path_buf, "C:/", 3 ) == 0)
        {
            /* Other arbitrary path on C: outside profile and system dirs */
            return WEX_LAUNCH_PREP_ERR_CWD_NOT_ALLOWED;
        }
        else if (strcmp( path_buf, "." ) == 0)
        {
            /* Current directory inside profile */
            anchor_fd = profile_dir_fd;
            relative_subpath[0] = '\0';
            snprintf( out_path, out_path_size, "%.256s", profile->windows_path );
        }
        else
        {
            /* Relative path inside Alice's profile directory */
            anchor_fd = profile_dir_fd;
            snprintf( relative_subpath, sizeof(relative_subpath), "%.192s", path_buf );
            snprintf( out_path, out_path_size, "%.256s\\%.192s", profile->windows_path, relative_subpath );
        }
    }

    if (fstat( anchor_fd, &st_anchor ) != 0 || !S_ISDIR( st_anchor.st_mode ))
        return WEX_LAUNCH_PREP_ERR_IO;

    cur_fd = fcntl( anchor_fd, F_DUPFD_CLOEXEC, 0 );
    if (cur_fd < 0) return WEX_LAUNCH_PREP_ERR_IO;

    if (relative_subpath[0] != '\0')
    {
        strcpy( path_buf, relative_subpath );
        for (part = strtok_r( path_buf, "/", &saveptr ); part; part = strtok_r( NULL, "/", &saveptr ))
        {
            int next_fd;
            struct stat st_lstat;

            if (part[0] == '\0' || strcmp( part, "." ) == 0) continue;
            if (strcmp( part, ".." ) == 0)
            {
                close( cur_fd );
                return WEX_LAUNCH_PREP_ERR_CWD_TRAVERSAL;
            }

            /* Reject symlinks explicitly */
            if (fstatat( cur_fd, part, &st_lstat, AT_SYMLINK_NOFOLLOW ) != 0)
            {
                close( cur_fd );
                return (errno == ENOENT) ? WEX_LAUNCH_PREP_ERR_CWD_NOT_DIR : WEX_LAUNCH_PREP_ERR_IO;
            }

            if (S_ISLNK( st_lstat.st_mode ))
            {
                close( cur_fd );
                return WEX_LAUNCH_PREP_ERR_CWD_TRAVERSAL;
            }

            if (!S_ISDIR( st_lstat.st_mode ))
            {
                close( cur_fd );
                return WEX_LAUNCH_PREP_ERR_CWD_NOT_DIR;
            }

            next_fd = openat( cur_fd, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC );
            if (next_fd < 0)
            {
                int err = errno;
                close( cur_fd );
                if (err == ELOOP) return WEX_LAUNCH_PREP_ERR_CWD_TRAVERSAL;
                if (err == ENOTDIR) return WEX_LAUNCH_PREP_ERR_CWD_NOT_DIR;
                return WEX_LAUNCH_PREP_ERR_IO;
            }

            /* Confirm device matches anchor */
            if (fstat( next_fd, &st_cur ) != 0 || st_cur.st_dev != st_anchor.st_dev)
            {
                close( next_fd );
                close( cur_fd );
                return WEX_LAUNCH_PREP_ERR_CWD_TRAVERSAL;
            }

            close( cur_fd );
            cur_fd = next_fd;
        }
    }

    *out_fd = cur_fd;
    return WEX_LAUNCH_PREP_OK;
}

enum wex_launch_prep_status wex_prepare_launch(
    const struct wex_launch_prep_params *params,
    struct wex_launch_prep_result *result )
{
    struct win_env_builder wb = { 0 };
    char **uenv = NULL;
    size_t uenv_cap = 32, uenv_count = 0;
    int verified_cwd_fd = -1;
    char verified_cwd_path[512] = { 0 };
    enum wex_launch_prep_status status;
    char buf[512];
    const char * const *p;

    if (!params || !result) return WEX_LAUNCH_PREP_ERR_INVALID_PARAM;
    memset( result, 0, sizeof(*result) );
    result->verified_cwd_fd = -1;

    /* 1. Real-UID transition check: target process MUST run under Bob's host UID/GID */
    if (params->bob_uid == 0 || params->bob_gid == 0)
        return WEX_LAUNCH_PREP_ERR_INVALID_PARAM;
    if (params->requested_uid != params->bob_uid || params->requested_gid != params->bob_gid)
        return WEX_LAUNCH_PREP_ERR_REAL_UID_UNSUPPORTED;

    /* 2. Inherited environment rejection */
    if (params->flags & WEX_LAUNCH_PREP_FLAG_INHERIT_ENV)
        return WEX_LAUNCH_PREP_ERR_INHERIT_REJECTED;

    /* 3. Token validation */
    if (!params->token.token_id || !params->token.auth_id ||
        params->token.elevation_type != 2 || params->token.integrity_level < 0x3000)
        return WEX_LAUNCH_PREP_ERR_TOKEN_INVALID;

    /* 4. Profile validation */
    if (!params->profile.account_name[0] || !params->profile.sid[0] ||
        !params->profile.windows_path[0])
        return WEX_LAUNCH_PREP_ERR_PROFILE_INVALID;

    /* 5. Descriptors validation */
    if (params->descriptors.prefix_fd < 0 || params->descriptors.profile_dir_fd < 0)
        return WEX_LAUNCH_PREP_ERR_INVALID_PARAM;

    /* 6. Caller Windows environment override checks */
    if (params->caller_win_env && params->caller_win_env_size > 0)
    {
        const char *cur = params->caller_win_env;
        const char *end = params->caller_win_env + params->caller_win_env_size;
        while (cur < end && *cur)
        {
            const char *eq = strchr( cur, '=' );
            if (eq && eq > cur)
            {
                size_t nlen = (size_t)(eq - cur);
                if (is_protected_win_var( cur, nlen ))
                    return WEX_LAUNCH_PREP_ERR_PROTECTED_OVERRIDE;
            }
            cur += strlen( cur ) + 1;
        }
    }

    /* 7. Working directory validation */
    status = verify_and_open_cwd( params->descriptors.prefix_fd,
                                 params->descriptors.profile_dir_fd,
                                 &params->profile,
                                 params->requested_cwd,
                                 &verified_cwd_fd,
                                 verified_cwd_path,
                                 sizeof(verified_cwd_path) );
    if (status != WEX_LAUNCH_PREP_OK) return status;

    /* 8. Windows Environment construction from clean allow-list */
    win_env_add( &wb, "USERNAME", params->profile.account_name );
    win_env_add( &wb, "USERPROFILE", params->profile.windows_path );

    if (params->profile.windows_path[0] && params->profile.windows_path[1] == ':')
    {
        char drive[3] = { params->profile.windows_path[0], ':', '\0' };
        win_env_add( &wb, "HOMEDRIVE", drive );
        win_env_add( &wb, "HOMEPATH", params->profile.windows_path + 2 );
    }
    else
    {
        win_env_add( &wb, "HOMEDRIVE", "C:" );
        win_env_add( &wb, "HOMEPATH", "\\users\\.e1s" );
    }

    win_env_add( &wb, "USERDOMAIN", "WINEHOST" );

    snprintf( buf, sizeof(buf), "%s\\AppData\\Roaming", params->profile.windows_path );
    win_env_add( &wb, "APPDATA", buf );

    snprintf( buf, sizeof(buf), "%s\\AppData\\Local", params->profile.windows_path );
    win_env_add( &wb, "LOCALAPPDATA", buf );

    snprintf( buf, sizeof(buf), "%s\\AppData\\Local\\Temp", params->profile.windows_path );
    win_env_add( &wb, "TEMP", buf );
    win_env_add( &wb, "TMP", buf );

    win_env_add( &wb, "SystemRoot", "C:\\windows" );
    win_env_add( &wb, "SystemDrive", "C:" );
    win_env_add( &wb, "ProgramFiles", "C:\\Program Files" );
    win_env_add( &wb, "ProgramFiles(x86)", "C:\\Program Files (x86)" );
    win_env_add( &wb, "CommonProgramFiles", "C:\\Program Files\\Common Files" );
    win_env_add( &wb, "CommonProgramFiles(x86)", "C:\\Program Files (x86)\\Common Files" );
    win_env_add( &wb, "ALLUSERSPROFILE", "C:\\ProgramData" );
    win_env_add( &wb, "ProgramData", "C:\\ProgramData" );
    win_env_add( &wb, "PUBLIC", "C:\\users\\Public" );
    win_env_add( &wb, "COMPUTERNAME", "WINEHOST" );
    win_env_add( &wb, "COMSPEC", "C:\\windows\\system32\\cmd.exe" );
    win_env_add( &wb, "PATH", "C:\\windows\\system32;C:\\windows;C:\\windows\\system32\\wbem" );
    win_env_add( &wb, "PATHEXT", ".COM;.EXE;.BAT;.CMD;.VBS;.VBE;.JS;.JSE;.WSF;.WSH;.MSC" );
    win_env_add( &wb, "PROCESSOR_ARCHITECTURE", "AMD64" );

    if (wb.error || !wb.buf)
    {
        free( wb.buf );
        close( verified_cwd_fd );
        return WEX_LAUNCH_PREP_ERR_IO;
    }

    /* 9. Unix Environment construction from sanitized allow-list */
    uenv = calloc( uenv_cap, sizeof(char *) );
    if (!uenv)
    {
        free( wb.buf );
        close( verified_cwd_fd );
        return WEX_LAUNCH_PREP_ERR_IO;
    }

    /* Derive Bob's home directory. The host account lookup must succeed; a
     * missing or incomplete entry fails closed instead of inventing a home or
     * user name for an identity the host does not know. */
    {
        struct passwd pw_buf, *pw = NULL;
        char pw_str[1024];

        if (getpwuid_r( params->bob_uid, &pw_buf, pw_str, sizeof(pw_str), &pw ) != 0 ||
            !pw || !pw->pw_dir || pw->pw_dir[0] != '/' || !pw->pw_name || !pw->pw_name[0])
        {
            size_t i;
            for (i = 0; i < uenv_count; i++) free( uenv[i] );
            free( uenv );
            free( wb.buf );
            close( verified_cwd_fd );
            return WEX_LAUNCH_PREP_ERR_ACCOUNT_LOOKUP;
        }

        snprintf( buf, sizeof(buf), "HOME=%s", pw->pw_dir );
        uenv[uenv_count++] = strdup( buf );

        snprintf( buf, sizeof(buf), "USER=%s", pw->pw_name );
        uenv[uenv_count++] = strdup( buf );

        snprintf( buf, sizeof(buf), "LOGNAME=%s", pw->pw_name );
        uenv[uenv_count++] = strdup( buf );
    }

    uenv[uenv_count++] = strdup( "PATH=/usr/local/bin:/usr/bin:/bin" );
    uenv[uenv_count++] = strdup( "SHELL=/bin/bash" );

    /* Safe caller passthrough variables */
    if (params->caller_unix_env)
    {
        for (p = params->caller_unix_env; *p; p++)
        {
            const char *entry = *p;
            if (is_forbidden_unix_var( entry )) continue;
            if (!is_safe_string( entry )) continue;

            if (strncmp( entry, "TERM=", 5 ) == 0)
            {
                if (strlen( entry + 5 ) <= 32)
                    uenv[uenv_count++] = strdup( entry );
            }
            else if (strncmp( entry, "LANG=", 5 ) == 0 ||
                     strncmp( entry, "LC_ALL=", 7 ) == 0 ||
                     strncmp( entry, "LC_CTYPE=", 9 ) == 0 ||
                     strncmp( entry, "LC_MESSAGES=", 12 ) == 0)
            {
                if (strlen( entry ) <= 64)
                    uenv[uenv_count++] = strdup( entry );
            }
            else if (strncmp( entry, "DISPLAY=", 8 ) == 0)
            {
                if (is_safe_display( entry + 8 ))
                    uenv[uenv_count++] = strdup( entry );
            }
            else if (strncmp( entry, "WAYLAND_DISPLAY=", 16 ) == 0)
            {
                if (is_safe_wayland_display( entry + 16 ))
                    uenv[uenv_count++] = strdup( entry );
            }
            else if (strncmp( entry, "XDG_SESSION_TYPE=", 17 ) == 0 ||
                     strncmp( entry, "XDG_CURRENT_DESKTOP=", 20 ) == 0 ||
                     strncmp( entry, "XDG_SESSION_CLASS=", 18 ) == 0)
            {
                if (strlen( entry ) <= 64)
                    uenv[uenv_count++] = strdup( entry );
            }
            else if (strncmp( entry, "XDG_RUNTIME_DIR=", 16 ) == 0)
            {
                struct stat st;
                const char *rpath = entry + 16;
                if (lstat( rpath, &st ) == 0 && S_ISDIR( st.st_mode ) &&
                    !S_ISLNK( st.st_mode ) && st.st_uid == params->bob_uid &&
                    (st.st_mode & 0777) == 0700)
                {
                    uenv[uenv_count++] = strdup( entry );
                }
            }
            if (uenv_count >= uenv_cap - 2) break;
        }
    }

    uenv[uenv_count] = NULL;

    /* Populate results */
    result->exec_uid = params->bob_uid;
    result->exec_gid = params->bob_gid;
    result->win_env = (char *)wb.buf;
    result->win_env_size = (wb.length + 1) * sizeof(uint16_t); /* bytes, includes final double-NUL */
    result->unix_env = uenv;
    result->unix_env_count = uenv_count;
    result->verified_cwd_fd = verified_cwd_fd;
    snprintf( result->verified_cwd_path, sizeof(result->verified_cwd_path), "%s", verified_cwd_path );

    return WEX_LAUNCH_PREP_OK;
}

void wex_launch_prep_result_free( struct wex_launch_prep_result *result )
{
    if (!result) return;
    if (result->verified_cwd_fd >= 0)
    {
        close( result->verified_cwd_fd );
        result->verified_cwd_fd = -1;
    }
    free( result->win_env );
    result->win_env = NULL;
    result->win_env_size = 0;
    if (result->unix_env)
    {
        size_t i;
        for (i = 0; i < result->unix_env_count; i++)
            free( result->unix_env[i] );
        free( result->unix_env );
        result->unix_env = NULL;
        result->unix_env_count = 0;
    }
}

#endif /* WEX_E1S15_LAUNCH_PREP_TEST */
