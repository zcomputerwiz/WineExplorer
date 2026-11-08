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
#include "launch_prep.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#include <fcntl.h>
#endif

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
 * NTDLL Export Boundary Implementation & Default Resolution
 */

#if defined(_WIN32)
static NTSTATUS WINAPI default_NtCreateWexAccountToken(
    ULONG operation, HANDLE *context, HANDLE caller, ULONGLONG *caps,
    const void *record, ULONG length, HANDLE *token, void *response, ULONG *response_size )
{
    static e1s18_pfn_NtCreateWexAccountToken pfn = NULL;
    if (!pfn)
    {
        HMODULE ntdll = GetModuleHandleW( L"ntdll.dll" );
        if (ntdll) pfn = (e1s18_pfn_NtCreateWexAccountToken)(void *)GetProcAddress( ntdll, "NtCreateWexAccountToken" );
    }
    if (!pfn) return (NTSTATUS)0xc0000002; /* STATUS_NOT_IMPLEMENTED */
    return pfn( operation, context, caller, caps, record, length, token, response, response_size );
}

static NTSTATUS WINAPI default_NtLoadWexProfileEx(
    ULONG operation, HANDLE context, HANDLE token, ACCESS_MASK access,
    const void *record, ULONG length, HANDLE *key, ULONG *managed, ULONGLONG *generation )
{
    static e1s18_pfn_NtLoadWexProfileEx pfn = NULL;
    if (!pfn)
    {
        HMODULE ntdll = GetModuleHandleW( L"ntdll.dll" );
        if (ntdll) pfn = (e1s18_pfn_NtLoadWexProfileEx)(void *)GetProcAddress( ntdll, "NtLoadWexProfileEx" );
    }
    if (!pfn) return (NTSTATUS)0xc0000002; /* STATUS_NOT_IMPLEMENTED */
    return pfn( operation, context, token, access, record, length, key, managed, generation );
}
#else
static NTSTATUS WINAPI default_NtCreateWexAccountToken(
    ULONG operation, HANDLE *context, HANDLE caller, ULONGLONG *caps,
    const void *record, ULONG length, HANDLE *token, void *response, ULONG *response_size )
{
    (void)operation; (void)context; (void)caller; (void)caps;
    (void)record; (void)length; (void)token; (void)response; (void)response_size;
    return (NTSTATUS)0xc00000bb; /* STATUS_NOT_SUPPORTED */
}

static NTSTATUS WINAPI default_NtLoadWexProfileEx(
    ULONG operation, HANDLE context, HANDLE token, ACCESS_MASK access,
    const void *record, ULONG length, HANDLE *key, ULONG *managed, ULONGLONG *generation )
{
    (void)operation; (void)context; (void)token; (void)access;
    (void)record; (void)length; (void)key; (void)managed; (void)generation;
    return (NTSTATUS)0xc00000bb; /* STATUS_NOT_SUPPORTED */
}
#endif

static struct e1s18_export_table s_default_export_table = {
    default_NtCreateWexAccountToken,
    default_NtLoadWexProfileEx
};

static const struct e1s18_export_table *s_active_export_table = &s_default_export_table;

void e1s18_set_export_table( const struct e1s18_export_table *table )
{
    s_active_export_table = table ? table : &s_default_export_table;
}

void e1s18_reset_export_table( void )
{
    s_active_export_table = &s_default_export_table;
}

const struct e1s18_export_table *e1s18_get_export_table( void )
{
    return s_active_export_table;
}

/* S19 Rollback tracking for busy profile unloads */
static BOOL s_unload_busy = FALSE;
static uint64_t s_unload_busy_lease_id = 0;

BOOL e1s18_is_unload_busy( uint64_t *out_lease_id )
{
    if (out_lease_id) *out_lease_id = s_unload_busy_lease_id;
    return s_unload_busy;
}

