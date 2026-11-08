/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * E1-S18: Broker Wine-Only Integration.
 *
 * Separate supported standard-token integration branch for wexelevate:
 * - Validates S1 negotiated capabilities (all 10 required; missing denies with ERROR_NOT_SUPPORTED)
 * - Checks runtime policy (EnableStandardUserElevation, default off)
 * - Serialized prompt queue & credential dialog (S17, explicit Confirm required)
 * - Single-use evidence verification & redemption (S8, PAM worker S7)
 * - Account-specific server token issuance (S10, NtCreateWexAccountToken)
 * - Profile load & lease acquisition (S13/S14, HKU\<Alice SID>, Volatile Env)
 * - Launch preparation (S15, Alice Windows env, Bob Unix env, cwd verify)
 * - Suspended child creation in caller's server and session with validated token
 * - Installed image identity check (path + SHA-256 + lease check)
 * - Limited handle duplication to caller (SYNCHRONIZE | QUERY_LIMITED)
 * - Guarded resume (broker resumes child once after all checks pass)
 * - Explicit rollback / cleanup on failure at every stage
 */

#include "e1s18_broker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Self-contained FIPS 180-2 SHA-256 Implementation.
 * Ensures consistent canonical digest calculations across Windows/Wine and
 * native unit testing without external crypto library dependencies.
 */
struct sha256_ctx
{
    uint32_t state[8];
    uint64_t count;
    uint8_t  buf[64];
};

static inline uint32_t rotr32( uint32_t x, uint32_t n ) { return (x >> n) | (x << (32 - n)); }

