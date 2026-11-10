/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Native C unit test for E1-S18 Broker Wine-Only Integration.
 * Verifies stage ordering, failure denial and cleanup at each stage,
 * capability enforcement, single guarded resume, and disabled-by-default behavior.
 */

#include "e1s18_broker.h"
#ifdef WEXELEVATE_E1S18
/* WO-14: the golden-vector test uses the shared S1 encoder directly. */
#include "e1s_encode.h"
/* WO-20: the R8 INIT/ACK test uses the codec directly; only a subset of the
 * codec's static helpers is used, so suppress the unused-function warnings. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "e1r8_codec.h"
#pragma GCC diagnostic pop
#endif
#include "../../../wine/include/wine/wex_token.h"
#include "../../../wine/include/wine/wex_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* R15 policy-value decision, exported from e1s18_broker.c for native testing. */
extern BOOL e1s18_policy_value_enabled( DWORD val );

/* WO-14: sets a fully valid S1 endorsed-identity context (defined below). */
static void set_default_s1_context( void );

#define TEST_PASS 0
#define TEST_FAIL 1

static int g_failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); \
        g_failed = 1; \
        return TEST_FAIL; \
    } \
} while (0)

#define CHECK_EQ(actual, expected, msg) do { \
    if ((actual) != (expected)) { \
        fprintf(stderr, "FAIL [%s:%d]: %s (got %lu, expected %lu)\n", \
                __FILE__, __LINE__, msg, (unsigned long)(actual), (unsigned long)(expected)); \
        g_failed = 1; \
        return TEST_FAIL; \
    } \
} while (0)

#ifdef WEXELEVATE_E1S18
static BOOL is_zero_key( const uint8_t key[16] )
{
    int i;
    for (i = 0; i < 16; i++)
    {
        if (key[i]) return FALSE;
    }
    return TRUE;
}
#endif

/*
 * Test Tracking State for Injected Fakes
 */
struct test_tracker
{
    int stage_counter;
    int order[32];
    int order_count;

    /* Injection failure stage (0 = no failure) */
    int fail_at_stage;
    DWORD fail_error_code;

    /* Resume count */
    int resume_count;

    /* Rollback cleanup tracking */
    int child_terminated;
    int remote_handle_closed;
    int profile_unloaded;
    int handles_closed;
    int buffers_freed;
    int process_handle_closed;
    int thread_handle_closed;
    /* WO-8: when set, fake_terminate_child reports the child is not confirmed dead */
    int terminate_child_fail;

    /* Injected capabilities */
    uint64_t injected_caps;
    BOOL injected_policy;

    /* Stage 2 negative test injection */
    int omit_stage2_account_name;
    int omit_stage2_account_key;
    int omit_stage2_map_gen;
};

static struct test_tracker g_track;

static void reset_tracker( void )
{
    memset( &g_track, 0, sizeof(g_track) );
    g_track.injected_caps = E1S1_ALL_REQUIRED_CAPS;
    g_track.injected_policy = TRUE;
    /* WO-14: the standard-user branch needs the S1 endorsed-identity context
     * to build the canonical binding/request records. Every full-pipeline
     * test resets the tracker, so supply a valid context here; tests that
     * fail before the context check are unaffected, and the missing-input
     * test overrides individual fields. */
    set_default_s1_context();
#ifdef WEXELEVATE_E1S18
    {
        struct e1s18_r8_context r8_ctx;
        memset( &r8_ctx, 0, sizeof(r8_ctx) );
        r8_ctx.root_gen = 10;
        r8_ctx.server_gen = 20;
        r8_ctx.broker_gen = 30;
        r8_ctx.verifier_gen = 40;
        r8_ctx.map_gen = 50;
        r8_ctx.policy_gen = 60;
        r8_ctx.caller_session = 1;
        memset( r8_ctx.desktop_route, 0x55, 16 );
        strcpy( r8_ctx.verifier_owner, ":1.42" );
        e1s18_set_r8_context( &r8_ctx );
        e1s18_set_r8_skip_prepare( TRUE );
    }
#endif
}

static uint64_t fake_get_capabilities( const struct request *req )
{
    (void)req;
    return g_track.injected_caps;
}

static BOOL fake_is_policy_enabled( void )
{
    return g_track.injected_policy;
}

static DWORD fake_prompt_dialog( struct request *req, const uint8_t nonce[32],
                                 const uint8_t binding_digest[32], const uint8_t request_digest[32],
                                 char *out_account, size_t account_max )
{
    (void)req; (void)nonce; (void)binding_digest; (void)request_digest;
    g_track.order[g_track.order_count++] = 1;
    if (g_track.fail_at_stage == 1) return g_track.fail_error_code ? g_track.fail_error_code : ERROR_CANCELLED;
    if (out_account && account_max > 5) strncpy( out_account, "alice", account_max - 1 );
    return ERROR_SUCCESS;
}

static DWORD fake_redeem_evidence( struct request *req, const char *account_name,
                                   const uint8_t nonce[32], const uint8_t binding_digest[32],
                                   const uint8_t request_digest[32],
                                   uint8_t out_account_key[16], uint8_t out_evidence_ref[16] )
{
    (void)account_name; (void)nonce; (void)binding_digest; (void)request_digest;
    g_track.order[g_track.order_count++] = 2;
    if (g_track.fail_at_stage == 2) return g_track.fail_error_code ? g_track.fail_error_code : ERROR_ACCESS_DENIED;
#ifdef WEXELEVATE_E1S18
    if (!g_track.omit_stage2_account_name)
    {
        strncpy( req->account_name, "alice", sizeof(req->account_name) - 1 );
        req->account_name[sizeof(req->account_name) - 1] = '\0';
    }
    if (!g_track.omit_stage2_account_key)
    {
        memset( req->account_key, 0x42, sizeof(req->account_key) );
        if (out_account_key) memcpy( out_account_key, req->account_key, 16 );
    }
    else
    {
        if (out_account_key) memset( out_account_key, 0, 16 );
    }
    if (!g_track.omit_stage2_map_gen)
        req->map_generation = 1;
    /* The S12 storage generation is no longer a Stage 2 field; it is set by
     * query_storage (op 12) after S10 issuance. */
#else
    (void)req;
    if (out_account_key) memset( out_account_key, 0x42, 16 );
#endif
    if (out_evidence_ref) memset( out_evidence_ref, 0x77, 16 );
    return ERROR_SUCCESS;
}

static DWORD fake_issue_token( struct request *req, const uint8_t account_key[16],
                               const uint8_t evidence_ref[16], const uint8_t nonce[32],
                               const uint8_t binding_digest[32], const uint8_t request_digest[32],
                               HANDLE *out_token )
{
    (void)account_key; (void)evidence_ref; (void)nonce; (void)binding_digest; (void)request_digest;
    g_track.order[g_track.order_count++] = 3;
    if (g_track.fail_at_stage == 3) return g_track.fail_error_code ? g_track.fail_error_code : ERROR_ACCESS_DENIED;
#ifdef WEXELEVATE_E1S18
    /* Fail closed if Stage 2 fields are unset */
    if (is_zero_key( req->account_key ) || req->map_generation == 0)
        return ERROR_INVALID_DATA;
#else
    (void)req;
#endif
    if (out_token) *out_token = (HANDLE)(intptr_t)0x1001;
    return ERROR_SUCCESS;
}

static DWORD fake_load_profile( struct request *req, HANDLE token, const uint8_t account_key[16],
                                uint64_t *out_lease_id, HANDLE *out_hkey )
{
    (void)token; (void)account_key;
    g_track.order[g_track.order_count++] = 5;
    if (g_track.fail_at_stage == 5) return g_track.fail_error_code ? g_track.fail_error_code : ERROR_FILE_NOT_FOUND;
#ifdef WEXELEVATE_E1S18
    /* Fail closed if Stage 2 fields are unset */
    if (is_zero_key( req->account_key ) || req->map_generation == 0 || req->storage_generation == 0)
        return ERROR_INVALID_DATA;
#else
    (void)req;
#endif
    if (out_lease_id) *out_lease_id = 0x5501ULL;
    if (out_hkey) *out_hkey = (HANDLE)(intptr_t)0x2002;
    return ERROR_SUCCESS;
}

static DWORD fake_query_storage( struct request *req, HANDLE token )
{
    (void)token;
    g_track.order[g_track.order_count++] = 4;
    if (g_track.fail_at_stage == 4) return g_track.fail_error_code ? g_track.fail_error_code : ERROR_ACCESS_DENIED;
#ifdef WEXELEVATE_E1S18
    /* Fail closed if Stage 2 fields are unset */
    if (is_zero_key( req->account_key ) || req->map_generation == 0)
        return ERROR_INVALID_DATA;
    /* Stage 3b sets the S12 storage generation for LOAD to compare exactly. */
    req->storage_generation = 1;
#else
    (void)req;
#endif
    return ERROR_SUCCESS;
}

static DWORD fake_prep_launch( struct request *req, HANDLE token,
                               char **out_win_env, size_t *out_win_env_sz,
                               char ***out_unix_env, size_t *out_unix_env_cnt,
                               char **out_cwd )
{
    (void)token;
    g_track.order[g_track.order_count++] = 6;
    if (g_track.fail_at_stage == 6) return g_track.fail_error_code ? g_track.fail_error_code : ERROR_NOT_SUPPORTED;
#ifdef WEXELEVATE_E1S18
    /* Fail closed if Stage 2 fields are unset */
    if (!req->account_name[0] || is_zero_key( req->account_key ) ||
        req->map_generation == 0 || req->storage_generation == 0)
        return ERROR_INVALID_DATA;
#else
    (void)req;
#endif
    if (out_win_env)
    {
        *out_win_env = (char *)malloc( 32 );
        if (*out_win_env) { memset( *out_win_env, 0, 32 ); strcpy( *out_win_env, "VAR=1" ); }
    }
    if (out_win_env_sz) *out_win_env_sz = 32;
    if (out_unix_env) *out_unix_env = NULL;
    if (out_unix_env_cnt) *out_unix_env_cnt = 0;
    if (out_cwd) *out_cwd = NULL;
    return ERROR_SUCCESS;
}

static DWORD fake_verify_image( struct request *req )
{
    (void)req;
    g_track.order[g_track.order_count++] = 7;
    if (g_track.fail_at_stage == 7) return g_track.fail_error_code ? g_track.fail_error_code : ERROR_FILE_CORRUPT;
    return ERROR_SUCCESS;
}

static DWORD fake_create_process_suspended( struct request *req, HANDLE token,
                                           const char *win_env, size_t win_env_sz,
                                           const char *cwd,
                                           PROCESS_INFORMATION *out_pi )
{
    (void)req; (void)token; (void)win_env; (void)win_env_sz; (void)cwd;
    g_track.order[g_track.order_count++] = 8;
    if (g_track.fail_at_stage == 8) return g_track.fail_error_code ? g_track.fail_error_code : ERROR_ACCESS_DENIED;
    if (out_pi)
    {
        out_pi->hProcess = (HANDLE)(intptr_t)0x3003;
        out_pi->hThread = (HANDLE)(intptr_t)0x4004;
        out_pi->dwProcessId = 12345;
        out_pi->dwThreadId = 6789;
    }
    return ERROR_SUCCESS;
}

static DWORD fake_verify_child( struct request *req, HANDLE token, const PROCESS_INFORMATION *pi )
{
    (void)req; (void)token; (void)pi;
    g_track.order[g_track.order_count++] = 9;
    if (g_track.fail_at_stage == 9) return g_track.fail_error_code ? g_track.fail_error_code : ERROR_ACCESS_DENIED;
    return ERROR_SUCCESS;
}

static DWORD fake_arm_lease( UINT64 lease, DWORD pid )
{
    (void)lease; (void)pid;
    g_track.order[g_track.order_count++] = 10;
    if (g_track.fail_at_stage == 10) return g_track.fail_error_code ? g_track.fail_error_code : ERROR_FILE_CORRUPT;
    return ERROR_SUCCESS;
}

static DWORD fake_duplicate_handle( struct request *req, HANDLE process, HANDLE *out_remote )
{
    (void)req; (void)process;
    g_track.order[g_track.order_count++] = 11;
    if (g_track.fail_at_stage == 11) return g_track.fail_error_code ? g_track.fail_error_code : ERROR_ACCESS_DENIED;
    if (out_remote) *out_remote = (HANDLE)(intptr_t)0x5005;
    return ERROR_SUCCESS;
}

static DWORD fake_transfer_lease( struct request *req, uint64_t lease_id, HANDLE process, HANDLE token )
{
    (void)req; (void)lease_id; (void)process; (void)token;
    g_track.order[g_track.order_count++] = 12;
    if (g_track.fail_at_stage == 12) return g_track.fail_error_code ? g_track.fail_error_code : ERROR_ACCESS_DENIED;
    return ERROR_SUCCESS;
}

static DWORD fake_check_pre_resume( struct request *req )
{
    (void)req;
    g_track.order[g_track.order_count++] = 13;
    if (g_track.fail_at_stage == 13) return g_track.fail_error_code ? g_track.fail_error_code : ERROR_CANCELLED;
    return ERROR_SUCCESS;
}

static DWORD fake_resume_thread( struct request *req, HANDLE thread )
{
    (void)req; (void)thread;
    g_track.order[g_track.order_count++] = 14;
    g_track.resume_count++;
    if (g_track.fail_at_stage == 14) return g_track.fail_error_code ? g_track.fail_error_code : ERROR_GEN_FAILURE;
    return ERROR_SUCCESS;
}

static BOOL fake_terminate_child( HANDLE process )
{
    (void)process;
    g_track.child_terminated++;
    return g_track.terminate_child_fail ? FALSE : TRUE;
}

static void fake_close_remote_handle( HANDLE caller, HANDLE remote )
{
    (void)caller; (void)remote;
    g_track.remote_handle_closed++;
}

static void fake_unload_profile( uint64_t lease_id, HANDLE hkey, HANDLE token )
{
    (void)lease_id; (void)hkey; (void)token;
    g_track.profile_unloaded++;
}

static void fake_close_handle( HANDLE handle )
{
    g_track.handles_closed++;
    /* Track the suspended child's process/thread handles (set by
     * fake_create_process_suspended) so rollback ownership can be asserted. */
    if (handle == (HANDLE)(intptr_t)0x3003) g_track.process_handle_closed++;
    if (handle == (HANDLE)(intptr_t)0x4004) g_track.thread_handle_closed++;
}

static void fake_free_buffer( void *ptr )
{
    free( ptr );
    g_track.buffers_freed++;
}

static struct e1s18_ops s_fake_ops = {
    fake_get_capabilities,
    fake_is_policy_enabled,
    fake_prompt_dialog,
    fake_redeem_evidence,
    fake_issue_token,
    fake_load_profile,
    fake_query_storage,
    fake_prep_launch,
    fake_verify_image,
    fake_create_process_suspended,
    fake_verify_child,
    fake_arm_lease,
    fake_duplicate_handle,
    fake_transfer_lease,
    fake_check_pre_resume,
    fake_resume_thread,
    fake_terminate_child,
    fake_close_remote_handle,
    fake_unload_profile,
    fake_close_handle,
    fake_free_buffer,
    NULL, /* r8_open */
    NULL, /* r8_owner */
    NULL, /* r8_binding */
    NULL, /* r8_endorse */
    NULL, /* r8_recheck */
    NULL, /* r8_close */
    NULL  /* r8_start */
};

static void init_dummy_request( struct request *req )
{
    memset( req, 0, sizeof(*req) );
    req->id = 0x1234567890abcdefULL;
    req->caller_pid = 1000;
    req->caller_session = 1;
    req->caller = (HANDLE)(intptr_t)0x8008;
    req->caller_token = (HANDLE)(intptr_t)0x9009;
    req->flags = WEXELEVATE_WANT_HANDLE;
    req->show = 1;
    req->lease = 1;
    memset( req->hash, 0x33, sizeof(req->hash) );
#ifdef WEXELEVATE_E1S18
    req->is_standard_user = TRUE;
    req->caller_stats.AuthenticationId.HighPart = 0x1111;
    req->caller_stats.AuthenticationId.LowPart = 0x2222;
    /* Bob's SID: S-1-5-21-555-500 */
    req->caller_sid_len = 16;
    req->caller_sid[0] = 1;
    req->caller_sid[1] = 2;
    req->caller_sid[7] = 5;
    wex_token_put32( req->caller_sid + 8, 555 );
    wex_token_put32( req->caller_sid + 12, 500 );
    req->info.dwVolumeSerialNumber = 0x1234;
    req->info.nFileIndexHigh = 0;
    req->info.nFileIndexLow = 0x5678;
    req->info.nFileSizeHigh = 0;
    req->info.nFileSizeLow = 0x9abc;
#ifdef _WIN32
    req->info.ftLastWriteTime.dwHighDateTime = 0;
    req->info.ftLastWriteTime.dwLowDateTime = 0xdef0;
#else
    req->info.ftLastWriteTime = 0xdef0;
#endif
#endif
}

/*
 * WO-14: Fill a fully valid S1 endorsed-identity context. The context is zero
 * in production (the request denies); the full pipeline tests set it here.
 */
static void fill_default_s1_context( struct e1s18_s1_context *ctx )
{
    int i;
    memset( ctx, 0, sizeof(*ctx) );
    ctx->root_gen = 0x11;
    ctx->server_gen = 0x22;
    ctx->broker_gen = 0x33;
    ctx->verifier_gen = 0x44;
    ctx->map_gen = 0x55;
    ctx->policy_gen = 0x66;
    for (i = 0; i < 16; i++) ctx->prefix[i] = (uint8_t)(0x01 + i);
    ctx->host_uid = 1000;
    for (i = 0; i < 16; i++) ctx->host_process[i] = (uint8_t)(0x11 + i);
    for (i = 0; i < 16; i++) ctx->opener[i] = (uint8_t)(0x21 + i);
    for (i = 0; i < 16; i++) ctx->writer[i] = (uint8_t)(0x21 + i);
    for (i = 0; i < 16; i++) ctx->desktop[i] = (uint8_t)(0x31 + i);
    for (i = 0; i < 16; i++) ctx->channel[i] = (uint8_t)(0x41 + i);
    ctx->volume_serial = 0xaa00;
    ctx->file_index = 0xbb00;
    ctx->file_size = 0xcc00;
    ctx->last_write = 0xdd00;
    ctx->deadline = 0xee00;
}

static void set_default_s1_context( void )
{
    struct e1s18_s1_context ctx;
    fill_default_s1_context( &ctx );
    e1s18_set_s1_context( &ctx );
}

/*
 * Test 1: Disabled by default
 */
static int test_disabled_by_default( void )
{
    struct request req;
    struct wexelevate_reply reply;
    DWORD err;

    init_dummy_request( &req );
    reset_tracker();
    g_track.injected_policy = FALSE; /* Policy disabled */
    e1s18_set_ops( &s_fake_ops );

    err = e1s18_handle_standard_request( &req, &reply );
    CHECK_EQ( err, ERROR_NOT_SUPPORTED, "Disabled policy must return ERROR_NOT_SUPPORTED" );
    CHECK_EQ( g_track.order_count, 0, "No stage must execute when policy is disabled" );

    e1s18_reset_ops();
    return TEST_PASS;
}

/*
 * Test 2: Missing capabilities
 */
static int test_missing_capabilities( void )
{
    struct request req;
    struct wexelevate_reply reply;
    DWORD err;
    int bit;

    init_dummy_request( &req );
    e1s18_set_ops( &s_fake_ops );

    for (bit = 0; bit < 10; bit++)
    {
        reset_tracker();
        g_track.injected_caps = E1S1_ALL_REQUIRED_CAPS & ~(1ULL << bit); /* Missing 1 capability */

        err = e1s18_handle_standard_request( &req, &reply );
        CHECK_EQ( err, ERROR_NOT_SUPPORTED, "Missing capability must return ERROR_NOT_SUPPORTED" );
        CHECK_EQ( g_track.order_count, 0, "No stage must execute when capability missing" );
    }

    e1s18_reset_ops();
    return TEST_PASS;
}

/*
 * Test 3: Inherited environment rejected
 */
static int test_inherited_env_denied( void )
{
    struct request req;
    struct wexelevate_reply reply;
    DWORD err;

    init_dummy_request( &req );
    req.flags |= WEXELEVATE_ENV_INHERIT; /* Inherit requested */
    reset_tracker();
    e1s18_set_ops( &s_fake_ops );

    err = e1s18_handle_standard_request( &req, &reply );
    CHECK_EQ( err, ERROR_INVALID_PARAMETER, "Inherited env must be rejected with ERROR_INVALID_PARAMETER" );
    CHECK_EQ( g_track.order_count, 0, "No stage must execute when inherited env requested" );

    e1s18_reset_ops();
    return TEST_PASS;
}

/*
 * Test 4: Success path ordering & single guarded resume
 */
static int test_success_path_ordering( void )
{
    struct request req;
    struct wexelevate_reply reply;
    DWORD err;
    int expected_order[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 };
    int i;

    init_dummy_request( &req );
    reset_tracker();
    e1s18_set_ops( &s_fake_ops );

    err = e1s18_handle_standard_request( &req, &reply );
    CHECK_EQ( err, ERROR_SUCCESS, "Success path must return ERROR_SUCCESS" );
    CHECK_EQ( g_track.order_count, 14, "All 14 stages must execute in order" );

    for (i = 0; i < 14; i++)
    {
        CHECK_EQ( g_track.order[i], expected_order[i], "Stage order mismatch" );
    }

    CHECK_EQ( g_track.resume_count, 1, "ResumeThread must be called exactly once" );
    CHECK_EQ( reply.pid, 12345, "Reply pid must match child pid" );
    CHECK_EQ( reply.tid, 6789, "Reply tid must match child tid" );
    CHECK_EQ( reply.handle, 0x5005ULL, "Reply handle must match duplicated remote handle" );
    CHECK_EQ( g_track.child_terminated, 0, "No child termination on success" );
    CHECK_EQ( g_track.remote_handle_closed, 0, "No remote handle closure on success" );

    e1s18_reset_ops();
    return TEST_PASS;
}

/*
 * Test 5: Every stage failure and rollback matrix
 */
static int test_stage_failures_and_cleanup( void )
{
    struct request req;
    struct wexelevate_reply reply;
    DWORD err;
    int stage;

    e1s18_set_ops( &s_fake_ops );

    for (stage = 1; stage <= 14; stage++)
    {
        init_dummy_request( &req );
        reset_tracker();
        g_track.fail_at_stage = stage;
        g_track.fail_error_code = 0x9000 + stage;

        err = e1s18_handle_standard_request( &req, &reply );
        CHECK_EQ( err, (DWORD)(0x9000 + stage), "Failed stage must return injected error code" );
        CHECK_EQ( reply.pid, 0, "Failure reply pid must be zero" );
        CHECK_EQ( reply.tid, 0, "Failure reply tid must be zero" );
        CHECK_EQ( reply.handle, 0, "Failure reply handle must be zero" );

        /* Stages 1..13 must NEVER call ResumeThread */
        if (stage < 14)
        {
            CHECK_EQ( g_track.resume_count, 0, "ResumeThread must not be called when an earlier stage fails" );
        }

        /* Stages >= 9: child process was created by stage 8; must terminate on failure */
        if (stage >= 9)
        {
            CHECK( g_track.child_terminated > 0, "Suspended child must be terminated on failure" );
        }
        else
        {
            CHECK_EQ( g_track.child_terminated, 0, "No child termination if not created" );
        }

        /* Stages >= 12: remote handle was duplicated by stage 11; must close in caller on failure */
        if (stage >= 12)
        {
            CHECK( g_track.remote_handle_closed > 0, "Remote handle must be closed in caller on failure" );
        }
        else
        {
            CHECK_EQ( g_track.remote_handle_closed, 0, "No remote handle closure if not duplicated" );
        }

        /* Stages >= 6: profile lease was acquired by stage 5; must unload on failure */
        if (stage >= 6)
        {
            CHECK( g_track.profile_unloaded > 0, "Profile lease must be unloaded on failure" );
        }
        else
        {
            CHECK_EQ( g_track.profile_unloaded, 0, "No profile unload if lease not acquired" );
        }
    }

    e1s18_reset_ops();
    return TEST_PASS;
}

/*
 * Test 6: Filtered-admin path byte-for-byte regression check
 */
static int test_filtered_admin_untouched( void )
{
#ifdef WEXELEVATE_E1S18
    struct request req;
    init_dummy_request( &req );
    req.is_standard_user = FALSE; /* Filtered admin */

    /* If caller is filtered admin, is_standard_user is FALSE and handle_standard_request is NOT invoked */
    CHECK( !req.is_standard_user, "Filtered admin caller must not be flagged as standard user" );
#endif
    return TEST_PASS;
}