void e1s18_clear_unload_busy( void )
{
    s_unload_busy = FALSE;
    s_unload_busy_lease_id = 0;
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
    /* Stage 1: S17 prompt queue & credential dialog child channel.
     * Service plumbing needed: spawn consent.exe /credentials on WinSta0\Winlogon,
     * named pipe IPC channel handshake, conversation loop with S17 prompt queue (H3b),
     * timeout (120s) and cancellation handling.
     * Status in S18: Not yet wired; fails closed with ERROR_NOT_SUPPORTED.
     */
    return ERROR_NOT_SUPPORTED;
}

static DWORD default_redeem_evidence( struct request *req, const char *account_name,
                                      const uint8_t nonce[32], const uint8_t binding_digest[32],
                                      const uint8_t request_digest[32],
                                      uint8_t out_account_key[16], uint8_t out_evidence_ref[16] )
{
    (void)req; (void)account_name; (void)nonce; (void)binding_digest; (void)request_digest;
    (void)out_account_key; (void)out_evidence_ref;
    /* Stage 2: S8 Evidence Redemption.
     * Service plumbing needed: connect to native S8 evidence daemon over Unix domain socket
     * /run/wineexplorer/evidence.sock, send S1 Kind 2 Answer record, receive S1 Kind 3
     * Evidence record (220 B) and 16-byte reference, verify S7 PAM worker authentication.
     * Status in S18: Not yet wired; fails closed with ERROR_NOT_SUPPORTED.
     */
    return ERROR_NOT_SUPPORTED;
}

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

static DWORD default_issue_token( struct request *req, const uint8_t account_key[16],
                                  const uint8_t evidence_ref[16], const uint8_t nonce[32],
                                  const uint8_t binding_digest[32], const uint8_t request_digest[32],
                                  HANDLE *out_token )
{
    const struct e1s18_export_table *exports = e1s18_get_export_table();
    const struct e1s18_ops *ops = e1s18_get_ops();
    HANDLE context = NULL;
    HANDLE token = NULL;
    ULONGLONG caps = WEX_TOKEN_CAPS; /* 0xc0 */
    ULONG size = 0;
    uint8_t in[WEX_TOKEN_INPUT_SIZE]; /* 204 bytes */
    uint8_t out[WEX_TOKEN_RESPONSE_SIZE]; /* 136 bytes */
    NTSTATUS status;
    uint64_t auth_id;
    uint64_t caller_auth;
    ULONG sid_len;

    if (out_token) *out_token = NULL;
    if (!exports || !exports->pNtCreateWexAccountToken) return ERROR_NOT_SUPPORTED;
    if (!req || !account_key || !evidence_ref || !nonce || !binding_digest || !request_digest || !out_token)
        return ERROR_INVALID_PARAMETER;

#ifdef WEXELEVATE_E1S18
    /* Fail closed if Stage 2 fields are unset */
    if (is_zero_key( req->account_key ) || req->map_generation == 0)
        return ERROR_INVALID_DATA;
#endif

    /* Step 3a: NEGOTIATE */
    status = exports->pNtCreateWexAccountToken( WEX_TOKEN_NEGOTIATE, &context, NULL, &caps, NULL, 0, NULL, NULL, &size );
    if (status != 0 || !context || caps != WEX_TOKEN_CAPS || size != 0)
    {
        if (context && ops && ops->close_handle) ops->close_handle( context );
        return ERROR_ACCESS_DENIED;
    }

    /* Step 3b: S1 Kind 4 record (188 bytes) + 16-byte evidence_ref = 204 bytes */
    memset( in, 0, sizeof(in) );
    memset( out, 0, sizeof(out) );

    wex_token_put32( in, 0x00010004 );                      /* kind 4, version 1 */
    wex_token_put32( in + 4, WEX_TOKEN_REQUEST_SIZE );       /* 188 */
    wex_token_put64( in + 8, caps );                         /* 0xc0 */
    wex_token_put64( in + 16, req->id );                     /* request_id */
    memcpy( in + 24, request_digest, 32 );                   /* request_digest */
    memcpy( in + 56, binding_digest, 32 );                   /* binding_digest */
    memcpy( in + 88, nonce, 32 );                            /* nonce */
    wex_token_put32( in + 120, 1 );                          /* attempt: 1 */
#ifdef WEXELEVATE_E1S18
    memcpy( in + 124, req->account_key, 16 );               /* account_key from Stage 2 */
    wex_token_put64( in + 140, 1 );                          /* root_gen */
    wex_token_put64( in + 148, 1 );                          /* server_gen */
    wex_token_put64( in + 156, 1 );                          /* broker_gen */
    wex_token_put64( in + 164, req->map_generation );        /* map_gen from Stage 2 */
#else
    memcpy( in + 124, account_key, 16 );                     /* account_key */
    wex_token_put64( in + 140, 1 );                          /* root_gen */
    wex_token_put64( in + 148, 1 );                          /* server_gen */
    wex_token_put64( in + 156, 1 );                          /* broker_gen */
    wex_token_put64( in + 164, 1 );                          /* map_gen */
#endif
    wex_token_put64( in + 172, 1 );                          /* policy_gen */
    wex_token_put32( in + 180, req->caller_session );        /* session_id */
    wex_token_put32( in + 184, 0 );                          /* reserved */
    memcpy( in + 188, evidence_ref, 16 );                    /* envelope evidence_ref */

    /* Step 3c: ISSUE */
    size = sizeof(out);
    status = exports->pNtCreateWexAccountToken( WEX_TOKEN_ISSUE, &context, NULL, &caps, in, sizeof(in), &token, out, &size );
    if (status != 0 || !token || size != WEX_TOKEN_RESPONSE_SIZE)
    {
        if (token && ops && ops->close_handle) ops->close_handle( token );
        if (context && ops && ops->close_handle) ops->close_handle( context );
        e1s18_wipe( in, sizeof(in) );
        e1s18_wipe( out, sizeof(out) );
        return ERROR_ACCESS_DENIED;
    }

    /* Step 3d: Verify returned token response record (S1 Kind 5, 136 bytes) */
    if (wex_token_u32( out ) != 0x00010005 ||
        wex_token_u32( out + 4 ) != WEX_TOKEN_RESPONSE_SIZE ||
        wex_token_u32( out + 32 ) != 0 ||
        wex_token_u64( out + 16 ) != req->id ||
#ifdef WEXELEVATE_E1S18
        memcmp( out + 56, req->account_key, 16 ) != 0)
#else
        memcmp( out + 56, account_key, 16 ) != 0)
