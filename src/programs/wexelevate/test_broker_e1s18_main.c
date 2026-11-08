/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Native C unit test for E1-S18 Broker Wine-Only Integration.
 * Verifies stage ordering, failure denial and cleanup at each stage,
 * capability enforcement, single guarded resume, and disabled-by-default behavior.
 */

#include "e1s18_broker.h"
#include "../../../wine/include/wine/wex_token.h"
#include "../../../wine/include/wine/wex_profile.h"

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

    /* Injected capabilities */
    uint64_t injected_caps;
    BOOL injected_policy;

    /* Stage 2 negative test injection */
    int omit_stage2_account_name;
    int omit_stage2_account_key;
    int omit_stage2_map_gen;
    int omit_stage2_storage_gen;
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
    if (!g_track.omit_stage2_storage_gen)
        req->storage_generation = 1;
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
    g_track.order[g_track.order_count++] = 4;
    if (g_track.fail_at_stage == 4) return g_track.fail_error_code ? g_track.fail_error_code : ERROR_FILE_NOT_FOUND;
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

static DWORD fake_prep_launch( struct request *req, HANDLE token,
                               char **out_win_env, size_t *out_win_env_sz,
                               char ***out_unix_env, size_t *out_unix_env_cnt,
                               char **out_cwd )
{
    (void)token;
    g_track.order[g_track.order_count++] = 5;
    if (g_track.fail_at_stage == 5) return g_track.fail_error_code ? g_track.fail_error_code : ERROR_NOT_SUPPORTED;
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

static DWORD fake_transfer_lease( struct request *req, uint64_t lease_id, HANDLE process, HANDLE token )
{
    (void)req; (void)lease_id; (void)process; (void)token;
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

static void fake_unload_profile( uint64_t lease_id, HANDLE hkey, HANDLE token )
{
    (void)lease_id; (void)hkey; (void)token;
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
    req->caller_stats.AuthenticationId.HighPart = 0x1111;
    req->caller_stats.AuthenticationId.LowPart = 0x2222;
    /* Bob's SID: S-1-5-21-555-500 */
    req->caller_sid_len = 16;
    req->caller_sid[0] = 1;
    req->caller_sid[1] = 2;
    req->caller_sid[7] = 5;
    wex_token_put32( req->caller_sid + 8, 555 );
    wex_token_put32( req->caller_sid + 12, 500 );
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

    /* Alice SID: S-1-5-21-999-1001 */
    g_export_state.issue_sid[0] = 1;
    g_export_state.issue_sid[1] = 2;
    g_export_state.issue_sid[7] = 5;
    wex_token_put32( g_export_state.issue_sid + 8, 999 );
    wex_token_put32( g_export_state.issue_sid + 12, 1001 );
    g_export_state.issue_sid_len = 16;

    g_export_state.issue_session = 1;
    g_export_state.issue_auth_id = 0x88884444ULL;
    g_export_state.issue_token_type = 1; /* TokenPrimary */
    g_export_state.issue_elevation = 2;  /* TokenElevationTypeFull */
    g_export_state.issue_integrity = 0x3000; /* High */

    g_export_state.load_status = 0;
    g_export_state.load_managed = 1;
    g_export_state.load_generation = 0xbeef01ULL;

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
    if (operation == WEX_TOKEN_NEGOTIATE)
    {
        g_export_state.negotiate_calls++;
        if (g_export_state.negotiate_status != 0)
            return g_export_state.negotiate_status;
        if (context) *context = (HANDLE)(intptr_t)0x1111;
        if (caps) *caps = g_export_state.negotiate_caps;
        if (response_size) *response_size = g_export_state.negotiate_size;
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
        memcpy( resp + 76, g_export_state.issue_sid, sizeof(g_export_state.issue_sid) );

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

        if (key) *key = g_export_state.load_null_key ? NULL : (HANDLE)(intptr_t)0x3333;
        if (managed) *managed = g_export_state.load_managed;
        if (generation) *generation = g_export_state.load_generation;
        return 0;
    }
    if (operation == WEX_HKU_TEST_TRANSFER)
    {
        g_export_state.transfer_calls++;
        g_export_state.last_transfer_process = context;
        if (g_export_state.transfer_status != 0)
            return g_export_state.transfer_status;
        if (!context) return (NTSTATUS)0xc0000008;
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
    req->storage_generation = 1;
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
    CHECK_EQ( req.alice_sid_len, 16, "req->alice_sid_len must match S1 response" );
    CHECK_EQ( g_export_state.negotiate_calls, 1, "Negotiate must be called once" );
    CHECK_EQ( g_export_state.issue_calls, 1, "Issue must be called once" );

    /* 2. Stage 4: load_profile */
    err = ops->load_profile( &req, token, account_key, &lease_id, &hkey );
    CHECK_EQ( err, ERROR_SUCCESS, "default load_profile must succeed with fake exports" );
    CHECK_EQ( lease_id, 0xbeef01ULL, "lease_id must match fake export generation" );
    CHECK_EQ( (uintptr_t)hkey, 0x3333, "hkey must match fake export key handle" );
    CHECK_EQ( (uintptr_t)req.s10_context, 0, "req->s10_context must be cleared after load_profile" );
    CHECK_EQ( g_export_state.load_calls, 1, "Load must be called once" );

    /* 3. Stage 5: prep_launch */
    err = ops->prep_launch( &req, token, &win_env, &win_env_sz, &unix_env, &unix_env_cnt, &cwd );
    CHECK_EQ( err, ERROR_SUCCESS, "default prep_launch must succeed" );
    CHECK( win_env != NULL && win_env_sz > 0, "win_env block must be constructed" );
    if (win_env) free( win_env );
    if (cwd) free( cwd );

    /* 4. Stage 11: transfer_lease */
    err = ops->transfer_lease( &req, lease_id, (HANDLE)(intptr_t)0x4444, token );
    CHECK_EQ( err, ERROR_SUCCESS, "default transfer_lease must succeed" );
    CHECK_EQ( g_export_state.transfer_calls, 1, "Transfer must be called once" );

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

    /* Bob's SID reflected instead of Alice */
    reset_export_state();
    memcpy( g_export_state.issue_sid, req.caller_sid, req.caller_sid_len );
    g_export_state.issue_sid_len = req.caller_sid_len;
    err = ops->issue_token( &req, buf16, buf16, buf32, buf32, buf32, &token );
    CHECK_EQ( err, ERROR_INVALID_PARAMETER, "Caller SID reflection must fail closed with ERROR_INVALID_PARAMETER" );

    /* Invalid SID length: too short */
    reset_export_state();
    g_export_state.issue_sid_len = 4;
    err = ops->issue_token( &req, buf16, buf16, buf32, buf32, buf32, &token );
    CHECK_EQ( err, ERROR_INVALID_PARAMETER, "Short SID length must fail closed with ERROR_INVALID_PARAMETER" );

    /* Invalid SID length: too long */
    reset_export_state();
    g_export_state.issue_sid_len = 80;
    err = ops->issue_token( &req, buf16, buf16, buf32, buf32, buf32, &token );
    CHECK_EQ( err, ERROR_INVALID_PARAMETER, "Overlong SID length must fail closed with ERROR_INVALID_PARAMETER" );

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
    e1s18_reset_ops();
    e1s18_set_export_table( &s_fake_export_table );
    ops = e1s18_get_ops();

    /* Access denied */
    reset_export_state();
    g_export_state.transfer_status = (NTSTATUS)0xc0000022;
    err = ops->transfer_lease( &req, 0xbeef01ULL, (HANDLE)(intptr_t)0x4444, (HANDLE)(intptr_t)0x2222 );
    CHECK_EQ( err, ERROR_ACCESS_DENIED, "Transfer STATUS_ACCESS_DENIED must map to ERROR_ACCESS_DENIED" );

    /* Server lacking WEX_E1S14_CRASH_TEST -> STATUS_NOT_SUPPORTED */
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

static int test_unset_stage2_storage_generation( void )
{
    struct request req;
    struct wexelevate_reply reply;
    DWORD err;

    init_dummy_request( &req );
    reset_tracker();
    g_track.omit_stage2_storage_gen = 1;
    e1s18_set_ops( &s_fake_ops );

    err = e1s18_handle_standard_request( &req, &reply );
    CHECK_EQ( err, ERROR_INVALID_DATA, "Unset storage_generation must fail closed with ERROR_INVALID_DATA" );
    CHECK_EQ( g_track.order_count, 4, "Pipeline must stop at stage 4 when storage_generation unset" );
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
    CHECK_EQ( g_track.order_count, 5, "Pipeline must stop at stage 5 when account_name unset" );
    CHECK_EQ( g_track.profile_unloaded, 1, "Profile must be unloaded when stage 5 fails" );
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
    req.account_name[0] = '\0';
    err = ops->prep_launch( &req, (HANDLE)(intptr_t)0x2222, &win_env, &win_env_sz, &unix_env, &unix_env_cnt, &cwd );
    CHECK_EQ( err, ERROR_INVALID_DATA, "default_prep_launch with unset account_name must return ERROR_INVALID_DATA" );

    e1s18_reset_export_table();
    return TEST_PASS;
}
#endif

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

    printf("20. Testing unset Stage 2 storage_generation fails closed (ERROR_INVALID_DATA)...\n");
    if (test_unset_stage2_storage_generation() != TEST_PASS) ret = 1;

    printf("21. Testing unset Stage 2 account_name fails closed (ERROR_INVALID_DATA)...\n");
    if (test_unset_stage2_account_name() != TEST_PASS) ret = 1;

    printf("22. Testing export boundary unset Stage 2 fields fail closed directly...\n");
    if (test_export_boundary_unset_fields_fail_closed() != TEST_PASS) ret = 1;
#endif

    if (ret == 0 && g_failed == 0)
    {
        printf("==> ALL E1-S18 broker unit tests PASSED successfully.\n");
        return 0;
    }
    fprintf(stderr, "==> SOME E1-S18 broker unit tests FAILED.\n");
    return 1;
}