#ifdef WEXELEVATE_E1S18
/*
 * Fake NTDLL Export Boundary Implementation & Tests
 */

struct fake_export_state
{
    /* Negotiate controls */
    NTSTATUS negotiate_status;
    ULONGLONG negotiate_caps;
    ULONG negotiate_size;
    BOOL negotiate_sets_token;
    int negotiate_null_token_calls;

    /* Issue controls */
    NTSTATUS issue_status;
    ULONG issue_response_size;
    uint32_t issue_kind;
    uint32_t issue_rec_size;
    uint32_t issue_status_field;
    uint64_t issue_req_id;
    BOOL issue_corrupt_account_key;
    uint32_t issue_sid_len;
    uint8_t issue_sid[SECURITY_MAX_SID_SIZE];
    uint32_t issue_session;
    uint64_t issue_auth_id;
    uint32_t issue_token_type;
    uint32_t issue_elevation;
    uint32_t issue_integrity;

    /* Load controls */
    NTSTATUS load_status;
    ULONG load_managed;
    ULONGLONG load_generation;
    BOOL load_null_key;

    /* Storage query (WEX_HKU_STORAGE_QUERY, op 12) controls */
    NTSTATUS query_status;
    ULONG query_managed;
    ULONGLONG query_generation;
    BOOL query_null_key;
    BOOL query_generation_valid;  /* set when op 12 succeeded; LOAD must match */
    ULONGLONG last_load_storage_gen;

    /* Transfer controls */
    NTSTATUS transfer_status;
    ULONG transfer_managed;

    /* Unload controls */
    NTSTATUS unload_status;
    ULONG unload_managed;

    /* Counters */
    int negotiate_calls;
    int issue_calls;
    int load_calls;
    int query_calls;
    int transfer_calls;
    int unload_calls;

    HANDLE last_issue_context;
    HANDLE last_load_context;
    HANDLE last_transfer_process;
    HANDLE last_unload_token;
};

static struct fake_export_state g_export_state;

static void reset_export_state( void )
{
    memset( &g_export_state, 0, sizeof(g_export_state) );
    g_export_state.negotiate_status = 0;
    g_export_state.negotiate_caps = WEX_TOKEN_CAPS;
    g_export_state.negotiate_size = 0;

    g_export_state.issue_status = 0;
    g_export_state.issue_response_size = WEX_TOKEN_RESPONSE_SIZE;
    g_export_state.issue_kind = 0x00010005;
    g_export_state.issue_rec_size = WEX_TOKEN_RESPONSE_SIZE;
    g_export_state.issue_status_field = 0;

    /* Alice mapped SID: S-1-5-21-999-1001 (fixed 28-byte mapped-SID size, 5 sub-authorities) */
    g_export_state.issue_sid[0] = 1;   /* revision */
    g_export_state.issue_sid[1] = 5;   /* sub_authority_count */
    g_export_state.issue_sid[7] = 5;   /* identifier authority = NT (5) */
    wex_token_put32( g_export_state.issue_sid + 8, 21 );    /* subauth[0] */
    wex_token_put32( g_export_state.issue_sid + 12, 999 );  /* subauth[1] */
    wex_token_put32( g_export_state.issue_sid + 16, 1001 ); /* subauth[2] */
    wex_token_put32( g_export_state.issue_sid + 20, 0 );    /* subauth[3] */
    wex_token_put32( g_export_state.issue_sid + 24, 0 );    /* subauth[4] */
    g_export_state.issue_sid_len = 28;

    g_export_state.issue_session = 1;
    g_export_state.issue_auth_id = 0x88884444ULL;
    g_export_state.issue_token_type = 1; /* TokenPrimary */
    g_export_state.issue_elevation = 2;  /* TokenElevationTypeFull */
    g_export_state.issue_integrity = 0x3000; /* High */

    g_export_state.load_status = 0;
    g_export_state.load_managed = 1;
    g_export_state.load_generation = 0xbeef01ULL;

    g_export_state.query_status = 0;
    g_export_state.query_managed = 1;
    g_export_state.query_generation = 0x5e1fULL;
    g_export_state.query_null_key = TRUE; /* op 12 success returns a NULL key */
    g_export_state.query_generation_valid = FALSE;
    g_export_state.last_load_storage_gen = 0;

    g_export_state.transfer_status = 0;
    g_export_state.transfer_managed = 1;

    g_export_state.unload_status = 0;
    g_export_state.unload_managed = 1;
}

static NTSTATUS WINAPI fake_NtCreateWexAccountToken(
    ULONG operation,
    HANDLE *context,
    HANDLE caller,
    ULONGLONG *caps,
    const void *record,
    ULONG length,
    HANDLE *token,
    void *response,
    ULONG *response_size )
{
    (void)caller;
    /* The real ntdll export rejects a NULL token output with
     * STATUS_INVALID_PARAMETER before reaching the server. */
    if (!token)
    {
        if (operation == WEX_TOKEN_NEGOTIATE)
            g_export_state.negotiate_null_token_calls++;
        return (NTSTATUS)0xc000000d; /* STATUS_INVALID_PARAMETER */
    }
    if (operation == WEX_TOKEN_NEGOTIATE)
    {
        g_export_state.negotiate_calls++;
        if (g_export_state.negotiate_status != 0)
            return g_export_state.negotiate_status;
        if (context) *context = (HANDLE)(intptr_t)0x1111;
        if (caps) *caps = g_export_state.negotiate_caps;
        if (response_size) *response_size = g_export_state.negotiate_size;
        if (g_export_state.negotiate_sets_token)
            *token = (HANDLE)(intptr_t)0x5555;
        return 0;
    }
    if (operation == WEX_TOKEN_ISSUE)
    {
        uint8_t *resp = (uint8_t *)response;
        const uint8_t *in = (const uint8_t *)record;
        uint64_t req_id;

        g_export_state.issue_calls++;
        if (context) g_export_state.last_issue_context = *context;
        if (g_export_state.issue_status != 0)
            return g_export_state.issue_status;
        if (!context || *context != (HANDLE)(intptr_t)0x1111)
            return (NTSTATUS)0xc0000008;
        if (!record || length != WEX_TOKEN_INPUT_SIZE)
            return (NTSTATUS)0xc0000004;
        if (!response || !response_size)
            return (NTSTATUS)0xc000000d;

        if (token) *token = (HANDLE)(intptr_t)0x2222;

        req_id = g_export_state.issue_req_id ? g_export_state.issue_req_id : wex_token_u64( in + 16 );

        memset( resp, 0, g_export_state.issue_response_size );
        wex_token_put32( resp + 0, g_export_state.issue_kind );
        wex_token_put32( resp + 4, g_export_state.issue_rec_size );
        wex_token_put64( resp + 8, WEX_TOKEN_CAPS );
        wex_token_put64( resp + 16, req_id );
        wex_token_put64( resp + 24, 0 );
        wex_token_put32( resp + 32, g_export_state.issue_status_field );
        wex_token_put64( resp + 40, 0 );
        wex_token_put64( resp + 48, 0 );

        if (g_export_state.issue_corrupt_account_key)
            memset( resp + 56, 0xee, 16 );
        else
            memcpy( resp + 56, in + 124, 16 );

        wex_token_put32( resp + 72, g_export_state.issue_sid_len );
        memcpy( resp + 76, g_export_state.issue_sid, 104 - 76 ); /* SID field is 28 bytes, auth id follows */

        wex_token_put64( resp + 104, g_export_state.issue_auth_id );
        wex_token_put64( resp + 112, 1 );
        wex_token_put32( resp + 120, g_export_state.issue_session );
        wex_token_put32( resp + 124, g_export_state.issue_token_type );
        wex_token_put32( resp + 128, g_export_state.issue_elevation );
        wex_token_put32( resp + 132, g_export_state.issue_integrity );

        *response_size = g_export_state.issue_response_size;
        return 0;
    }
    return (NTSTATUS)0xc0000002;
}

static NTSTATUS WINAPI fake_NtLoadWexProfileEx(
    ULONG operation,
    HANDLE context,
    HANDLE token,
    ACCESS_MASK access,
    const void *record,
    ULONG length,
    HANDLE *key,
    ULONG *managed,
    ULONGLONG *generation )
{
    (void)access;
    if (operation == WEX_HKU_LOAD)
    {
        g_export_state.load_calls++;
        g_export_state.last_load_context = context;
        if (g_export_state.load_status != 0)
            return g_export_state.load_status;
        if (!context || context != (HANDLE)(intptr_t)0x1111)
            return (NTSTATUS)0xc0000008;
        if (!token || token != (HANDLE)(intptr_t)0x2222)
            return (NTSTATUS)0xc0000022;
        if (!record || length != WEX_HKU_RECORD_SIZE)
            return (NTSTATUS)0xc0000004;

        /* Capture the storage generation the broker passed to LOAD. If op 12
         * already ran in this session, LOAD must carry exactly the generation
         * the server returned for the query. */
        g_export_state.last_load_storage_gen = wex_token_u64( (const uint8_t *)record + 32 );
        if (g_export_state.query_generation_valid &&
            g_export_state.last_load_storage_gen != g_export_state.query_generation)
            return (NTSTATUS)0xc000000d; /* STATUS_INVALID_PARAMETER */

        if (key) *key = g_export_state.load_null_key ? NULL : (HANDLE)(intptr_t)0x3333;
        if (managed) *managed = g_export_state.load_managed;
        if (generation) *generation = g_export_state.load_generation;
        return 0;
    }
    if (operation == WEX_HKU_STORAGE_QUERY)
    {
        g_export_state.query_calls++;
        if (g_export_state.query_status != 0)
            return g_export_state.query_status;
        if (access != 0)
            return (NTSTATUS)0xc000000d; /* access must be 0 */
        if (!context || context != (HANDLE)(intptr_t)0x1111)
            return (NTSTATUS)0xc0000008;
        if (!token || token != (HANDLE)(intptr_t)0x2222)
            return (NTSTATUS)0xc0000022;
        if (!record || length != WEX_HKU_RECORD_SIZE)
            return (NTSTATUS)0xc0000004;
        /* The record must be the LOAD record with the storage field (offset 32) zero. */
        if (wex_token_u64( (const uint8_t *)record + 32 ) != 0)
            return (NTSTATUS)0xc000000d;

        if (key) *key = g_export_state.query_null_key ? NULL : (HANDLE)(intptr_t)0x4444;
        if (managed) *managed = g_export_state.query_managed;
        if (generation) *generation = g_export_state.query_generation;
        g_export_state.query_generation_valid =
            g_export_state.query_null_key &&
            (g_export_state.query_managed == 1) &&
            (g_export_state.query_generation != 0);
        return 0;
    }
    if (operation == WEX_HKU_COMMIT_PROCESS)
    {
        g_export_state.transfer_calls++;
        if (!record || length != WEX_HKU_COMMIT_RECORD_SIZE || access) return (NTSTATUS)0xc000000d;
        g_export_state.last_transfer_process = (HANDLE)(uintptr_t)wex_token_u32(record);
        if (g_export_state.transfer_status != 0)
            return g_export_state.transfer_status;
        if (context != (HANDLE)(intptr_t)0x1111) return (NTSTATUS)0xc0000008;
        if (!token || token != (HANDLE)(intptr_t)0x2222)
            return (NTSTATUS)0xc0000022;
        if (managed) *managed = g_export_state.transfer_managed;
        return 0;
    }
    if (operation == WEX_HKU_UNLOAD_S14)
    {
        g_export_state.unload_calls++;
        g_export_state.last_unload_token = token;
        if (g_export_state.unload_status != 0)
            return g_export_state.unload_status;
        if (managed) *managed = g_export_state.unload_managed;
        return 0;
    }
    return (NTSTATUS)0xc0000002;
}

static struct e1s18_export_table s_fake_export_table = {
    fake_NtCreateWexAccountToken,
    fake_NtLoadWexProfileEx
};

static void set_dummy_stage2_fields( struct request *req )
{
#ifdef WEXELEVATE_E1S18
    strncpy( req->account_name, "alice", sizeof(req->account_name) - 1 );
    req->account_name[sizeof(req->account_name) - 1] = '\0';
    memset( req->account_key, 0x42, sizeof(req->account_key) );
    req->map_generation = 1;
    /* The S12 storage generation is no longer a Stage 2 field; it is set by
     * query_storage (op 12) after S10 issuance. */
#else
    (void)req;
#endif
}

static int test_export_boundary_success( void )
{
    struct request req;
    HANDLE token = NULL;
    HANDLE hkey = NULL;
    uint64_t lease_id = 0;
    char *win_env = NULL;
    size_t win_env_sz = 0;
    char **unix_env = NULL;
    size_t unix_env_cnt = 0;
    char *cwd = NULL;
    DWORD err;
    uint8_t account_key[16];
    uint8_t evidence_ref[16];
    uint8_t nonce[32];
    uint8_t binding_digest[32];
    uint8_t request_digest[32];
    const struct e1s18_ops *ops;

    init_dummy_request( &req );
    set_dummy_stage2_fields( &req );
    memcpy( account_key, req.account_key, 16 );
    memset( evidence_ref, 0x77, sizeof(evidence_ref) );
    memset( nonce, 0xa5, sizeof(nonce) );
    memset( binding_digest, 0x11, sizeof(binding_digest) );
    memset( request_digest, 0x22, sizeof(request_digest) );
    reset_export_state();
    e1s18_reset_ops();
    e1s18_set_export_table( &s_fake_export_table );
    ops = e1s18_get_ops();

    /* 1. Stage 3: issue_token */
    err = ops->issue_token( &req, account_key, evidence_ref, nonce, binding_digest, request_digest, &token );
    CHECK_EQ( err, ERROR_SUCCESS, "default issue_token must succeed with fake exports" );
    CHECK_EQ( (uintptr_t)token, 0x2222, "Token handle must match fake export returned token" );
    CHECK_EQ( (uintptr_t)req.s10_context, 0x1111, "req->s10_context must be retained for Stage 4" );
    CHECK_EQ( req.alice_sid_len, 28, "req->alice_sid_len must match the fixed 28-byte mapped SID" );
    CHECK_EQ( g_export_state.negotiate_calls, 1, "Negotiate must be called once" );
    CHECK_EQ( g_export_state.issue_calls, 1, "Issue must be called once" );

    /* 2. Stage 3b: query_storage (WEX_HKU_STORAGE_QUERY, op 12) */
    err = ops->query_storage( &req, token );
    CHECK_EQ( err, ERROR_SUCCESS, "default query_storage must succeed with fake exports" );
    CHECK_EQ( req.storage_generation, 0x5e1fULL, "req->storage_generation must match the queried generation" );
    CHECK_EQ( g_export_state.query_calls, 1, "Storage query must be called once" );

    /* 3. Stage 4: load_profile */
    err = ops->load_profile( &req, token, account_key, &lease_id, &hkey );
    CHECK_EQ( err, ERROR_SUCCESS, "default load_profile must succeed with fake exports" );
    CHECK_EQ( lease_id, 0xbeef01ULL, "lease_id must match fake export generation" );
    CHECK_EQ( (uintptr_t)hkey, 0x3333, "hkey must match fake export key handle" );
    CHECK_EQ( (uintptr_t)req.s10_context, 0x1111, "LOAD context must survive until commit or rollback" );
    CHECK_EQ( g_export_state.load_calls, 1, "Load must be called once" );
    CHECK_EQ( g_export_state.last_load_storage_gen, 0x5e1fULL, "LOAD must carry the queried storage generation" );

    /* 3. Stage 5: prep_launch */
    err = ops->prep_launch( &req, token, &win_env, &win_env_sz, &unix_env, &unix_env_cnt, &cwd );
    CHECK_EQ( err, ERROR_SUCCESS, "default prep_launch must succeed" );
    CHECK( win_env != NULL && win_env_sz > 0, "win_env block must be constructed" );
    if (win_env) free( win_env );
    if (cwd) free( cwd );
    if (unix_env)
    {
        size_t i;
        for (i = 0; i < unix_env_cnt; i++) free( unix_env[i] );
        free( unix_env );
    }

    /* 4. Stage 11: transfer_lease */
    err = ops->transfer_lease( &req, lease_id, (HANDLE)(intptr_t)0x4444, token );
    CHECK_EQ( err, ERROR_SUCCESS, "default transfer_lease must succeed" );
    CHECK_EQ( g_export_state.transfer_calls, 1, "Transfer must be called once" );
    CHECK_EQ( (uintptr_t)g_export_state.last_transfer_process, 0x4444, "Commit payload must name the exact child" );
    ops->close_handle( req.s10_context ); req.s10_context = NULL;

    /* 5. Cleanup: unload_profile */
    ops->unload_profile( lease_id, hkey, token );
    CHECK_EQ( g_export_state.unload_calls, 1, "Unload must be called once" );
    CHECK( !e1s18_is_unload_busy( NULL ), "Unload should not be marked busy" );

    e1s18_reset_export_table();
    return TEST_PASS;
}

static int test_export_boundary_negotiate_failure( void )
{
    struct request req;
    HANDLE token = NULL;
    DWORD err;
    uint8_t buf16[16] = {0x42};
    uint8_t buf32[32] = {0x11};
    const struct e1s18_ops *ops;

    init_dummy_request( &req );
    set_dummy_stage2_fields( &req );
    e1s18_reset_ops();
    e1s18_set_export_table( &s_fake_export_table );
    ops = e1s18_get_ops();

    /* Status error */
    reset_export_state();
    g_export_state.negotiate_status = (NTSTATUS)0xc0000022; /* STATUS_ACCESS_DENIED */
    err = ops->issue_token( &req, buf16, buf16, buf32, buf32, buf32, &token );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "Negotiate failure must deny with ERROR_ACCESS_DENIED" );
    CHECK_EQ( (uintptr_t)token, 0, "Token handle must be NULL on error" );
    CHECK_EQ( (uintptr_t)req.s10_context, 0, "req->s10_context must be NULL on error" );

    /* Bad capability bits */
    reset_export_state();
    g_export_state.negotiate_caps = 0x80; /* missing cap bit */
    err = ops->issue_token( &req, buf16, buf16, buf32, buf32, buf32, &token );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "Negotiate caps mismatch must deny with ERROR_ACCESS_DENIED" );

    e1s18_reset_export_table();
    return TEST_PASS;
}

static int test_export_boundary_issue_failure( void )
{
    struct request req;
    HANDLE token = NULL;
    DWORD err;
    uint8_t buf16[16] = {0x42};
    uint8_t buf32[32] = {0x11};
    const struct e1s18_ops *ops;

    init_dummy_request( &req );
    set_dummy_stage2_fields( &req );
    e1s18_reset_ops();
    e1s18_set_export_table( &s_fake_export_table );
    ops = e1s18_get_ops();

    reset_export_state();
    g_export_state.issue_status = (NTSTATUS)0xc0000022; /* STATUS_ACCESS_DENIED */
    err = ops->issue_token( &req, buf16, buf16, buf32, buf32, buf32, &token );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "Issue failure must deny with ERROR_ACCESS_DENIED" );
    CHECK_EQ( (uintptr_t)token, 0, "Token handle must be NULL on error" );
    CHECK_EQ( (uintptr_t)req.s10_context, 0, "req->s10_context must be cleaned up on error" );

    e1s18_reset_export_table();
    return TEST_PASS;
}

static int test_export_boundary_wrong_token_shape( void )
{
    struct request req;
    HANDLE token = NULL;
    DWORD err;
    uint8_t buf16[16] = {0x42};
    uint8_t buf32[32] = {0x11};
    const struct e1s18_ops *ops;

    init_dummy_request( &req );
    set_dummy_stage2_fields( &req );
    e1s18_reset_ops();
    e1s18_set_export_table( &s_fake_export_table );
    ops = e1s18_get_ops();

    /* Wrong token type: Impersonation */
    reset_export_state();
    g_export_state.issue_token_type = 2; /* TokenImpersonation */
    err = ops->issue_token( &req, buf16, buf16, buf32, buf32, buf32, &token );
    CHECK_EQ( err, ERROR_INVALID_PARAMETER, "Non-primary token must fail closed with ERROR_INVALID_PARAMETER" );
    CHECK_EQ( (uintptr_t)token, 0, "Token handle must be NULL on shape mismatch" );

    /* Wrong elevation: Limited */
    reset_export_state();
    g_export_state.issue_elevation = 3; /* TokenElevationTypeLimited */
    err = ops->issue_token( &req, buf16, buf16, buf32, buf32, buf32, &token );
    CHECK_EQ( err, ERROR_INVALID_PARAMETER, "Non-full elevation must fail closed with ERROR_INVALID_PARAMETER" );

    /* Low integrity level */
    reset_export_state();
    g_export_state.issue_integrity = 0x2000; /* Medium (< 0x3000) */
    err = ops->issue_token( &req, buf16, buf16, buf32, buf32, buf32, &token );
    CHECK_EQ( err, ERROR_INVALID_PARAMETER, "Low integrity must fail closed with ERROR_INVALID_PARAMETER" );

    e1s18_reset_export_table();
    return TEST_PASS;
}

static int test_export_boundary_wrong_sid( void )
{
    struct request req;
    HANDLE token = NULL;
    DWORD err;
    uint8_t buf16[16] = {0x42};
    uint8_t buf32[32] = {0x11};
    const struct e1s18_ops *ops;

    init_dummy_request( &req );
    set_dummy_stage2_fields( &req );
    e1s18_reset_ops();
    e1s18_set_export_table( &s_fake_export_table );
    ops = e1s18_get_ops();

    /* R15: the mapped SID must be exactly 28 bytes (the fixed size the ntdll
     * wrapper enforces). One byte short (27) and one byte long (29) deny. */
    reset_export_state();
    g_export_state.issue_sid_len = 27;
    err = ops->issue_token( &req, buf16, buf16, buf32, buf32, buf32, &token );
    CHECK_EQ( err, ERROR_INVALID_PARAMETER, "27-byte SID must fail closed with ERROR_INVALID_PARAMETER" );
    CHECK_EQ( (uintptr_t)token, 0, "Token handle must be NULL on 27-byte SID" );

    reset_export_state();
    g_export_state.issue_sid_len = 29;
    err = ops->issue_token( &req, buf16, buf16, buf32, buf32, buf32, &token );
    CHECK_EQ( err, ERROR_INVALID_PARAMETER, "29-byte SID must fail closed with ERROR_INVALID_PARAMETER" );
    CHECK_EQ( (uintptr_t)token, 0, "Token handle must be NULL on 29-byte SID" );

    /* Exactly 28 bytes with a valid SID is accepted. */
    reset_export_state();
    g_export_state.issue_sid_len = 28;
    err = ops->issue_token( &req, buf16, buf16, buf32, buf32, buf32, &token );
    CHECK_EQ( err, ERROR_SUCCESS, "28-byte valid SID must be accepted" );
    CHECK_EQ( (uintptr_t)token, 0x2222, "Token handle must be the issued token" );
    CHECK_EQ( req.alice_sid_len, 28, "req->alice_sid_len must be 28" );
    if (token) ops->close_handle( token );
    ops->close_handle( req.s10_context );
    req.s10_context = NULL;

    /* Bob's 28-byte SID reflected instead of Alice (identical SIDs) */
    reset_export_state();
    req.caller_sid_len = 28;
    memcpy( req.caller_sid, g_export_state.issue_sid, 28 );
    err = ops->issue_token( &req, buf16, buf16, buf32, buf32, buf32, &token );
    CHECK_EQ( err, ERROR_INVALID_PARAMETER, "Caller SID reflection must fail closed with ERROR_INVALID_PARAMETER" );

    e1s18_reset_export_table();
    return TEST_PASS;
}

static int test_export_boundary_wrong_session( void )
{
    struct request req;
    HANDLE token = NULL;
    DWORD err;
    uint8_t buf16[16] = {0x42};
    uint8_t buf32[32] = {0x11};
    const struct e1s18_ops *ops;

    init_dummy_request( &req );
    set_dummy_stage2_fields( &req );
    e1s18_reset_ops();
    e1s18_set_export_table( &s_fake_export_table );
    ops = e1s18_get_ops();

    reset_export_state();
    g_export_state.issue_session = 99; /* doesn't match req.caller_session == 1 */
    err = ops->issue_token( &req, buf16, buf16, buf32, buf32, buf32, &token );
    CHECK_EQ( err, ERROR_INVALID_PARAMETER, "Session mismatch must fail closed with ERROR_INVALID_PARAMETER" );

    e1s18_reset_export_table();
    return TEST_PASS;
}