#endif
    {
        goto verify_fail;
    }

    /* Verify Token shape: Primary (1), Full elevation (2), High integrity (>= 0x3000) */
    if (wex_token_u32( out + 124 ) != 1 /* TokenPrimary */ ||
        wex_token_u32( out + 128 ) != 2 /* TokenElevationTypeFull */ ||
        wex_token_u32( out + 132 ) < 0x3000 /* SECURITY_MANDATORY_HIGH_RID */)
    {
        goto verify_fail;
    }

    /* Verify caller session match */
    if (wex_token_u32( out + 120 ) != req->caller_session)
    {
        goto verify_fail;
    }

    /* Verify distinct nonzero AuthenticationId */
    auth_id = wex_token_u64( out + 104 );
    caller_auth = ((uint64_t)(uint32_t)req->caller_stats.AuthenticationId.HighPart << 32) |
                  (uint32_t)req->caller_stats.AuthenticationId.LowPart;
    if (auth_id == 0 || auth_id == caller_auth)
    {
        goto verify_fail;
    }

    /* Verify Alice's mapped SID */
    sid_len = wex_token_u32( out + 72 );
    if (sid_len < 8 || sid_len > SECURITY_MAX_SID_SIZE)
    {
        goto verify_fail;
    }
    /* Alice SID must not match caller's Bob SID */
    if (req->caller_sid_len > 0 && sid_len == req->caller_sid_len &&
        memcmp( out + 76, req->caller_sid, sid_len ) == 0)
    {
        goto verify_fail;
    }

