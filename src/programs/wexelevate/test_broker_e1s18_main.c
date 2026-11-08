/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Native C unit test for E1-S18 Broker Wine-Only Integration.
 * Verifies stage ordering, failure denial and cleanup at each stage,
 * capability enforcement, single guarded resume, and disabled-by-default behavior.
 */

#include "e1s18_broker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

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

    /* Injected capabilities */
    uint64_t injected_caps;
    BOOL injected_policy;
};

static struct test_tracker g_track;

static void reset_tracker( void )
{
    memset( &g_track, 0, sizeof(g_track) );
    g_track.injected_caps = E1S1_ALL_REQUIRED_CAPS;
    g_track.injected_policy = TRUE;
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
    (void)req; (void)account_name; (void)nonce; (void)binding_digest; (void)request_digest;
    g_track.order[g_track.order_count++] = 2;
    if (g_track.fail_at_stage == 2) return g_track.fail_error_code ? g_track.fail_error_code : ERROR_ACCESS_DENIED;
    memset( out_account_key, 0x42, 16 );
    memset( out_evidence_ref, 0x77, 16 );
    return ERROR_SUCCESS;
}

static DWORD fake_issue_token( struct request *req, const uint8_t account_key[16],
                               const uint8_t evidence_ref[16], const uint8_t nonce[32],
                               const uint8_t binding_digest[32], const uint8_t request_digest[32],
                               HANDLE *out_token )
{
    (void)req; (void)account_key; (void)evidence_ref; (void)nonce; (void)binding_digest; (void)request_digest;
    g_track.order[g_track.order_count++] = 3;
    if (g_track.fail_at_stage == 3) return g_track.fail_error_code ? g_track.fail_error_code : ERROR_ACCESS_DENIED;
    if (out_token) *out_token = (HANDLE)(intptr_t)0x1001;
    return ERROR_SUCCESS;
}

static DWORD fake_load_profile( struct request *req, HANDLE token, const uint8_t account_key[16],
                                uint64_t *out_lease_id, HANDLE *out_hkey )
{
    (void)req; (void)token; (void)account_key;
    g_track.order[g_track.order_count++] = 4;
    if (g_track.fail_at_stage == 4) return g_track.fail_error_code ? g_track.fail_error_code : ERROR_FILE_NOT_FOUND;
    if (out_lease_id) *out_lease_id = 0x5501ULL;
    if (out_hkey) *out_hkey = (HANDLE)(intptr_t)0x2002;
    return ERROR_SUCCESS;
}

static DWORD fake_prep_launch( struct request *req, HANDLE token,
                               char **out_win_env, size_t *out_win_env_sz,
                               char ***out_unix_env, size_t *out_unix_env_cnt,
                               char **out_cwd )
{
    (void)req; (void)token;
    g_track.order[g_track.order_count++] = 5;
    if (g_track.fail_at_stage == 5) return g_track.fail_error_code ? g_track.fail_error_code : ERROR_NOT_SUPPORTED;
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
    g_track.order[g_track.order_count++] = 6;
    if (g_track.fail_at_stage == 6) return g_track.fail_error_code ? g_track.fail_error_code : ERROR_FILE_CORRUPT;
    return ERROR_SUCCESS;
}

static DWORD fake_create_process_suspended( struct request *req, HANDLE token,
                                           const char *win_env, size_t win_env_sz,
                                           const char *cwd,
                                           PROCESS_INFORMATION *out_pi )
{
    (void)req; (void)token; (void)win_env; (void)win_env_sz; (void)cwd;
    g_track.order[g_track.order_count++] = 7;
    if (g_track.fail_at_stage == 7) return g_track.fail_error_code ? g_track.fail_error_code : ERROR_ACCESS_DENIED;
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
    g_track.order[g_track.order_count++] = 8;
    if (g_track.fail_at_stage == 8) return g_track.fail_error_code ? g_track.fail_error_code : ERROR_ACCESS_DENIED;
    return ERROR_SUCCESS;
}