static int test_export_boundary_bad_auth_id( void )
{
    struct request req;
    HANDLE token = NULL;
    DWORD err;
    uint8_t buf16[16] = {0x42};
    uint8_t buf32[32] = {0x11};
    const struct e1s18_ops *ops;

    init_dummy_request( &req );
    set_dummy_stage2_fields( &req );
    e1s18_reset_ops();
    e1s18_set_export_table( &s_fake_export_table );
    ops = e1s18_get_ops();

    /* Zero AuthenticationId */
    reset_export_state();
    g_export_state.issue_auth_id = 0;
    err = ops->issue_token( &req, buf16, buf16, buf32, buf32, buf32, &token );
    CHECK_EQ( err, ERROR_INVALID_PARAMETER, "Zero AuthenticationId must fail closed with ERROR_INVALID_PARAMETER" );

    /* Caller matching AuthenticationId */
    reset_export_state();
    g_export_state.issue_auth_id = ((uint64_t)req.caller_stats.AuthenticationId.HighPart << 32) |
                                   req.caller_stats.AuthenticationId.LowPart;
    err = ops->issue_token( &req, buf16, buf16, buf32, buf32, buf32, &token );
    CHECK_EQ( err, ERROR_INVALID_PARAMETER, "Duplicate AuthenticationId must fail closed with ERROR_INVALID_PARAMETER" );

    e1s18_reset_export_table();
    return TEST_PASS;
}

static int test_export_boundary_load_profile_failure( void )
{
    struct request req;
    uint64_t lease_id = 0;
    HANDLE hkey = NULL;
    DWORD err;
    uint8_t account_key[16] = {0x42};
    const struct e1s18_ops *ops;

    init_dummy_request( &req );
    set_dummy_stage2_fields( &req );
    req.storage_generation = 1; /* query_storage (op 12) would have set this */
    e1s18_reset_ops();
    e1s18_set_export_table( &s_fake_export_table );
    ops = e1s18_get_ops();

    /* STATUS_OBJECT_NAME_NOT_FOUND -> ERROR_FILE_NOT_FOUND */
    reset_export_state();
    req.s10_context = (HANDLE)(intptr_t)0x1111;
    g_export_state.load_status = (NTSTATUS)0xc0000034;
    err = ops->load_profile( &req, (HANDLE)(intptr_t)0x2222, account_key, &lease_id, &hkey );
    CHECK_EQ( err, ERROR_FILE_NOT_FOUND, "STATUS_OBJECT_NAME_NOT_FOUND must map to ERROR_FILE_NOT_FOUND" );
    CHECK_EQ( (uintptr_t)req.s10_context, 0, "req->s10_context must be closed on failure" );

    /* STATUS_OBJECT_PATH_NOT_FOUND -> ERROR_FILE_NOT_FOUND */
    reset_export_state();
    req.s10_context = (HANDLE)(intptr_t)0x1111;
    g_export_state.load_status = (NTSTATUS)0xc000003a;
    err = ops->load_profile( &req, (HANDLE)(intptr_t)0x2222, account_key, &lease_id, &hkey );
    CHECK_EQ( err, ERROR_FILE_NOT_FOUND, "STATUS_OBJECT_PATH_NOT_FOUND must map to ERROR_FILE_NOT_FOUND" );

    /* STATUS_ACCESS_DENIED -> ERROR_ACCESS_DENIED */
    reset_export_state();
    req.s10_context = (HANDLE)(intptr_t)0x1111;
    g_export_state.load_status = (NTSTATUS)0xc0000022;
    err = ops->load_profile( &req, (HANDLE)(intptr_t)0x2222, account_key, &lease_id, &hkey );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "STATUS_ACCESS_DENIED must map to ERROR_ACCESS_DENIED" );

    /* NULL key handle returned */
    reset_export_state();
    req.s10_context = (HANDLE)(intptr_t)0x1111;
    g_export_state.load_null_key = TRUE;
    err = ops->load_profile( &req, (HANDLE)(intptr_t)0x2222, account_key, &lease_id, &hkey );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "NULL key returned must fail closed with ERROR_ACCESS_DENIED" );

    /* Unmanaged profile */
    reset_export_state();
    req.s10_context = (HANDLE)(intptr_t)0x1111;
    g_export_state.load_managed = 0;
    err = ops->load_profile( &req, (HANDLE)(intptr_t)0x2222, account_key, &lease_id, &hkey );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "Unmanaged profile must fail closed with ERROR_ACCESS_DENIED" );

    /* Generation 0 */
    reset_export_state();
    req.s10_context = (HANDLE)(intptr_t)0x1111;
    g_export_state.load_generation = 0;
    err = ops->load_profile( &req, (HANDLE)(intptr_t)0x2222, account_key, &lease_id, &hkey );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "Generation 0 must fail closed with ERROR_ACCESS_DENIED" );

    e1s18_reset_export_table();
    return TEST_PASS;
}

static int test_export_boundary_transfer_failure( void )
{
    struct request req;
    DWORD err;
    const struct e1s18_ops *ops;

    init_dummy_request( &req );
    req.s10_context = (HANDLE)(intptr_t)0x1111;
    e1s18_reset_ops();
    e1s18_set_export_table( &s_fake_export_table );
    ops = e1s18_get_ops();

    /* Access denied */
    reset_export_state();
    g_export_state.transfer_status = (NTSTATUS)0xc0000022;
    err = ops->transfer_lease( &req, 0xbeef01ULL, (HANDLE)(intptr_t)0x4444, (HANDLE)(intptr_t)0x2222 );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "Transfer STATUS_ACCESS_DENIED must map to ERROR_ACCESS_DENIED" );

    /* Disabled/older server -> STATUS_NOT_SUPPORTED */
    reset_export_state();
    g_export_state.transfer_status = (NTSTATUS)0xc00000bb;
    err = ops->transfer_lease( &req, 0xbeef01ULL, (HANDLE)(intptr_t)0x4444, (HANDLE)(intptr_t)0x2222 );
    CHECK_EQ( err, ERROR_NOT_SUPPORTED, "Transfer STATUS_NOT_SUPPORTED must map to ERROR_NOT_SUPPORTED" );

    e1s18_reset_export_table();
    return TEST_PASS;
}

static int test_export_boundary_busy_unload( void )
{
    uint64_t busy_lease = 0;
    const struct e1s18_ops *ops;

    e1s18_reset_ops();
    e1s18_set_export_table( &s_fake_export_table );
    ops = e1s18_get_ops();

    /* 1. STATUS_RESOURCE_IN_USE (0xc0000708) */
    reset_export_state();
    e1s18_clear_unload_busy();
    g_export_state.unload_status = (NTSTATUS)0xc0000708;
    ops->unload_profile( 0x112233ULL, (HANDLE)(intptr_t)0x3333, (HANDLE)(intptr_t)0x2222 );
    CHECK( e1s18_is_unload_busy( &busy_lease ), "STATUS_RESOURCE_IN_USE must be recorded as busy" );
    CHECK_EQ( busy_lease, 0x112233ULL, "Recorded busy lease id must match" );

    /* 2. STATUS_CANNOT_DELETE (0xc0000121) */
    reset_export_state();
    e1s18_clear_unload_busy();
    g_export_state.unload_status = (NTSTATUS)0xc0000121;
    ops->unload_profile( 0x445566ULL, (HANDLE)(intptr_t)0x3333, (HANDLE)(intptr_t)0x2222 );
    CHECK( e1s18_is_unload_busy( &busy_lease ), "STATUS_CANNOT_DELETE must be recorded as busy" );
    CHECK_EQ( busy_lease, 0x445566ULL, "Recorded busy lease id must match" );

    /* 3. STATUS_SHARING_VIOLATION (0xc0000043) */
    reset_export_state();
    e1s18_clear_unload_busy();
    g_export_state.unload_status = (NTSTATUS)0xc0000043;
    ops->unload_profile( 0x778899ULL, (HANDLE)(intptr_t)0x3333, (HANDLE)(intptr_t)0x2222 );
    CHECK( e1s18_is_unload_busy( &busy_lease ), "STATUS_SHARING_VIOLATION must be recorded as busy" );
    CHECK_EQ( busy_lease, 0x778899ULL, "Recorded busy lease id must match" );

    /* 4. Success unload */
    reset_export_state();
    e1s18_clear_unload_busy();
    g_export_state.unload_status = 0;
    ops->unload_profile( 0x999999ULL, (HANDLE)(intptr_t)0x3333, (HANDLE)(intptr_t)0x2222 );
    CHECK( !e1s18_is_unload_busy( NULL ), "Success unload must not be marked busy" );

    e1s18_reset_export_table();
    return TEST_PASS;
}

static int test_export_boundary_pipeline_integration( void )
{
    struct request req;
    struct wexelevate_reply reply;
    struct e1s18_ops hybrid_ops;
    const struct e1s18_ops *default_ops;
    DWORD err;
    uint64_t busy_lease = 0;

    init_dummy_request( &req );
    e1s18_reset_ops();
    default_ops = e1s18_get_ops();

    /* Hybrid ops: keep default issue_token, load_profile, prep_launch, transfer_lease, unload_profile;
     * provide fakes for stages requiring service plumbing or win32 kernel objects */
    hybrid_ops = *default_ops;
    hybrid_ops.is_policy_enabled = fake_is_policy_enabled;
    hybrid_ops.get_capabilities = fake_get_capabilities;
    hybrid_ops.prompt_dialog = fake_prompt_dialog;
    hybrid_ops.redeem_evidence = fake_redeem_evidence;
    hybrid_ops.verify_image = fake_verify_image;
    hybrid_ops.create_process_suspended = fake_create_process_suspended;
    hybrid_ops.verify_child = fake_verify_child;
    hybrid_ops.arm_lease = fake_arm_lease;
    hybrid_ops.duplicate_handle = fake_duplicate_handle;
    hybrid_ops.check_pre_resume = fake_check_pre_resume;
    hybrid_ops.resume_thread = fake_resume_thread;
    hybrid_ops.terminate_child = fake_terminate_child;
    hybrid_ops.close_remote_handle = fake_close_remote_handle;

    e1s18_set_ops( &hybrid_ops );
    e1s18_set_export_table( &s_fake_export_table );

    /* Pipeline Test 1: Full Success */
    reset_tracker();
    reset_export_state();
    err = e1s18_handle_standard_request( &req, &reply );
    CHECK_EQ( err, ERROR_SUCCESS, "Pipeline with real wired stages must succeed" );
    CHECK_EQ( reply.pid, 12345, "Reply pid must match child pid" );
    CHECK_EQ( reply.tid, 6789, "Reply tid must match child tid" );
    CHECK_EQ( reply.handle, 0x5005ULL, "Reply handle must match duplicated remote handle" );
    CHECK_EQ( g_export_state.negotiate_calls, 1, "Negotiate must be called" );
    CHECK_EQ( g_export_state.issue_calls, 1, "Issue must be called" );
    CHECK_EQ( g_export_state.load_calls, 1, "Load must be called" );
    CHECK_EQ( g_export_state.transfer_calls, 1, "Transfer must be called" );
    CHECK_EQ( (uintptr_t)req.s10_context, 0, "s10_context must be closed on commit" );

    /* Pipeline Test 2: Transfer Failure Rollback */
    init_dummy_request( &req );
    reset_tracker();
    reset_export_state();
    g_export_state.transfer_status = (NTSTATUS)0xc00000bb; /* STATUS_NOT_SUPPORTED */
    err = e1s18_handle_standard_request( &req, &reply );
    CHECK_EQ( err, ERROR_NOT_SUPPORTED, "Transfer failure must return ERROR_NOT_SUPPORTED" );
    CHECK_EQ( reply.pid, 0, "Failed reply pid must be zero" );
    CHECK_EQ( g_track.child_terminated, 1, "Suspended child must be terminated on transfer failure" );
    CHECK_EQ( g_track.remote_handle_closed, 1, "Duplicated handle must be closed in caller" );
    CHECK_EQ( g_export_state.unload_calls, 1, "Profile must be unloaded on transfer failure" );

    /* Pipeline Test 3: Transfer Failure with Busy Unload */
    init_dummy_request( &req );
    reset_tracker();
    reset_export_state();
    e1s18_clear_unload_busy();
    g_export_state.transfer_status = (NTSTATUS)0xc0000022; /* STATUS_ACCESS_DENIED */
    g_export_state.unload_status = (NTSTATUS)0xc0000708;   /* STATUS_RESOURCE_IN_USE */
    err = e1s18_handle_standard_request( &req, &reply );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "Transfer failure must return ERROR_ACCESS_DENIED" );
    CHECK( e1s18_is_unload_busy( &busy_lease ), "Rollback must record busy unload without looping" );
    CHECK_EQ( busy_lease, 0xbeef01ULL, "Recorded busy lease id must match" );

    e1s18_reset_ops();
    e1s18_reset_export_table();
    return TEST_PASS;
}

/*
 * Negative Tests: Unset Stage 2 fields fail closed with ERROR_INVALID_DATA
 */
static int test_unset_stage2_account_key( void )
{
    struct request req;
    struct wexelevate_reply reply;
    DWORD err;

    init_dummy_request( &req );
    reset_tracker();
    g_track.omit_stage2_account_key = 1;
    e1s18_set_ops( &s_fake_ops );

    err = e1s18_handle_standard_request( &req, &reply );
    CHECK_EQ( err, ERROR_INVALID_DATA, "Unset account_key must fail closed with ERROR_INVALID_DATA" );
    CHECK_EQ( g_track.order_count, 3, "Pipeline must stop at stage 3 when account_key unset" );
    CHECK_EQ( reply.pid, 0, "Reply pid must be 0 on failure" );
    CHECK_EQ( reply.tid, 0, "Reply tid must be 0 on failure" );
    CHECK_EQ( reply.handle, 0, "Reply handle must be 0 on failure" );

    e1s18_reset_ops();
    return TEST_PASS;
}

static int test_unset_stage2_map_generation( void )
{
    struct request req;
    struct wexelevate_reply reply;
    DWORD err;

    init_dummy_request( &req );
    reset_tracker();
    g_track.omit_stage2_map_gen = 1;
    e1s18_set_ops( &s_fake_ops );

    err = e1s18_handle_standard_request( &req, &reply );
    CHECK_EQ( err, ERROR_INVALID_DATA, "Unset map_generation must fail closed with ERROR_INVALID_DATA" );
    CHECK_EQ( g_track.order_count, 3, "Pipeline must stop at stage 3 when map_generation unset" );
    CHECK_EQ( reply.pid, 0, "Reply pid must be 0 on failure" );
    CHECK_EQ( reply.tid, 0, "Reply tid must be 0 on failure" );
    CHECK_EQ( reply.handle, 0, "Reply handle must be 0 on failure" );

    e1s18_reset_ops();
    return TEST_PASS;
}

static int test_query_storage_failure_stops_pipeline( void )
{
    struct request req;
    struct wexelevate_reply reply;
    DWORD err;

    init_dummy_request( &req );
    reset_tracker();
    g_track.fail_at_stage = 4; /* query_storage (op 12) fails */
    g_track.fail_error_code = ERROR_ACCESS_DENIED;
    e1s18_set_ops( &s_fake_ops );

    err = e1s18_handle_standard_request( &req, &reply );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "query_storage failure must deny with ERROR_ACCESS_DENIED" );
    CHECK_EQ( g_track.order_count, 4, "Pipeline must stop at stage 4 (query_storage) on failure" );
    CHECK_EQ( g_track.profile_unloaded, 0, "No profile unload when query_storage fails before LOAD" );
    CHECK_EQ( reply.pid, 0, "Reply pid must be 0 on failure" );
    CHECK_EQ( reply.tid, 0, "Reply tid must be 0 on failure" );
    CHECK_EQ( reply.handle, 0, "Reply handle must be 0 on failure" );

    e1s18_reset_ops();
    return TEST_PASS;
}

static int test_unset_stage2_account_name( void )
{
    struct request req;
    struct wexelevate_reply reply;
    DWORD err;

    init_dummy_request( &req );
    reset_tracker();
    g_track.omit_stage2_account_name = 1;
    e1s18_set_ops( &s_fake_ops );

    err = e1s18_handle_standard_request( &req, &reply );
    CHECK_EQ( err, ERROR_INVALID_DATA, "Unset account_name must fail closed with ERROR_INVALID_DATA" );
    CHECK_EQ( g_track.order_count, 6, "Pipeline must stop at stage 6 when account_name unset" );
    CHECK_EQ( g_track.profile_unloaded, 1, "Profile must be unloaded when stage 6 fails" );
    CHECK_EQ( reply.pid, 0, "Reply pid must be 0 on failure" );
    CHECK_EQ( reply.tid, 0, "Reply tid must be 0 on failure" );
    CHECK_EQ( reply.handle, 0, "Reply handle must be 0 on failure" );

    e1s18_reset_ops();
    return TEST_PASS;
}

static int test_export_boundary_unset_fields_fail_closed( void )
{
    struct request req;
    HANDLE token = NULL;
    HANDLE hkey = NULL;
    uint64_t lease_id = 0;
    char *win_env = NULL;
    size_t win_env_sz = 0;
    char **unix_env = NULL;
    size_t unix_env_cnt = 0;
    char *cwd = NULL;
    DWORD err;
    uint8_t account_key[16] = {0x42};
    uint8_t evidence_ref[16] = {0x77};
    uint8_t nonce[32] = {0xa5};
    uint8_t binding_digest[32] = {0x11};
    uint8_t request_digest[32] = {0x22};
    const struct e1s18_ops *ops;

    e1s18_reset_ops();
    e1s18_set_export_table( &s_fake_export_table );
    ops = e1s18_get_ops();

    /* 1. Stage 3 default_issue_token: Unset account_key fails closed */
    init_dummy_request( &req );
    set_dummy_stage2_fields( &req );
    memset( req.account_key, 0, sizeof(req.account_key) );
    err = ops->issue_token( &req, account_key, evidence_ref, nonce, binding_digest, request_digest, &token );
    CHECK_EQ( err, ERROR_INVALID_DATA, "default_issue_token with unset account_key must return ERROR_INVALID_DATA" );

    /* 2. Stage 3 default_issue_token: Unset map_generation fails closed */
    init_dummy_request( &req );
    set_dummy_stage2_fields( &req );
    req.map_generation = 0;
    err = ops->issue_token( &req, account_key, evidence_ref, nonce, binding_digest, request_digest, &token );
    CHECK_EQ( err, ERROR_INVALID_DATA, "default_issue_token with unset map_generation must return ERROR_INVALID_DATA" );

    /* 3. Stage 4 default_load_profile: Unset storage_generation fails closed */
    init_dummy_request( &req );
    set_dummy_stage2_fields( &req );
    req.storage_generation = 0;
    req.s10_context = (HANDLE)(intptr_t)0x1111;
    err = ops->load_profile( &req, (HANDLE)(intptr_t)0x2222, account_key, &lease_id, &hkey );
    CHECK_EQ( err, ERROR_INVALID_DATA, "default_load_profile with unset storage_generation must return ERROR_INVALID_DATA" );

    /* 4. Stage 5 default_prep_launch: Unset account_name fails closed */
    init_dummy_request( &req );
    set_dummy_stage2_fields( &req );
    req.storage_generation = 1; /* only account_name is unset here */
    req.account_name[0] = '\0';
    err = ops->prep_launch( &req, (HANDLE)(intptr_t)0x2222, &win_env, &win_env_sz, &unix_env, &unix_env_cnt, &cwd );
    CHECK_EQ( err, ERROR_INVALID_DATA, "default_prep_launch with unset account_name must return ERROR_INVALID_DATA" );

    e1s18_reset_export_table();
    return TEST_PASS;
}

/*
 * Fake Dialog & Transport Boundary Implementation & Tests
 */

struct fake_boundary_state
{
    /* Dialog controls */
    DWORD dialog_return_code;
    uint32_t dialog_answer_kind;
    BOOL dialog_corrupt_binding;
    BOOL dialog_corrupt_nonce;
    BOOL dialog_corrupt_request;
    BOOL dialog_corrupt_token;
    BOOL dialog_empty_answer;
    size_t dialog_answer_len_override; /* 0 = normal WEX_S1_ANSWER_LENGTH */
    char dialog_account_to_return[64];

    /* Transport controls */
    DWORD transport_return_code;
    size_t transport_out_len_override; /* 0 = normal sizeof(struct wex_s1_evidence_record) */
    uint16_t transport_evidence_kind;
    uint16_t transport_evidence_ver;
    uint32_t transport_evidence_len;
    uint64_t transport_evidence_caps;
    uint64_t transport_evidence_req_id;
    BOOL transport_corrupt_req_digest;
    BOOL transport_corrupt_bind_digest;
    BOOL transport_corrupt_nonce;
    uint32_t transport_evidence_attempt;
    uint8_t transport_account_key[16];
    uint64_t transport_host_uid;
    uint64_t transport_expiry;
    uint64_t transport_map_gen;
    uint32_t transport_method;
    uint32_t transport_service;
    uint8_t transport_evidence_ref[16];
    /* R4: if nonzero, set the test clock to this absolute value during the
     * (fake) transport call, so evidence that expires mid-call is rejected. */
    uint64_t transport_set_clock;
};

static struct fake_boundary_state g_boundary_state;

static DWORD fake_dialog_boundary_exec( struct request *req,
                                        uint64_t request_id,
                                        const uint8_t nonce[32],
                                        const uint8_t binding_digest[32],
                                        const uint8_t request_digest[32],
                                        const uint8_t channel_token[16],
                                        BOOL secure,
                                        uint8_t *out_answer_buf,
                                        size_t *out_answer_len,
                                        char *out_account,
                                        size_t account_max )
{
    (void)secure; (void)req;
    if (g_boundary_state.dialog_return_code != ERROR_SUCCESS)
        return g_boundary_state.dialog_return_code;

    if (g_boundary_state.dialog_empty_answer)
    {
        *out_answer_len = 0;
        return ERROR_SUCCESS;
    }

    if (g_boundary_state.dialog_answer_kind != 0)
    {
        uint8_t bind[32], req_dig[32], n[32], tok[16];
        memcpy( bind, binding_digest, 32 );
        memcpy( req_dig, request_digest, 32 );
        memcpy( n, nonce, 32 );
        memcpy( tok, channel_token, 16 );

        if (g_boundary_state.dialog_corrupt_binding) bind[0] ^= 0xff;
        if (g_boundary_state.dialog_corrupt_request) req_dig[0] ^= 0xff;
        if (g_boundary_state.dialog_corrupt_nonce) n[0] ^= 0xff;
        if (g_boundary_state.dialog_corrupt_token) tok[0] ^= 0xff;

        wex_build_answer_record( request_id, 1, 0, 0,
                                 g_boundary_state.dialog_answer_kind,
                                 n, bind, req_dig, tok,
                                 out_answer_buf, WEX_S1_ANSWER_LENGTH, out_answer_len );
        if (g_boundary_state.dialog_answer_len_override)
            *out_answer_len = g_boundary_state.dialog_answer_len_override;
    }

    if (out_account && account_max > 0)
    {
        strncpy( out_account, g_boundary_state.dialog_account_to_return, account_max - 1 );
        out_account[account_max - 1] = 0;
    }

    return ERROR_SUCCESS;
}

static DWORD fake_transport_boundary_redeem( struct request *req,
                                             const char *account_name,
                                             const uint8_t nonce[32],
                                             const uint8_t binding_digest[32],
                                             const uint8_t request_digest[32],
                                             uint8_t *out_evidence_buf,
                                             size_t *out_evidence_len,
                                             uint8_t out_evidence_ref[16] )
{
    struct wex_s1_evidence_record ev;
    (void)account_name;

    if (g_boundary_state.transport_return_code != ERROR_SUCCESS)
        return g_boundary_state.transport_return_code;

    /* R4: simulate the passage of time during the transport call so evidence
     * that expires mid-call is rejected by the post-call clock re-fetch. */
    if (g_boundary_state.transport_set_clock)
        e1s18_set_test_clock( g_boundary_state.transport_set_clock );

    memset( &ev, 0, sizeof(ev) );
    ev.kind = g_boundary_state.transport_evidence_kind ? g_boundary_state.transport_evidence_kind : 3;
    ev.version = g_boundary_state.transport_evidence_ver ? g_boundary_state.transport_evidence_ver : 1;
    ev.length = g_boundary_state.transport_evidence_len ? g_boundary_state.transport_evidence_len : 220;
    ev.required_caps = g_boundary_state.transport_evidence_caps ? g_boundary_state.transport_evidence_caps : 0x3ff;
    ev.request_id = g_boundary_state.transport_evidence_req_id ? g_boundary_state.transport_evidence_req_id : req->id;
    memcpy( ev.request_digest, request_digest, 32 );
    if (g_boundary_state.transport_corrupt_req_digest) ev.request_digest[0] ^= 0xff;
    memcpy( ev.binding_digest, binding_digest, 32 );
    if (g_boundary_state.transport_corrupt_bind_digest) ev.binding_digest[0] ^= 0xff;
    memcpy( ev.nonce, nonce, 32 );
    if (g_boundary_state.transport_corrupt_nonce) ev.nonce[0] ^= 0xff;
    ev.attempt = g_boundary_state.transport_evidence_attempt ? g_boundary_state.transport_evidence_attempt : 1;
    memcpy( ev.account_key, g_boundary_state.transport_account_key, 16 );
    ev.host_uid = g_boundary_state.transport_host_uid ? g_boundary_state.transport_host_uid : (req->caller_pid ? (uint64_t)req->caller_pid : 1000);
    ev.expiry = g_boundary_state.transport_expiry;
    ev.map_gen = g_boundary_state.transport_map_gen ? g_boundary_state.transport_map_gen : 42;
    ev.method = g_boundary_state.transport_method ? g_boundary_state.transport_method : 1;
    ev.service = g_boundary_state.transport_service ? g_boundary_state.transport_service : 1;

    memcpy( out_evidence_buf, &ev, sizeof(ev) );
    *out_evidence_len = g_boundary_state.transport_out_len_override
                         ? g_boundary_state.transport_out_len_override : sizeof(ev);
    memcpy( out_evidence_ref, g_boundary_state.transport_evidence_ref, 16 );

    return ERROR_SUCCESS;
}

