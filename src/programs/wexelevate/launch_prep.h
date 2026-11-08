/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Pure, unit-testable launch preparation for E1-S15.
 * Validates same-server inputs, sanitizes Windows and Unix environments,
 * verifies working directory containment without symlink traversal,
 * and enforces Bob's host UID with hard rejection of real-UID transitions.
 * Does not launch any process; no credential launches enabled.
 */
#ifndef WEXELEVATE_LAUNCH_PREP_H
#define WEXELEVATE_LAUNCH_PREP_H

#include <stddef.h>
#include <stdint.h>
#ifdef _WIN32
typedef unsigned int uid_t;
typedef unsigned int gid_t;
#else
#include <sys/types.h>
#endif

enum wex_launch_prep_status
{
    WEX_LAUNCH_PREP_OK = 0,
    WEX_LAUNCH_PREP_ERR_DISABLED = 1,
    WEX_LAUNCH_PREP_ERR_INVALID_PARAM = 2,
    WEX_LAUNCH_PREP_ERR_REAL_UID_UNSUPPORTED = 3,
    WEX_LAUNCH_PREP_ERR_INHERIT_REJECTED = 4,
    WEX_LAUNCH_PREP_ERR_PROTECTED_OVERRIDE = 5,
    WEX_LAUNCH_PREP_ERR_CWD_NOT_ALLOWED = 6,
    WEX_LAUNCH_PREP_ERR_CWD_TRAVERSAL = 7,
    WEX_LAUNCH_PREP_ERR_CWD_NOT_DIR = 8,
    WEX_LAUNCH_PREP_ERR_TOKEN_INVALID = 9,
    WEX_LAUNCH_PREP_ERR_PROFILE_INVALID = 10,
    WEX_LAUNCH_PREP_ERR_IO = 11,
};

/* Flags for wex_launch_prep_params */
#define WEX_LAUNCH_PREP_FLAG_INHERIT_ENV  0x0001 /* caller requested inherit -> REJECTED */
#define WEX_LAUNCH_PREP_FLAG_TEST_MODE    0x0002

struct wex_token_ref
{
    uint64_t token_id;
    uint64_t auth_id;
    unsigned char account_key[16];
    uint32_t session_id;
    uint32_t elevation_type;  /* must be Full (2) */
    uint32_t integrity_level; /* must be High (>= 0x3000) */
};

struct wex_profile_ref
{
    char account_name[64];
    char sid[64];
    unsigned char account_key[16];
    uint64_t map_generation;
    uint64_t storage_generation;
    char windows_path[256];       /* e.g. "C:\\users\\.e1s\\<SID>\\<incarnation>-<name>" */
    char unix_relative_path[256]; /* relative to prefix, e.g. "drive_c/users/.e1s/..." */
};

struct wex_launch_descriptors
{
    int prefix_fd;      /* open descriptor to Bob's wine prefix directory */
    int profile_dir_fd; /* open descriptor to Alice's profile directory */
    int cwd_dir_fd;     /* open descriptor to requested working directory, or -1 if using path */
};

struct wex_launch_prep_params
{
    uint32_t flags;
    struct wex_token_ref token;
    struct wex_profile_ref profile;
    struct wex_launch_descriptors descriptors;

    /* Host identities */
    uid_t bob_uid;          /* Bob's host UID */
    gid_t bob_gid;          /* Bob's primary host GID */
    uid_t requested_uid;    /* Must match bob_uid; any other UID denies */
    gid_t requested_gid;    /* Must match bob_gid */

    /* Working directory input */
    const char *requested_cwd; /* Unix path or Windows-relative path to check */

    /* Caller-supplied environments (if any) */
    const char *caller_win_env;          /* Double-NUL terminated block or key=val\0 */
    size_t caller_win_env_size;
    const char * const *caller_unix_env; /* NULL-terminated array of strings */
};

struct wex_launch_prep_result
{
    /* Sanitized execution identity: always Bob's host UID/GID */
    uid_t exec_uid;
    gid_t exec_gid;

    /* Windows environment block for Alice (double-NUL terminated) */
    char *win_env;
    size_t win_env_size;

    /* Sanitized Unix environment (NULL-terminated array of strings) */
    char **unix_env;
    size_t unix_env_count;

    /* Verified working directory */
    int verified_cwd_fd;         /* Owned open descriptor to verified directory */
    char verified_cwd_path[512]; /* Verified path inside profile or allowed system dir */
};

/* Core launch preparation function */
enum wex_launch_prep_status wex_prepare_launch(
    const struct wex_launch_prep_params *params,
    struct wex_launch_prep_result *result );

/* Free result buffers and close verified_cwd_fd */
void wex_launch_prep_result_free( struct wex_launch_prep_result *result );

#endif /* WEXELEVATE_LAUNCH_PREP_H */