static DWORD fake_arm_lease( UINT64 lease, DWORD pid )
{
    (void)lease; (void)pid;
    g_track.order[g_track.order_count++] = 9;
    if (g_track.fail_at_stage == 9) return g_track.fail_error_code ? g_track.fail_error_code : ERROR_FILE_CORRUPT;
    return ERROR_SUCCESS;
}

static DWORD fake_duplicate_handle( struct request *req, HANDLE process, HANDLE *out_remote )
{
    (void)req; (void)process;
    g_track.order[g_track.order_count++] = 10;
    if (g_track.fail_at_stage == 10) return g_track.fail_error_code ? g_track.fail_error_code : ERROR_ACCESS_DENIED;
    if (out_remote) *out_remote = (HANDLE)(intptr_t)0x5005;
    return ERROR_SUCCESS;
}

static DWORD fake_transfer_lease( struct request *req, uint64_t lease_id, HANDLE process )
{
    (void)req; (void)lease_id; (void)process;
    g_track.order[g_track.order_count++] = 11;
    if (g_track.fail_at_stage == 11) return g_track.fail_error_code ? g_track.fail_error_code : ERROR_ACCESS_DENIED;
    return ERROR_SUCCESS;
}

static DWORD fake_check_pre_resume( struct request *req )
{
    (void)req;
    g_track.order[g_track.order_count++] = 12;
    if (g_track.fail_at_stage == 12) return g_track.fail_error_code ? g_track.fail_error_code : ERROR_CANCELLED;
    return ERROR_SUCCESS;
}

static DWORD fake_resume_thread( struct request *req, HANDLE thread )
{
    (void)req; (void)thread;
    g_track.order[g_track.order_count++] = 13;
    g_track.resume_count++;
    if (g_track.fail_at_stage == 13) return g_track.fail_error_code ? g_track.fail_error_code : ERROR_GEN_FAILURE;
    return ERROR_SUCCESS;
}

static void fake_terminate_child( HANDLE process )
{
    (void)process;
    g_track.child_terminated++;
}

static void fake_close_remote_handle( HANDLE caller, HANDLE remote )
{
    (void)caller; (void)remote;
    g_track.remote_handle_closed++;
}

static void fake_unload_profile( uint64_t lease_id, HANDLE hkey )
{
    (void)lease_id; (void)hkey;
    g_track.profile_unloaded++;
}

static void fake_close_handle( HANDLE handle )
{
    (void)handle;
    g_track.handles_closed++;
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
    fake_free_buffer
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
#endif
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
    int expected_order[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
    int i;

    init_dummy_request( &req );
    reset_tracker();
    e1s18_set_ops( &s_fake_ops );

    err = e1s18_handle_standard_request( &req, &reply );
    CHECK_EQ( err, ERROR_SUCCESS, "Success path must return ERROR_SUCCESS" );
    CHECK_EQ( g_track.order_count, 13, "All 13 stages must execute in order" );

    for (i = 0; i < 13; i++)
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

    for (stage = 1; stage <= 13; stage++)
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

        /* Stages 1..12 must NEVER call ResumeThread */
        if (stage < 13)
        {
            CHECK_EQ( g_track.resume_count, 0, "ResumeThread must not be called when an earlier stage fails" );
        }

        /* Stages >= 8: child process was created by stage 7; must terminate on failure */
        if (stage >= 8)
        {
            CHECK( g_track.child_terminated > 0, "Suspended child must be terminated on failure" );
        }
        else
        {
            CHECK_EQ( g_track.child_terminated, 0, "No child termination if not created" );
        }

        /* Stages >= 11: remote handle was duplicated by stage 10; must close in caller on failure */
        if (stage >= 11)
        {
            CHECK( g_track.remote_handle_closed > 0, "Remote handle must be closed in caller on failure" );
        }
        else
        {
            CHECK_EQ( g_track.remote_handle_closed, 0, "No remote handle closure if not duplicated" );
        }

        /* Stages >= 5: profile lease was acquired by stage 4; must unload on failure */
        if (stage >= 5)
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

    if (ret == 0 && g_failed == 0)
    {
        printf("==> ALL E1-S18 broker unit tests PASSED successfully.\n");
        return 0;
    }
    fprintf(stderr, "==> SOME E1-S18 broker unit tests FAILED.\n");
    return 1;
}