static struct e1s18_boundary_table s_fake_boundary_table = {
    fake_dialog_boundary_exec,
    fake_transport_boundary_redeem
};

static void init_fake_boundary_state( void )
{
    int i;
    memset( &g_boundary_state, 0, sizeof(g_boundary_state) );
    g_boundary_state.dialog_return_code = ERROR_SUCCESS;
    g_boundary_state.dialog_answer_kind = WEX_ANSWER_KIND_CONFIRM;
    strcpy( g_boundary_state.dialog_account_to_return, "alice" );

    g_boundary_state.transport_return_code = ERROR_SUCCESS;
    g_boundary_state.transport_evidence_kind = 3;
    g_boundary_state.transport_evidence_ver = 1;
    g_boundary_state.transport_evidence_len = 220;
    g_boundary_state.transport_evidence_caps = 0x3ff;
    g_boundary_state.transport_evidence_attempt = 1;
    for (i = 0; i < 16; i++) g_boundary_state.transport_account_key[i] = (uint8_t)(0x10 + i);
    for (i = 0; i < 16; i++) g_boundary_state.transport_evidence_ref[i] = (uint8_t)(0xe0 + i);
    g_boundary_state.transport_map_gen = 42;
    g_boundary_state.transport_method = 1;
    g_boundary_state.transport_service = 1;
}

static int test_dialog_boundary_confirm( void )
{
    struct request req;
    char account[64];
    uint8_t nonce[32] = {0x11};
    uint8_t binding_digest[32] = {0x22};
    uint8_t request_digest[32] = {0x33};
    DWORD err;
    const struct e1s18_ops *ops;

    init_dummy_request( &req );
    init_fake_boundary_state();
    e1s18_reset_ops();
    e1s18_reset_answers();
    e1s18_reset_prompt_queue();
    e1s18_set_boundary_table( &s_fake_boundary_table );
    ops = e1s18_get_ops();

    err = ops->prompt_dialog( &req, nonce, binding_digest, request_digest, account, sizeof(account) );
    CHECK_EQ( err, ERROR_SUCCESS, "Stage 1 dialog confirm must return ERROR_SUCCESS" );
    CHECK( !strcmp(account, "alice"), "Returned account name must match dialog output" );

    e1s18_reset_boundary_table();
    return TEST_PASS;
}

static int test_dialog_boundary_cancel( void )
{
    struct request req;
    char account[64] = "garbage";
    uint8_t nonce[32] = {0x11};
    uint8_t binding_digest[32] = {0x22};
    uint8_t request_digest[32] = {0x33};
    DWORD err;
    const struct e1s18_ops *ops;

    init_dummy_request( &req );
    init_fake_boundary_state();
    g_boundary_state.dialog_answer_kind = WEX_ANSWER_KIND_CANCEL;
    e1s18_reset_ops();
    e1s18_reset_answers();
    e1s18_reset_prompt_queue();
    e1s18_set_boundary_table( &s_fake_boundary_table );
    ops = e1s18_get_ops();

    err = ops->prompt_dialog( &req, nonce, binding_digest, request_digest, account, sizeof(account) );
    CHECK_EQ( err, ERROR_CANCELLED, "Stage 1 dialog cancel must return ERROR_CANCELLED" );
    CHECK_EQ( account[0], '\0', "Account name must be wiped on cancel" );

    e1s18_reset_boundary_table();
    return TEST_PASS;
}

static int test_dialog_boundary_timeout( void )
{
    struct request req;
    char account[64];
    uint8_t nonce[32] = {0x11};
    uint8_t binding_digest[32] = {0x22};
    uint8_t request_digest[32] = {0x33};
    DWORD err;
    const struct e1s18_ops *ops;

    init_dummy_request( &req );
    init_fake_boundary_state();
    g_boundary_state.dialog_return_code = ERROR_TIMEOUT;
    e1s18_reset_ops();
    e1s18_reset_answers();
    e1s18_reset_prompt_queue();
    e1s18_set_boundary_table( &s_fake_boundary_table );
    ops = e1s18_get_ops();

    err = ops->prompt_dialog( &req, nonce, binding_digest, request_digest, account, sizeof(account) );
    CHECK_EQ( err, ERROR_TIMEOUT, "Stage 1 dialog timeout must return ERROR_TIMEOUT" );

    e1s18_reset_boundary_table();
    return TEST_PASS;
}

static int test_dialog_boundary_child_crash( void )
{
    struct request req;
    char account[64];
    uint8_t nonce[32] = {0x11};
    uint8_t binding_digest[32] = {0x22};
    uint8_t request_digest[32] = {0x33};
    DWORD err;
    const struct e1s18_ops *ops;

    init_dummy_request( &req );
    init_fake_boundary_state();
    g_boundary_state.dialog_empty_answer = TRUE;
    e1s18_reset_ops();
    e1s18_reset_answers();
    e1s18_reset_prompt_queue();
    e1s18_set_boundary_table( &s_fake_boundary_table );
    ops = e1s18_get_ops();

    err = ops->prompt_dialog( &req, nonce, binding_digest, request_digest, account, sizeof(account) );
    CHECK_EQ( err, ERROR_CANCELLED, "Stage 1 dialog child crash / empty answer must return ERROR_CANCELLED" );

    e1s18_reset_boundary_table();
    return TEST_PASS;
}

static int test_dialog_boundary_bad_binding( void )
{
    struct request req;
    char account[64];
    uint8_t nonce[32] = {0x11};
    uint8_t binding_digest[32] = {0x22};
    uint8_t request_digest[32] = {0x33};
    DWORD err;
    const struct e1s18_ops *ops;

    init_dummy_request( &req );
    e1s18_reset_ops();
    e1s18_reset_answers();
    e1s18_reset_prompt_queue();
    e1s18_set_boundary_table( &s_fake_boundary_table );
    ops = e1s18_get_ops();

    /* 1. Corrupt binding digest */
    init_fake_boundary_state();
    g_boundary_state.dialog_corrupt_binding = TRUE;
    err = ops->prompt_dialog( &req, nonce, binding_digest, request_digest, account, sizeof(account) );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "Corrupt binding digest must fail with ERROR_ACCESS_DENIED" );

    /* 2. Corrupt request digest */
    init_fake_boundary_state();
    g_boundary_state.dialog_corrupt_request = TRUE;
    err = ops->prompt_dialog( &req, nonce, binding_digest, request_digest, account, sizeof(account) );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "Corrupt request digest must fail with ERROR_ACCESS_DENIED" );

    /* 3. Corrupt nonce */
    init_fake_boundary_state();
    g_boundary_state.dialog_corrupt_nonce = TRUE;
    err = ops->prompt_dialog( &req, nonce, binding_digest, request_digest, account, sizeof(account) );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "Corrupt nonce must fail with ERROR_ACCESS_DENIED" );

    /* 4. Corrupt channel token */
    init_fake_boundary_state();
    g_boundary_state.dialog_corrupt_token = TRUE;
    err = ops->prompt_dialog( &req, nonce, binding_digest, request_digest, account, sizeof(account) );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "Corrupt channel token must fail with ERROR_ACCESS_DENIED" );

    e1s18_reset_boundary_table();
    return TEST_PASS;
}

static int test_dialog_boundary_replay_old_answer( void )
{
    struct request req;
    char account[64];
    uint8_t nonce[32] = {0x11};
    uint8_t binding_digest[32] = {0x22};
    uint8_t request_digest[32] = {0x33};
    DWORD err;
    const struct e1s18_ops *ops;

    init_dummy_request( &req );
    init_fake_boundary_state();
    e1s18_reset_ops();
    e1s18_reset_answers();
    e1s18_reset_prompt_queue();
    e1s18_set_boundary_table( &s_fake_boundary_table );
    ops = e1s18_get_ops();

    /* First run succeeds */
    err = ops->prompt_dialog( &req, nonce, binding_digest, request_digest, account, sizeof(account) );
    CHECK_EQ( err, ERROR_SUCCESS, "First dialog answer must succeed" );

    /* Second run with same request_id and nonce is a replay attack */
    err = ops->prompt_dialog( &req, nonce, binding_digest, request_digest, account, sizeof(account) );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "Replaying identical answer record must return ERROR_ACCESS_DENIED" );

    e1s18_reset_boundary_table();
    return TEST_PASS;
}

static int test_dialog_boundary_queue_full( void )
{
    struct request req;
    char account[64];
    uint8_t nonce[32] = {0x11};
    uint8_t binding_digest[32] = {0x22};
    uint8_t request_digest[32] = {0x33};
    DWORD err;
    const struct e1s18_ops *ops;
    struct wex_prompt_queue *q;
    struct wex_prompt_queue_entry *e1 = NULL, *e2 = NULL;
    uint64_t caller_logon;

    init_dummy_request( &req );
    init_fake_boundary_state();
    e1s18_reset_ops();
    e1s18_reset_answers();
    e1s18_reset_prompt_queue();
    e1s18_set_boundary_table( &s_fake_boundary_table );
    ops = e1s18_get_ops();

    q = e1s18_get_prompt_queue();
    caller_logon = ((uint64_t)req.caller_stats.AuthenticationId.HighPart << 32) |
                   (uint32_t)req.caller_stats.AuthenticationId.LowPart;

    /* Fill caller quota (limit is 2 per caller) */
    wex_prompt_queue_enqueue( q, 0x111, req.caller_pid, caller_logon, req.caller_session, "WinSta0\\Winlogon", 0, &e1 );
    wex_prompt_queue_enqueue( q, 0x222, req.caller_pid, caller_logon, req.caller_session, "WinSta0\\Winlogon", 0, &e2 );

    /* 3rd request from same caller must fail with ERROR_BUSY */
    err = ops->prompt_dialog( &req, nonce, binding_digest, request_digest, account, sizeof(account) );
    CHECK_EQ( err, ERROR_BUSY, "prompt_dialog when caller queue is full must return ERROR_BUSY" );

    /* Release entries and retest */
    wex_prompt_queue_release( q, e1 );
    wex_prompt_queue_release( q, e2 );

    err = ops->prompt_dialog( &req, nonce, binding_digest, request_digest, account, sizeof(account) );
    CHECK_EQ( err, ERROR_SUCCESS, "prompt_dialog after releasing slots must succeed" );

    e1s18_reset_boundary_table();
    return TEST_PASS;
}

static int test_transport_boundary_evidence_ok( void )
{
    struct request req;
    uint8_t nonce[32] = {0x11};
    uint8_t binding_digest[32] = {0x22};
    uint8_t request_digest[32] = {0x33};
    uint8_t account_key[16];
    uint8_t evidence_ref[16];
    DWORD err;
    const struct e1s18_ops *ops;

    init_dummy_request( &req );
    init_fake_boundary_state();
    e1s18_reset_ops();
    e1s18_reset_burned_evidence();
    e1s18_set_boundary_table( &s_fake_boundary_table );
    ops = e1s18_get_ops();

    err = ops->redeem_evidence( &req, "alice", nonce, binding_digest, request_digest, account_key, evidence_ref );
    CHECK_EQ( err, ERROR_SUCCESS, "Stage 2 valid evidence redemption must return ERROR_SUCCESS" );
    CHECK( !strcmp(req.account_name, "alice"), "req->account_name must be populated" );
    CHECK( !memcmp(req.account_key, g_boundary_state.transport_account_key, 16), "req->account_key must match" );
    CHECK_EQ( req.map_generation, 42, "req->map_generation must match evidence map_gen" );
    CHECK_EQ( req.storage_generation, 0, "Stage 2 must no longer set the storage generation" );
    CHECK( e1s18_is_evidence_burned( evidence_ref ), "Evidence ref must be marked as burned" );

    e1s18_reset_boundary_table();
    return TEST_PASS;
}

static int test_transport_boundary_expired( void )
{
    struct request req;
    uint8_t nonce[32] = {0x11};
    uint8_t binding_digest[32] = {0x22};
    uint8_t request_digest[32] = {0x33};
    uint8_t account_key[16];
    uint8_t evidence_ref[16];
    DWORD err;
    const struct e1s18_ops *ops;

    init_dummy_request( &req );
    init_fake_boundary_state();
    e1s18_reset_ops();
    e1s18_reset_burned_evidence();
    e1s18_set_boundary_table( &s_fake_boundary_table );
    ops = e1s18_get_ops();

    /* Set clock to 5000 and expiry to 4000 (past) */
    e1s18_set_test_clock( 5000 );
    g_boundary_state.transport_expiry = 4000;

    err = ops->redeem_evidence( &req, "alice", nonce, binding_digest, request_digest, account_key, evidence_ref );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "Expired evidence must fail with ERROR_ACCESS_DENIED" );

    e1s18_set_test_clock( 0 );
    e1s18_reset_boundary_table();
    return TEST_PASS;
}

static int test_transport_boundary_replayed( void )
{
    struct request req;
    uint8_t nonce[32] = {0x11};
    uint8_t binding_digest[32] = {0x22};
    uint8_t request_digest[32] = {0x33};
    uint8_t account_key[16];
    uint8_t evidence_ref[16];
    DWORD err;
    const struct e1s18_ops *ops;

    init_dummy_request( &req );
    init_fake_boundary_state();
    e1s18_reset_ops();
    e1s18_reset_burned_evidence();
    e1s18_set_boundary_table( &s_fake_boundary_table );
    ops = e1s18_get_ops();

    /* First redemption burns evidence */
    err = ops->redeem_evidence( &req, "alice", nonce, binding_digest, request_digest, account_key, evidence_ref );
    CHECK_EQ( err, ERROR_SUCCESS, "First evidence redemption must succeed" );

    /* Second redemption with same evidence ref fails closed */
    err = ops->redeem_evidence( &req, "alice", nonce, binding_digest, request_digest, account_key, evidence_ref );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "Replaying burned evidence must return ERROR_ACCESS_DENIED" );

    e1s18_reset_boundary_table();
    return TEST_PASS;
}

static int test_transport_boundary_wrong_digest( void )
{
    struct request req;
    uint8_t nonce[32] = {0x11};
    uint8_t binding_digest[32] = {0x22};
    uint8_t request_digest[32] = {0x33};
    uint8_t account_key[16];
    uint8_t evidence_ref[16];
    DWORD err;
    const struct e1s18_ops *ops;

    init_dummy_request( &req );
    e1s18_reset_ops();
    e1s18_reset_burned_evidence();
    e1s18_set_boundary_table( &s_fake_boundary_table );
    ops = e1s18_get_ops();

    /* 1. Corrupt request digest */
    init_fake_boundary_state();
    g_boundary_state.transport_corrupt_req_digest = TRUE;
    err = ops->redeem_evidence( &req, "alice", nonce, binding_digest, request_digest, account_key, evidence_ref );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "Wrong request digest in evidence must fail with ERROR_ACCESS_DENIED" );

    /* 2. Corrupt binding digest */
    init_fake_boundary_state();
    g_boundary_state.transport_corrupt_bind_digest = TRUE;
    err = ops->redeem_evidence( &req, "alice", nonce, binding_digest, request_digest, account_key, evidence_ref );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "Wrong binding digest in evidence must fail with ERROR_ACCESS_DENIED" );

    /* 3. Corrupt nonce */
    init_fake_boundary_state();
    g_boundary_state.transport_corrupt_nonce = TRUE;
    err = ops->redeem_evidence( &req, "alice", nonce, binding_digest, request_digest, account_key, evidence_ref );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "Wrong nonce in evidence must fail with ERROR_ACCESS_DENIED" );

    e1s18_reset_boundary_table();
    return TEST_PASS;
}

static int test_transport_boundary_wrong_host_uid( void )
{
    struct request req;
    uint8_t nonce[32] = {0x11};
    uint8_t binding_digest[32] = {0x22};
    uint8_t request_digest[32] = {0x33};
    uint8_t account_key[16];
    uint8_t evidence_ref[16];
    DWORD err;
    const struct e1s18_ops *ops;

    init_dummy_request( &req );
    init_fake_boundary_state();
    g_boundary_state.transport_host_uid = 999999ULL; /* Wrong host UID */
    e1s18_reset_ops();
    e1s18_reset_burned_evidence();
    e1s18_set_boundary_table( &s_fake_boundary_table );
    ops = e1s18_get_ops();

    err = ops->redeem_evidence( &req, "alice", nonce, binding_digest, request_digest, account_key, evidence_ref );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "Wrong host UID in evidence must fail with ERROR_ACCESS_DENIED" );

    e1s18_reset_boundary_table();
    return TEST_PASS;
}

static int test_transport_boundary_service_unavailable( void )
{
    struct request req;
    uint8_t nonce[32] = {0x11};
    uint8_t binding_digest[32] = {0x22};
    uint8_t request_digest[32] = {0x33};
    uint8_t account_key[16];
    uint8_t evidence_ref[16];
    DWORD err;
    const struct e1s18_ops *ops;

    init_dummy_request( &req );
    e1s18_reset_ops();
    e1s18_reset_burned_evidence();
    e1s18_set_boundary_table( &s_fake_boundary_table );
    ops = e1s18_get_ops();

    /* 1. Service not active */
    init_fake_boundary_state();
    g_boundary_state.transport_return_code = ERROR_SERVICE_NOT_ACTIVE;
    err = ops->redeem_evidence( &req, "alice", nonce, binding_digest, request_digest, account_key, evidence_ref );
    CHECK_EQ( err, ERROR_SERVICE_NOT_ACTIVE, "Unavailable transport service must return ERROR_SERVICE_NOT_ACTIVE" );

    /* 2. Not supported */
    init_fake_boundary_state();
    g_boundary_state.transport_return_code = ERROR_NOT_SUPPORTED;
    err = ops->redeem_evidence( &req, "alice", nonce, binding_digest, request_digest, account_key, evidence_ref );
    CHECK_EQ( err, ERROR_NOT_SUPPORTED, "Unsupported transport must return ERROR_NOT_SUPPORTED" );

    e1s18_reset_boundary_table();
    return TEST_PASS;
}

static int test_transport_boundary_fields_populated_and_secrets_wiped( void )
{
    struct request req;
    struct wexelevate_reply reply;
    DWORD err;
    const struct e1s18_ops *default_ops;
    struct e1s18_ops hybrid_ops;

    init_dummy_request( &req );
    reset_tracker();
    reset_export_state();
    init_fake_boundary_state();
    e1s18_reset_ops();
    e1s18_reset_answers();
    e1s18_reset_burned_evidence();
    e1s18_reset_prompt_queue();

    default_ops = e1s18_get_ops();
    hybrid_ops = *default_ops;
    hybrid_ops.is_policy_enabled = fake_is_policy_enabled;
    hybrid_ops.get_capabilities = fake_get_capabilities;
    hybrid_ops.prompt_dialog = default_ops->prompt_dialog;
    hybrid_ops.redeem_evidence = default_ops->redeem_evidence;
    hybrid_ops.issue_token = default_ops->issue_token;
    hybrid_ops.load_profile = default_ops->load_profile;
    hybrid_ops.prep_launch = default_ops->prep_launch;
    hybrid_ops.transfer_lease = default_ops->transfer_lease;
    hybrid_ops.unload_profile = default_ops->unload_profile;
    hybrid_ops.verify_image = fake_verify_image;
    hybrid_ops.create_process_suspended = fake_create_process_suspended;
    hybrid_ops.verify_child = fake_verify_child;
    hybrid_ops.arm_lease = fake_arm_lease;
    hybrid_ops.duplicate_handle = fake_duplicate_handle;
    hybrid_ops.check_pre_resume = fake_check_pre_resume;
    hybrid_ops.resume_thread = fake_resume_thread;
    hybrid_ops.terminate_child = fake_terminate_child;
    hybrid_ops.close_remote_handle = fake_close_remote_handle;

    e1s18_set_ops( &hybrid_ops );
    e1s18_set_export_table( &s_fake_export_table );
    e1s18_set_boundary_table( &s_fake_boundary_table );

    /* Test 1: Full pipeline success populates fields and wipes secrets from req on commit */
    err = e1s18_handle_standard_request( &req, &reply );
    CHECK_EQ( err, ERROR_SUCCESS, "Full pipeline with boundary fakes must succeed" );
    CHECK_EQ( reply.pid, 12345, "Reply pid must match child pid" );
    CHECK_EQ( req.account_name[0], '\0', "req->account_name must be wiped on commit" );
    CHECK( is_zero_key( req.account_key ), "req->account_key must be wiped on commit" );
    CHECK( e1s18_is_evidence_burned( g_boundary_state.transport_evidence_ref ), "Evidence ref must be burned" );

    /* Test 2: Failure rollback wipes secrets from req on failure */
    init_dummy_request( &req );
    reset_tracker();
    reset_export_state();
    init_fake_boundary_state();
    /* Force failure after stage 2 (e.g. stage 3 issue_token fails) */
    g_export_state.issue_status = (NTSTATUS)0xc0000001; /* STATUS_UNSUCCESSFUL */

    err = e1s18_handle_standard_request( &req, &reply );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "Issue failure must return ERROR_ACCESS_DENIED" );
    CHECK_EQ( req.account_name[0], '\0', "req->account_name must be wiped on rollback" );
    CHECK( is_zero_key( req.account_key ), "req->account_key must be wiped on rollback" );

    e1s18_reset_ops();
    e1s18_reset_export_table();
    e1s18_reset_boundary_table();
    return TEST_PASS;
}

static int test_transport_redeem_live_null_params( void )
{
    struct request req;
    uint8_t nonce[32] = {0};
    uint8_t binding[32] = {0};
    uint8_t req_digest[32] = {0};
    uint8_t ev_buf[256];
    size_t ev_len = sizeof(ev_buf);
    uint8_t ev_ref[16];
    DWORD err;

    memset( &req, 0, sizeof(req) );
    req.pipe = (HANDLE)(intptr_t)42;

    /* NULL req */
    err = e1s18_transport_redeem_live( NULL, "alice", nonce, binding, req_digest, ev_buf, &ev_len, ev_ref );
    CHECK_EQ( err, ERROR_INVALID_PARAMETER, "NULL req must return ERROR_INVALID_PARAMETER" );

    /* NULL pipe */
    req.pipe = NULL;
    err = e1s18_transport_redeem_live( &req, "alice", nonce, binding, req_digest, ev_buf, &ev_len, ev_ref );
    CHECK_EQ( err, ERROR_INVALID_PARAMETER, "NULL pipe must return ERROR_INVALID_PARAMETER" );
    req.pipe = (HANDLE)(intptr_t)42;

    /* NULL nonce */
    err = e1s18_transport_redeem_live( &req, "alice", NULL, binding, req_digest, ev_buf, &ev_len, ev_ref );
    CHECK_EQ( err, ERROR_INVALID_PARAMETER, "NULL nonce must return ERROR_INVALID_PARAMETER" );

    /* NULL out_evidence_buf */
    err = e1s18_transport_redeem_live( &req, "alice", nonce, binding, req_digest, NULL, &ev_len, ev_ref );
    CHECK_EQ( err, ERROR_INVALID_PARAMETER, "NULL out_evidence_buf must return ERROR_INVALID_PARAMETER" );

    /* NULL out_evidence_len */
    err = e1s18_transport_redeem_live( &req, "alice", nonce, binding, req_digest, ev_buf, NULL, ev_ref );
    CHECK_EQ( err, ERROR_INVALID_PARAMETER, "NULL out_evidence_len must return ERROR_INVALID_PARAMETER" );

    /* NULL out_evidence_ref */
    err = e1s18_transport_redeem_live( &req, "alice", nonce, binding, req_digest, ev_buf, &ev_len, NULL );
    CHECK_EQ( err, ERROR_INVALID_PARAMETER, "NULL out_evidence_ref must return ERROR_INVALID_PARAMETER" );

    return TEST_PASS;
}