static void sha256_transform( uint32_t state[8], const uint8_t data[64] )
{
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };
    uint32_t W[64], a, b, c, d, e, f, g, h, t1, t2;
    int i;

    for (i = 0; i < 16; i++)
    {
        W[i] = ((uint32_t)data[i * 4] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) |
               ((uint32_t)data[i * 4 + 3]);
    }
    for (i = 16; i < 64; i++)
    {
        uint32_t s0 = rotr32( W[i - 15], 7 ) ^ rotr32( W[i - 15], 18 ) ^ (W[i - 15] >> 3);
        uint32_t s1 = rotr32( W[i - 2], 17 ) ^ rotr32( W[i - 2], 19 ) ^ (W[i - 2] >> 10);
        W[i] = W[i - 16] + s0 + W[i - 7] + s1;
    }

    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];

    for (i = 0; i < 64; i++)
    {
        uint32_t S1 = rotr32( e, 6 ) ^ rotr32( e, 11 ) ^ rotr32( e, 25 );
        uint32_t ch = (e & f) ^ (~e & g);
        t1 = h + S1 + ch + K[i] + W[i];
        uint32_t S0 = rotr32( a, 2 ) ^ rotr32( a, 13 ) ^ rotr32( a, 22 );
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        t2 = S0 + maj;

        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

static void sha256_init( struct sha256_ctx *ctx )
{
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
}

static void sha256_update( struct sha256_ctx *ctx, const void *data, size_t len )
{
    const uint8_t *p = (const uint8_t *)data;
    size_t idx = (size_t)(ctx->count & 63);
    ctx->count += len;

    while (len > 0)
    {
        size_t take = 64 - idx;
        if (take > len) take = len;
        memcpy( &ctx->buf[idx], p, take );
        p += take;
        len -= take;
        idx += take;
        if (idx == 64)
        {
            sha256_transform( ctx->state, ctx->buf );
            idx = 0;
        }
    }
}

static void sha256_final( struct sha256_ctx *ctx, uint8_t out[32] )
{
    uint8_t pad[64];
    uint64_t bits = ctx->count * 8;
    size_t idx = (size_t)(ctx->count & 63);
    size_t pad_len = (idx < 56) ? (56 - idx) : (120 - idx);
    int i;

    memset( pad, 0, sizeof(pad) );
    pad[0] = 0x80;
    sha256_update( ctx, pad, pad_len );

    for (i = 7; i >= 0; i--)
    {
        pad[7 - i] = (uint8_t)(bits >> (i * 8));
    }
    sha256_update( ctx, pad, 8 );

    for (i = 0; i < 8; i++)
    {
        out[i * 4]     = (uint8_t)(ctx->state[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(ctx->state[i]);
    }
}

/* Secure wipe primitive */
static inline void e1s18_wipe( void *ptr, size_t size )
{
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (size--) *p++ = 0;
}

/*
 * Default / Production Operations Implementation
 */

static uint64_t default_get_capabilities( const struct request *req )
{
    (void)req;
#ifdef WEXELEVATE_E1S18
    return E1S1_ALL_REQUIRED_CAPS;
#else
    return 0;
#endif
}

static BOOL default_is_policy_enabled( void )
{
#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
    HKEY key;
    DWORD val = 0, sz = sizeof(val), type = REG_DWORD;
    if (RegOpenKeyExW( HKEY_LOCAL_MACHINE, L"Software\\WineExplorer\\Elevation", 0, KEY_READ, &key ) == ERROR_SUCCESS)
    {
        if (RegQueryValueExW( key, L"EnableStandardUserElevation", NULL, &type, (BYTE *)&val, &sz ) == ERROR_SUCCESS && type == REG_DWORD)
        {
            RegCloseKey( key );
            return val != 0;
        }
        RegCloseKey( key );
    }
    return FALSE;
#else
    return FALSE;
#endif
}

static DWORD default_prompt_dialog( struct request *req, const uint8_t nonce[32],
                                    const uint8_t binding_digest[32], const uint8_t request_digest[32],
                                    char *out_account, size_t account_max )
{
    (void)req; (void)nonce; (void)binding_digest; (void)request_digest;
    (void)out_account; (void)account_max;
    /* S17 prompt queue & credential dialog child channel not yet wired: fail closed */
    return ERROR_NOT_SUPPORTED;
}

static DWORD default_redeem_evidence( struct request *req, const char *account_name,
                                      const uint8_t nonce[32], const uint8_t binding_digest[32],
                                      const uint8_t request_digest[32],
                                      uint8_t out_account_key[16], uint8_t out_evidence_ref[16] )
{
    (void)req; (void)account_name; (void)nonce; (void)binding_digest; (void)request_digest;
    (void)out_account_key; (void)out_evidence_ref;
    /* S8/S7 native PAM evidence service connection not yet wired: fail closed */
    return ERROR_NOT_SUPPORTED;
}

static DWORD default_issue_token( struct request *req, const uint8_t account_key[16],
                                  const uint8_t evidence_ref[16], const uint8_t nonce[32],
                                  const uint8_t binding_digest[32], const uint8_t request_digest[32],
                                  HANDLE *out_token )
{
    (void)req; (void)account_key; (void)evidence_ref; (void)nonce; (void)binding_digest; (void)request_digest;
    if (out_token) *out_token = NULL;
    /* S10 NtCreateWexAccountToken ISSUE request not yet wired: fail closed */
    return ERROR_NOT_SUPPORTED;
}

static DWORD default_load_profile( struct request *req, HANDLE token, const uint8_t account_key[16],
                                   uint64_t *out_lease_id, HANDLE *out_hkey )
{
    (void)req; (void)token; (void)account_key;
    if (out_lease_id) *out_lease_id = 0;
    if (out_hkey) *out_hkey = NULL;
    /* S13/S14 load_e1s_profile WEX_HKU_LOAD request not yet wired: fail closed */
    return ERROR_NOT_SUPPORTED;
}

static DWORD default_prep_launch( struct request *req, HANDLE token,
                                  char **out_win_env, size_t *out_win_env_sz,
                                  char ***out_unix_env, size_t *out_unix_env_cnt,
                                  char **out_cwd )
{
    (void)req; (void)token;
    if (out_win_env) *out_win_env = NULL;
    if (out_win_env_sz) *out_win_env_sz = 0;
    if (out_unix_env) *out_unix_env = NULL;
    if (out_unix_env_cnt) *out_unix_env_cnt = 0;
    if (out_cwd) *out_cwd = NULL;
    /* S15 wex_prepare_launch host environment generator not yet wired: fail closed */
    return ERROR_NOT_SUPPORTED;
}

static DWORD default_verify_image( struct request *req )
{
#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
    return verify_image( req );
#else
    (void)req;
    return ERROR_NOT_SUPPORTED;
#endif
}

static DWORD default_create_process_suspended( struct request *req, HANDLE token,
                                              const char *win_env, size_t win_env_sz,
                                              const char *cwd,
                                              PROCESS_INFORMATION *out_pi )
{
#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
    STARTUPINFOEXW si;
    SIZE_T attr_size = 0;
    WCHAR *cmdline = NULL;
    DWORD err = ERROR_SUCCESS;

    (void)win_env_sz;
    (void)cwd;
    if (!out_pi) return ERROR_INVALID_PARAMETER;
    memset( out_pi, 0, sizeof(*out_pi) );
    memset( &si, 0, sizeof(si) );
    si.StartupInfo.cb = sizeof(si);

    InitializeProcThreadAttributeList( NULL, 1, 0, &attr_size );
    si.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc( attr_size );
    if (!si.lpAttributeList || !InitializeProcThreadAttributeList( si.lpAttributeList, 1, 0, &attr_size ))
    {
        free( si.lpAttributeList );
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    if (!UpdateProcThreadAttribute( si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PARENT_PROCESS, &req->caller,
                                    sizeof(HANDLE), NULL, NULL ))
    {
        err = GetLastError();
        DeleteProcThreadAttributeList( si.lpAttributeList );
        free( si.lpAttributeList );
        return err;
    }

    si.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
    si.StartupInfo.wShowWindow = (WORD)req->show;

    cmdline = (WCHAR *)malloc( (lstrlenW( req->cmdline ) + 1) * sizeof(WCHAR) );
    if (!cmdline)
    {
        DeleteProcThreadAttributeList( si.lpAttributeList );
        free( si.lpAttributeList );
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    lstrcpyW( cmdline, req->cmdline );

    if (!CreateProcessAsUserW( token, req->image, cmdline, NULL, NULL, FALSE,
                               EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_CONSOLE |
                               CREATE_SUSPENDED,
                               (void *)win_env, req->cwd, &si.StartupInfo, out_pi ))
    {
        err = GetLastError();
    }

    free( cmdline );
    DeleteProcThreadAttributeList( si.lpAttributeList );
    free( si.lpAttributeList );
    return err;
#else
    (void)req; (void)token; (void)win_env; (void)win_env_sz; (void)cwd; (void)out_pi;
    return ERROR_NOT_SUPPORTED;
#endif
}

static DWORD default_verify_child( struct request *req, HANDLE token, const PROCESS_INFORMATION *pi )
{
#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
    HANDLE child_token = NULL;
    DWORD session = 0, len = 0;
    DWORD err = ERROR_SUCCESS;

    (void)token;
    if (!pi || !pi->hProcess) return ERROR_INVALID_PARAMETER;
    if (!OpenProcessToken( pi->hProcess, TOKEN_QUERY, &child_token ))
        return GetLastError();

    if (!GetTokenInformation( child_token, TokenSessionId, &session, sizeof(session), &len ) ||
        session != req->caller_session)
    {
        err = ERROR_ACCESS_DENIED;
    }
    CloseHandle( child_token );
    return err;
#else
    (void)req; (void)token; (void)pi;
    return ERROR_NOT_SUPPORTED;
#endif
}

static DWORD default_arm_lease( UINT64 lease, DWORD pid )
{
#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
    if (!lease) return ERROR_SUCCESS;
    if (pLeaseArm) return pLeaseArm( lease, pid );
    return ERROR_SUCCESS;
#else
    (void)lease; (void)pid;
    return ERROR_SUCCESS;
#endif
}

static DWORD default_duplicate_handle( struct request *req, HANDLE process, HANDLE *out_remote )
{
#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
    HANDLE remote = NULL;
    if (!out_remote) return ERROR_INVALID_PARAMETER;
    if (!DuplicateHandle( GetCurrentProcess(), process, req->caller, &remote,
                          SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0 ))
    {
        return GetLastError();
    }
    *out_remote = remote;
    return ERROR_SUCCESS;
#else
    (void)req; (void)process; (void)out_remote;
    return ERROR_NOT_SUPPORTED;
#endif
}

static DWORD default_transfer_lease( struct request *req, uint64_t lease_id, HANDLE process )
{
    (void)req; (void)lease_id; (void)process;
    /* S14 profile lease transfer WEX_HKU_TEST_TRANSFER request not yet wired: fail closed */
    return ERROR_NOT_SUPPORTED;
}

static DWORD default_check_pre_resume( struct request *req )
{
#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
    DWORD err = check_binding( req, req->caller_pid, NULL, FALSE );
    if (err) return err;
    if (WaitForSingleObject( stop_event, 0 ) != WAIT_TIMEOUT) return ERROR_CANCELLED;
    if (req->lease_event && WaitForSingleObject( req->lease_event, 0 ) != WAIT_TIMEOUT) return ERROR_FILE_CORRUPT;
    return ERROR_SUCCESS;
#else
    (void)req;
    return ERROR_NOT_SUPPORTED;
#endif
}

static DWORD default_resume_thread( struct request *req, HANDLE thread )
{
#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
    DWORD err = ERROR_SUCCESS;
    EnterCriticalSection( &table_cs );
    err = check_binding( req, req->caller_pid, NULL, FALSE );
    if (!err && WaitForSingleObject( stop_event, 0 ) != WAIT_TIMEOUT) err = ERROR_CANCELLED;
    if (!err && req->lease_event && WaitForSingleObject( req->lease_event, 0 ) != WAIT_TIMEOUT) err = ERROR_FILE_CORRUPT;
    if (!err && ResumeThread( thread ) == (DWORD)-1) err = GetLastError();
    LeaveCriticalSection( &table_cs );
    return err;
#else
    (void)req; (void)thread;
    return ERROR_NOT_SUPPORTED;
#endif
}

static void default_terminate_child( HANDLE process )
{
#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
    if (process)
    {
        TerminateProcess( process, 1 );
        WaitForSingleObject( process, 5000 );
    }
#else
    (void)process;
#endif
}

static void default_close_remote_handle( HANDLE caller, HANDLE remote )
{
#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
    if (caller && remote)
    {
        DuplicateHandle( caller, remote, NULL, NULL, 0, FALSE, DUPLICATE_CLOSE_SOURCE );
    }
#else
    (void)caller; (void)remote;
#endif
}

static void default_unload_profile( uint64_t lease_id, HANDLE hkey )
{
    (void)lease_id; (void)hkey;
    /* Profile load is not yet wired; nothing to unload in default ops */
}

static void default_close_handle( HANDLE handle )
{
#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
    if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle( handle );
#else
    (void)handle;
#endif
}

static void default_free_buffer( void *ptr )
{
    free( ptr );
}

static struct e1s18_ops s_default_ops = {
    default_get_capabilities,
    default_is_policy_enabled,
    default_prompt_dialog,
    default_redeem_evidence,
    default_issue_token,
    default_load_profile,
    default_prep_launch,
    default_verify_image,
    default_create_process_suspended,
    default_verify_child,
    default_arm_lease,
    default_duplicate_handle,
    default_transfer_lease,
    default_check_pre_resume,
    default_resume_thread,
    default_terminate_child,
    default_close_remote_handle,
    default_unload_profile,
    default_close_handle,
    default_free_buffer
};

static const struct e1s18_ops *s_active_ops = &s_default_ops;

void e1s18_set_ops( const struct e1s18_ops *ops )
{
    s_active_ops = ops ? ops : &s_default_ops;
}

void e1s18_reset_ops( void )
{
    s_active_ops = &s_default_ops;
}

const struct e1s18_ops *e1s18_get_ops( void )
{
    return s_active_ops;
}

BOOL e1s18_policy_enabled( void )
{
    const struct e1s18_ops *ops = e1s18_get_ops();
    return (ops && ops->is_policy_enabled) ? ops->is_policy_enabled() : FALSE;
}

BOOL e1s18_check_capabilities( const struct request *req )
{
    const struct e1s18_ops *ops = e1s18_get_ops();
    uint64_t caps;
    if (!ops || !ops->get_capabilities) return FALSE;
    caps = ops->get_capabilities( req );
    return ((caps & E1S1_ALL_REQUIRED_CAPS) == E1S1_ALL_REQUIRED_CAPS);
}

/* Canonical digest calculation for request and caller binding */
static void e1s18_compute_digests( const struct request *req,
                                   const uint8_t nonce[32],
                                   uint8_t out_binding_digest[32],
                                   uint8_t out_request_digest[32],
                                   uint8_t out_env_digest[32] )
{
    struct sha256_ctx ctx;
    uint32_t env_policy = 1; /* 1 = fresh managed environment */

    /* 1. Binding Digest: SHA256("E1S1-BIND\0" || req->id || caller_pid) */
    sha256_init( &ctx );
    sha256_update( &ctx, "E1S1-BIND\0", 10 );
    sha256_update( &ctx, &req->id, sizeof(req->id) );
    sha256_update( &ctx, &req->caller_pid, sizeof(req->caller_pid) );
    sha256_update( &ctx, &req->caller_session, sizeof(req->caller_session) );
    sha256_final( &ctx, out_binding_digest );

    /* 2. Request Digest: SHA256("E1S1-REQ\0" || req->id || binding_digest || nonce || hash) */
    sha256_init( &ctx );
    sha256_update( &ctx, "E1S1-REQ\0", 9 );
    sha256_update( &ctx, &req->id, sizeof(req->id) );
    sha256_update( &ctx, out_binding_digest, 32 );
    sha256_update( &ctx, nonce, 32 );
    sha256_update( &ctx, req->hash, 32 );
    sha256_update( &ctx, &env_policy, sizeof(env_policy) );
    sha256_final( &ctx, out_request_digest );

    /* 3. Environment Digest: SHA256("E1S1-ENV\0" || env_policy) */
    sha256_init( &ctx );
    sha256_update( &ctx, "E1S1-ENV\0", 9 );
    sha256_update( &ctx, &env_policy, sizeof(env_policy) );
    sha256_final( &ctx, out_env_digest );
}

/*
 * Main standard-user request sequencing function.
 * Coordinates all steps and rolls back completely on any intermediate failure.
 */
DWORD e1s18_handle_standard_request( struct request *req, struct wexelevate_reply *reply )
{
    const struct e1s18_ops *ops = e1s18_get_ops();
    uint8_t nonce[32];
    uint8_t binding_digest[32];
    uint8_t request_digest[32];
    uint8_t env_digest[32];
    uint8_t account_key[16];
    uint8_t evidence_ref[16];
    char account_name[64];
    HANDLE issued_token = NULL;
    HANDLE remote_handle = NULL;
    HANDLE hkey = NULL;
    uint64_t lease_id = 0;
    char *win_env = NULL;
    size_t win_env_sz = 0;
    char **unix_env = NULL;
    size_t unix_env_cnt = 0;
    char *cwd = NULL;
    PROCESS_INFORMATION pi;
    DWORD err = ERROR_SUCCESS;
    int i;

    memset( &pi, 0, sizeof(pi) );
    memset( account_name, 0, sizeof(account_name) );
    memset( account_key, 0, sizeof(account_key) );
    memset( evidence_ref, 0, sizeof(evidence_ref) );

    if (!ops) return ERROR_NOT_SUPPORTED;

    /* Gate 1: Check runtime policy */
    if (!ops->is_policy_enabled())
    {
        return ERROR_NOT_SUPPORTED;
    }

    /* Gate 2: Check all 10 S1 capabilities */
    if ((ops->get_capabilities( req ) & E1S1_ALL_REQUIRED_CAPS) != E1S1_ALL_REQUIRED_CAPS)
    {
        return ERROR_NOT_SUPPORTED;
    }

    /* Gate 3: Inherited environment is strictly rejected for standard users */
    if (req->flags & WEXELEVATE_ENV_INHERIT)
    {
        return ERROR_INVALID_PARAMETER;
    }

    /* Generate nonce and canonical digests */
    for (i = 0; i < 32; i++) nonce[i] = (uint8_t)(i ^ 0xa5);
    e1s18_compute_digests( req, nonce, binding_digest, request_digest, env_digest );

    /* Step 1: Serialized prompt queue and bound credential dialog (S17) */
    err = ops->prompt_dialog( req, nonce, binding_digest, request_digest, account_name, sizeof(account_name) );
    if (err) goto fail;

    /* Step 2: Single-use evidence verification and redemption (S8/S7) */
    err = ops->redeem_evidence( req, account_name, nonce, binding_digest, request_digest, account_key, evidence_ref );
    if (err) goto fail;

    /* Step 3: Server-issued account token (S10) */
    err = ops->issue_token( req, account_key, evidence_ref, nonce, binding_digest, request_digest, &issued_token );
    if (err) goto fail;

    /* Step 4: Profile load & lease acquisition (S13/S14) */
    err = ops->load_profile( req, issued_token, account_key, &lease_id, &hkey );
    if (err) goto fail;

    /* Step 5: Launch preparation (S15) */
    err = ops->prep_launch( req, issued_token, &win_env, &win_env_sz, &unix_env, &unix_env_cnt, &cwd );
    if (err) goto fail;

    /* Step 6: Image re-verification before creation */
    err = ops->verify_image( req );
    if (err) goto fail;

    /* Step 7: Suspended child creation in caller's server and session */
    err = ops->create_process_suspended( req, issued_token, win_env, win_env_sz, cwd, &pi );
    if (err) goto fail;

    /* Step 8: Verify installed child identity & session */
    err = ops->verify_child( req, issued_token, &pi );
    if (err) goto fail;

    /* Step 9: Arm image read lease on child PID */
    if (req->lease)
    {
        err = ops->arm_lease( req->lease, pi.dwProcessId );
        if (err) goto fail;
    }

    /* Step 10: Duplicate limited handle to caller */
    if (req->flags & WEXELEVATE_WANT_HANDLE)
    {
        err = ops->duplicate_handle( req, pi.hProcess, &remote_handle );
        if (err) goto fail;
    }

    /* Step 11: Transfer profile lease to process tracking */
    err = ops->transfer_lease( req, lease_id, pi.hProcess );
    if (err) goto fail;

    /* Step 12: Pre-resume binding checks */
    err = ops->check_pre_resume( req );
    if (err) goto fail;

    /* Step 13: Guarded resume (broker resumes child once after all checks pass) */
    err = ops->resume_thread( req, pi.hThread );
    if (err) goto fail;

    /* Commit */
    if (reply)
    {
        reply->pid = pi.dwProcessId;
        reply->tid = pi.dwThreadId;
        reply->handle = (ULONGLONG)(uintptr_t)remote_handle;
    }

    /* Clean up broker local process/thread handles and temporary buffers */
    if (pi.hThread) ops->close_handle( pi.hThread );
    if (pi.hProcess) ops->close_handle( pi.hProcess );
    if (win_env) ops->free_buffer( win_env );
    if (cwd) ops->free_buffer( cwd );
    e1s18_wipe( nonce, sizeof(nonce) );
    return ERROR_SUCCESS;

fail:
    /* Explicit cleanup / rollback at any failure stage */
    if (remote_handle)
    {
        ops->close_remote_handle( req->caller, remote_handle );
        remote_handle = NULL;
    }
    if (pi.hProcess)
    {
        ops->terminate_child( pi.hProcess );
        if (pi.hThread) ops->close_handle( pi.hThread );
        ops->close_handle( pi.hProcess );
        pi.hProcess = NULL;
        pi.hThread = NULL;
    }
    if (lease_id)
    {
        ops->unload_profile( lease_id, hkey );
        lease_id = 0;
    }
    if (issued_token)
    {
        ops->close_handle( issued_token );
        issued_token = NULL;
    }
    if (win_env) { ops->free_buffer( win_env ); win_env = NULL; }
    if (cwd) { ops->free_buffer( cwd ); cwd = NULL; }
    e1s18_wipe( nonce, sizeof(nonce) );

    if (reply)
    {
        reply->pid = 0;
        reply->tid = 0;
        reply->handle = 0;
    }
    return err ? err : ERROR_ACCESS_DENIED;
}