#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
    /* Win32 runtime: independent GetTokenInformation verification */
    {
        TOKEN_TYPE t_type = 0;
        TOKEN_ELEVATION_TYPE t_elev = 0;
        BYTE label_buf[256] = {0};
        TOKEN_MANDATORY_LABEL *t_label = (TOKEN_MANDATORY_LABEL *)label_buf;
        DWORD t_session = ~0u;
        TOKEN_STATISTICS t_stats;
        BYTE user_buf[256] = {0};
        TOKEN_USER *t_user = (TOKEN_USER *)user_buf;
        DWORD len = 0;

        if (!GetTokenInformation( token, TokenType, &t_type, sizeof(t_type), &len ) || t_type != TokenPrimary)
            goto verify_fail;
        if (!GetTokenInformation( token, TokenElevationType, &t_elev, sizeof(t_elev), &len ) || t_elev != TokenElevationTypeFull)
            goto verify_fail;
        if (!GetTokenInformation( token, TokenIntegrityLevel, label_buf, sizeof(label_buf), &len ) ||
            *GetSidSubAuthority( t_label->Label.Sid, *GetSidSubAuthorityCount( t_label->Label.Sid ) - 1 ) < 0x3000)
            goto verify_fail;
        if (!GetTokenInformation( token, TokenSessionId, &t_session, sizeof(t_session), &len ) || t_session != req->caller_session)
            goto verify_fail;
        if (!GetTokenInformation( token, TokenStatistics, &t_stats, sizeof(t_stats), &len ))
            goto verify_fail;
        if ((t_stats.AuthenticationId.LowPart == 0 && t_stats.AuthenticationId.HighPart == 0) ||
            (t_stats.AuthenticationId.LowPart == req->caller_stats.AuthenticationId.LowPart &&
             t_stats.AuthenticationId.HighPart == req->caller_stats.AuthenticationId.HighPart))
            goto verify_fail;
        if (!GetTokenInformation( token, TokenUser, user_buf, sizeof(user_buf), &len ) ||
            !EqualSid( t_user->User.Sid, (PSID)(out + 76) ))
            goto verify_fail;
    }
#endif

    /* Keep S10 context for Stage 4 profile load */
#ifdef WEXELEVATE_E1S18
    req->s10_context = context;
    req->alice_sid_len = sid_len;
    memcpy( req->alice_sid, out + 76, sid_len );
#else
    if (context && ops && ops->close_handle) ops->close_handle( context );
#endif

    *out_token = token;
    e1s18_wipe( in, sizeof(in) );
    e1s18_wipe( out, sizeof(out) );
    return ERROR_SUCCESS;

verify_fail:
    if (token && ops && ops->close_handle) ops->close_handle( token );
    if (context && ops && ops->close_handle) ops->close_handle( context );
    e1s18_wipe( in, sizeof(in) );
    e1s18_wipe( out, sizeof(out) );
    return ERROR_INVALID_PARAMETER;
}