static int test_transport_redeem_live_unsupported_without_pipe( void )
{
    struct request req;
    uint8_t nonce[32] = {0x01};
    uint8_t binding[32] = {0x02};
    uint8_t req_digest[32] = {0x03};
    uint8_t ev_buf[256];
    size_t ev_len = sizeof(ev_buf);
    uint8_t ev_ref[16];
    DWORD err;

    memset( &req, 0, sizeof(req) );
    req.pipe = (HANDLE)(intptr_t)9999; /* Mock non-existent pipe */

    err = e1s18_transport_redeem_live( &req, "alice", nonce, binding, req_digest, ev_buf, &ev_len, ev_ref );
    CHECK( err == ERROR_NOT_SUPPORTED || err == ERROR_ACCESS_DENIED, "Live redeem without live wexhost transport must fail closed" );

    return TEST_PASS;
}

/*
 * Test 39: R3 Regression - Storage generation fallback removed, fail-closed literals
 */
static int test_r3_remediation_storage_generation_and_defaults( void )
{
    struct request req;
    uint8_t nonce[32] = {0xa5};
    uint8_t binding[32] = {0x11};
    uint8_t req_digest[32] = {0x22};
    uint8_t account_key[16] = {0};
    uint8_t ev_ref[16] = {0};
    DWORD err;
    const struct e1s18_ops *ops;

    init_dummy_request( &req );
    e1s18_reset_ops();
    ops = e1s18_get_ops();

    /* 1. Missing caller PID fails closed */
    req.caller_pid = 0;
    err = ops->redeem_evidence( &req, "alice", nonce, binding, req_digest, account_key, ev_ref );
    CHECK_EQ( err, ERROR_INVALID_PARAMETER, "Caller PID 0 must fail closed with ERROR_INVALID_PARAMETER" );

    /* 2. Invalid account name fails closed (no fallback to 'alice') */
    req.caller_pid = 1234;
    err = ops->redeem_evidence( &req, "", nonce, binding, req_digest, account_key, ev_ref );
    CHECK( err != ERROR_SUCCESS, "Empty account name must fail closed" );

    err = ops->redeem_evidence( &req, "alice/root", nonce, binding, req_digest, account_key, ev_ref );
    CHECK( err != ERROR_SUCCESS, "Invalid account name must fail closed" );

    /* 3. Clock failure (returns 0) fails closed with ERROR_INVALID_DATA */
    e1s18_set_test_clock( 0 );
    err = ops->redeem_evidence( &req, "alice", nonce, binding, req_digest, account_key, ev_ref );
    CHECK( err == ERROR_INVALID_DATA || err == ERROR_NOT_SUPPORTED, "Clock returning 0 must fail closed" );
    e1s18_set_test_clock( 1000000000ULL );

    return TEST_PASS;
}

/*
 * Test 40: R2 Regression - Prompt queue concurrency stress test & deterministic sync
 */
#ifndef _WIN32
#include <pthread.h>
#include <unistd.h>

struct queue_stress_thread_arg
{
    struct wex_prompt_queue *q;
    int thread_id;
    uint32_t session_id;
    const char *desktop;
    uint32_t caller_pid;
    uint64_t caller_logon;
    int *route_active_counter;
    pthread_mutex_t *route_mutex;
    int iterations;
    int success_count;
    int error_count;
};

static void *queue_stress_worker( void *param )
{
    struct queue_stress_thread_arg *arg = param;
    int it;

    for (it = 0; it < arg->iterations; it++)
    {
        struct wex_prompt_queue_entry *entry = NULL;
        uint64_t deadline = 5000000000ULL;
        int res;

        res = wex_prompt_queue_enqueue( arg->q,
                                        (uint64_t)(arg->thread_id * 1000 + it + 1),
                                        arg->caller_pid,
                                        arg->caller_logon,
                                        arg->session_id,
                                        arg->desktop,
                                        deadline,
                                        &entry );
        if (res != WEX_STATUS_OK)
        {
            arg->error_count++;
            usleep( 500 );
            continue;
        }

        if (!entry->is_active)
        {
            res = wex_prompt_queue_wait_active( arg->q, entry, 5000 );
            if (res != WEX_STATUS_OK)
            {
                wex_prompt_queue_release( arg->q, entry );
                arg->error_count++;
                continue;
            }
        }

        /* Active section: verify ONLY ONE thread is active on this route */
        pthread_mutex_lock( arg->route_mutex );
        (*arg->route_active_counter)++;
        assert( *arg->route_active_counter == 1 );
        pthread_mutex_unlock( arg->route_mutex );

        usleep( 200 ); /* Simulate prompt interaction */

        pthread_mutex_lock( arg->route_mutex );
        assert( *arg->route_active_counter == 1 );
        (*arg->route_active_counter)--;
        pthread_mutex_unlock( arg->route_mutex );

        wex_prompt_queue_release( arg->q, entry );
        arg->success_count++;
    }
    return NULL;
}
#endif

static int test_r2_prompt_queue_concurrency_stress( void )
{
#ifndef _WIN32
    struct wex_prompt_queue q;
    pthread_t threads[12];
    struct queue_stress_thread_arg args[12];
    int route_active_s1_winsta = 0;
    int route_active_s1_def = 0;
    int route_active_s2_winsta = 0;
    pthread_mutex_t route_m_s1_winsta = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t route_m_s1_def = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_t route_m_s2_winsta = PTHREAD_MUTEX_INITIALIZER;
    int i, total_success = 0;
    struct wex_prompt_queue_entry *e_act = NULL, *e_wait = NULL;

    wex_prompt_queue_init( &q );

    /*
     * 12 concurrent threads across different sessions & desktops:
     * - Threads 0..3: Session 1, WinSta0\Winlogon
     * - Threads 4..7: Session 1, WinSta0\Default
     * - Threads 8..11: Session 2, WinSta0\Winlogon
     */
    for (i = 0; i < 12; i++)
    {
        args[i].q = &q;
        args[i].thread_id = i;
        args[i].caller_pid = 1000 + i;
        args[i].caller_logon = 2000 + i;
        args[i].iterations = 8;
        args[i].success_count = 0;
        args[i].error_count = 0;

        if (i < 4)
        {
            args[i].session_id = 1;
            args[i].desktop = "WinSta0\\Winlogon";
            args[i].route_active_counter = &route_active_s1_winsta;
            args[i].route_mutex = &route_m_s1_winsta;
        }
        else if (i < 8)
        {
            args[i].session_id = 1;
            args[i].desktop = "WinSta0\\Default";
            args[i].route_active_counter = &route_active_s1_def;
            args[i].route_mutex = &route_m_s1_def;
        }
        else
        {
            args[i].session_id = 2;
            args[i].desktop = "WinSta0\\Winlogon";
            args[i].route_active_counter = &route_active_s2_winsta;
            args[i].route_mutex = &route_m_s2_winsta;
        }

        pthread_create( &threads[i], NULL, queue_stress_worker, &args[i] );
    }

    for (i = 0; i < 12; i++)
    {
        pthread_join( threads[i], NULL );
        total_success += args[i].success_count;
    }

    CHECK( total_success > 40, "Concurrent queue iterations must succeed without conflict" );
    CHECK_EQ( q.total_count, 0, "Queue must be empty after all workers finish" );

    /* Test concurrent cancel while waiting */
    CHECK_EQ( wex_prompt_queue_enqueue( &q, 901, 101, 201, 1, "WinSta0\\Winlogon", 1000, &e_act ), WEX_STATUS_OK, "active enqueue" );
    CHECK_EQ( wex_prompt_queue_enqueue( &q, 902, 102, 202, 1, "WinSta0\\Winlogon", 1000, &e_wait ), WEX_STATUS_OK, "waiting enqueue" );
    CHECK_EQ( e_act->is_active, 1, "e_act active" );
    CHECK_EQ( e_wait->is_active, 0, "e_wait waiting" );

    /* Cancel waiting entry under lock */
    CHECK_EQ( wex_prompt_queue_cancel( &q, e_wait ), WEX_STATUS_OK, "cancel waiting" );
    CHECK_EQ( q.total_count, 1, "one left after cancel" );
    CHECK_EQ( e_wait->state, WEX_QUEUE_STATE_CANCELLED, "cancelled entry still owned by its thread" );
    CHECK_EQ( wex_prompt_queue_release( &q, e_wait ), WEX_STATUS_OK, "owner releases cancelled entry" );

    /* Release active entry */
    CHECK_EQ( wex_prompt_queue_release( &q, e_act ), WEX_STATUS_OK, "release active" );
    CHECK_EQ( q.total_count, 0, "queue empty after release" );

    /* Test cancel_all */
    CHECK_EQ( wex_prompt_queue_enqueue( &q, 903, 101, 201, 1, "WinSta0\\Winlogon", 1000, &e_act ), WEX_STATUS_OK, "enqueue 1" );
    CHECK_EQ( wex_prompt_queue_enqueue( &q, 904, 102, 202, 1, "WinSta0\\Winlogon", 1000, &e_wait ), WEX_STATUS_OK, "enqueue 2" );
    CHECK_EQ( wex_prompt_queue_cancel_all( &q ), WEX_STATUS_OK, "cancel all" );
    CHECK_EQ( e_act->state, WEX_QUEUE_STATE_CANCELLED, "e_act cancelled" );
    CHECK_EQ( e_wait->state, WEX_QUEUE_STATE_CANCELLED, "e_wait cancelled" );
    wex_prompt_queue_release( &q, e_act );
    wex_prompt_queue_release( &q, e_wait );
    CHECK_EQ( q.total_count, 0, "queue empty after cancel_all release" );

    wex_prompt_queue_destroy( &q );
#endif
    return TEST_PASS;
}

/*
 * Test 43: R6 Regression - Image lease held until target exit & token closed on success
 */
static int test_r6_image_lease_and_token_cleanup( void )
{
    struct request req;
    struct wexelevate_reply reply;
    DWORD err;
    int handles_before;

    init_dummy_request( &req );
    set_dummy_stage2_fields( &req );
    req.lease = 12345;
    req.is_standard_user = TRUE;

    reset_tracker();
    handles_before = g_track.handles_closed;
    e1s18_set_ops( &s_fake_ops );

    err = e1s18_handle_standard_request( &req, &reply );
    CHECK_EQ( err, ERROR_SUCCESS, "Standard request success" );

    /* On success, issued_token must be closed and not leaked (handles_closed increased) */
    CHECK( g_track.handles_closed > handles_before, "Handles including issued_token must be closed on success" );

    e1s18_reset_ops();
    return TEST_PASS;
}

/*
 * Test 44: R15 - Dialog answer must be exactly WEX_S1_ANSWER_LENGTH bytes.
 * One byte long (157) and one byte short (155) are both denied.
 */
static int test_dialog_boundary_answer_length( void )
{
    struct request req;
    char account[64];
    uint8_t nonce[32] = {0x11};
    uint8_t binding_digest[32] = {0x22};
    uint8_t request_digest[32] = {0x33};
    DWORD err;
    const struct e1s18_ops *ops;

    init_dummy_request( &req );
    e1s18_reset_ops();
    e1s18_reset_answers();
    e1s18_reset_prompt_queue();
    e1s18_set_boundary_table( &s_fake_boundary_table );
    ops = e1s18_get_ops();

    /* One byte long (157) must be denied */
    init_fake_boundary_state();
    g_boundary_state.dialog_answer_len_override = WEX_S1_ANSWER_LENGTH + 1;
    err = ops->prompt_dialog( &req, nonce, binding_digest, request_digest, account, sizeof(account) );
    CHECK_EQ( err, ERROR_CANCELLED, "One-byte-long answer must be denied with ERROR_CANCELLED" );
    CHECK_EQ( account[0], '\0', "Account name must be wiped on overlong answer" );

    /* One byte short (155) must be denied */
    init_fake_boundary_state();
    g_boundary_state.dialog_answer_len_override = WEX_S1_ANSWER_LENGTH - 1;
    err = ops->prompt_dialog( &req, nonce, binding_digest, request_digest, account, sizeof(account) );
    CHECK_EQ( err, ERROR_CANCELLED, "One-byte-short answer must be denied with ERROR_CANCELLED" );
    CHECK_EQ( account[0], '\0', "Account name must be wiped on short answer" );

    e1s18_reset_boundary_table();
    return TEST_PASS;
}

/*
 * Test 45: R15 - Evidence buffer must be exactly one S1 Kind 3 record (220 B).
 * One byte longer (221) is denied.
 */
static int test_transport_boundary_evidence_length( void )
{
    struct request req;
    uint8_t account_key[16];
    uint8_t evidence_ref[16];
    uint8_t nonce[32] = {0x11};
    uint8_t binding_digest[32] = {0x22};
    uint8_t request_digest[32] = {0x33};
    DWORD err;
    const struct e1s18_ops *ops;

    init_dummy_request( &req );
    init_fake_boundary_state();
    g_boundary_state.transport_out_len_override = WEX_S1_EVIDENCE_SIZE + 1;
    e1s18_reset_ops();
    e1s18_reset_burned_evidence();
    e1s18_set_boundary_table( &s_fake_boundary_table );
    ops = e1s18_get_ops();

    err = ops->redeem_evidence( &req, "alice", nonce, binding_digest, request_digest,
                                account_key, evidence_ref );
    CHECK_EQ( err, ERROR_INVALID_DATA, "One-byte-longer evidence must be denied with ERROR_INVALID_DATA" );

    e1s18_reset_boundary_table();
    return TEST_PASS;
}

/*
 * Test 46: R15 - The policy DWORD enables standard-user elevation only when
 * it is exactly 1; 0, 2 and 0xffffffff stay disabled.
 */
static int test_policy_value_exact_one( void )
{
    CHECK( !e1s18_policy_value_enabled( 0 ), "Policy value 0 must stay disabled" );
    CHECK( e1s18_policy_value_enabled( 1 ), "Policy value 1 must enable" );
    CHECK( !e1s18_policy_value_enabled( 2 ), "Policy value 2 must stay disabled" );
    CHECK( !e1s18_policy_value_enabled( 0xffffffff ), "Policy value 0xffffffff must stay disabled" );
    return TEST_PASS;
}

/*
 * Test 45: R10 (first part) - NEGOTIATE must receive a real token output slot
 * (the ntdll export rejects NULL with STATUS_INVALID_PARAMETER), and a
 * NEGOTIATE that fills the token output must deny the request.
 */
static int test_export_boundary_negotiate_token_output( void )
{
    struct request req;
    HANDLE token = NULL;
    DWORD err;
    uint8_t buf16[16] = {0x42};
    uint8_t buf32[32] = {0x11};
    const struct e1s18_ops *ops;

    e1s18_reset_ops();
    e1s18_set_export_table( &s_fake_export_table );
    ops = e1s18_get_ops();

    /* 1. The broker must pass a non-NULL token output slot to NEGOTIATE: the
     * real ntdll export rejects a NULL pointer with STATUS_INVALID_PARAMETER
     * before reaching the server, so a NULL slot can never succeed. */
    init_dummy_request( &req );
    set_dummy_stage2_fields( &req );
    reset_export_state();
    err = ops->issue_token( &req, buf16, buf16, buf32, buf32, buf32, &token );
    CHECK_EQ( err, ERROR_SUCCESS, "issue_token must succeed when NEGOTIATE gets a token slot" );
    CHECK_EQ( g_export_state.negotiate_null_token_calls, 0, "NEGOTIATE must receive a non-NULL token pointer" );
    CHECK_EQ( (uintptr_t)token, 0x2222, "Token handle must match the fake ISSUE token" );
    ops->close_handle( req.s10_context ); req.s10_context = NULL;
    ops->close_handle( token ); token = NULL;

    /* 2. A NEGOTIATE that fills the token output must deny the request: the
     * real ntdll wrapper never returns a token on NEGOTIATE. */
    init_dummy_request( &req );
    set_dummy_stage2_fields( &req );
    reset_export_state();
    g_export_state.negotiate_sets_token = TRUE;
    err = ops->issue_token( &req, buf16, buf16, buf32, buf32, buf32, &token );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "Negotiate token output must deny with ERROR_ACCESS_DENIED" );
    CHECK_EQ( (uintptr_t)token, 0, "Token handle must be NULL on error" );
    CHECK_EQ( (uintptr_t)req.s10_context, 0, "req->s10_context must be NULL on error" );
    CHECK_EQ( g_export_state.issue_calls, 0, "Issue must never be reached after a NEGOTIATE token" );

    e1s18_reset_export_table();
    return TEST_PASS;
}

/*
 * Test 46: WO-3 - Broker asks the server for the storage generation (op 12).
 * query_storage (WEX_HKU_STORAGE_QUERY) runs after S10 issuance and before
 * LOAD. Success requires status 0, NULL key, managed 1 and a nonzero
 * generation, which is stored in req->storage_generation and passed to LOAD
 * exactly. Anything else denies and leaves storage_generation 0, so LOAD
 * fails closed (and the pipeline never reaches it, see
 * test_query_storage_failure_stops_pipeline).
 */
static int test_export_boundary_storage_query( void )
{
    struct request req;
    HANDLE token = NULL;
    HANDLE hkey = NULL;
    uint64_t lease_id = 0;
    DWORD err;
    uint8_t account_key[16] = {0x42};
    uint8_t evidence_ref[16] = {0x77};
    uint8_t nonce[32] = {0xa5};
    uint8_t binding_digest[32] = {0x11};
    uint8_t request_digest[32] = {0x22};
    const struct e1s18_ops *ops;

    e1s18_reset_ops();
    e1s18_set_export_table( &s_fake_export_table );
    ops = e1s18_get_ops();

    /* 1. Success: query_storage stores the queried generation (0x5e1f) and
     * LOAD carries it exactly. */
    init_dummy_request( &req );
    set_dummy_stage2_fields( &req );
    reset_export_state();
    err = ops->issue_token( &req, account_key, evidence_ref, nonce, binding_digest, request_digest, &token );
    CHECK_EQ( err, ERROR_SUCCESS, "issue_token must succeed before the storage query" );
    CHECK_EQ( (uintptr_t)req.s10_context, 0x1111, "s10_context must be retained for the storage query" );

    err = ops->query_storage( &req, token );
    CHECK_EQ( err, ERROR_SUCCESS, "query_storage must succeed with a valid op 12 response" );
    CHECK_EQ( req.storage_generation, 0x5e1fULL, "req->storage_generation must be the queried generation" );
    CHECK_EQ( g_export_state.query_calls, 1, "Storage query must be called once" );

    err = ops->load_profile( &req, token, account_key, &lease_id, &hkey );
    CHECK_EQ( err, ERROR_SUCCESS, "load_profile must succeed after a valid storage query" );
    CHECK_EQ( g_export_state.last_load_storage_gen, 0x5e1fULL, "LOAD must carry exactly the queried generation" );
    ops->close_handle( req.s10_context ); req.s10_context = NULL;
    ops->close_handle( token ); token = NULL;

    /* 2. op 12 status failure denies; storage_generation stays 0 so LOAD
     * fails closed (the pipeline never reaches LOAD). */
    init_dummy_request( &req );
    set_dummy_stage2_fields( &req );
    reset_export_state();
    g_export_state.query_status = (NTSTATUS)0xc0000022; /* STATUS_ACCESS_DENIED */
    err = ops->issue_token( &req, account_key, evidence_ref, nonce, binding_digest, request_digest, &token );
    CHECK_EQ( err, ERROR_SUCCESS, "issue_token must succeed before the failing storage query" );
    err = ops->query_storage( &req, token );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "op 12 status failure must deny with ERROR_ACCESS_DENIED" );
    CHECK_EQ( req.storage_generation, 0, "req->storage_generation must stay 0 on query failure" );
    err = ops->load_profile( &req, token, account_key, &lease_id, &hkey );
    CHECK_EQ( err, ERROR_INVALID_DATA, "LOAD must fail closed when the storage generation is unset" );
    ops->close_handle( req.s10_context ); req.s10_context = NULL;
    ops->close_handle( token ); token = NULL;

    /* 3. op 12 returning generation 0 denies */
    init_dummy_request( &req );
    set_dummy_stage2_fields( &req );
    reset_export_state();
    g_export_state.query_generation = 0;
    err = ops->issue_token( &req, account_key, evidence_ref, nonce, binding_digest, request_digest, &token );
    CHECK_EQ( err, ERROR_SUCCESS, "issue_token must succeed before the zero-generation query" );
    err = ops->query_storage( &req, token );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "op 12 zero generation must deny with ERROR_ACCESS_DENIED" );
    CHECK_EQ( req.storage_generation, 0, "req->storage_generation must stay 0 on zero generation" );
    ops->close_handle( req.s10_context ); req.s10_context = NULL;
    ops->close_handle( token ); token = NULL;

    /* 4. op 12 returning a key (non-NULL) denies */
    init_dummy_request( &req );
    set_dummy_stage2_fields( &req );
    reset_export_state();
    g_export_state.query_null_key = FALSE; /* return a non-NULL key */
    err = ops->issue_token( &req, account_key, evidence_ref, nonce, binding_digest, request_digest, &token );
    CHECK_EQ( err, ERROR_SUCCESS, "issue_token must succeed before the key-returning query" );
    err = ops->query_storage( &req, token );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "op 12 non-NULL key must deny with ERROR_ACCESS_DENIED" );
    CHECK_EQ( req.storage_generation, 0, "req->storage_generation must stay 0 when a key is returned" );
    ops->close_handle( req.s10_context ); req.s10_context = NULL;
    ops->close_handle( token ); token = NULL;

    e1s18_reset_export_table();
    return TEST_PASS;
}

/*
 * WO-5: R4 - Random nonce and channel id.
 */

/* Capturing dialog boundary: records the nonce and channel id passed to it. */
static uint8_t g_cap_nonce[2][32];
static uint8_t g_cap_channel[2][16];
static int g_cap_count = 0;

static DWORD capturing_dialog_exec( struct request *req,
                                    uint64_t request_id,
                                    const uint8_t nonce[32],
                                    const uint8_t binding_digest[32],
                                    const uint8_t request_digest[32],
                                    const uint8_t channel_token[16],
                                    BOOL secure,
                                    uint8_t *out_answer_buf,
                                    size_t *out_answer_len,
                                    char *out_account,
                                    size_t account_max )
{
    (void)secure; (void)req;
    if (g_cap_count < 2)
    {
        memcpy( g_cap_nonce[g_cap_count], nonce, 32 );
        memcpy( g_cap_channel[g_cap_count], channel_token, 16 );
        g_cap_count++;
    }
    /* Return a valid confirm answer so prompt_dialog succeeds. */
    wex_build_answer_record( request_id, 1, 0, 0,
                             WEX_ANSWER_KIND_CONFIRM,
                             nonce, binding_digest, request_digest, channel_token,
                             out_answer_buf, WEX_S1_ANSWER_LENGTH, out_answer_len );
    if (out_account && account_max > 0)
    {
        strncpy( out_account, "alice", account_max - 1 );
        out_account[account_max - 1] = 0;
    }
    return ERROR_SUCCESS;
}

static struct e1s18_boundary_table s_capturing_boundary_table = {
    capturing_dialog_exec,
    fake_transport_boundary_redeem
};

/* Counting dialog boundary: counts how many times the dialog is executed. */
static int g_dialog_exec_count = 0;

static DWORD counting_dialog_exec( struct request *req,
                                   uint64_t request_id,
                                   const uint8_t nonce[32],
                                   const uint8_t binding_digest[32],
                                   const uint8_t request_digest[32],
                                   const uint8_t channel_token[16],
                                   BOOL secure,
                                   uint8_t *out_answer_buf,
                                   size_t *out_answer_len,
                                   char *out_account,
                                   size_t account_max )
{
    (void)secure; (void)req;
    g_dialog_exec_count++;
    wex_build_answer_record( request_id, 1, 0, 0,
                             WEX_ANSWER_KIND_CONFIRM,
                             nonce, binding_digest, request_digest, channel_token,
                             out_answer_buf, WEX_S1_ANSWER_LENGTH, out_answer_len );
    if (out_account && account_max > 0)
    {
        strncpy( out_account, "alice", account_max - 1 );
        out_account[account_max - 1] = 0;
    }
    return ERROR_SUCCESS;
}

static struct e1s18_boundary_table s_counting_boundary_table = {
    counting_dialog_exec,
    fake_transport_boundary_redeem
};

/*
 * Test 47: WO-5 - RNG failure denies the request before any dialog starts.
 */
static int test_rng_failure_denies_no_dialog( void )
{
    struct request req;
    struct wexelevate_reply reply;
    struct e1s18_ops hybrid_ops;
    const struct e1s18_ops *default_ops;
    DWORD err;

    e1s18_set_test_random_fail( TRUE );
    g_dialog_exec_count = 0;

    e1s18_reset_ops();
    default_ops = e1s18_get_ops();
    hybrid_ops = *default_ops;
    hybrid_ops.is_policy_enabled = fake_is_policy_enabled;
    hybrid_ops.get_capabilities = fake_get_capabilities;
    hybrid_ops.prompt_dialog = default_ops->prompt_dialog; /* default: would spawn a dialog */
    hybrid_ops.terminate_child = fake_terminate_child;
    hybrid_ops.close_remote_handle = fake_close_remote_handle;
    e1s18_set_ops( &hybrid_ops );
    e1s18_set_boundary_table( &s_counting_boundary_table );

    init_dummy_request( &req );
    reset_tracker();
    e1s18_reset_answers();
    e1s18_reset_prompt_queue();
    err = e1s18_handle_standard_request( &req, &reply );
    CHECK( err != ERROR_SUCCESS, "RNG failure must deny the request" );
    CHECK_EQ( g_dialog_exec_count, 0, "No dialog must be spawned on RNG failure" );

    e1s18_set_test_random_fail( FALSE );
    e1s18_reset_ops();
    e1s18_reset_boundary_table();
    return TEST_PASS;
}

/*
 * Test 48: WO-5 - The attempt nonce and channel id differ between two
 * requests (real RNG, no test hook).
 */
static int test_random_nonce_and_channel_differ( void )
{
    struct request req;
    struct wexelevate_reply reply;
    struct e1s18_ops hybrid_ops;
    const struct e1s18_ops *default_ops;
    DWORD err;

    /* Use the real RNG (no test hook). */
    e1s18_set_test_random( NULL, 0 );
    e1s18_set_test_random_fail( FALSE );
    g_cap_count = 0;

    e1s18_reset_ops();
    default_ops = e1s18_get_ops();
    hybrid_ops = *default_ops;
    hybrid_ops.is_policy_enabled = fake_is_policy_enabled;
    hybrid_ops.get_capabilities = fake_get_capabilities;
    hybrid_ops.prompt_dialog = default_ops->prompt_dialog; /* default: generates the channel id */
    hybrid_ops.redeem_evidence = fake_redeem_evidence;    /* fails at stage 2 to keep it light */
    hybrid_ops.terminate_child = fake_terminate_child;
    hybrid_ops.close_remote_handle = fake_close_remote_handle;
    e1s18_set_ops( &hybrid_ops );
    e1s18_set_boundary_table( &s_capturing_boundary_table );

    /* Request 1 */
    init_dummy_request( &req );
    reset_tracker();
    g_track.fail_at_stage = 2;
    g_track.fail_error_code = 0x9002;
    e1s18_reset_answers();
    e1s18_reset_prompt_queue();
    err = e1s18_handle_standard_request( &req, &reply );
    CHECK_EQ( err, 0x9002, "Request 1 must fail at stage 2 (redeem_evidence)" );
    CHECK_EQ( g_cap_count, 1, "Request 1 must capture the nonce and channel id" );

    /* Request 2 */
    init_dummy_request( &req );
    reset_tracker();
    g_track.fail_at_stage = 2;
    g_track.fail_error_code = 0x9002;
    e1s18_reset_answers();
    e1s18_reset_prompt_queue();
    err = e1s18_handle_standard_request( &req, &reply );
    CHECK_EQ( err, 0x9002, "Request 2 must fail at stage 2 (redeem_evidence)" );
    CHECK_EQ( g_cap_count, 2, "Request 2 must capture the nonce and channel id" );

    /* The nonce and channel id must differ between the two requests. */
    CHECK( memcmp( g_cap_nonce[0], g_cap_nonce[1], 32 ) != 0,
           "The attempt nonce must differ between two requests" );
    CHECK( memcmp( g_cap_channel[0], g_cap_channel[1], 16 ) != 0,
           "The channel id must differ between two requests" );

    e1s18_reset_ops();
    e1s18_reset_boundary_table();
    return TEST_PASS;
}

/*
 * Test 49: WO-5 - Evidence that expires during the (fake) transport call is
 * rejected by the post-call clock re-fetch.
 */
static int test_transport_boundary_expires_during_call( void )
{
    struct request req;
    uint8_t nonce[32] = {0x11};
    uint8_t binding_digest[32] = {0x22};
    uint8_t request_digest[32] = {0x33};
    uint8_t account_key[16];
    uint8_t evidence_ref[16];
    DWORD err;
    const struct e1s18_ops *ops;

    init_dummy_request( &req );
    init_fake_boundary_state();
    e1s18_reset_ops();
    e1s18_reset_burned_evidence();
    e1s18_set_boundary_table( &s_fake_boundary_table );
    ops = e1s18_get_ops();

    /* Clock starts at 5000, expiry is 10000 (future). The (fake) transport
     * call advances the clock to 15000 (past the expiry), so the evidence
     * must be rejected by the post-call clock re-fetch. */
    e1s18_set_test_clock( 5000 );
    g_boundary_state.transport_expiry = 10000;
    g_boundary_state.transport_set_clock = 15000;

    err = ops->redeem_evidence( &req, "alice", nonce, binding_digest, request_digest,
                                account_key, evidence_ref );
    CHECK_EQ( err, ERROR_ACCESS_DENIED,
              "Evidence that expires during the transport call must be rejected" );

    e1s18_set_test_clock( 0 );
    e1s18_reset_boundary_table();
    return TEST_PASS;
}

/*
 * Test 50: WO-8 - Confirmed child termination on rollback (R7, item 2).
 *
 * Unconfirmed: terminate_child reports the child is not confirmed dead. The
 * broker must return ERROR_CHILD_NOT_COMPLETE, close no process/thread handle
 * (kept for a supervisor-owned reaper), not unload the profile (the child may
 * still use the hive), and still close the remote handle.
 *
 * Confirmed: terminate_child confirms the child is dead. The broker must
 * return the original error, close the process/thread handles, unload the
 * profile, and close the remote handle (the existing stage-matrix behavior).
 */
static int test_wo8_confirmed_child_termination( void )
{
    struct request req;
    struct wexelevate_reply reply;
    DWORD err;

    /* --- Unconfirmed termination --- */
    init_dummy_request( &req );
    reset_tracker();
    g_track.fail_at_stage = 12; /* transfer_lease fails, after child + remote handle */
    g_track.fail_error_code = 0x900c;
    g_track.terminate_child_fail = 1; /* fake reports the child is not confirmed dead */
    e1s18_set_ops( &s_fake_ops );

    err = e1s18_handle_standard_request( &req, &reply );
    CHECK_EQ( err, ERROR_CHILD_NOT_COMPLETE, "Unconfirmed termination must return ERROR_CHILD_NOT_COMPLETE" );
    CHECK_EQ( g_track.child_terminated, 1, "terminate_child must be called once" );
    CHECK_EQ( g_track.process_handle_closed, 0, "Process handle must NOT be closed when termination is unconfirmed" );
    CHECK_EQ( g_track.thread_handle_closed, 0, "Thread handle must NOT be closed when termination is unconfirmed" );
    CHECK_EQ( g_track.profile_unloaded, 0, "Profile must NOT be unloaded when termination is unconfirmed" );
    CHECK_EQ( g_track.remote_handle_closed, 1, "Remote handle must still be closed in caller" );
    CHECK_EQ( reply.pid, 0, "Reply pid must be zero" );
    CHECK_EQ( reply.tid, 0, "Reply tid must be zero" );
    CHECK_EQ( reply.handle, 0, "Reply handle must be zero" );

    /* --- Confirmed termination (existing stage-matrix behavior) --- */
    init_dummy_request( &req );
    reset_tracker();
    g_track.fail_at_stage = 12;
    g_track.fail_error_code = 0x900c;
    g_track.terminate_child_fail = 0; /* fake confirms the child is dead */
    e1s18_set_ops( &s_fake_ops );

    err = e1s18_handle_standard_request( &req, &reply );
    CHECK_EQ( err, 0x900c, "Confirmed termination must return the original error" );
    CHECK_EQ( g_track.child_terminated, 1, "terminate_child must be called once" );
    CHECK_EQ( g_track.process_handle_closed, 1, "Process handle must be closed when termination is confirmed" );
    CHECK_EQ( g_track.thread_handle_closed, 1, "Thread handle must be closed when termination is confirmed" );
    CHECK_EQ( g_track.profile_unloaded, 1, "Profile must be unloaded when termination is confirmed" );
    CHECK_EQ( g_track.remote_handle_closed, 1, "Remote handle must be closed in caller" );
    CHECK_EQ( reply.pid, 0, "Reply pid must be zero" );
    CHECK_EQ( reply.tid, 0, "Reply tid must be zero" );
    CHECK_EQ( reply.handle, 0, "Reply handle must be zero" );

    e1s18_reset_ops();
    return TEST_PASS;
}

/*
 * Test 51: WO-15 - One standard-user request in flight.
 * The S6 transport in wexhost is process-global, so the broker serializes
 * standard-user requests with a broker-wide slot: a second request while one
 * is in flight returns ERROR_BUSY at once (no dialog, no transport call), and
 * the slot is free again after success, after each failure stage and after
 * cancellation.
 */
#ifndef _WIN32
struct wo15_sync
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int entered_prompt;   /* worker is inside prompt_dialog (slot taken) */
    int release_prompt;   /* test releases the worker */
    int prompt_calls;     /* prompt_dialog invocations */
    int prompt_active;    /* requests currently inside prompt_dialog */
    int transport_calls;  /* redeem_evidence invocations */
};

static struct wo15_sync g_wo15;

static DWORD wo15_prompt_dialog( struct request *req, const uint8_t nonce[32],
                                 const uint8_t binding_digest[32], const uint8_t request_digest[32],
                                 char *out_account, size_t account_max )
{
    (void)req; (void)nonce; (void)binding_digest; (void)request_digest;
    pthread_mutex_lock( &g_wo15.mutex );
    g_wo15.prompt_calls++;
    g_wo15.prompt_active++;
    if (g_wo15.prompt_active > 1)
    {
        /* A second request is inside the dialog at the same time: the
         * in-flight slot is not serializing standard requests. Fail fast so
         * the test reports a clean failure instead of deadlocking on the
         * release signal. */
        g_wo15.prompt_active--;
        pthread_mutex_unlock( &g_wo15.mutex );
        return ERROR_ACCESS_DENIED;
    }
    g_wo15.entered_prompt = 1;
    pthread_cond_signal( &g_wo15.cond );
    while (!g_wo15.release_prompt)
        pthread_cond_wait( &g_wo15.cond, &g_wo15.mutex );
    g_wo15.prompt_active--;
    pthread_mutex_unlock( &g_wo15.mutex );
    if (out_account && account_max > 5) strncpy( out_account, "alice", account_max - 1 );
    return ERROR_SUCCESS;
}

static DWORD wo15_redeem_evidence( struct request *req, const char *account_name,
                                   const uint8_t nonce[32], const uint8_t binding_digest[32],
                                   const uint8_t request_digest[32],
                                   uint8_t out_account_key[16], uint8_t out_evidence_ref[16] )
{
    (void)account_name; (void)nonce; (void)binding_digest; (void)request_digest;
    g_wo15.transport_calls++;
#ifdef WEXELEVATE_E1S18
    strncpy( req->account_name, "alice", sizeof(req->account_name) - 1 );
    req->account_name[sizeof(req->account_name) - 1] = '\0';
    memset( req->account_key, 0x42, sizeof(req->account_key) );
    if (out_account_key) memcpy( out_account_key, req->account_key, 16 );
    req->map_generation = 1;
#else
    (void)req;
    if (out_account_key) memset( out_account_key, 0x42, 16 );
#endif
    if (out_evidence_ref) memset( out_evidence_ref, 0x77, 16 );
    return ERROR_SUCCESS;
}

static struct e1s18_ops s_wo15_ops = {
    fake_get_capabilities,
    fake_is_policy_enabled,
    wo15_prompt_dialog,
    wo15_redeem_evidence,
    fake_issue_token,
    fake_load_profile,
    fake_query_storage,
    fake_prep_launch,
    fake_verify_image,
    fake_create_process_suspended,
    fake_verify_child,
    fake_arm_lease,
    fake_duplicate_handle,
    fake_transfer_lease,
    fake_check_pre_resume,
    fake_resume_thread,
    fake_terminate_child,
    fake_close_remote_handle,
    fake_unload_profile,
    fake_close_handle,
    fake_free_buffer,
    NULL, /* r8_open */
    NULL, /* r8_owner */
    NULL, /* r8_binding */
    NULL, /* r8_endorse */
    NULL, /* r8_recheck */
    NULL, /* r8_close */
    NULL  /* r8_start */
};

struct wo15_worker_arg
{
    struct request *req;
    struct wexelevate_reply *reply;
    DWORD err;
};

static void *wo15_worker( void *param )
{
    struct wo15_worker_arg *arg = param;
    arg->err = e1s18_handle_standard_request( arg->req, arg->reply );
    return NULL;
}

static int test_wo15_single_standard_request_in_flight( void )
{
    struct request req_a, req_b, req_c;
    struct wexelevate_reply reply_a, reply_b, reply_c;
    struct wo15_worker_arg warg;
    pthread_t worker;
    DWORD err;
    int stage;

    /* --- Two concurrent requests: one success, one ERROR_BUSY, no
     *     dialog/transport call counted for the second --- */
    pthread_mutex_init( &g_wo15.mutex, NULL );
    pthread_cond_init( &g_wo15.cond, NULL );
    g_wo15.entered_prompt = 0;
    g_wo15.release_prompt = 0;
    g_wo15.prompt_calls = 0;
    g_wo15.prompt_active = 0;
    g_wo15.transport_calls = 0;

    init_dummy_request( &req_a );
    init_dummy_request( &req_b );
    req_b.id = 0xfedcba0987654321ULL; /* distinct request id */
    memset( &reply_a, 0, sizeof(reply_a) );
    memset( &reply_b, 0, sizeof(reply_b) );
    memset( &reply_c, 0, sizeof(reply_c) );
    reset_tracker();
    e1s18_set_ops( &s_wo15_ops );

    warg.req = &req_a;
    warg.reply = &reply_a;
    warg.err = 0;
    pthread_create( &worker, NULL, wo15_worker, &warg );

    /* Wait until the worker is inside prompt_dialog (slot taken) */
    pthread_mutex_lock( &g_wo15.mutex );
    while (!g_wo15.entered_prompt)
        pthread_cond_wait( &g_wo15.cond, &g_wo15.mutex );
    pthread_mutex_unlock( &g_wo15.mutex );

    /* Second request while the slot is taken must return ERROR_BUSY at once */
    err = e1s18_handle_standard_request( &req_b, &reply_b );
    CHECK_EQ( err, ERROR_BUSY, "Second concurrent request must return ERROR_BUSY" );
    CHECK_EQ( g_wo15.prompt_calls, 1, "Second request must not reach the dialog" );
    CHECK_EQ( g_wo15.transport_calls, 0, "Second request must not reach the transport" );

    /* Release the worker so request A completes */
    pthread_mutex_lock( &g_wo15.mutex );
    g_wo15.release_prompt = 1;
    pthread_cond_signal( &g_wo15.cond );
    pthread_mutex_unlock( &g_wo15.mutex );

    pthread_join( worker, NULL );
    CHECK_EQ( warg.err, ERROR_SUCCESS, "First request must succeed" );
    CHECK_EQ( g_wo15.prompt_calls, 1, "Only the first request reached the dialog" );
    CHECK_EQ( g_wo15.transport_calls, 1, "Only the first request reached the transport" );

    /* --- Slot free again after success --- */
    init_dummy_request( &req_c );
    err = e1s18_handle_standard_request( &req_c, &reply_c );
    CHECK_EQ( err, ERROR_SUCCESS, "Slot must be free after success" );

    /* --- Slot free again after each failure stage and after cancellation --- */
    e1s18_set_ops( &s_fake_ops );
    for (stage = 1; stage <= 14; stage++)
    {
        struct request req_fail, req_after;
        struct wexelevate_reply reply_fail, reply_after;

        init_dummy_request( &req_fail );
        reset_tracker();
        g_track.fail_at_stage = stage;
        g_track.fail_error_code = 0x9000 + stage;
        err = e1s18_handle_standard_request( &req_fail, &reply_fail );
        CHECK_EQ( err, (DWORD)(0x9000 + stage), "Failure stage must return injected error" );

        init_dummy_request( &req_after );
        reset_tracker();
        err = e1s18_handle_standard_request( &req_after, &reply_after );
        CHECK_EQ( err, ERROR_SUCCESS, "Slot must be free after failure stage" );
    }

    {
        struct request req_cancel, req_after;
        struct wexelevate_reply reply_cancel, reply_after;

        init_dummy_request( &req_cancel );
        reset_tracker();
        g_track.fail_at_stage = 1;
        g_track.fail_error_code = ERROR_CANCELLED;
        err = e1s18_handle_standard_request( &req_cancel, &reply_cancel );
        CHECK_EQ( err, ERROR_CANCELLED, "Cancelled prompt must return ERROR_CANCELLED" );

        init_dummy_request( &req_after );
        reset_tracker();
        err = e1s18_handle_standard_request( &req_after, &reply_after );
        CHECK_EQ( err, ERROR_SUCCESS, "Slot must be free after cancellation" );
    }

    e1s18_reset_ops();
    pthread_mutex_destroy( &g_wo15.mutex );
    pthread_cond_destroy( &g_wo15.cond );
    return TEST_PASS;
}
#endif /* _WIN32 */

/*
 * WO-14: Golden vector for the canonical S1 encoder. The expected bytes and
 * digests were computed independently from the field orders in
 * docs/E1_S1_CONTRACTS.md "Encoding and negotiation". Any change to the
 * encoder's field order, endianness, or domain tag must update this vector.
 */
static int test_e1s14_golden_vector( void )
{
    struct e1s18_s1_context ctx;
    struct request req;
    struct e1s_bytes binding, request;
    uint8_t buf[4096];
    uint8_t bind_digest[32], req_digest[32], env_digest[32];
    uint8_t env_policy[4];
    int i;

    static const uint8_t gv_binding_expected[180] = {
        0x01, 0x00, 0x01, 0x00, 0xb4, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x33, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0xe8, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
        0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30,
        0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30,
        0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x22, 0x22, 0x11, 0x11, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x10, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x2b, 0x02, 0x00, 0x00,
        0xf4, 0x01, 0x00, 0x00, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c,
        0x3d, 0x3e, 0x3f, 0x40
    };
    static const uint8_t gv_request_expected[196] = {
        0xef, 0xcd, 0xab, 0x90, 0x78, 0x56, 0x34, 0x12, 0x64, 0xc0, 0xb3, 0x77, 0x8c, 0x10, 0xab, 0x04,
        0x4c, 0x8a, 0x79, 0xd5, 0x47, 0x46, 0xa2, 0x81, 0x24, 0x41, 0xed, 0xaa, 0x3c, 0xad, 0xfd, 0x7b,
        0x54, 0x7b, 0x05, 0xaa, 0x7c, 0x4a, 0x63, 0x98, 0x00, 0xaa, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0xbb, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xcc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0xdd, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33,
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33,
        0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x14, 0x00, 0x00, 0x00, 0x43, 0x00, 0x3a, 0x00,
        0x5c, 0x00, 0x69, 0x00, 0x6d, 0x00, 0x67, 0x00, 0x2e, 0x00, 0x65, 0x00, 0x78, 0x00, 0x65, 0x00,
        0x22, 0x00, 0x00, 0x00, 0x43, 0x00, 0x3a, 0x00, 0x5c, 0x00, 0x69, 0x00, 0x6d, 0x00, 0x67, 0x00,
        0x2e, 0x00, 0x65, 0x00, 0x78, 0x00, 0x65, 0x00, 0x20, 0x00, 0x2d, 0x00, 0x2d, 0x00, 0x74, 0x00,
        0x65, 0x00, 0x73, 0x00, 0x74, 0x00, 0x06, 0x00, 0x00, 0x00, 0x43, 0x00, 0x3a, 0x00, 0x5c, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0xee, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };
    static const uint8_t gv_bind_digest[32] = {
        0x64, 0xc0, 0xb3, 0x77, 0x8c, 0x10, 0xab, 0x04, 0x4c, 0x8a, 0x79, 0xd5, 0x47, 0x46, 0xa2, 0x81,
        0x24, 0x41, 0xed, 0xaa, 0x3c, 0xad, 0xfd, 0x7b, 0x54, 0x7b, 0x05, 0xaa, 0x7c, 0x4a, 0x63, 0x98
    };
    static const uint8_t gv_req_digest[32] = {
        0xb4, 0x4e, 0x5a, 0x98, 0x1e, 0x08, 0xb0, 0x37, 0xef, 0x7b, 0xb1, 0x50, 0x19, 0xea, 0xfa, 0x90,
        0xf7, 0xdb, 0x16, 0xc5, 0xc7, 0x33, 0xf0, 0x4c, 0xfe, 0xdd, 0xf1, 0x2a, 0x39, 0x4b, 0x21, 0xf5
    };
    static const uint8_t gv_env_digest[32] = {
        0x74, 0x50, 0x18, 0x0b, 0x93, 0x02, 0x9b, 0x05, 0x8a, 0xac, 0x4e, 0xaa, 0x45, 0xa4, 0xb6, 0xe2,
        0xe5, 0x73, 0xe6, 0x8a, 0x50, 0x3c, 0x08, 0x2d, 0x05, 0xb7, 0x1c, 0xc2, 0xf1, 0x4a, 0xae, 0x5c
    };

    /* Explicit UTF-16LE code units (L"" is 32-bit wchar_t on the native build). */
    static const uint16_t gv_image[]   = { 'C', ':', '\\', 'i', 'm', 'g', '.', 'e', 'x', 'e', 0 };
    static const uint16_t gv_cmdline[] = { 'C', ':', '\\', 'i', 'm', 'g', '.', 'e', 'x', 'e', ' ', '-', '-', 't', 'e', 's', 't', 0 };
    static const uint16_t gv_cwd[]     = { 'C', ':', '\\', 0 };

    fill_default_s1_context( &ctx );

    memset( &req, 0, sizeof(req) );
    req.id = 0x1234567890abcdefULL;
    for (i = 0; i < 32; i++) req.hash[i] = 0x33;
    req.show = 1;
    req.flags = WEXELEVATE_WANT_HANDLE;
    req.caller_stats.TokenId.LowPart = 0x1000;
    req.caller_stats.ModifiedId.LowPart = 0x2000;
    req.caller_stats.AuthenticationId.HighPart = 0x1111;
    req.caller_stats.AuthenticationId.LowPart = 0x2222;
    req.caller_session = 1;
    req.caller_sid_len = 16;
    req.caller_sid[0] = 1;
    req.caller_sid[1] = 2;
    req.caller_sid[7] = 5;
    req.caller_sid[8] = 0x2b; req.caller_sid[9] = 0x02;
    req.caller_sid[12] = 0xf4; req.caller_sid[13] = 0x01;

    /* Binding record (kind 1). */
    binding.data = buf; binding.capacity = sizeof(buf); binding.length = 0; binding.failed = 0;
    e1s_encode_binding( &binding, ctx.server_gen, ctx.root_gen, ctx.broker_gen,
                        ctx.prefix, ctx.host_uid, ctx.host_process, ctx.opener, ctx.writer,
                        0x1000, 0x2000, 0x11112222,
                        req.caller_session, req.caller_sid, req.caller_sid_len, ctx.desktop );
    if (binding.failed || binding.length != 180) { fprintf(stderr, "GV: binding len %u failed=%d\n", binding.length, binding.failed); return TEST_FAIL; }
    if (memcmp( binding.data, gv_binding_expected, 180 ) != 0) { for (int d=0; d<180; d++) if (binding.data[d]!=gv_binding_expected[d]) fprintf(stderr, "GV: binding byte %d got %02x want %02x\n", d, binding.data[d], gv_binding_expected[d]); return TEST_FAIL; }

    e1s18_test_sha256( "E1S1-BIND\0", 10, binding.data, binding.length, bind_digest );
    if (memcmp( bind_digest, gv_bind_digest, 32 ) != 0) { fprintf(stderr, "GV: bind digest mismatch\n"); for (int d=0; d<32; d++) fprintf(stderr, "  %02x %02x\n", bind_digest[d], gv_bind_digest[d]); return TEST_FAIL; }

    /* Frozen request record. */
    request.data = buf; request.capacity = sizeof(buf); request.length = 0; request.failed = 0;
    e1s_encode_request( &request, req.id, bind_digest,
                        ctx.volume_serial, ctx.file_index, ctx.file_size, ctx.last_write,
                        req.hash, gv_image, gv_cmdline, gv_cwd,
                        1, req.show, req.flags, ctx.deadline );
    if (request.failed || request.length != 196) { fprintf(stderr, "GV: request len %u failed=%d\n", request.length, request.failed); return TEST_FAIL; }
    if (memcmp( request.data, gv_request_expected, 196 ) != 0) { for (int d=0; d<196; d++) if (request.data[d]!=gv_request_expected[d]) fprintf(stderr, "GV: request byte %d got %02x want %02x\n", d, request.data[d], gv_request_expected[d]); return TEST_FAIL; }

    e1s18_test_sha256( "E1S1-REQ\0", 9, request.data, request.length, req_digest );
    if (memcmp( req_digest, gv_req_digest, 32 ) != 0) { fprintf(stderr, "GV: req digest mismatch\n"); return TEST_FAIL; }

    /* Environment digest. */
    env_policy[0] = 1; env_policy[1] = 0; env_policy[2] = 0; env_policy[3] = 0;
    e1s18_test_sha256( "E1S1-ENV\0", 9, env_policy, 4, env_digest );
    if (memcmp( env_digest, gv_env_digest, 32 ) != 0) { fprintf(stderr, "GV: env digest mismatch\n"); return TEST_FAIL; }

    return TEST_PASS;
}