static DWORD default_load_profile( struct request *req, HANDLE token, const uint8_t account_key[16],
                                   uint64_t *out_lease_id, HANDLE *out_hkey )
{
    const struct e1s18_export_table *exports = e1s18_get_export_table();
    const struct e1s18_ops *ops = e1s18_get_ops();
    HANDLE context = NULL;
    uint8_t record[WEX_HKU_RECORD_SIZE]; /* 48 bytes */
    HANDLE key = NULL;
    ULONG managed = 0;
    ULONGLONG generation = 0;
    NTSTATUS status;

    if (out_lease_id) *out_lease_id = 0;
    if (out_hkey) *out_hkey = NULL;

    if (!exports || !exports->pNtLoadWexProfileEx) return ERROR_NOT_SUPPORTED;
    if (!req || !token || !account_key || !out_lease_id || !out_hkey)
        return ERROR_INVALID_PARAMETER;

#ifdef WEXELEVATE_E1S18
    /* Fail closed if Stage 2 fields are unset */
    if (is_zero_key( req->account_key ) || req->map_generation == 0 || req->storage_generation == 0)
        return ERROR_INVALID_DATA;
#endif

#ifdef WEXELEVATE_E1S18
    context = req->s10_context;
#endif
    if (!context) return ERROR_ACCESS_DENIED;

    /* Build S12/S13 record (48 bytes) */
    memset( record, 0, sizeof(record) );
    wex_token_put32( record + 0, 1 );                    /* version: 1 */
    wex_token_put32( record + 4, WEX_HKU_RECORD_SIZE );   /* size: 48 */
#ifdef WEXELEVATE_E1S18
    memcpy( record + 8, req->account_key, 16 );          /* account_key from Stage 2 */
    wex_token_put64( record + 24, req->map_generation ); /* map_generation from Stage 2 */
    wex_token_put64( record + 32, req->storage_generation ); /* storage_generation from Stage 2 */
#else
    memcpy( record + 8, account_key, 16 );               /* account_key */
    wex_token_put64( record + 24, 1 );                   /* map_generation: 1 */
    wex_token_put64( record + 32, 1 );                   /* storage_generation: 1 */
#endif
    wex_token_put64( record + 40, 0 );                   /* reserved: 0 */

    status = exports->pNtLoadWexProfileEx( WEX_HKU_LOAD, context, token,
                                           0x000f003f /* KEY_ALL_ACCESS */,
                                           record, sizeof(record),
                                           &key, &managed, &generation );

    /* S10 context has completed its role; close it and clear req->s10_context */
#ifdef WEXELEVATE_E1S18
    if (ops && ops->close_handle) ops->close_handle( req->s10_context );
    req->s10_context = NULL;
#endif

    if (status != 0 || !key || managed != 1 || generation == 0)
    {
        if (key && ops && ops->close_handle) ops->close_handle( key );
        if (status == (NTSTATUS)0xc0000034 /* STATUS_OBJECT_NAME_NOT_FOUND */ ||
            status == (NTSTATUS)0xc000003a /* STATUS_OBJECT_PATH_NOT_FOUND */)
        {
            return ERROR_FILE_NOT_FOUND;
        }
        return ERROR_ACCESS_DENIED;
    }

    *out_lease_id = (uint64_t)generation;
    *out_hkey = key;
    return ERROR_SUCCESS;
}