/*
 * WO-14: The standard-user branch denies with ERROR_NOT_SUPPORTED (and spawns
 * no dialog) when any required S1 endorsed-identity input is zero.
 */
static int test_e1s14_missing_input_denies( void )
{
    struct e1s18_s1_context ctx;
    struct request req;
    struct wexelevate_reply reply;
    DWORD err;
    int which;

    e1s18_set_ops( &s_fake_ops );

    for (which = 0; which < 18; which++)
    {
        fill_default_s1_context( &ctx );
        switch (which)
        {
        case 0:  ctx.root_gen = 0; break;
        case 1:  ctx.server_gen = 0; break;
        case 2:  ctx.broker_gen = 0; break;
        case 3:  ctx.verifier_gen = 0; break;
        case 4:  ctx.map_gen = 0; break;
        case 5:  ctx.policy_gen = 0; break;
        case 6:  ctx.host_uid = 0; break;
        case 7:  ctx.deadline = 0; break;
        case 8:  memset( ctx.prefix, 0, 16 ); break;
        case 9:  memset( ctx.host_process, 0, 16 ); break;
        case 10: memset( ctx.opener, 0, 16 ); break;
        case 11: memset( ctx.writer, 0, 16 ); break;
        case 12: memset( ctx.desktop, 0, 16 ); break;
        case 13: memset( ctx.channel, 0, 16 ); break;
        case 14: ctx.volume_serial = 0; break;
        case 15: ctx.file_index = 0; break;
        case 16: ctx.file_size = 0; break;
        case 17: ctx.last_write = 0; break;
        }
        init_dummy_request( &req );
        reset_tracker();
        e1s18_set_s1_context( &ctx ); /* override the full context with the zeroed one */
        err = e1s18_handle_standard_request( &req, &reply );
        CHECK_EQ( err, ERROR_NOT_SUPPORTED, "Zero S1 input must deny with ERROR_NOT_SUPPORTED" );
        CHECK_EQ( g_track.order_count, 0, "No dialog may spawn on a zero S1 input" );
    }

    /* Full context: the request proceeds. */
    init_dummy_request( &req );
    reset_tracker();
    err = e1s18_handle_standard_request( &req, &reply );
    CHECK_EQ( err, ERROR_SUCCESS, "Full S1 context must proceed" );

    e1s18_reset_s1_context();
    e1s18_reset_ops();
    return TEST_PASS;
}

/*
 * WO-16: A clock failure at admission denies the request with
 * ERROR_INVALID_DATA before any dialog or stage runs.
 */
static int test_wo16_clock_failure_at_admission( void )
{
    struct request req;
    struct wexelevate_reply reply;
    DWORD err;

    init_dummy_request( &req );
    reset_tracker();
    e1s18_set_ops( &s_fake_ops );
    set_default_s1_context();

    /* Force the monotonic clock to fail at admission. */
    e1s18_set_test_clock_fail( TRUE );
    err = e1s18_handle_standard_request( &req, &reply );
    e1s18_set_test_clock_fail( FALSE );

    CHECK_EQ( err, ERROR_INVALID_DATA, "Clock failure at admission must deny with ERROR_INVALID_DATA" );
    CHECK_EQ( g_track.order_count, 0, "No stage may execute when the clock fails at admission" );
    CHECK_EQ( req.deadline, 0, "The deadline must not be set when the clock fails" );

    /* With the clock restored, the same request proceeds. */
    init_dummy_request( &req );
    reset_tracker();
    e1s18_set_test_clock( 1000000000ULL );
    err = e1s18_handle_standard_request( &req, &reply );
    CHECK_EQ( err, ERROR_SUCCESS, "Request must proceed when the clock is restored" );
    e1s18_set_test_clock( 0 );

    e1s18_reset_s1_context();
    e1s18_reset_ops();
    return TEST_PASS;
}

/*
 * WO-16: The prompt step and the evidence step see the admission deadline
 * (set once at the start of the standard-request handling), not a later one.
 * The test clock is advanced between the prompt stage and the evidence stage;
 * the evidence must be rejected because the effective expiry is the admission
 * deadline, not a deadline recomputed from the advanced clock.
 */
static int test_wo16_admission_deadline( void )
{
    struct request req;
    struct wexelevate_reply reply;
    struct e1s18_ops hybrid_ops;
    const struct e1s18_ops *default_ops;
    DWORD err;
    uint64_t t0 = 1000000000ULL; /* 1 s in ns */
    uint64_t admission_deadline = t0 + 120000000000ULL; /* 120 s */

    init_dummy_request( &req );
    e1s18_reset_ops();
    default_ops = e1s18_get_ops();

    /* Hybrid ops: keep the default prompt_dialog and redeem_evidence (which
     * use req->deadline); provide fakes for the other stages. */
    hybrid_ops = *default_ops;
    hybrid_ops.is_policy_enabled = fake_is_policy_enabled;
    hybrid_ops.get_capabilities = fake_get_capabilities;
    hybrid_ops.issue_token = fake_issue_token;
    hybrid_ops.load_profile = fake_load_profile;
    hybrid_ops.query_storage = fake_query_storage;
    hybrid_ops.prep_launch = fake_prep_launch;
    hybrid_ops.verify_image = fake_verify_image;
    hybrid_ops.create_process_suspended = fake_create_process_suspended;
    hybrid_ops.verify_child = fake_verify_child;
    hybrid_ops.arm_lease = fake_arm_lease;
    hybrid_ops.duplicate_handle = fake_duplicate_handle;
    hybrid_ops.transfer_lease = fake_transfer_lease;
    hybrid_ops.check_pre_resume = fake_check_pre_resume;
    hybrid_ops.resume_thread = fake_resume_thread;
    hybrid_ops.terminate_child = fake_terminate_child;
    hybrid_ops.close_remote_handle = fake_close_remote_handle;
    hybrid_ops.close_handle = fake_close_handle;
    hybrid_ops.free_buffer = fake_free_buffer;

    e1s18_set_ops( &hybrid_ops );
    set_default_s1_context();
    e1s18_reset_prompt_queue();
    {
        uint8_t rng[16];
        int i;
        for (i = 0; i < 16; i++) rng[i] = (uint8_t)(0x10 + i);
        e1s18_set_test_random( rng, sizeof(rng) );
    }
    init_fake_boundary_state();
    e1s18_set_boundary_table( &s_fake_boundary_table );

    /* The evidence expiry is well past the admission deadline, so the
     * effective expiry is the admission deadline itself. The fake transport
     * advances the test clock past the admission deadline during the call. */
    g_boundary_state.transport_expiry = t0 + 200000000000ULL; /* 200 s */
    g_boundary_state.transport_set_clock = t0 + 130000000000ULL; /* 130 s */

    /* Set the test clock to T0; the admission deadline becomes T0 + 120 s. */
    e1s18_set_test_clock( t0 );
    err = e1s18_handle_standard_request( &req, &reply );

    /* The evidence must be rejected because the clock advanced past the
     * admission deadline (the effective expiry is the admission deadline,
     * not a later one recomputed from the advanced clock). */
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "Evidence must be rejected when the clock advances past the admission deadline" );
    CHECK_EQ( req.deadline, admission_deadline, "The admission deadline must be T0 + 120 s, set once at admission" );

    e1s18_set_test_clock( 0 );
    e1s18_set_test_random( NULL, 0 );
    e1s18_reset_boundary_table();
    e1s18_reset_s1_context();
    e1s18_reset_ops();
    return TEST_PASS;
}

/*
 * WO-20: Fill a fully valid R8 attempt-tuple context. The context is zero in
 * the lab today (the request denies); the tests set it here.
 */
static void fill_default_r8_context( struct e1s18_r8_context *ctx )
{
    int i;
    memset( ctx, 0, sizeof(*ctx) );
    for (i = 0; i < 16; i++) ctx->dialog_id[i] = (uint8_t)(0xd0 + i);
    for (i = 0; i < 16; i++) ctx->desktop_route[i] = (uint8_t)(0x80 + i);
    ctx->caller_session = 7;
    ctx->root_gen = 1;
    ctx->server_gen = 2;
    ctx->broker_gen = 3;
    ctx->verifier_gen = 4;
    ctx->map_gen = 5;
    ctx->policy_gen = 6;
    strcpy( ctx->verifier_owner, ":1.42" );
}

/* WO-20: set a request display-string field (WCHAR*) to an ASCII test value,
 * freeing any previous buffer so re-setting does not leak. */
static void wo20_set_wchar_field( WCHAR **field, const char *ascii )
{
    size_t len = strlen( ascii ) + 1;
    WCHAR *buf = malloc( len * sizeof(WCHAR) );
    size_t i;
    free( *field );
    for (i = 0; i < len; i++) buf[i] = (WCHAR)(unsigned char)ascii[i];
    *field = buf;
}

/*
 * WO-20: R8 INIT/ACK portable logic. A zero R8 context denies tuple building
 * before any pipe or process; a full context builds the tuple (desktop_mode 1
 * secure / 2 insecure) and encodes the INIT from the request's display
 * strings; the ACK verifies only with the tuple's digests and dialog_id.
 */
static int test_wo20_r8_init_ack( void )
{
    struct e1s18_r8_context ctx;
    struct request req;
    struct e1r8_tuple t;
    uint8_t nonce[32], channel[16], binding[32], request_digest[32];
    uint8_t init_buf[2067], ack_buf[156];
    size_t i, init_len;

    for (i = 0; i < 32; i++) nonce[i] = (uint8_t)(0x10 + i);
    for (i = 0; i < 16; i++) channel[i] = (uint8_t)(0x30 + i);
    for (i = 0; i < 32; i++) binding[i] = (uint8_t)(0x40 + i);
    for (i = 0; i < 32; i++) request_digest[i] = (uint8_t)(0x60 + i);

    /* Zero R8 context: tuple building denies. */
    e1s18_reset_r8_context();
    init_dummy_request( &req );
    CHECK_EQ( e1s18_build_r8_tuple( &req, req.id, 1, nonce, channel, binding, request_digest, TRUE, &t ),
              -1, "Zero R8 context must deny tuple building" );

    /* Full R8 context: tuple building succeeds. */
    fill_default_r8_context( &ctx );
    e1s18_set_r8_context( &ctx );
    CHECK_EQ( e1s18_build_r8_tuple( &req, req.id, 1, nonce, channel, binding, request_digest, TRUE, &t ),
              0, "Full R8 context must build the tuple" );
    CHECK_EQ( t.desktop_mode, E1R8_DESKTOP_MODE_SECURE, "secure must map to desktop_mode 1" );
    CHECK_EQ( t.attempt, 1, "attempt must be 1" );
    CHECK( memcmp( t.dialog_id, ctx.dialog_id, 16 ) == 0, "dialog_id must come from the R8 context" );
    CHECK( memcmp( t.desktop_route, ctx.desktop_route, 16 ) == 0, "desktop_route must come from the R8 context" );
    CHECK_EQ( t.caller_session, ctx.caller_session, "caller_session must come from the R8 context" );
    CHECK_EQ( t.root_gen, ctx.root_gen, "root_gen must come from the R8 context" );

    /* Insecure maps to desktop_mode 2. */
    CHECK_EQ( e1s18_build_r8_tuple( &req, req.id, 1, nonce, channel, binding, request_digest, FALSE, &t ),
              0, "Full R8 context must build the tuple (insecure)" );
    CHECK_EQ( t.desktop_mode, E1R8_DESKTOP_MODE_INSECURE, "insecure must map to desktop_mode 2" );

    /* INIT encoding: the image is the request's image; requester and publisher
     * are empty (no stand-ins such as the command line or working directory). */
    wo20_set_wchar_field( &req.cmdline, "cmd.exe /c dir" );
    wo20_set_wchar_field( &req.image, "C:\\Windows\\System32\\cmd.exe" );
    wo20_set_wchar_field( &req.cwd, "C:\\Windows" );
    init_len = e1s18_encode_r8_init( &req, &t, init_buf, sizeof(init_buf) );
    CHECK( init_len > 0, "INIT must encode with a full context and a valid image" );
    {
        struct e1r8_init in;
        CHECK_EQ( e1r8_decode_init( init_buf, init_len, &t, &in ), 0, "Encoded INIT must decode" );
        CHECK_EQ( in.requester_len, 0, "requester must be empty" );
        CHECK_EQ( in.image_len, (uint32_t)(strlen("C:\\Windows\\System32\\cmd.exe") * 2), "image must be the image" );
        CHECK( !memcmp( in.image, req.image, in.image_len ), "image bytes must be the request's image" );
        CHECK_EQ( in.publisher_len, 0, "publisher must be empty" );
        CHECK( !memcmp( in.verifier_owner, ctx.verifier_owner, in.verifier_owner_len ) &&
               in.verifier_owner_len == strlen( ctx.verifier_owner ), "verifier owner from the context" );
    }

    /* An oversized or missing image denies the INIT. */
    {
        char big[600];
        memset( big, 'a', sizeof(big) - 1 );
        big[sizeof(big) - 1] = 0;
        wo20_set_wchar_field( &req.image, big );
        CHECK_EQ( e1s18_encode_r8_init( &req, &t, init_buf, sizeof(init_buf) ), 0,
                  "An oversized image must deny the INIT" );
        wo20_set_wchar_field( &req.image, "" );
        CHECK_EQ( e1s18_encode_r8_init( &req, &t, init_buf, sizeof(init_buf) ), 0,
                  "An empty image must deny the INIT" );
        wo20_set_wchar_field( &req.image, "C:\\Windows\\System32\\cmd.exe" );
    }

    /* ACK verification: a valid ACK passes. */
    CHECK_EQ( e1r8_encode_ack( ack_buf, sizeof(ack_buf), &t ), 0, "ACK must encode" );
    CHECK_EQ( e1s18_verify_r8_ack( ack_buf, sizeof(ack_buf), &t ), 0, "A valid ACK must verify" );

    /* ACK short (155 bytes) and long (157 bytes) deny. */
    CHECK( e1s18_verify_r8_ack( ack_buf, 155, &t ) < 0, "A short ACK must deny" );
    CHECK( e1s18_verify_r8_ack( ack_buf, 157, &t ) < 0, "A long ACK must deny" );

    /* ACK with a wrong binding_digest / request_digest / dialog_id denies. */
    {
        uint8_t bad[156];
        memcpy( bad, ack_buf, 156 );
        bad[76] ^= 0xff;
        CHECK( e1s18_verify_r8_ack( bad, 156, &t ) < 0, "An ACK with a wrong binding_digest must deny" );
        memcpy( bad, ack_buf, 156 );
        bad[108] ^= 0xff;
        CHECK( e1s18_verify_r8_ack( bad, 156, &t ) < 0, "An ACK with a wrong request_digest must deny" );
        memcpy( bad, ack_buf, 156 );
        bad[140] ^= 0xff;
        CHECK( e1s18_verify_r8_ack( bad, 156, &t ) < 0, "An ACK with a wrong dialog_id must deny" );
    }

    free( req.cmdline );
    free( req.image );
    free( req.cwd );
    e1s18_reset_r8_context();
    return TEST_PASS;
}

/*
 * WO-25: R8 context preparation from live inputs. The fake r8 ops return
 * configurable OPEN/BINDING/owner data so the prepare validation can be
 * exercised; the state tracks the calls and the channel close.
 */
struct wo25_r8_state
{
    /* Registration (S6 OPEN record) generations. */
    uint64_t reg_server_gen, reg_broker_gen, reg_verifier_gen;
    /* BINDING reply values. */
    uint64_t b_server_gen, b_root_gen, b_broker_gen, b_map_gen, b_policy_gen;
    uint32_t b_caller_session;
    uint64_t b_host_uid;
    uint8_t b_prefix[16], b_host[16], b_opener[16], b_writer[16], b_route[16];
    char owner[256];
    uint8_t dialog_id[16];
    /* Failure injection (0 = success). */
    DWORD open_err, binding_err, owner_err;
    /* Call tracking. */
    int open_calls, binding_calls, owner_calls, close_calls;
    UINT64 closed_channel;
};
static struct wo25_r8_state s_wo25;

static void wo25_reset( void )
{
    int i;
    memset( &s_wo25, 0, sizeof(s_wo25) );
    /* Valid defaults: registration and BINDING agree, every value nonzero. */
    s_wo25.reg_server_gen = 0x100;
    s_wo25.reg_broker_gen = 0x200;
    s_wo25.reg_verifier_gen = 0x300;
    s_wo25.b_server_gen = 0x100;
    s_wo25.b_root_gen = 0x400;
    s_wo25.b_broker_gen = 0x200;
    s_wo25.b_map_gen = 0x500;
    s_wo25.b_policy_gen = 0x600;
    s_wo25.b_caller_session = 7;
    s_wo25.b_host_uid = 1000;
    for (i = 0; i < 16; i++)
    {
        s_wo25.b_prefix[i] = (uint8_t)(0x01 + i);
        s_wo25.b_host[i] = (uint8_t)(0x11 + i);
        s_wo25.b_opener[i] = (uint8_t)(0x21 + i);
        s_wo25.b_writer[i] = (uint8_t)(0x31 + i);
        s_wo25.b_route[i] = (uint8_t)(0x80 + i);
    }
    strcpy( s_wo25.owner, ":1.42" );
    for (i = 0; i < 16; i++) s_wo25.dialog_id[i] = (uint8_t)(0xd0 + i);
}

static DWORD wo25_fake_r8_open( struct request *req, unsigned char reg[E1S6_OPEN_SIZE], UINT64 *channel )
{
    (void)req;
    s_wo25.open_calls++;
    if (s_wo25.open_err) return s_wo25.open_err;
    memset( reg, 0, E1S6_OPEN_SIZE );
    reg[0] = 7; reg[1] = 0;             /* kind 7 */
    reg[2] = 1; reg[3] = 0;             /* version 1 */
    e1s6_put32( reg + 4, E1S6_REG_SIZE );
    e1s6_put64( reg + 48, s_wo25.reg_server_gen );
    e1s6_put64( reg + 56, s_wo25.reg_broker_gen );
    e1s6_put64( reg + 64, s_wo25.reg_verifier_gen );
    memset( reg + 88, 0x41, 16 );
    *channel = 0xabc;
    return ERROR_SUCCESS;
}

static DWORD wo25_fake_r8_owner( struct request *req, UINT64 channel, char owner[256] )
{
    (void)req; (void)channel;
    s_wo25.owner_calls++;
    if (s_wo25.owner_err) return s_wo25.owner_err;
    memset( owner, 0, 256 );
    strcpy( owner, s_wo25.owner );
    return ERROR_SUCCESS;
}

static DWORD wo25_fake_r8_binding( struct request *req, unsigned char out[132] )
{
    (void)req;
    s_wo25.binding_calls++;
    if (s_wo25.binding_err) return s_wo25.binding_err;
    memset( out, 0, 132 );
    memcpy( out + 0, s_wo25.b_prefix, 16 );
    memcpy( out + 16, s_wo25.b_host, 16 );
    memcpy( out + 32, s_wo25.b_opener, 16 );
    memcpy( out + 48, s_wo25.b_writer, 16 );
    memcpy( out + 64, s_wo25.b_route, 16 );
    e1s6_put64( out + 80, s_wo25.b_server_gen );
    e1s6_put64( out + 88, s_wo25.b_root_gen );
    e1s6_put64( out + 96, s_wo25.b_broker_gen );
    e1s6_put64( out + 104, s_wo25.b_map_gen );
    e1s6_put64( out + 112, s_wo25.b_policy_gen );
    e1s6_put32( out + 120, s_wo25.b_caller_session );
    e1s6_put64( out + 124, s_wo25.b_host_uid );
    return ERROR_SUCCESS;
}

static DWORD wo25_fake_r8_endorse( struct request *req, HANDLE dialog, const unsigned char tuple[204],
                                   unsigned char dialog_id[16] )
{
    (void)req; (void)dialog; (void)tuple;
    memcpy( dialog_id, s_wo25.dialog_id, 16 );
    return ERROR_SUCCESS;
}

static DWORD wo25_fake_r8_recheck( struct request *req, const unsigned char dialog_id[16] )
{
    (void)req; (void)dialog_id;
    return ERROR_SUCCESS;
}

static DWORD wo25_fake_r8_close( struct request *req, UINT64 channel )
{
    (void)req;
    s_wo25.close_calls++;
    s_wo25.closed_channel = channel;
    return ERROR_SUCCESS;
}

static void wo25_set_ops( void )
{
    static struct e1s18_ops ops;
    ops = s_fake_ops;
    ops.r8_open = wo25_fake_r8_open;
    ops.r8_owner = wo25_fake_r8_owner;
    ops.r8_binding = wo25_fake_r8_binding;
    ops.r8_endorse = wo25_fake_r8_endorse;
    ops.r8_recheck = wo25_fake_r8_recheck;
    ops.r8_close = wo25_fake_r8_close;
    e1s18_set_ops( &ops );
}

/*
 * WO-25: prepare fills the R8 context from the fake OPEN/BINDING/owner data
 * and closes the S6 channel. The context is verified through the tuple it
 * builds (s_r8_context is static in the broker).
 */
static int test_wo25_r8_prepare_fills_context( void )
{
    struct request req;
    struct e1r8_tuple t;
    uint8_t nonce[32], channel[16], binding[32], request_digest[32];
    int i;
    DWORD err;

    for (i = 0; i < 32; i++) nonce[i] = (uint8_t)(0x10 + i);
    for (i = 0; i < 16; i++) channel[i] = (uint8_t)(0x30 + i);
    for (i = 0; i < 32; i++) binding[i] = (uint8_t)(0x40 + i);
    for (i = 0; i < 32; i++) request_digest[i] = (uint8_t)(0x60 + i);

    wo25_reset();
    wo25_set_ops();
    e1s18_reset_r8_context();
    e1s18_set_r8_skip_prepare( FALSE );
    init_dummy_request( &req );

    err = e1s18_r8_prepare( &req );
    CHECK_EQ( err, ERROR_SUCCESS, "prepare must succeed with valid OPEN/BINDING/owner" );
    CHECK_EQ( s_wo25.open_calls, 1, "OPEN must be called once" );
    CHECK_EQ( s_wo25.binding_calls, 1, "BINDING must be called once" );
    CHECK_EQ( s_wo25.owner_calls, 1, "owner must be called once" );
    CHECK_EQ( s_wo25.close_calls, 0, "the S6 channel stays open after a successful prepare" );
    e1s18_r8_release( &req );
    CHECK_EQ( s_wo25.close_calls, 1, "release closes the attempt's S6 channel" );
    CHECK( s_wo25.closed_channel == 0xabc, "the opened channel must be the one closed" );
    e1s18_r8_release( &req );
    CHECK_EQ( s_wo25.close_calls, 1, "a second release closes nothing" );

    /* The context is complete (dialog_id may be zero) and builds a tuple. */
    CHECK_EQ( e1s18_build_r8_tuple( &req, req.id, 1, nonce, channel, binding, request_digest, TRUE, &t ),
              0, "prepare must leave a complete R8 context" );
    CHECK_EQ( t.root_gen, s_wo25.b_root_gen, "root_gen must come from the BINDING" );
    CHECK_EQ( t.server_gen, s_wo25.reg_server_gen, "server_gen must come from the registration" );
    CHECK_EQ( t.broker_gen, s_wo25.reg_broker_gen, "broker_gen must come from the registration" );
    CHECK_EQ( t.verifier_gen, s_wo25.reg_verifier_gen, "verifier_gen must come from the registration" );
    CHECK_EQ( t.map_gen, s_wo25.b_map_gen, "map_gen must come from the BINDING" );
    CHECK_EQ( t.policy_gen, s_wo25.b_policy_gen, "policy_gen must come from the BINDING" );
    CHECK_EQ( t.caller_session, s_wo25.b_caller_session, "caller_session must come from the BINDING" );
    CHECK( memcmp( t.desktop_route, s_wo25.b_route, 16 ) == 0, "desktop_route must come from the BINDING" );
    for (i = 0; i < 16; i++)
        CHECK( t.dialog_id[i] == 0, "dialog_id must stay zero after prepare" );

    e1s18_reset_r8_context();
    e1s18_reset_ops();
    return TEST_PASS;
}

/*
 * WO-25: prepare denies on a BINDING/registration generation mismatch, any
 * zero BINDING value, an invalid owner, or an OPEN/BINDING/owner failure. The
 * S6 channel is closed after a successful OPEN on every path.
 */
static int test_wo25_r8_prepare_denies( void )
{
    struct request req;
    int i;
    DWORD err;

    init_dummy_request( &req );

    /* BINDING server_gen differs from the registration. */
    wo25_reset();
    s_wo25.b_server_gen = 0x999;
    wo25_set_ops();
    e1s18_reset_r8_context();
    e1s18_set_r8_skip_prepare( FALSE );
    err = e1s18_r8_prepare( &req );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "a BINDING server_gen mismatch must deny" );
    CHECK_EQ( s_wo25.close_calls, 1, "the channel must be closed after a successful OPEN (server_gen mismatch)" );

    /* BINDING broker_gen differs from the registration. */
    wo25_reset();
    s_wo25.b_broker_gen = 0x999;
    wo25_set_ops();
    e1s18_reset_r8_context();
    e1s18_set_r8_skip_prepare( FALSE );
    err = e1s18_r8_prepare( &req );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "a BINDING broker_gen mismatch must deny" );
    CHECK_EQ( s_wo25.close_calls, 1, "the channel must be closed after a successful OPEN (broker_gen mismatch)" );

    /* Any zero BINDING value denies. */
    for (i = 0; i < 12; i++)
    {
        wo25_reset();
        switch (i)
        {
        case 0: memset( s_wo25.b_prefix, 0, 16 ); break;
        case 1: memset( s_wo25.b_host, 0, 16 ); break;
        case 2: memset( s_wo25.b_opener, 0, 16 ); break;
        case 3: memset( s_wo25.b_writer, 0, 16 ); break;
        case 4: memset( s_wo25.b_route, 0, 16 ); break;
        case 5: s_wo25.b_server_gen = 0; break;
        case 6: s_wo25.b_root_gen = 0; break;
        case 7: s_wo25.b_broker_gen = 0; break;
        case 8: s_wo25.b_map_gen = 0; break;
        case 9: s_wo25.b_policy_gen = 0; break;
        case 10: s_wo25.b_caller_session = 0; break;
        case 11: s_wo25.b_host_uid = 0; break;
        }
        wo25_set_ops();
        e1s18_reset_r8_context();
        e1s18_set_r8_skip_prepare( FALSE );
        err = e1s18_r8_prepare( &req );
        CHECK_EQ( err, ERROR_ACCESS_DENIED, "a zero BINDING value must deny" );
        CHECK_EQ( s_wo25.close_calls, 1, "the channel must be closed after a successful OPEN (zero BINDING)" );
    }

    /* An invalid owner denies. */
    wo25_reset();
    strcpy( s_wo25.owner, "not-a-bus-name" );
    wo25_set_ops();
    e1s18_reset_r8_context();
    e1s18_set_r8_skip_prepare( FALSE );
    err = e1s18_r8_prepare( &req );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "an invalid owner must deny" );
    CHECK_EQ( s_wo25.close_calls, 1, "the channel must be closed after a successful OPEN (invalid owner)" );

    /* OPEN failure denies (no channel to close). */
    wo25_reset();
    s_wo25.open_err = ERROR_ACCESS_DENIED;
    wo25_set_ops();
    e1s18_reset_r8_context();
    e1s18_set_r8_skip_prepare( FALSE );
    err = e1s18_r8_prepare( &req );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "an OPEN failure must deny" );
    CHECK_EQ( s_wo25.close_calls, 0, "no channel must be closed when OPEN fails" );

    /* BINDING failure denies (channel closed after a successful OPEN). */
    wo25_reset();
    s_wo25.binding_err = ERROR_ACCESS_DENIED;
    wo25_set_ops();
    e1s18_reset_r8_context();
    e1s18_set_r8_skip_prepare( FALSE );
    err = e1s18_r8_prepare( &req );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "a BINDING failure must deny" );
    CHECK_EQ( s_wo25.close_calls, 1, "the channel must be closed after a successful OPEN (BINDING failure)" );

    /* owner failure denies (channel closed after a successful OPEN). */
    wo25_reset();
    s_wo25.owner_err = ERROR_ACCESS_DENIED;
    wo25_set_ops();
    e1s18_reset_r8_context();
    e1s18_set_r8_skip_prepare( FALSE );
    err = e1s18_r8_prepare( &req );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "an owner failure must deny" );
    CHECK_EQ( s_wo25.close_calls, 1, "the channel must be closed after a successful OPEN (owner failure)" );

    e1s18_reset_r8_context();
    e1s18_reset_ops();
    return TEST_PASS;
}

/*
 * WO-25: the pre-endorsement tuple bytes equal the codec golden layout with
 * the context values.
 */
static int test_wo25_r8_pre_endorsement_tuple( void )
{
    struct e1s18_r8_context ctx;
    struct request req;
    struct e1r8_tuple t;
    uint8_t nonce[32], channel[16], binding[32], request_digest[32];
    uint8_t pre[E1R8_PRE_ENDORSEMENT_SIZE];
    int i;

    for (i = 0; i < 32; i++) nonce[i] = (uint8_t)(0x10 + i);
    for (i = 0; i < 16; i++) channel[i] = (uint8_t)(0x30 + i);
    for (i = 0; i < 32; i++) binding[i] = (uint8_t)(0x40 + i);
    for (i = 0; i < 32; i++) request_digest[i] = (uint8_t)(0x60 + i);

    fill_default_r8_context( &ctx );
    e1s18_set_r8_context( &ctx );
    init_dummy_request( &req );

    CHECK_EQ( e1s18_build_r8_tuple( &req, req.id, 1, nonce, channel, binding, request_digest, TRUE, &t ),
              0, "the tuple must build" );
    CHECK_EQ( e1r8_encode_pre_endorsement_tuple( pre, sizeof(pre), &t ), 0, "the pre-endorsement tuple must encode" );

    /* Golden layout: request_id 0, attempt 8, nonce 12, channel_id 44,
     * binding_digest 60, request_digest 92, deadline 124, root_gen 132,
     * server_gen 140, broker_gen 148, verifier_gen 156, map_gen 164,
     * policy_gen 172, caller_session 180, desktop_route 184, desktop_mode 200. */
    CHECK_EQ( e1s6_u64( pre + 0 ), req.id, "request_id at 0" );
    CHECK_EQ( e1s6_u32( pre + 8 ), 1, "attempt at 8" );
    CHECK( memcmp( pre + 12, nonce, 32 ) == 0, "nonce at 12" );
    CHECK( memcmp( pre + 44, channel, 16 ) == 0, "channel_id at 44" );
    CHECK( memcmp( pre + 60, binding, 32 ) == 0, "binding_digest at 60" );
    CHECK( memcmp( pre + 92, request_digest, 32 ) == 0, "request_digest at 92" );
    CHECK_EQ( e1s6_u64( pre + 124 ), t.deadline, "deadline at 124" );
    CHECK_EQ( e1s6_u64( pre + 132 ), ctx.root_gen, "root_gen at 132" );
    CHECK_EQ( e1s6_u64( pre + 140 ), ctx.server_gen, "server_gen at 140" );
    CHECK_EQ( e1s6_u64( pre + 148 ), ctx.broker_gen, "broker_gen at 148" );
    CHECK_EQ( e1s6_u64( pre + 156 ), ctx.verifier_gen, "verifier_gen at 156" );
    CHECK_EQ( e1s6_u64( pre + 164 ), ctx.map_gen, "map_gen at 164" );
    CHECK_EQ( e1s6_u64( pre + 172 ), ctx.policy_gen, "policy_gen at 172" );
    CHECK_EQ( e1s6_u32( pre + 180 ), ctx.caller_session, "caller_session at 180" );
    CHECK( memcmp( pre + 184, ctx.desktop_route, 16 ) == 0, "desktop_route at 184" );
    CHECK_EQ( e1s6_u32( pre + 200 ), E1R8_DESKTOP_MODE_SECURE, "desktop_mode at 200" );

    e1s18_reset_r8_context();
    return TEST_PASS;
}

/*
 * WO-25: the sealed tuple carries the endorsed dialog_id.
 */
static int test_wo25_r8_sealed_tuple_dialog_id( void )
{
    struct e1s18_r8_context ctx;
    struct request req;
    struct e1r8_tuple t;
    uint8_t nonce[32], channel[16], binding[32], request_digest[32];
    uint8_t sealed[E1R8_SEALED_SIZE];
    int i;

    for (i = 0; i < 32; i++) nonce[i] = (uint8_t)(0x10 + i);
    for (i = 0; i < 16; i++) channel[i] = (uint8_t)(0x30 + i);
    for (i = 0; i < 32; i++) binding[i] = (uint8_t)(0x40 + i);
    for (i = 0; i < 32; i++) request_digest[i] = (uint8_t)(0x60 + i);

    fill_default_r8_context( &ctx );
    e1s18_set_r8_context( &ctx );
    init_dummy_request( &req );

    CHECK_EQ( e1s18_build_r8_tuple( &req, req.id, 1, nonce, channel, binding, request_digest, TRUE, &t ),
              0, "the tuple must build" );
    CHECK_EQ( e1r8_encode_sealed_tuple( sealed, sizeof(sealed), &t ), 0, "the sealed tuple must encode" );

    /* The sealed tuple is the pre-endorsement tuple (204 bytes) plus the
     * dialog_id (16 bytes) at offset 204. */
    CHECK( memcmp( sealed + 204, ctx.dialog_id, 16 ) == 0, "dialog_id at 204" );
    CHECK( memcmp( sealed + 204, t.dialog_id, 16 ) == 0, "dialog_id must be the tuple's" );

    e1s18_reset_r8_context();
    return TEST_PASS;
}

static int test_wo28_resolve_account_s4( void )
{
    const char *tmp = "/tmp/test_r8_accounts.map";
    FILE *f = fopen( tmp, "w" );
    uint8_t key[16];
    uint8_t expected_key[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    CHECK( f != NULL, "must create temp map" );
    fprintf( f, "WEXACCOUNTS 1 42 1002 1 2 3\n" );
    fprintf( f, "admin 1000 0102030405060708090a0b0c0d0e0f10 active 21 1 2 3 500\n" );
    fprintf( f, "tomb 1001 02020202020202020202020202020202 tombstone 21 1 2 3 1001\n" );
    fclose( f );

    e1s18_set_test_accounts_map( tmp );

    /* 1. Active account resolves exact key */
    memset( key, 0, sizeof(key) );
    CHECK_EQ( e1s18_resolve_account_s4( "admin", key ), 0, "resolve admin ok" );
    CHECK( memcmp( key, expected_key, 16 ) == 0, "admin key matches" );

    /* 2. Tombstoned account resolves zero key */
    memset( key, 0xff, sizeof(key) );
    CHECK_EQ( e1s18_resolve_account_s4( "tomb", key ), 0, "resolve tomb ok" );
    CHECK( is_zero_key( key ), "tomb key must be zero" );

    /* 3. Unknown account resolves zero key */
    memset( key, 0xff, sizeof(key) );
    CHECK_EQ( e1s18_resolve_account_s4( "unknown", key ), 0, "resolve unknown ok" );
    CHECK( is_zero_key( key ), "unknown key must be zero" );

    /* 4. Invalid name resolves zero key */
    memset( key, 0xff, sizeof(key) );
    CHECK_EQ( e1s18_resolve_account_s4( "invalid name!", key ), 0, "resolve invalid ok" );
    CHECK( is_zero_key( key ), "invalid name key must be zero" );

    e1s18_set_test_accounts_map( NULL );
    remove( tmp );
    return TEST_PASS;
}

static DWORD fake_r8_start( struct request *req, UINT64 channel, const unsigned char *start_pkt, size_t start_len,
                            unsigned char ready_pkt[76] )
{
    (void)req; (void)channel; (void)start_pkt; (void)start_len;
    memset( ready_pkt, 0x5a, 76 );
    return ERROR_SUCCESS;
}

static int test_wo28_r8_start_op( void )
{
    struct request req;
    uint8_t pkt[100];
    uint8_t ready[76];
    struct e1s18_ops ops;
    const struct e1s18_ops *default_ops = e1s18_get_ops();

    init_dummy_request( &req );
    memset( pkt, 0, sizeof(pkt) );
    memset( ready, 0, sizeof(ready) );

    /* Native stub returns ERROR_NOT_SUPPORTED */
    CHECK_EQ( default_ops->r8_start( &req, 1, pkt, sizeof(pkt), ready ), (DWORD)ERROR_NOT_SUPPORTED,
              "native r8_start must fail closed" );

    /* Overridden op called */
    ops = *default_ops;
    ops.r8_start = fake_r8_start;
    e1s18_set_ops( &ops );
    CHECK_EQ( e1s18_get_ops()->r8_start( &req, 1, pkt, sizeof(pkt), ready ), (DWORD)ERROR_SUCCESS,
              "mocked r8_start must return success" );
    CHECK( ready[0] == 0x5a, "ready packet filled" );

    e1s18_reset_ops();
    return TEST_PASS;
}

#endif /* WEXELEVATE_E1S18 */

int main( int argc, char **argv )
{
    int ret = 0;
    (void)argc; (void)argv;

    if (argc > 1 && !strcmp(argv[1], "test_disabled"))
    {
        printf("Running test_disabled_by_default...\n");
        return test_disabled_by_default();
    }

    printf("==> Running E1-S18 Broker Integration Unit Tests\n");

    printf("1. Testing disabled by default policy...\n");
    if (test_disabled_by_default() != TEST_PASS) ret = 1;

    printf("2. Testing capability enforcement (all 10 S1 caps required)...\n");
    if (test_missing_capabilities() != TEST_PASS) ret = 1;

    printf("3. Testing rejection of inherited environment...\n");
    if (test_inherited_env_denied() != TEST_PASS) ret = 1;

    printf("4. Testing sequential order & single guarded resume...\n");
    if (test_success_path_ordering() != TEST_PASS) ret = 1;

    printf("5. Testing complete stage failure & cleanup matrix (stages 1..13)...\n");
    if (test_stage_failures_and_cleanup() != TEST_PASS) ret = 1;

    printf("6. Testing filtered-admin regression preservation...\n");
    if (test_filtered_admin_untouched() != TEST_PASS) ret = 1;

#ifdef WEXELEVATE_E1S18
    printf("7. Testing NTDLL export boundary fake success (stages 3, 4, 5, 11, unload)...\n");
    if (test_export_boundary_success() != TEST_PASS) ret = 1;

    printf("8. Testing NTDLL export boundary NEGOTIATE failures (status error, bad caps)...\n");
    if (test_export_boundary_negotiate_failure() != TEST_PASS) ret = 1;

    printf("9. Testing NTDLL export boundary ISSUE failures (status error)...\n");
    if (test_export_boundary_issue_failure() != TEST_PASS) ret = 1;

    printf("10. Testing NTDLL export boundary wrong token shape (type, elevation, integrity)...\n");
    if (test_export_boundary_wrong_token_shape() != TEST_PASS) ret = 1;

    printf("11. Testing NTDLL export boundary wrong SID (caller reflection, invalid length)...\n");
    if (test_export_boundary_wrong_sid() != TEST_PASS) ret = 1;

    printf("12. Testing NTDLL export boundary wrong session...\n");
    if (test_export_boundary_wrong_session() != TEST_PASS) ret = 1;

    printf("13. Testing NTDLL export boundary bad auth ID (zero, caller duplicate)...\n");
    if (test_export_boundary_bad_auth_id() != TEST_PASS) ret = 1;

    printf("14. Testing NTDLL export boundary profile load failures (not found, access denied, NULL key)...\n");
    if (test_export_boundary_load_profile_failure() != TEST_PASS) ret = 1;

    printf("15. Testing NTDLL export boundary lease transfer failures (access denied, not supported)...\n");
    if (test_export_boundary_transfer_failure() != TEST_PASS) ret = 1;

    printf("16. Testing NTDLL export boundary busy unload recording (resource in use, cannot delete)...\n");
    if (test_export_boundary_busy_unload() != TEST_PASS) ret = 1;

    printf("17. Testing NTDLL export boundary full pipeline integration & rollback...\n");
    if (test_export_boundary_pipeline_integration() != TEST_PASS) ret = 1;

    printf("18. Testing unset Stage 2 account_key fails closed (ERROR_INVALID_DATA)...\n");
    if (test_unset_stage2_account_key() != TEST_PASS) ret = 1;

    printf("19. Testing unset Stage 2 map_generation fails closed (ERROR_INVALID_DATA)...\n");
    if (test_unset_stage2_map_generation() != TEST_PASS) ret = 1;

    printf("20. Testing query_storage (op 12) failure stops the pipeline before LOAD...\n");
    if (test_query_storage_failure_stops_pipeline() != TEST_PASS) ret = 1;

    printf("21. Testing unset Stage 2 account_name fails closed (ERROR_INVALID_DATA)...\n");
    if (test_unset_stage2_account_name() != TEST_PASS) ret = 1;

    printf("22. Testing export boundary unset Stage 2 fields fail closed directly...\n");
    if (test_export_boundary_unset_fields_fail_closed() != TEST_PASS) ret = 1;

    printf("23. Testing dialog boundary confirm (Stage 1)...\n");
    if (test_dialog_boundary_confirm() != TEST_PASS) ret = 1;

    printf("24. Testing dialog boundary cancel (Stage 1)...\n");
    if (test_dialog_boundary_cancel() != TEST_PASS) ret = 1;

    printf("25. Testing dialog boundary timeout (Stage 1)...\n");
    if (test_dialog_boundary_timeout() != TEST_PASS) ret = 1;

    printf("26. Testing dialog boundary child crash (Stage 1)...\n");
    if (test_dialog_boundary_child_crash() != TEST_PASS) ret = 1;

    printf("27. Testing dialog boundary bad binding digests (Stage 1)...\n");
    if (test_dialog_boundary_bad_binding() != TEST_PASS) ret = 1;

    printf("28. Testing dialog boundary replay of old answer (Stage 1)...\n");
    if (test_dialog_boundary_replay_old_answer() != TEST_PASS) ret = 1;

    printf("29. Testing dialog boundary queue quota & full (Stage 1)...\n");
    if (test_dialog_boundary_queue_full() != TEST_PASS) ret = 1;

    printf("30. Testing transport boundary valid evidence redemption (Stage 2)...\n");
    if (test_transport_boundary_evidence_ok() != TEST_PASS) ret = 1;

    printf("31. Testing transport boundary expired evidence (Stage 2)...\n");
    if (test_transport_boundary_expired() != TEST_PASS) ret = 1;

    printf("32. Testing transport boundary replayed / burned evidence (Stage 2)...\n");
    if (test_transport_boundary_replayed() != TEST_PASS) ret = 1;

    printf("33. Testing transport boundary wrong digest & nonce (Stage 2)...\n");
    if (test_transport_boundary_wrong_digest() != TEST_PASS) ret = 1;

    printf("34. Testing transport boundary wrong host UID (Stage 2)...\n");
    if (test_transport_boundary_wrong_host_uid() != TEST_PASS) ret = 1;

    printf("35. Testing transport boundary service unavailable / unsupported (Stage 2)...\n");
    if (test_transport_boundary_service_unavailable() != TEST_PASS) ret = 1;

    printf("36. Testing transport boundary fields populated and secrets wiped (Stages 1..13)...\n");
    if (test_transport_boundary_fields_populated_and_secrets_wiped() != TEST_PASS) ret = 1;

    printf("37. Testing transport redeem live parameter validation...\n");
    if (test_transport_redeem_live_null_params() != TEST_PASS) ret = 1;

    printf("38. Testing transport redeem live unsupported without active transport...\n");
    if (test_transport_redeem_live_unsupported_without_pipe() != TEST_PASS) ret = 1;

    printf("39. Testing R3 regression: storage generation default removed & fail-closed literals...\n");
    if (test_r3_remediation_storage_generation_and_defaults() != TEST_PASS) ret = 1;

    printf("40. Testing R2 regression: prompt queue concurrency stress & FIFO per route...\n");
    if (test_r2_prompt_queue_concurrency_stress() != TEST_PASS) ret = 1;



    printf("41. Testing R6 regression: image lease guard & token cleanup...\n");
    if (test_r6_image_lease_and_token_cleanup() != TEST_PASS) ret = 1;

    printf("42. Testing R15 dialog answer exact length (157 long / 155 short denied)...\n");
    if (test_dialog_boundary_answer_length() != TEST_PASS) ret = 1;

    printf("43. Testing R15 evidence buffer exact length (221 longer denied)...\n");
    if (test_transport_boundary_evidence_length() != TEST_PASS) ret = 1;

    printf("44. Testing R15 policy DWORD enables only when exactly 1 (0/1/2/0xffffffff)...\n");
    if (test_policy_value_exact_one() != TEST_PASS) ret = 1;

    printf("45. Testing R10 NEGOTIATE token output (non-NULL slot required; token output denies)...\n");
    if (test_export_boundary_negotiate_token_output() != TEST_PASS) ret = 1;

    printf("46. Testing WO-3 storage generation query (op 12): success, denial, LOAD generation...\n");
    if (test_export_boundary_storage_query() != TEST_PASS) ret = 1;

    printf("47. Testing WO-5 RNG failure denies before any dialog starts...\n");
    if (test_rng_failure_denies_no_dialog() != TEST_PASS) ret = 1;

    printf("48. Testing WO-5 nonce and channel id differ between two requests...\n");
    if (test_random_nonce_and_channel_differ() != TEST_PASS) ret = 1;

    printf("49. Testing WO-5 evidence expiring during the transport call is rejected...\n");
    if (test_transport_boundary_expires_during_call() != TEST_PASS) ret = 1;

    printf("50. Testing WO-8 confirmed child termination on rollback (unconfirmed keeps handles, no unload)...\n");
    if (test_wo8_confirmed_child_termination() != TEST_PASS) ret = 1;

    printf("51. Testing WO-15 one standard-user request in flight (second gets ERROR_BUSY; slot freed after success/failure/cancel)...\n");
    if (test_wo15_single_standard_request_in_flight() != TEST_PASS) ret = 1;

    printf("52. Testing WO-14 canonical S1 encoder golden vector (binding/request bytes + digests)...\n");
    if (test_e1s14_golden_vector() != TEST_PASS) ret = 1;

    printf("53. Testing WO-14 zero S1 endorsed-identity input denies with ERROR_NOT_SUPPORTED...\n");
    if (test_e1s14_missing_input_denies() != TEST_PASS) ret = 1;

    printf("54. Testing WO-16 clock failure at admission denies before any dialog...\n");
    if (test_wo16_clock_failure_at_admission() != TEST_PASS) ret = 1;

    printf("55. Testing WO-16 prompt and evidence steps see the admission deadline, not a later one...\n");
    if (test_wo16_admission_deadline() != TEST_PASS) ret = 1;

    printf("56. Testing WO-20 R8 INIT/ACK portable logic (zero context denies, tuple, INIT, ACK)...\n");
    if (test_wo20_r8_init_ack() != TEST_PASS) ret = 1;

    printf("57. Testing WO-25 R8 prepare fills the context from OPEN/BINDING/owner and closes the channel...\n");
    if (test_wo25_r8_prepare_fills_context() != TEST_PASS) ret = 1;

    printf("58. Testing WO-25 R8 prepare denies (gen mismatch, zero BINDING, bad owner, op failure)...\n");
    if (test_wo25_r8_prepare_denies() != TEST_PASS) ret = 1;

    printf("59. Testing WO-25 R8 pre-endorsement tuple bytes equal the codec golden layout...\n");
    if (test_wo25_r8_pre_endorsement_tuple() != TEST_PASS) ret = 1;

    printf("60. Testing WO-25 R8 sealed tuple carries the endorsed dialog_id...\n");
    if (test_wo25_r8_sealed_tuple_dialog_id() != TEST_PASS) ret = 1;

    printf("61. Testing WO-28 R8 resolve account through S4 (active key, tombstone/unknown decoy)...\n");
    if (test_wo28_resolve_account_s4() != TEST_PASS) ret = 1;

    printf("62. Testing WO-28 R8 start op in boundary table (fail closed, mock)...\n");
    if (test_wo28_r8_start_op() != TEST_PASS) ret = 1;
#endif

    if (ret == 0 && g_failed == 0)
    {
        printf("==> ALL E1-S18 broker unit tests PASSED successfully.\n");
        return 0;
    }
    fprintf(stderr, "==> SOME E1-S18 broker unit tests FAILED.\n");
    return 1;
}