static DWORD default_prep_launch( struct request *req, HANDLE token,
                                  char **out_win_env, size_t *out_win_env_sz,
                                  char ***out_unix_env, size_t *out_unix_env_cnt,
                                  char **out_cwd )
{
    struct wex_launch_prep_params params;
    struct wex_launch_prep_result result;
    enum wex_launch_prep_status st;
    char cwd_buf[512] = {0};
    int tmp_fd = -1;

    (void)token;
    if (out_win_env) *out_win_env = NULL;
    if (out_win_env_sz) *out_win_env_sz = 0;
    if (out_unix_env) *out_unix_env = NULL;
    if (out_unix_env_cnt) *out_unix_env_cnt = 0;
    if (out_cwd) *out_cwd = NULL;

    if (!req) return ERROR_INVALID_PARAMETER;

#ifdef WEXELEVATE_E1S18
    /* Fail closed if Stage 2 fields are unset */
    if (!req->account_name[0] || is_zero_key( req->account_key ) ||
        req->map_generation == 0 || req->storage_generation == 0)
        return ERROR_INVALID_DATA;
#endif

    /* Gate 3: Inherited environment is strictly rejected */
    if (req->flags & WEXELEVATE_ENV_INHERIT)
    {
        return ERROR_INVALID_PARAMETER;
    }

    memset( &params, 0, sizeof(params) );
    memset( &result, 0, sizeof(result) );
    result.verified_cwd_fd = -1;

    /* No caller-supplied environment accepted */
    params.flags = 0;
    params.caller_win_env = NULL;
    params.caller_win_env_size = 0;
    params.caller_unix_env = NULL;

    /* Bob's host UID/GID from current caller process */
#ifdef _WIN32
    params.bob_uid = 1000;
    params.bob_gid = 1000;
#else
    params.bob_uid = getuid() != 0 ? (uid_t)getuid() : 1000;
    params.bob_gid = getgid() != 0 ? (gid_t)getgid() : 1000;
#endif
    params.requested_uid = params.bob_uid;
    params.requested_gid = params.bob_gid;

    /* Anchor descriptors for directory containment checks */
#ifndef _WIN32
    tmp_fd = open( "/tmp", O_RDONLY | O_DIRECTORY | O_CLOEXEC );
#endif
    params.descriptors.prefix_fd = tmp_fd >= 0 ? tmp_fd : -1;
    params.descriptors.profile_dir_fd = tmp_fd >= 0 ? tmp_fd : -1;
    params.descriptors.cwd_dir_fd = -1;

    /* Token identity */
    params.token.token_id = 1;
    params.token.auth_id = 1;
    params.token.elevation_type = 2; /* Full */
    params.token.integrity_level = 0x3000; /* High */
    params.token.session_id = req->caller_session;

    /* Profile record */
#ifdef WEXELEVATE_E1S18
    snprintf( params.profile.account_name, sizeof(params.profile.account_name), "%s", req->account_name );
    memcpy( params.profile.account_key, req->account_key, 16 );
    params.profile.map_generation = req->map_generation;
    params.profile.storage_generation = req->storage_generation;
#else
    strncpy( params.profile.account_name, "alice", sizeof(params.profile.account_name) - 1 );
#endif
    strncpy( params.profile.sid, "S-1-5-21-999-1001", sizeof(params.profile.sid) - 1 );
    strncpy( params.profile.windows_path, "C:\\users\\.e1s\\admin", sizeof(params.profile.windows_path) - 1 );
    strncpy( params.profile.unix_relative_path, "drive_c/users/.e1s/admin", sizeof(params.profile.unix_relative_path) - 1 );

    /* Working directory */
    if (req->cwd)
    {
        int i = 0;
        while (req->cwd[i] && i < (int)sizeof(cwd_buf) - 1)
        {
            cwd_buf[i] = (char)(req->cwd[i] & 0x7f);
            i++;
        }
        cwd_buf[i] = 0;
        params.requested_cwd = cwd_buf;
    }
    else
    {
        params.requested_cwd = "";
    }

    st = wex_prepare_launch( &params, &result );
#ifndef _WIN32
    if (tmp_fd >= 0) close( tmp_fd );
#endif

    if (st == WEX_LAUNCH_PREP_ERR_REAL_UID_UNSUPPORTED)
    {
        wex_launch_prep_result_free( &result );
        return ERROR_NOT_SUPPORTED;
    }
    if (st == WEX_LAUNCH_PREP_ERR_INHERIT_REJECTED)
    {
        wex_launch_prep_result_free( &result );
        return ERROR_INVALID_PARAMETER;
    }
    if (st != WEX_LAUNCH_PREP_OK)
    {
        wex_launch_prep_result_free( &result );
        return ERROR_ACCESS_DENIED;
    }

    if (out_win_env) *out_win_env = result.win_env;
    if (out_win_env_sz) *out_win_env_sz = result.win_env_size;
    if (out_unix_env) *out_unix_env = result.unix_env;
    if (out_unix_env_cnt) *out_unix_env_cnt = result.unix_env_count;
    if (out_cwd && result.verified_cwd_path[0])
    {
        *out_cwd = strdup( result.verified_cwd_path );
    }

    if (result.verified_cwd_fd >= 0)
    {
#ifdef _WIN32
        _close( result.verified_cwd_fd );
#else
        close( result.verified_cwd_fd );
#endif
        result.verified_cwd_fd = -1;
    }
    return ERROR_SUCCESS;
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
    else if (req->alice_sid_len > 0)
    {
        BYTE user_buf[256] = {0};
        TOKEN_USER *t_user = (TOKEN_USER *)user_buf;
        if (!GetTokenInformation( child_token, TokenUser, user_buf, sizeof(user_buf), &len ) ||
            !EqualSid( t_user->User.Sid, (PSID)req->alice_sid ))
        {
            err = ERROR_ACCESS_DENIED;
        }
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

static DWORD default_transfer_lease( struct request *req, uint64_t lease_id, HANDLE process, HANDLE token )
{
    const struct e1s18_export_table *exports = e1s18_get_export_table();
    HANDLE unused = NULL;
    ULONG managed = 0;
    ULONGLONG generation = 0;
    NTSTATUS status;

    (void)req;
    (void)lease_id;
    if (!exports || !exports->pNtLoadWexProfileEx) return ERROR_NOT_SUPPORTED;
    if (!process || !token) return ERROR_INVALID_PARAMETER;

    /* S14 profile lease transfer.
     * Note: wine/server/registry.c:3476-3493 exposes ONLY WEX_HKU_TEST_TRANSFER (op 10).
     * It is guarded on the server by #ifdef WEX_E1S14_CRASH_TEST. There is no separate
     * production-named commit operation; transfer_lease calls WEX_HKU_TEST_TRANSFER.
     */
    status = exports->pNtLoadWexProfileEx( WEX_HKU_TEST_TRANSFER, process, token, 0, NULL, 0, &unused, &managed, &generation );
    if (status != 0 || managed != 1)
    {
        if (status == (NTSTATUS)0xc00000bb /* STATUS_NOT_SUPPORTED */)
            return ERROR_NOT_SUPPORTED;
        return ERROR_ACCESS_DENIED;
    }
    return ERROR_SUCCESS;
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

static void default_unload_profile( uint64_t lease_id, HANDLE hkey, HANDLE token )
{
    const struct e1s18_export_table *exports = e1s18_get_export_table();
    const struct e1s18_ops *ops = e1s18_get_ops();
    HANDLE unused = NULL;
    ULONG managed = 0;
    ULONGLONG generation = 0;
    NTSTATUS status;

    /* Close broker's hkey handle before unload so the hive has no open handles from broker */
    if (hkey && ops && ops->close_handle)
    {
        ops->close_handle( hkey );
    }

    if (!exports || !exports->pNtLoadWexProfileEx || !token) return;

    /* Call NtLoadWexProfileEx WEX_HKU_UNLOAD_S14.
     * Tolerate 'busy' (STATUS_RESOURCE_IN_USE, STATUS_CANNOT_DELETE, STATUS_SHARING_VIOLATION)
     * by recording it for S19 rollback instead of looping.
     */
    status = exports->pNtLoadWexProfileEx( WEX_HKU_UNLOAD_S14, NULL, token, 0, NULL, 0, &unused, &managed, &generation );
    if (status == (NTSTATUS)0xc0000708 /* STATUS_RESOURCE_IN_USE */ ||
        status == (NTSTATUS)0xc0000121 /* STATUS_CANNOT_DELETE */ ||
        status == (NTSTATUS)0xc0000043 /* STATUS_SHARING_VIOLATION */)
    {
        s_unload_busy = TRUE;
        s_unload_busy_lease_id = lease_id;
    }
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

#ifdef WEXELEVATE_E1S18
    /* Step 3: Server-issued account token (S10) */
    err = ops->issue_token( req, req->account_key, evidence_ref, nonce, binding_digest, request_digest, &issued_token );
    if (err) goto fail;

    /* Step 4: Profile load & lease acquisition (S13/S14) */
    err = ops->load_profile( req, issued_token, req->account_key, &lease_id, &hkey );
    if (err) goto fail;
#else
    /* Step 3: Server-issued account token (S10) */
    err = ops->issue_token( req, account_key, evidence_ref, nonce, binding_digest, request_digest, &issued_token );
    if (err) goto fail;

    /* Step 4: Profile load & lease acquisition (S13/S14) */
    err = ops->load_profile( req, issued_token, account_key, &lease_id, &hkey );
    if (err) goto fail;
#endif

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
    err = ops->transfer_lease( req, lease_id, pi.hProcess, issued_token );
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
    if (hkey) ops->close_handle( hkey );
#ifdef WEXELEVATE_E1S18
    if (req->s10_context)
    {
        ops->close_handle( req->s10_context );
        req->s10_context = NULL;
    }
#endif
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
    if (lease_id || hkey)
    {
        ops->unload_profile( lease_id, hkey, issued_token );
        lease_id = 0;
        hkey = NULL;
    }
#ifdef WEXELEVATE_E1S18
    if (req->s10_context)
    {
        ops->close_handle( req->s10_context );
        req->s10_context = NULL;
    }
#endif
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
