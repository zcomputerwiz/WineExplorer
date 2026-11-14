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
#include "cred_dialog.h"
#include "launch_prep.h"
#ifdef __WINE_PE_BUILD
#include "cred_dialog.c"
#include "launch_prep.c"
#endif
#include "e1s6_protocol.h"
#include "e1s_encode.h"
#ifdef WEXELEVATE_E1S18
#include "e1r8_codec.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <io.h>
#include <bcrypt.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/random.h>
#endif

static inline void e1s18_safe_copy_str( char *dst, const char *src, size_t max_size )
{
    size_t i = 0;
    if (!dst || !max_size) return;
    if (src)
    {
        while (i + 1 < max_size && src[i])
        {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = '\0';
}

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

#ifdef WEX_E1_TEST_HOOKS
/* Test hook, compiled only with WEX_E1_TEST_HOOKS (docs/E1_PRODUCTION_PLAN.md, "Test hooks
 * that must never ship"): injects fakes at the NTDLL export boundary for unit tests. */
void e1s18_set_export_table( const struct e1s18_export_table *table )
{
    s_active_export_table = table ? table : &s_default_export_table;
}

void e1s18_reset_export_table( void )
{
    s_active_export_table = &s_default_export_table;
}
#endif /* WEX_E1_TEST_HOOKS */

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

/*
 * R15: The EnableStandardUserElevation policy DWORD enables standard-user
 * elevation only when it is exactly 1. Any other value (0, 2, 0xffffffff, ...)
 * leaves the feature disabled.
 */
BOOL e1s18_policy_value_enabled( DWORD val )
{
    return val == 1;
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
            return e1s18_policy_value_enabled( val );
        }
        RegCloseKey( key );
    }
    return FALSE;
#else
    return FALSE;
#endif
}

/*
 * Dialog & Transport Boundary Implementation & Default Resolution
 */

static struct e1s18_boundary_table s_default_boundary_table = { NULL, NULL };
static const struct e1s18_boundary_table *s_active_boundary_table = &s_default_boundary_table;

#ifdef WEX_E1_TEST_HOOKS
/* Test hook, compiled only with WEX_E1_TEST_HOOKS (docs/E1_PRODUCTION_PLAN.md, "Test hooks
 * that must never ship"): injects fakes at the dialog/transport boundary for unit tests. */
void e1s18_set_boundary_table( const struct e1s18_boundary_table *table )
{
    s_active_boundary_table = table ? table : &s_default_boundary_table;
}

void e1s18_reset_boundary_table( void )
{
    s_active_boundary_table = &s_default_boundary_table;
}
#endif /* WEX_E1_TEST_HOOKS */

const struct e1s18_boundary_table *e1s18_get_boundary_table( void )
{
    return s_active_boundary_table;
}

/*
 * Monotonic Time Provider & Test Clock
 *
 * WO-16: S1 says all times are nanoseconds of the verifier host's
 * CLOCK_MONOTONIC. The PE build takes the time only from the wexhost
 * WexMonotonicNs export (the native transport); native builds keep
 * clock_gettime( CLOCK_MONOTONIC ). A missing export or a failure returns 0,
 * which the callers already treat as a denial. The test clock hooks stay
 * first so existing tests stay deterministic.
 */
#ifdef WEX_E1_TEST_HOOKS
/* Test hook state, compiled only with WEX_E1_TEST_HOOKS (docs/E1_PRODUCTION_PLAN.md,
 * "Test hooks that must never ship"): a deterministic clock for unit tests. */
static uint64_t s_test_clock_now = 0;
static BOOL s_test_clock_fail = FALSE;
#endif /* WEX_E1_TEST_HOOKS */

/* The admission deadline lives in the lab-only part of struct request; outside lab builds the
 * standard-user path is denied by the policy gate before any deadline is used. */
#ifdef WEXELEVATE_E1S18
#define E1S18_DEADLINE(req) ((req)->deadline)
#else
#define E1S18_DEADLINE(req) ((uint64_t)0)
#endif

#ifdef WEX_E1_TEST_HOOKS
/* Test hook, compiled only with WEX_E1_TEST_HOOKS (docs/E1_PRODUCTION_PLAN.md,
 * "Test hooks that must never ship"): sets the deterministic clock for unit tests. */
void e1s18_set_test_clock( uint64_t now )
{
    s_test_clock_now = now;
}

void e1s18_set_test_clock_fail( BOOL fail )
{
    s_test_clock_fail = fail;
}
#endif /* WEX_E1_TEST_HOOKS */

static uint64_t e1s18_get_monotonic_time( void )
{
#ifdef WEX_E1_TEST_HOOKS
    /* Test hook, compiled only with WEX_E1_TEST_HOOKS: the deterministic clock
     * takes precedence so existing tests stay deterministic. */
    if (s_test_clock_fail) return 0;
    if (s_test_clock_now) return s_test_clock_now;
#endif /* WEX_E1_TEST_HOOKS */
#if defined(_WIN32)
    /* PE build: the verifier host's CLOCK_MONOTONIC via wexhost, loaded once. */
    typedef DWORD (WINAPI *pfn_WexMonotonicNs)( UINT64 * );
    static pfn_WexMonotonicNs pfn = NULL;
    static BOOL resolved = FALSE;
    UINT64 now = 0;
    if (!resolved)
    {
        resolved = TRUE;
        HMODULE wexhost = GetModuleHandleW( L"wexhost.dll" );
        if (!wexhost) wexhost = LoadLibraryW( L"wexhost.dll" );
        if (wexhost) pfn = (pfn_WexMonotonicNs)(void *)GetProcAddress( wexhost, "WexMonotonicNs" );
    }
    if (!pfn) return 0;
    if (pfn( &now ) != ERROR_SUCCESS || now == 0) return 0;
    return (uint64_t)now;
#elif defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime( CLOCK_MONOTONIC, &ts ) == 0)
        return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
    return 0;
#else
    return 0;
#endif
}

/*
 * R4: Cryptographic random source for the attempt nonce and channel id.
 * Windows: BCryptGenRandom with the system-preferred RNG.
 * Native: getrandom(2), full length required.
 * Test hooks: e1s18_set_test_random() supplies deterministic bytes so existing
 * tests stay deterministic; e1s18_set_test_random_fail() forces the RNG to fail
 * so the fail-closed denial path can be exercised.
 */
#ifdef WEX_E1_TEST_HOOKS
/* Test hook state, compiled only with WEX_E1_TEST_HOOKS (docs/E1_PRODUCTION_PLAN.md,
 * "Test hooks that must never ship"): deterministic RNG bytes / forced failure. */
static uint8_t s_test_random[256];
static size_t s_test_random_len = 0;
static size_t s_test_random_pos = 0;
static BOOL s_test_random_fail = FALSE;

void e1s18_set_test_random( const uint8_t *bytes, size_t len )
{
    if (bytes && len > 0 && len <= sizeof(s_test_random))
    {
        memcpy( s_test_random, bytes, len );
        s_test_random_len = len;
    }
    else
    {
        s_test_random_len = 0;
    }
    s_test_random_pos = 0;
}

void e1s18_set_test_random_fail( BOOL fail )
{
    s_test_random_fail = fail;
}
#endif /* WEX_E1_TEST_HOOKS */

static BOOL e1s18_fill_random( uint8_t *buf, size_t len )
{
#ifdef WEX_E1_TEST_HOOKS
    size_t i;
    /* Test hook, compiled only with WEX_E1_TEST_HOOKS: deterministic bytes or a
     * forced failure so existing tests stay deterministic / fail-closed. */
    if (s_test_random_fail)
        return FALSE;
    if (s_test_random_len)
    {
        for (i = 0; i < len; i++)
            buf[i] = s_test_random[(s_test_random_pos++) % s_test_random_len];
        return TRUE;
    }
#endif /* WEX_E1_TEST_HOOKS */
#if defined(_WIN32)
    return BCryptGenRandom( NULL, buf, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG ) == 0;
#else
    return getrandom( buf, len, 0 ) == (ssize_t)len;
#endif
}

static BOOL e1s18_is_all_zero( const uint8_t *buf, size_t len )
{
    size_t i;
    for (i = 0; i < len; i++)
        if (buf[i]) return FALSE;
    return TRUE;
}

/*
 * WO-14: S1 endorsed-identity context. Zero in production (the request
 * denies before any dialog); tests set it through e1s18_set_s1_context.
 */
static struct e1s18_s1_context s_s1_context;

void e1s18_set_s1_context( const struct e1s18_s1_context *ctx )
{
    if (ctx) s_s1_context = *ctx;
    else memset( &s_s1_context, 0, sizeof(s_s1_context) );
}

#ifdef WEX_E1_TEST_HOOKS
/* Test hook, compiled only with WEX_E1_TEST_HOOKS (docs/E1_PRODUCTION_PLAN.md,
 * "Test hooks that must never ship"): resets the S1 context between unit tests. */
void e1s18_reset_s1_context( void )
{
    memset( &s_s1_context, 0, sizeof(s_s1_context) );
}
#endif /* WEX_E1_TEST_HOOKS */

/* Every required S1 input must be nonzero; any zero input means the endorsed
 * identities/generations are not yet established, so the request denies. */
static BOOL e1s18_s1_context_complete( const struct e1s18_s1_context *c )
{
    return c->root_gen && c->server_gen && c->broker_gen &&
           c->verifier_gen && c->map_gen && c->policy_gen &&
           c->host_uid && c->deadline &&
           !e1s18_is_all_zero( c->prefix, 16 ) &&
           !e1s18_is_all_zero( c->host_process, 16 ) &&
           !e1s18_is_all_zero( c->opener, 16 ) &&
           !e1s18_is_all_zero( c->writer, 16 ) &&
           !e1s18_is_all_zero( c->desktop, 16 ) &&
           !e1s18_is_all_zero( c->channel, 16 ) &&
           c->volume_serial && c->file_index && c->file_size && c->last_write;
}

#ifdef WEXELEVATE_E1S18
/*
 * WO-20: R8 attempt-tuple context. Zero in the lab today; any zero required
 * input denies the request with ERROR_NOT_SUPPORTED before a pipe or process
 * is created (the WO-14 pattern). Tests set it through e1s18_set_r8_context.
 */
static struct e1s18_r8_context s_r8_context;

#ifdef WEX_E1_TEST_HOOKS
/* Test hook, compiled only with WEX_E1_TEST_HOOKS (docs/E1_PRODUCTION_PLAN.md,
 * "Test hooks that must never ship"): presets the R8 context for unit tests. */
void e1s18_set_r8_context( const struct e1s18_r8_context *ctx )
{
    if (ctx) s_r8_context = *ctx;
    else memset( &s_r8_context, 0, sizeof(s_r8_context) );
}

void e1s18_reset_r8_context( void )
{
    memset( &s_r8_context, 0, sizeof(s_r8_context) );
}
#endif /* WEX_E1_TEST_HOOKS */

/* Every required R8 input must be nonzero/valid; any zero input means the
 * sealed-tuple inputs are not yet established, so the request denies.
 * WO-25: dialog_id is assigned by the server at endorsement, so it is not
 * required for the pre-endorsement context; everything else must be set. */
static BOOL e1s18_r8_context_complete( const struct e1s18_r8_context *c )
{
    size_t vo_len = strlen( c->verifier_owner );
    return !e1s18_is_all_zero( c->desktop_route, 16 ) &&
           c->caller_session &&
           c->root_gen && c->server_gen && c->broker_gen &&
           c->verifier_gen && c->map_gen && c->policy_gen &&
           e1r8_valid_verifier_owner( (const uint8_t *)c->verifier_owner, vo_len );
}

/*
 * WO-28: S4 account resolution for R8. Looks up the account in accounts.map
 * (or the test override if set) and fills the 16-byte account key. An unknown
 * or inactive account yields the 16-byte zero key (the decoy).
 * Lab limit (Claude review): this reads the map file itself through the Z:
 * drive. Before the chain leaves the lab it must ask the server instead
 * (wine/server/accounts.c lookup_account, with its protected-directory and
 * ownership checks); the final account is resolved again there (spec section 8).
 */
#ifdef WEX_E1_TEST_HOOKS
/* Test hook state, compiled only with WEX_E1_TEST_HOOKS (docs/E1_PRODUCTION_PLAN.md,
 * "Test hooks that must never ship"): a test override for the accounts map path. */
static const char *s_r8_test_accounts_map = NULL;

void e1s18_set_test_accounts_map( const char *path )
{
    s_r8_test_accounts_map = path;
}
#endif /* WEX_E1_TEST_HOOKS */

int e1s18_resolve_account_s4( const char *name, uint8_t key[16] )
{
    FILE *f = NULL;
    char line[1024];
    int found = 0;

    memset( key, 0, 16 );
    if (!name || !*name || !e1r8_valid_account_name( (const uint8_t *)name, strlen( name ) ))
        return 0;

#ifdef WEX_E1_TEST_HOOKS
    /* Test hook, compiled only with WEX_E1_TEST_HOOKS: a test override for the
     * accounts map path. */
    if (s_r8_test_accounts_map)
        f = fopen( s_r8_test_accounts_map, "r" );
#endif /* WEX_E1_TEST_HOOKS */
    if (!f)
        f = fopen( "Z:\\etc\\wineexplorer\\accounts.map", "r" );
    if (!f)
        f = fopen( "/etc/wineexplorer/accounts.map", "r" );
    if (!f)
        return 0;

    if (fgets( line, sizeof(line), f ))
    {
        while (fgets( line, sizeof(line), f ))
        {
            char *p = line;
            char *f_name, *f_key, *f_active;
            size_t l = strlen( line );
            while (l > 0 && (line[l - 1] == '\r' || line[l - 1] == '\n'))
                line[--l] = '\0';

            f_name = p;
            p = strchr( p, ' ' );
            if (!p) continue;
            *p++ = '\0';

            /* skip uid */
            p = strchr( p, ' ' );
            if (!p) continue;
            *p++ = '\0';

            f_key = p;
            p = strchr( p, ' ' );
            if (!p) continue;
            *p++ = '\0';

            f_active = p;
            p = strchr( p, ' ' );
            if (p) *p = '\0';

            if (!strcmp( f_name, name ) && !strcmp( f_active, "active" ))
            {
                if (strlen( f_key ) == 32)
                {
                    int i;
                    for (i = 0; i < 16; i++)
                    {
                        char hex[3] = { f_key[2 * i], f_key[2 * i + 1], 0 };
                        key[i] = (uint8_t)strtoul( hex, NULL, 16 );
                    }
                    found = 1;
                    break;
                }
            }
        }
    }
    fclose( f );
    if (!found)
        memset( key, 0, 16 );
    return 0;
}

/*
 * WO-20: Build the sealed attempt tuple for the R8 INIT/ACK exchange.
 * The request_id, attempt, nonce, channel_id, binding_digest, request_digest
 * and deadline come from the request and the in-flight attempt; the dialog_id,
 * verifier generations, caller_session and desktop_route come from the R8
 * context; desktop_mode is 1 (secure) or 2 (insecure). Returns 0 on success,
 * -1 if the R8 context is incomplete (the caller denies with
 * ERROR_NOT_SUPPORTED).
 */
int e1s18_build_r8_tuple( const struct request *req,
                          uint64_t request_id,
                          uint32_t attempt,
                          const uint8_t nonce[32],
                          const uint8_t channel_id[16],
                          const uint8_t binding_digest[32],
                          const uint8_t request_digest[32],
                          BOOL secure,
                          struct e1r8_tuple *out )
{
    const struct e1s18_r8_context *c = &s_r8_context;
    if (!e1s18_r8_context_complete( c )) return -1;
    memset( out, 0, sizeof(*out) );
    out->request_id = request_id;
    out->attempt = attempt;
    memcpy( out->nonce, nonce, 32 );
    memcpy( out->channel_id, channel_id, 16 );
    memcpy( out->binding_digest, binding_digest, 32 );
    memcpy( out->request_digest, request_digest, 32 );
    out->deadline = E1S18_DEADLINE( req );
    out->root_gen = c->root_gen;
    out->server_gen = c->server_gen;
    out->broker_gen = c->broker_gen;
    out->verifier_gen = c->verifier_gen;
    out->map_gen = c->map_gen;
    out->policy_gen = c->policy_gen;
    out->caller_session = c->caller_session;
    memcpy( out->desktop_route, c->desktop_route, 16 );
    out->desktop_mode = secure ? E1R8_DESKTOP_MODE_SECURE : E1R8_DESKTOP_MODE_INSECURE;
    memcpy( out->dialog_id, c->dialog_id, 16 );
    return 0;
}

/*
 * WO-20: The request's validated display strings are already UTF-16LE
 * (WCHAR*). Return the byte length of a display string, checking the 520-byte
 * bound and the section 4 display-string rules; -1 denies. A NULL string is
 * the empty (0-byte) display string.
 */
static int e1s18_display_string_len( const WCHAR *s, uint32_t *out_len )
{
    uint32_t chars = 0;
    if (!s) { *out_len = 0; return 0; }
    while (s[chars]) chars++;
    if (chars * 2 > 520) return -1;
    if (!e1r8_valid_display_string( (const uint8_t *)s, chars * 2 )) return -1;
    *out_len = chars * 2;
    return 0;
}

/*
 * WO-20: Encode the R8 INIT message (type 1) for the dialog. The image is the
 * request's validated image path; the verifier owner comes from the R8
 * context. The request carries no requester name and the broker checks no
 * publisher yet, so both are sent empty (never a stand-in such as the command
 * line or working directory); filling them is R8 step 8. Returns the encoded
 * length on success, 0 on denial (incomplete context or an invalid string).
 */
size_t e1s18_encode_r8_init( const struct request *req,
                             const struct e1r8_tuple *t,
                             uint8_t *buf, size_t buf_len )
{
    const struct e1s18_r8_context *c = &s_r8_context;
    uint32_t vo_len, img_len, total;
    if (!req->image || !req->image[0]) return 0;
    if (e1s18_display_string_len( req->image, &img_len )) return 0;
    vo_len = (uint32_t)strlen( c->verifier_owner );
    total = E1R8_HEADER_SIZE + E1R8_TAIL_SIZE + (4 + vo_len) + 4 + (4 + img_len) + 4;
    if (total > buf_len) return 0;
    if (e1r8_encode_init( buf, total, t,
                          (const uint8_t *)c->verifier_owner, vo_len,
                          NULL, 0,
                          (const uint8_t *)req->image, img_len,
                          NULL, 0 ))
        return 0;
    return total;
}

/*
 * WO-20: Decode and verify the R8 ACK (type 2) from the dialog. The ACK must
 * be exactly 156 bytes and carry the tuple's binding_digest, request_digest
 * and dialog_id. Returns 0 on success, negative on any rejection.
 */
int e1s18_verify_r8_ack( const uint8_t *buf, size_t len,
                         const struct e1r8_tuple *t )
{
    struct e1r8_ack ack;
    return e1r8_decode_ack( buf, len, t, &ack );
}

/* WO-25: test flag: a preset complete context skips prepare only when this is
 * set; no production path skips it. Compiled only with WEX_E1_TEST_HOOKS
 * (docs/E1_PRODUCTION_PLAN.md, "Test hooks that must never ship"). */
#ifdef WEX_E1_TEST_HOOKS
static BOOL s_r8_skip_prepare = FALSE;
#endif /* WEX_E1_TEST_HOOKS */
/* WO-25: the S6 channel of the attempt in flight (single in flight, WO-15). */
static UINT64 s_r8_channel;
#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
static HANDLE s_r9_pipe = INVALID_HANDLE_VALUE;
static struct e1r8_tuple s_r9_tuple;
#endif

#ifdef WEX_E1_TEST_HOOKS
/* Test hook, compiled only with WEX_E1_TEST_HOOKS (docs/E1_PRODUCTION_PLAN.md,
 * "Test hooks that must never ship"): sets the skip-prepare flag for unit tests. */
void e1s18_set_r8_skip_prepare( BOOL skip )
{
    s_r8_skip_prepare = skip;
}
#endif /* WEX_E1_TEST_HOOKS */

#ifdef _WIN32
typedef int (__cdecl *e1s18_pfn_wine_dbg_output)( const char * );

static void r8_trace_line( const char *msg )
{
    char buf[256];
    HMODULE ntdll = GetModuleHandleW( L"ntdll.dll" );
    snprintf( buf, sizeof(buf), "%s\n", msg );
    if (ntdll)
    {
        e1s18_pfn_wine_dbg_output dbg_out = (e1s18_pfn_wine_dbg_output)(void *)GetProcAddress( ntdll, "__wine_dbg_output" );
        if (dbg_out) dbg_out( buf );
    }
    fprintf( stderr, "%s", buf );
    fflush( stderr );
}

static void r8_trace_err( const char *step, unsigned long err )
{
    char buf[256];
    HMODULE ntdll = GetModuleHandleW( L"ntdll.dll" );
    snprintf( buf, sizeof(buf), "r8: %s failed %lu\n", step, err );
    if (ntdll)
    {
        e1s18_pfn_wine_dbg_output dbg_out = (e1s18_pfn_wine_dbg_output)(void *)GetProcAddress( ntdll, "__wine_dbg_output" );
        if (dbg_out) dbg_out( buf );
    }
    fprintf( stderr, "%s", buf );
    fflush( stderr );
}
#else
static void r8_trace_line( const char *msg )
{
    fprintf( stderr, "%s\n", msg );
    fflush( stderr );
}

static void r8_trace_err( const char *step, unsigned long err )
{
    fprintf( stderr, "r8: %s failed %lu\n", step, err );
    fflush( stderr );
}
#endif

/*
 * WO-25: Fill the R8 context from live inputs instead of relying on a preset
 * context. r8_open (the S6 OPEN record), r8_binding (the BINDING reply) and
 * r8_owner (the verifier owner) are called in that order. The BINDING
 * server_gen/broker_gen must equal the broker registration's, every parsed
 * value must be nonzero, and the owner must be a valid D-Bus unique name. The
 * context is filled from the BINDING (root_gen, map_gen, policy_gen,
 * caller_session, desktop_route) and the S6 record (server_gen, broker_gen,
 * verifier_gen, owner); dialog_id stays zero (the server assigns it at
 * endorsement). The S6 channel is closed on every path after a successful
 * OPEN. Returns ERROR_SUCCESS on success, or the denial.
 */
#ifdef _WIN32
static inline uint64_t e1s18_filetime_u64( const FILETIME *ft )
{
    return ((uint64_t)ft->dwHighDateTime << 32) | ft->dwLowDateTime;
}
#define E1S18_REQ_LAST_WRITE(req) e1s18_filetime_u64( &(req)->info.ftLastWriteTime )
#else
#define E1S18_REQ_LAST_WRITE(req) ((req)->info.ftLastWriteTime)
#endif

DWORD e1s18_r8_prepare( struct request *req )
{
    const struct e1s18_ops *ops = e1s18_get_ops();
    unsigned char reg[E1S6_OPEN_SIZE];
    unsigned char binding[132];
    char owner[256];
    UINT64 channel = 0;
    uint64_t reg_server_gen, reg_broker_gen, reg_verifier_gen;
    uint64_t b_server_gen, b_root_gen, b_broker_gen, b_map_gen, b_policy_gen;
    uint64_t b_host_uid;
    uint32_t b_caller_session;
    DWORD err = ERROR_SUCCESS;
    int opened = 0;

    if (!req || !ops) return ERROR_INVALID_PARAMETER;

#ifdef WEX_E1_TEST_HOOKS
    /* A preset complete context skips prepare only when the test flag says so. */
    if (s_r8_skip_prepare && e1s18_r8_context_complete( &s_r8_context ))
        return ERROR_SUCCESS;
#endif /* WEX_E1_TEST_HOOKS */

    memset( reg, 0, sizeof(reg) );
    memset( binding, 0, sizeof(binding) );
    memset( owner, 0, sizeof(owner) );

    if (!ops->r8_open) { err = ERROR_NOT_SUPPORTED; goto fail; }
    err = ops->r8_open( req, reg, &channel );
    if (err || !channel) { err = err ? err : ERROR_ACCESS_DENIED; goto fail; }
    opened = 1;

    if (!ops->r8_binding) { err = ERROR_NOT_SUPPORTED; goto close; }
    err = ops->r8_binding( req, binding );
    if (err) goto close;

    if (!ops->r8_owner) { err = ERROR_NOT_SUPPORTED; goto close; }
    err = ops->r8_owner( req, channel, owner );
    if (err) goto close;

    /* Parse the broker registration (first 112 bytes of the OPEN record):
     * server_gen at 48, broker_gen at 56, verifier_gen at 64. */
    reg_server_gen = e1s6_u64( reg + 48 );
    reg_broker_gen = e1s6_u64( reg + 56 );
    reg_verifier_gen = e1s6_u64( reg + 64 );

    /* Parse the BINDING reply: prefix_id 0, host_process_id 16, opener_id 32,
     * writer_id 48, desktop_route 64; server_gen 80, root_gen 88, broker_gen
     * 96, map_gen 104, policy_gen 112; caller_session 120, host_uid 124. */
    b_server_gen = e1s6_u64( binding + 80 );
    b_root_gen = e1s6_u64( binding + 88 );
    b_broker_gen = e1s6_u64( binding + 96 );
    b_map_gen = e1s6_u64( binding + 104 );
    b_policy_gen = e1s6_u64( binding + 112 );
    b_caller_session = e1s6_u32( binding + 120 );
    b_host_uid = e1s6_u64( binding + 124 );

    /* Deny unless the BINDING server_gen/broker_gen equal the registration's,
     * every value is nonzero, and the owner is a valid D-Bus unique name. */
    if (b_server_gen != reg_server_gen || b_broker_gen != reg_broker_gen)
    { err = ERROR_ACCESS_DENIED; goto close; }
    if (e1s18_is_all_zero( binding + 0, 16 ) ||   /* prefix_id */
        e1s18_is_all_zero( binding + 16, 16 ) ||  /* host_process_id */
        e1s18_is_all_zero( binding + 32, 16 ) ||  /* opener_id */
        e1s18_is_all_zero( binding + 48, 16 ) ||  /* writer_id */
        e1s18_is_all_zero( binding + 64, 16 ) ||  /* desktop_route */
        e1s18_is_all_zero( reg + 88, 16 ) ||      /* channel from registration */
        !b_server_gen || !b_root_gen || !b_broker_gen ||
        !b_map_gen || !b_policy_gen || !b_caller_session || !b_host_uid ||
        !reg_server_gen || !reg_broker_gen || !reg_verifier_gen)
    { err = ERROR_ACCESS_DENIED; goto close; }
    if (!e1r8_valid_verifier_owner( (const uint8_t *)owner, strlen( owner )))
    { err = ERROR_ACCESS_DENIED; goto close; }

    /* Fill the context: root_gen, map_gen, policy_gen, caller_session and
     * desktop_route from the BINDING; server_gen, broker_gen and verifier_gen
     * from the S6 record (server_gen/broker_gen are validated equal to the
     * BINDING's); the owner from S6. dialog_id stays zero. */
    memset( &s_r8_context, 0, sizeof(s_r8_context) );
    s_r8_context.root_gen = b_root_gen;
    s_r8_context.server_gen = reg_server_gen;
    s_r8_context.broker_gen = reg_broker_gen;
    s_r8_context.verifier_gen = reg_verifier_gen;
    s_r8_context.map_gen = b_map_gen;
    s_r8_context.policy_gen = b_policy_gen;
    s_r8_context.caller_session = b_caller_session;
    memcpy( s_r8_context.desktop_route, binding + 64, 16 );
    e1s18_safe_copy_str( s_r8_context.verifier_owner, owner, sizeof(s_r8_context.verifier_owner) );

    /* WO-27: Fill s_s1_context from live inputs */
    memset( &s_s1_context, 0, sizeof(s_s1_context) );
    s_s1_context.root_gen = b_root_gen;
    s_s1_context.server_gen = b_server_gen;
    s_s1_context.broker_gen = b_broker_gen;
    s_s1_context.verifier_gen = reg_verifier_gen;
    s_s1_context.map_gen = b_map_gen;
    s_s1_context.policy_gen = b_policy_gen;
    memcpy( s_s1_context.prefix, binding + 0, 16 );
    s_s1_context.host_uid = b_host_uid;
    memcpy( s_s1_context.host_process, binding + 16, 16 );
    memcpy( s_s1_context.opener, binding + 32, 16 );
    memcpy( s_s1_context.writer, binding + 48, 16 );
    memcpy( s_s1_context.desktop, binding + 64, 16 );
    memcpy( s_s1_context.channel, reg + 88, 16 );
    s_s1_context.volume_serial = req->info.dwVolumeSerialNumber;
    s_s1_context.file_index = ((uint64_t)req->info.nFileIndexHigh << 32) | req->info.nFileIndexLow;
    s_s1_context.file_size = ((uint64_t)req->info.nFileSizeHigh << 32) | req->info.nFileSizeLow;
    s_s1_context.last_write = E1S18_REQ_LAST_WRITE( req );
    s_s1_context.deadline = E1S18_DEADLINE( req );

    /* The verifier keeps the attempt's records per S6 session and ENDORSED
     * travels on its control socket: the channel stays open until the attempt
     * ends (e1s18_r8_release). */
    s_r8_channel = channel;
#ifdef WEXELEVATE_E1S18
    r8_trace_line( "r8: prepare ok" );
#endif
    return ERROR_SUCCESS;
close:
    if (opened && ops->r8_close)
        ops->r8_close( req, channel ); /* best-effort; result ignored */
fail:
#ifdef WEXELEVATE_E1S18
    r8_trace_err( "prepare", (unsigned long)err );
#endif
    return err;
}

/* WO-25: end of the attempt: close the S6 channel prepare kept open. */
void e1s18_r8_release( struct request *req )
{
    const struct e1s18_ops *ops = e1s18_get_ops();
    UINT64 channel = s_r8_channel;
    s_r8_channel = 0;
    if (channel && ops && ops->r8_close) ops->r8_close( req, channel ); /* best-effort */
#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
    if (s_r9_pipe != INVALID_HANDLE_VALUE)
    {
        CloseHandle( s_r9_pipe );
        s_r9_pipe = INVALID_HANDLE_VALUE;
        memset( &s_r9_tuple, 0, sizeof(s_r9_tuple) );
    }
#endif
}

/* WO-14 test hook: SHA-256 of tag||data, exposing the broker's digest for
 * golden-vector testing. Compiled only with WEX_E1_TEST_HOOKS
 * (docs/E1_PRODUCTION_PLAN.md, "Test hooks that must never ship"). */
#ifdef WEX_E1_TEST_HOOKS
void e1s18_test_sha256( const char *tag, size_t tag_len, const uint8_t *data, size_t data_len, uint8_t out[32] )
{
    struct sha256_ctx ctx;
    sha256_init( &ctx );
    sha256_update( &ctx, tag, tag_len );
    sha256_update( &ctx, data, data_len );
    sha256_final( &ctx, out );
}
#endif /* WEX_E1_TEST_HOOKS */
#endif

/*
 * Answer Replay Tracking (Stage 1)
 */
#define E1S18_MAX_REPLAY_TRACK 64
static struct {
    uint64_t request_id;
    uint8_t  nonce[32];
} s_consumed_answers[E1S18_MAX_REPLAY_TRACK];
static size_t s_consumed_answers_count = 0;

static BOOL e1s18_is_answer_replayed( uint64_t request_id, const uint8_t nonce[32] )
{
    size_t i;
    for (i = 0; i < s_consumed_answers_count; i++)
    {
        if (s_consumed_answers[i].request_id == request_id &&
            memcmp( s_consumed_answers[i].nonce, nonce, 32 ) == 0)
            return TRUE;
    }
    return FALSE;
}

static void e1s18_record_answer( uint64_t request_id, const uint8_t nonce[32] )
{
    if (s_consumed_answers_count < E1S18_MAX_REPLAY_TRACK)
    {
        s_consumed_answers[s_consumed_answers_count].request_id = request_id;
        memcpy( s_consumed_answers[s_consumed_answers_count].nonce, nonce, 32 );
        s_consumed_answers_count++;
    }
}

void e1s18_reset_answers( void )
{
    s_consumed_answers_count = 0;
    memset( s_consumed_answers, 0, sizeof(s_consumed_answers) );
}

/*
 * Evidence Single-Use Burn Tracking (Stage 2)
 */
static uint8_t s_burned_evidence[E1S18_MAX_REPLAY_TRACK][16];
static size_t s_burned_evidence_count = 0;

BOOL e1s18_is_evidence_burned( const uint8_t ref[16] )
{
    size_t i;
    for (i = 0; i < s_burned_evidence_count; i++)
    {
        if (memcmp( s_burned_evidence[i], ref, 16 ) == 0)
            return TRUE;
    }
    return FALSE;
}

static void e1s18_burn_evidence( const uint8_t ref[16] )
{
    if (s_burned_evidence_count < E1S18_MAX_REPLAY_TRACK)
    {
        memcpy( s_burned_evidence[s_burned_evidence_count], ref, 16 );
        s_burned_evidence_count++;
    }
}

void e1s18_reset_burned_evidence( void )
{
    s_burned_evidence_count = 0;
    memset( s_burned_evidence, 0, sizeof(s_burned_evidence) );
}

/*
 * Prompt Queue State (H3b Serialized Queue)
 */
static struct wex_prompt_queue s_prompt_queue;
static BOOL s_queue_init_done = FALSE;

static void e1s18_init_prompt_queue( void )
{
    if (!s_queue_init_done)
    {
        wex_prompt_queue_init( &s_prompt_queue );
        s_queue_init_done = TRUE;
    }
}

void e1s18_reset_prompt_queue( void )
{
    if (s_queue_init_done)
    {
        wex_prompt_queue_destroy( &s_prompt_queue );
        wex_prompt_queue_init( &s_prompt_queue );
    }
    else
    {
        e1s18_init_prompt_queue();
    }
}

void e1s18_stop_prompt_queue( void )
{
    if (s_queue_init_done)
    {
        wex_prompt_queue_cancel_all( &s_prompt_queue );
    }
}

struct wex_prompt_queue *e1s18_get_prompt_queue( void )
{
    e1s18_init_prompt_queue();
    return &s_prompt_queue;
}

#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
/*
 * WO-20: worker-thread context for the R8 INIT/ACK exchange. The worker
 * writes the INIT to to_d and reads the 156-byte ACK from from_d, each with an
 * exact-length WriteFile/ReadFile loop, then exits. The context (with both
 * buffers) is on the heap: if the worker cannot be joined after cancellation,
 * the broker keeps it (and the two pipe handles) instead of freeing memory the
 * worker may still touch.
 */
struct r8_worker_ctx
{
    HANDLE to_d_write;
    HANDLE from_d_read;
    size_t init_len;
    uint8_t init_buf[2067];
    uint8_t ack_buf[156];
    int write_ok;
    int read_ok;
};

/* Cancel the worker's blocking I/O, terminate D (which breaks both pipes),
 * then join the worker. FALSE: the worker did not exit within 5 s. */
static BOOL r8_stop_worker( HANDLE worker, HANDLE process )
{
    CancelSynchronousIo( worker );
    TerminateProcess( process, 1 );
    WaitForSingleObject( process, 5000 );
    return WaitForSingleObject( worker, 5000 ) == WAIT_OBJECT_0;
}

static DWORD WINAPI r8_worker_thread( void *param )
{
    struct r8_worker_ctx *ctx = (struct r8_worker_ctx *)param;
    size_t off;
    DWORD got;

    off = 0;
    while (off < ctx->init_len)
    {
        if (!WriteFile( ctx->to_d_write, ctx->init_buf + off,
                        (DWORD)(ctx->init_len - off), &got, NULL ) || !got)
            break;
        off += got;
    }
    ctx->write_ok = (off == ctx->init_len);

    off = 0;
    while (off < 156)
    {
        if (!ReadFile( ctx->from_d_read, ctx->ack_buf + off,
                       (DWORD)(156 - off), &got, NULL ) || !got)
            break;
        off += got;
    }
    ctx->read_ok = (off == 156);
    return 0;
}

/* WO-28: worker-thread context for SELECT_OK / SELECT exchange. */
struct r8_select_worker_ctx
{
    HANDLE to_d_write;
    HANDLE from_d_read;
    uint8_t select_ok_buf[76];
    uint8_t select_buf[143];
    size_t select_len;
    int write_ok;
    int read_ok;
};

static DWORD WINAPI r8_select_worker_thread( void *param )
{
    struct r8_select_worker_ctx *ctx = (struct r8_select_worker_ctx *)param;
    size_t off;
    DWORD got;
    uint32_t total;

    off = 0;
    while (off < 76)
    {
        if (!WriteFile( ctx->to_d_write, ctx->select_ok_buf + off,
                        (DWORD)(76 - off), &got, NULL ) || !got)
            break;
        off += got;
    }
    ctx->write_ok = (off == 76);
    if (!ctx->write_ok) return 0;

    off = 0;
    while (off < 76)
    {
        if (!ReadFile( ctx->from_d_read, ctx->select_buf + off,
                       (DWORD)(76 - off), &got, NULL ) || !got)
            break;
        off += got;
    }
    if (off != 76) return 0;

    total = e1r8_get_u32( ctx->select_buf + 8 );
    if (total < 81 || total > 143) return 0;

    while (off < total)
    {
        if (!ReadFile( ctx->from_d_read, ctx->select_buf + off,
                       (DWORD)(total - off), &got, NULL ) || !got)
            break;
        off += got;
    }
    if (off == total)
    {
        ctx->select_len = total;
        ctx->read_ok = 1;
    }
    return 0;
}

/* WO-28: worker-thread context for writing ATTACH_OK. */
struct r8_attach_worker_ctx
{
    HANDLE to_d_write;
    uint8_t attach_buf[76];
    int write_ok;
};

static DWORD WINAPI r8_attach_worker_thread( void *param )
{
    struct r8_attach_worker_ctx *ctx = (struct r8_attach_worker_ctx *)param;
    size_t off = 0;
    DWORD got = 0;
    while (off < 76)
    {
        if (!WriteFile( ctx->to_d_write, ctx->attach_buf + off,
                        (DWORD)(76 - off), &got, NULL ) || !got)
            break;
        off += got;
    }
    ctx->write_ok = (off == 76);
    return 0;
}

/* R8 steps 5-8 (lab): one pipe transfer to or from D in a worker, so the wait can also see D, the caller,
 * broker stop and a lease break. A read takes one frame of minimum..maximum bytes. */
struct r8_io_ctx
{
    HANDLE pipe;
    BOOL write;
    uint32_t minimum, maximum;
    size_t len;
    int ok;
    uint8_t buf[2148];
};

static DWORD WINAPI r8_io_thread( void *param )
{
    struct r8_io_ctx *io = param;
    size_t off = 0, want = io->write ? io->len : E1R8_HEADER_SIZE;
    DWORD got;

    while (off < want)
    {
        if (io->write ? !WriteFile( io->pipe, io->buf + off, (DWORD)(want - off), &got, NULL )
                      : !ReadFile( io->pipe, io->buf + off, (DWORD)(want - off), &got, NULL )) return 0;
        if (!got) return 0;
        off += got;
        if (!io->write && off == E1R8_HEADER_SIZE && want == E1R8_HEADER_SIZE)
        {
            uint32_t total = e1r8_get_u32( io->buf + 8 );
            if (total < io->minimum || total > io->maximum || total > sizeof(io->buf)) return 0;
            want = total;
        }
    }
    io->len = off;
    io->ok = 1;
    return 0;
}

/* Milliseconds left until the attempt's admission deadline (0 when it has passed or the clock fails). */
static DWORD r8_remaining( const struct request *req )
{
    uint64_t now = e1s18_get_monotonic_time(), deadline = E1S18_DEADLINE( req );
    if (!now || deadline <= now) return 0;
    return (deadline - now) / 1000000 > 130000 ? 130000 : (DWORD)((deadline - now) / 1000000);
}

static DWORD r8_left( ULONGLONG start, DWORD timeout )
{
    ULONGLONG elapsed = GetTickCount64() - start;
    return elapsed >= timeout ? 0 : (DWORD)(timeout - elapsed);
}

/* On failure D has been terminated when the worker had to be stopped; *stuck is set when the worker could
 * not be joined (its context and the pipe handles are then kept, as in the INIT/ACK exchange). */
static DWORD r8_pipe_transfer( struct request *req, HANDLE process, HANDLE pipe, BOOL write,
                               uint8_t *data, size_t *len, uint32_t minimum, uint32_t maximum,
                               ULONGLONG start, DWORD timeout, BOOL *stuck )
{
    struct r8_io_ctx *io;
    HANDLE thread, handles[5];
    DWORD ret;

    if (write && *len > sizeof(io->buf)) return ERROR_INVALID_PARAMETER;
    if (!(io = calloc( 1, sizeof(*io) ))) return ERROR_NOT_ENOUGH_MEMORY;
    io->pipe = pipe; io->write = write; io->minimum = minimum; io->maximum = maximum;
    if (write) { memcpy( io->buf, data, *len ); io->len = *len; }
    if (!(thread = CreateThread( NULL, 0, r8_io_thread, io, 0, NULL ))) { free( io ); return ERROR_NOT_ENOUGH_MEMORY; }
    handles[0] = thread; handles[1] = process; handles[2] = req->caller;
    handles[3] = stop_event; handles[4] = req->lease_event;
    ret = WaitForMultipleObjects( 5, handles, FALSE, r8_left( start, timeout ) );
    /* D may leave the moment it has read its last frame (DONE), before the worker thread has returned: D's
     * exit breaks the pipe, so the worker ends at once either way and its own result decides. */
    if (ret == WAIT_OBJECT_0 + 1 && WaitForSingleObject( thread, 5000 ) == WAIT_OBJECT_0) ret = WAIT_OBJECT_0;
    if (ret != WAIT_OBJECT_0 || !io->ok)
    {
        BOOL joined = ret == WAIT_OBJECT_0 || r8_stop_worker( thread, process );
        if (joined) { CloseHandle( thread ); e1s18_wipe( io, sizeof(*io) ); free( io ); }
        else *stuck = TRUE;
        return ret == WAIT_TIMEOUT ? ERROR_TIMEOUT : ret == WAIT_OBJECT_0 + 4 ? ERROR_FILE_CORRUPT : ERROR_CANCELLED;
    }
    if (!write) { memcpy( data, io->buf, io->len ); *len = io->len; }
    CloseHandle( thread );
    e1s18_wipe( io, sizeof(*io) );
    free( io );
    return ERROR_SUCCESS;
}

/* One E1R8 frame to or from the verifier on the attempt's open S6 channel. */
typedef DWORD (WINAPI *r8_pfn_transport)( HANDLE, DWORD, void *, DWORD, UINT64 * );

static r8_pfn_transport r8_transport( void )
{
    HMODULE wexhost = GetModuleHandleW( L"wexhost.dll" );
    if (!wexhost) wexhost = LoadLibraryW( L"wexhost.dll" );
    return wexhost ? (r8_pfn_transport)(void *)GetProcAddress( wexhost, "WexE1s6Transport" ) : NULL;
}

static DWORD r8_v_send( struct request *req, uint8_t *frame, size_t len )
{
    r8_pfn_transport transport = r8_transport();
    UINT64 channel = s_r8_channel;
    if (!transport || !channel) return ERROR_NOT_SUPPORTED;
    return transport( req->pipe, E1S6_R8_SEND, frame, (DWORD)len, &channel );
}

static DWORD r8_v_recv( struct request *req, uint8_t frame[E1S6_R8_FRAME_MAX], size_t *len, DWORD timeout )
{
    r8_pfn_transport transport = r8_transport();
    UINT64 channel = s_r8_channel;
    DWORD err;
    if (!transport || !channel) return ERROR_NOT_SUPPORTED;
    memset( frame, 0, E1S6_R8_FRAME_MAX );
    e1r8_put_u32( frame, timeout > 130000 ? 130000 : timeout );
    if ((err = transport( req->pipe, E1S6_R8_RECV, frame, E1S6_R8_FRAME_MAX, &channel ))) return err;
    *len = e1r8_get_u32( frame + 8 );
    return ERROR_SUCCESS;
}

/* The evidence record the verifier handed over at redemption inside the dialog step; consumed once by the
 * Stage 2 check (default_redeem_evidence). */
static uint8_t s_r8_evidence[220], s_r8_evidence_ref[16];
static BOOL s_r8_evidence_set;
static uint32_t s_r8_attempt;  /* the attempt whose evidence was redeemed (1..3) */

/* R9-4a: the dialog D of one attempt: the process (created suspended, endorsed by the server for the tuple) and
 * the broker's ends of its two pipes. The R8 broker and the R9 agent both create it here. */
struct r8_dialog
{
    HANDLE  process;
    HANDLE  thread;       /* still suspended: the caller resumes it and closes this */
    HANDLE  to_d_write;
    HANDLE  from_d_read;
    uint8_t dialog_id[16];
};

/* Create D suspended on the desktop the tuple's mode asks for, with exactly its two pipe ends inherited, and
 * have the server endorse it; the dialog id goes into the tuple and the R8 context. On failure nothing is left
 * (D terminated, handles closed, the secure desktop left). */
static DWORD r8_dialog_create( struct request *req, struct e1r8_tuple *tuple, uint64_t request_id,
                               uint32_t attempt, const uint8_t channel_token[16], BOOL secure,
                               struct r8_dialog *dlg )
{
    STARTUPINFOEXW si = { { sizeof(si) } };
    PROCESS_INFORMATION pi;
    WCHAR dir[MAX_PATH], *cmd;
    HANDLE to_d_read = INVALID_HANDLE_VALUE, to_d_write = INVALID_HANDLE_VALUE;
    HANDLE from_d_read = INVALID_HANDLE_VALUE, from_d_write = INVALID_HANDLE_VALUE;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, FALSE }; /* non-inheritable by default */
    SIZE_T attr_size = 0;
    HANDLE inherit_handles[2];
    DWORD len;
    size_t cmd_size;
    uint8_t pre_tuple[E1R8_PRE_ENDORSEMENT_SIZE];
    uint8_t dialog_id[16];
    int i;
    WCHAR token_hex[33];
    const struct e1s18_ops *ops = e1s18_get_ops();

    /* Two anonymous pipes, non-inheritable handles: to_d (B writes, D reads)
     * and from_d (D writes, B reads). */
    if (!CreatePipe( &to_d_read, &to_d_write, &sa, 0 ))
        return GetLastError();
    if (!CreatePipe( &from_d_read, &from_d_write, &sa, 0 ))
    {
        CloseHandle( to_d_read );
        CloseHandle( to_d_write );
        return GetLastError();
    }
    /* R8 step 3a: the server protects the dialog only if its ends carry exactly FILE_GENERIC_READ and
     * FILE_GENERIC_WRITE (CreatePipe also grants attribute rights), so narrow them before their
     * values go on the command line. */
    {
        HANDLE narrow_read = NULL, narrow_write = NULL;
        if (!DuplicateHandle( GetCurrentProcess(), to_d_read, GetCurrentProcess(), &narrow_read,
                              FILE_GENERIC_READ, FALSE, 0 )) narrow_read = NULL;
        if (!DuplicateHandle( GetCurrentProcess(), from_d_write, GetCurrentProcess(), &narrow_write,
                              FILE_GENERIC_WRITE, FALSE, 0 )) narrow_write = NULL;
        CloseHandle( to_d_read );
        CloseHandle( from_d_write );
        to_d_read = narrow_read;
        from_d_write = narrow_write;
        if (!to_d_read || !from_d_write)
        {
            if (to_d_read) CloseHandle( to_d_read );
            if (from_d_write) CloseHandle( from_d_write );
            CloseHandle( to_d_write );
            CloseHandle( from_d_read );
            return ERROR_NOT_ENOUGH_MEMORY;
        }
    }

    for (i = 0; i < 16; i++)
        wsprintfW( token_hex + i * 2, L"%02x", channel_token[i] );
    token_hex[32] = 0;

    if (!(len = GetSystemDirectoryW( dir, MAX_PATH )) || len >= MAX_PATH - 32)
    {
        CloseHandle( to_d_read ); CloseHandle( to_d_write );
        CloseHandle( from_d_read ); CloseHandle( from_d_write );
        return ERROR_SERVICE_NOT_ACTIVE;
    }

    cmd_size = (len + 256) * sizeof(WCHAR);
    if (!(cmd = malloc( cmd_size )))
    {
        CloseHandle( to_d_read ); CloseHandle( to_d_write );
        CloseHandle( from_d_read ); CloseHandle( from_d_write );
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    lstrcpyW( cmd, L"\"" );
    lstrcatW( cmd, dir );
    lstrcatW( cmd, L"\\" CONSENT_EXE L"\" /broker /credentials " );
    wsprintfW( cmd + lstrlenW( cmd ), L"/ppid %lu ", GetCurrentProcessId() );
    wsprintfW( cmd + lstrlenW( cmd ), L"/request %08lx%08lx ", (DWORD)(request_id >> 32), (DWORD)request_id );
    wsprintfW( cmd + lstrlenW( cmd ), L"/token %s ", token_hex );
    wsprintfW( cmd + lstrlenW( cmd ), L"/in %lu ", (DWORD)(ULONG_PTR)to_d_read );
    wsprintfW( cmd + lstrlenW( cmd ), L"/out %lu ", (DWORD)(ULONG_PTR)from_d_write );
    wsprintfW( cmd + lstrlenW( cmd ), L"/attempt %lu ", (DWORD)attempt );
    if (secure) lstrcatW( cmd, L"/secure " );

    if (secure && secure_enter())
    {
        free( cmd );
        CloseHandle( to_d_read ); CloseHandle( to_d_write );
        CloseHandle( from_d_read ); CloseHandle( from_d_write );
        return ERROR_NOT_READY;
    }

    si.StartupInfo.lpDesktop = (WCHAR *)(secure ? L"WinSta0\\Winlogon" : L"WinSta0\\Default");

    /* Only D's two ends (to_d read, from_d write) become inheritable, just
     * before CreateProcessW; B's own ends (to_d write, from_d read) are never
     * inheritable. Exactly those two are passed in the explicit handle list. */
    InitializeProcThreadAttributeList( NULL, 2, 0, &attr_size );
    if (!attr_size ||
        !SetHandleInformation( to_d_read, HANDLE_FLAG_INHERIT, 1 ) ||
        !SetHandleInformation( from_d_write, HANDLE_FLAG_INHERIT, 1 ))
    {
        free( cmd );
        CloseHandle( to_d_read ); CloseHandle( to_d_write );
        CloseHandle( from_d_read ); CloseHandle( from_d_write );
        if (secure) secure_leave();
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    si.lpAttributeList = malloc( attr_size );
    if (!si.lpAttributeList || !InitializeProcThreadAttributeList( si.lpAttributeList, 2, 0, &attr_size ))
    {
        free( si.lpAttributeList );
        free( cmd );
        CloseHandle( to_d_read ); CloseHandle( to_d_write );
        CloseHandle( from_d_read ); CloseHandle( from_d_write );
        if (secure) secure_leave();
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    inherit_handles[0] = to_d_read;
    inherit_handles[1] = from_d_write;
    if (!UpdateProcThreadAttribute( si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                    inherit_handles, sizeof(inherit_handles), NULL, NULL ))
    {
        DeleteProcThreadAttributeList( si.lpAttributeList );
        free( si.lpAttributeList );
        free( cmd );
        CloseHandle( to_d_read ); CloseHandle( to_d_write );
        CloseHandle( from_d_read ); CloseHandle( from_d_write );
        if (secure) secure_leave();
        return GetLastError();
    }
    if (!CreateProcessW( NULL, cmd, NULL, NULL, TRUE,
                         EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED, NULL, dir,
                         &si.StartupInfo, &pi ))
    {
        DeleteProcThreadAttributeList( si.lpAttributeList );
        free( si.lpAttributeList );
        free( cmd );
        CloseHandle( to_d_read ); CloseHandle( to_d_write );
        CloseHandle( from_d_read ); CloseHandle( from_d_write );
        if (secure) secure_leave();
        return ERROR_SERVICE_NOT_ACTIVE;
    }

    DeleteProcThreadAttributeList( si.lpAttributeList );
    free( si.lpAttributeList );
    free( cmd );
    /* D holds its own inherited copies; close B's copies of D's two ends.
     * pi.hThread is kept until after ResumeThread (D was created suspended). */
    CloseHandle( to_d_read );
    CloseHandle( from_d_write );

    /* WO-25: encode the 204-byte pre-endorsement tuple and endorse the
     * suspended dialog. On success the server returns the dialog_id, which is
     * put into the tuple and the context; INIT is then encoded from the sealed
     * tuple and D is resumed. A failed endorse terminates D and denies. */
    if (e1r8_encode_pre_endorsement_tuple( pre_tuple, sizeof(pre_tuple), tuple ))
    {
        TerminateProcess( pi.hProcess, 1 );
        WaitForSingleObject( pi.hProcess, 5000 );
        CloseHandle( pi.hProcess );
        CloseHandle( pi.hThread );
        CloseHandle( to_d_write );
        CloseHandle( from_d_read );
        if (secure) secure_leave();
        return ERROR_NOT_SUPPORTED;
    }
    if (!ops->r8_endorse ||
        ops->r8_endorse( req, pi.hProcess, pre_tuple, dialog_id ))
    {
#ifdef WEXELEVATE_E1S18
        r8_trace_err( "endorse", (DWORD)ERROR_ACCESS_DENIED );
#endif
        TerminateProcess( pi.hProcess, 1 );
        WaitForSingleObject( pi.hProcess, 5000 );
        CloseHandle( pi.hProcess );
        CloseHandle( pi.hThread );
        CloseHandle( to_d_write );
        CloseHandle( from_d_read );
        if (secure) secure_leave();
        return ERROR_ACCESS_DENIED;
    }
#ifdef WEXELEVATE_E1S18
    r8_trace_line( "r8: endorse ok" );
#endif
    memcpy( tuple->dialog_id, dialog_id, 16 );
    memcpy( s_r8_context.dialog_id, dialog_id, 16 );
    dlg->process = pi.hProcess;
    dlg->thread = pi.hThread;
    dlg->to_d_write = to_d_write;
    dlg->from_d_read = from_d_read;
    memcpy( dlg->dialog_id, dialog_id, 16 );
    return ERROR_SUCCESS;
}

static DWORD e1s18_spawn_consent_dialog( struct request *req, uint64_t request_id, uint32_t attempt,
                                         const uint8_t nonce[32],
                                         const uint8_t binding_digest[32],
                                         const uint8_t request_digest[32],
                                         const uint8_t channel_token[16],
                                         BOOL secure, char *out_account, size_t account_max )
{
    PROCESS_INFORMATION pi;
    struct r8_dialog dlg = { NULL, NULL, NULL, NULL, { 0 } };
    HANDLE handles[5];
    HANDLE to_d_write = INVALID_HANDLE_VALUE, from_d_read = INVALID_HANDLE_VALUE;
    DWORD ret;
    uint8_t init_buf[2067];
    DWORD err = ERROR_SUCCESS;
    struct e1r8_tuple tuple;
    size_t init_len;
    HANDLE worker_thread = NULL;
    struct r8_worker_ctx *wc;
    ULONGLONG start, elapsed;
    DWORD timeout;
    const struct e1s18_ops *ops = e1s18_get_ops();

    /* WO-25 / WO-27: reuse open channel and context if already prepared, or prepare now. */
    if (!s_r8_channel)
    {
        if ((err = e1s18_r8_prepare( req )))
            return err;
    }

    /* WO-20: the R8 context must be complete; a zero required input denies
     * with ERROR_NOT_SUPPORTED before any pipe or process is created. The
     * pre-endorsement tuple carries no dialog_id (the server assigns it at
     * endorsement). */
    if (e1s18_build_r8_tuple( req, request_id, attempt, nonce, channel_token,
                              binding_digest, request_digest, secure, &tuple ))
        return ERROR_NOT_SUPPORTED;

    /* R9-4a: the pipes, the suspended dialog and its endorsement (r8_dialog_create); the tuple is sealed with
     * the dialog id on return. */
    if ((err = r8_dialog_create( req, &tuple, request_id, attempt, channel_token, secure, &dlg )))
        return err;
    pi.hProcess = dlg.process;
    pi.hThread = dlg.thread;
    to_d_write = dlg.to_d_write;
    from_d_read = dlg.from_d_read;
    /* Encode the INIT from the sealed tuple; an invalid display string or an
     * undersized buffer terminates D and denies. */
    init_len = e1s18_encode_r8_init( req, &tuple, init_buf, sizeof(init_buf) );
    if (!init_len)
    {
        TerminateProcess( pi.hProcess, 1 );
        WaitForSingleObject( pi.hProcess, 5000 );
        CloseHandle( pi.hProcess );
        CloseHandle( pi.hThread );
        CloseHandle( to_d_write );
        CloseHandle( from_d_read );
        if (secure) secure_leave();
        return ERROR_NOT_SUPPORTED;
    }
    /* Resume D and close the thread handle only after resuming; D is never
     * left suspended. */
    if (ResumeThread( pi.hThread ) == (DWORD)-1)
    {
        TerminateProcess( pi.hProcess, 1 );
        WaitForSingleObject( pi.hProcess, 5000 );
        CloseHandle( pi.hProcess );
        CloseHandle( pi.hThread );
        CloseHandle( to_d_write );
        CloseHandle( from_d_read );
        if (secure) secure_leave();
        return ERROR_ACCESS_DENIED;
    }
    CloseHandle( pi.hThread );

    if (!(wc = calloc( 1, sizeof(*wc) )))
    {
        TerminateProcess( pi.hProcess, 1 );
        WaitForSingleObject( pi.hProcess, 5000 );
        CloseHandle( pi.hProcess );
        CloseHandle( to_d_write );
        CloseHandle( from_d_read );
        if (secure) secure_leave();
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    wc->to_d_write = to_d_write;
    wc->from_d_read = from_d_read;
    wc->init_len = init_len;
    memcpy( wc->init_buf, init_buf, init_len );
    if (!(worker_thread = CreateThread( NULL, 0, r8_worker_thread, wc, 0, NULL )))
    {
        free( wc );
        TerminateProcess( pi.hProcess, 1 );
        WaitForSingleObject( pi.hProcess, 5000 );
        CloseHandle( pi.hProcess );
        CloseHandle( to_d_write );
        CloseHandle( from_d_read );
        if (secure) secure_leave();
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    /* One consent deadline covers the INIT/ACK exchange and the dialog. */
    timeout = consent_timeout();
    start = GetTickCount64();
    handles[0] = worker_thread;
    handles[1] = pi.hProcess;
    handles[2] = req->caller;
    handles[3] = stop_event;
    handles[4] = req->lease_event;
    ret = WaitForMultipleObjects( 5, handles, FALSE, timeout );

    /* The ACK must have been written and read in full and decode with the
     * tuple's digests and dialog_id; anything else cancels. */
    if (ret != WAIT_OBJECT_0 || !wc->write_ok || !wc->read_ok ||
        e1s18_verify_r8_ack( wc->ack_buf, sizeof(wc->ack_buf), &tuple ))
    {
        BOOL joined = TRUE;

#ifdef WEXELEVATE_E1S18
        r8_trace_err( "ack", (DWORD)ERROR_CANCELLED );
#endif
        if (ret == WAIT_OBJECT_0)
        {
            TerminateProcess( pi.hProcess, 1 );
            WaitForSingleObject( pi.hProcess, 5000 );
        }
        else joined = r8_stop_worker( worker_thread, pi.hProcess );
        CloseHandle( pi.hProcess );
        if (joined)
        {
            CloseHandle( worker_thread );
            free( wc );
            CloseHandle( to_d_write );
            CloseHandle( from_d_read );
        }
        /* else: the worker did not exit; its context and the two pipe handles
         * are deliberately kept, since it may still touch them. */
        if (secure) secure_leave();
        if (ret == WAIT_TIMEOUT) return ERROR_TIMEOUT;
        if (ret == WAIT_OBJECT_0 + 4) return ERROR_FILE_CORRUPT;
        return ERROR_CANCELLED;
    }
    CloseHandle( worker_thread );
    free( wc );
#ifdef WEXELEVATE_E1S18
    r8_trace_line( "r8: ack ok" );
#endif

    /* WO-25: after a valid ACK, recheck the dialog with the server. A failed
     * recheck terminates D and denies. */
    if (!ops->r8_recheck || ops->r8_recheck( req, tuple.dialog_id ))
    {
#ifdef WEXELEVATE_E1S18
        r8_trace_err( "recheck", (DWORD)ERROR_ACCESS_DENIED );
#endif
        TerminateProcess( pi.hProcess, 1 );
        WaitForSingleObject( pi.hProcess, 5000 );
        CloseHandle( pi.hProcess );
        CloseHandle( to_d_write );
        CloseHandle( from_d_read );
        if (secure) secure_leave();
        return ERROR_ACCESS_DENIED;
    }
#ifdef WEXELEVATE_E1S18
    r8_trace_line( "r8: recheck ok" );

    /* WO-28: after recheck ok:
     * 1. Send SELECT_OK (76 bytes) to D, read SELECT (81..143 bytes) from D. */
    {
        struct r8_select_worker_ctx *sc = malloc( sizeof(*sc) );
        char sel_account[64] = {0};
        size_t sel_account_len = 0;
        struct e1r8_select decoded_sel;
        uint8_t sel_key[16];
        uint8_t start_pkt[159];
        size_t start_pkt_len = 0;
        uint8_t ready_pkt[76];
        BOOL locked = FALSE;
        struct r8_attach_worker_ctx *ac;
        HANDLE attach_thread;

        if (!sc)
        {
            TerminateProcess( pi.hProcess, 1 );
            WaitForSingleObject( pi.hProcess, 5000 );
            CloseHandle( pi.hProcess );
            CloseHandle( to_d_write );
            CloseHandle( from_d_read );
            if (secure) secure_leave();
            return ERROR_NOT_ENOUGH_MEMORY;
        }
        memset( sc, 0, sizeof(*sc) );
        sc->to_d_write = to_d_write;
        sc->from_d_read = from_d_read;
        e1r8_encode_select_ok( sc->select_ok_buf, sizeof(sc->select_ok_buf), &tuple );

        if (!(worker_thread = CreateThread( NULL, 0, r8_select_worker_thread, sc, 0, NULL )))
        {
            free( sc );
            TerminateProcess( pi.hProcess, 1 );
            WaitForSingleObject( pi.hProcess, 5000 );
            CloseHandle( pi.hProcess );
            CloseHandle( to_d_write );
            CloseHandle( from_d_read );
            if (secure) secure_leave();
            return ERROR_NOT_ENOUGH_MEMORY;
        }

        elapsed = GetTickCount64() - start;
        handles[0] = worker_thread;
        handles[1] = pi.hProcess;
        handles[2] = req->caller;
        handles[3] = stop_event;
        handles[4] = req->lease_event;
        ret = WaitForMultipleObjects( 5, handles, FALSE, elapsed >= timeout ? 0 : (DWORD)(timeout - elapsed) );

        if (ret != WAIT_OBJECT_0 || !sc->write_ok || !sc->read_ok ||
            e1r8_decode_select( sc->select_buf, sc->select_len, &tuple, &decoded_sel ) != 0)
        {
            BOOL joined = TRUE;
            r8_trace_err( "select", (DWORD)ERROR_CANCELLED );
            if (ret == WAIT_OBJECT_0)
            {
                TerminateProcess( pi.hProcess, 1 );
                WaitForSingleObject( pi.hProcess, 5000 );
            }
            else joined = r8_stop_worker( worker_thread, pi.hProcess );
            CloseHandle( pi.hProcess );
            if (joined)
            {
                CloseHandle( worker_thread );
                free( sc );
                CloseHandle( to_d_write );
                CloseHandle( from_d_read );
            }
            if (secure) secure_leave();
            if (ret == WAIT_TIMEOUT) return ERROR_TIMEOUT;
            if (ret == WAIT_OBJECT_0 + 4) return ERROR_FILE_CORRUPT;
            return ERROR_CANCELLED;
        }
        CloseHandle( worker_thread );
        free( sc );

        sel_account_len = decoded_sel.account_len < sizeof(sel_account) - 1 ? decoded_sel.account_len : sizeof(sel_account) - 1;
        memcpy( sel_account, decoded_sel.account, sel_account_len );
        sel_account[sel_account_len] = '\0';

        r8_trace_line( "r8: select ok" );
        lstrcpynA( req->account_name, sel_account, sizeof(req->account_name) );
        if (out_account && account_max)
            lstrcpynA( out_account, sel_account, (int)account_max );

        /* 2. Resolve account key through S4 (unknown name -> zero key decoy) */
        e1s18_resolve_account_s4( sel_account, sel_key );
        memcpy( req->account_key, sel_key, 16 );
        r8_trace_line( e1s18_is_all_zero( sel_key, 16 ) ? "r8: account decoy" : "r8: account known" );

        /* 3. Send START on open S6 channel */
        start_pkt_len = 96 + sel_account_len;
        if (e1r8_encode_start( start_pkt, start_pkt_len, &tuple,
                               (const uint8_t *)sel_account, (uint32_t)sel_account_len, sel_key ) != 0)
        {
            TerminateProcess( pi.hProcess, 1 );
            WaitForSingleObject( pi.hProcess, 5000 );
            CloseHandle( pi.hProcess );
            CloseHandle( to_d_write );
            CloseHandle( from_d_read );
            if (secure) secure_leave();
            return ERROR_INVALID_DATA;
        }
        r8_trace_line( "r8: start sent" );

        /* 4. Wait for WORKER_READY from verifier */
        if (!ops->r8_start ||
            ops->r8_start( req, s_r8_channel, start_pkt, (size_t)start_pkt_len, ready_pkt ) != ERROR_SUCCESS)
            memset( ready_pkt, 0, sizeof(ready_pkt) );
        /* The lockout policy closed the account: no worker. D is told in place of ATTACH_OK below. */
        locked = !e1r8_get_header( ready_pkt, sizeof(ready_pkt), E1R8_TYPE_LOCKED, &tuple, NULL );
        if (!locked && e1r8_decode_worker_ready( ready_pkt, sizeof(ready_pkt), &tuple ) != 0)
        {
            r8_trace_err( "worker ready", (DWORD)ERROR_ACCESS_DENIED );
            TerminateProcess( pi.hProcess, 1 );
            WaitForSingleObject( pi.hProcess, 5000 );
            CloseHandle( pi.hProcess );
            CloseHandle( to_d_write );
            CloseHandle( from_d_read );
            if (secure) secure_leave();
            return ERROR_ACCESS_DENIED;
        }
        r8_trace_line( locked ? "r8: account locked" : "r8: worker ready" );

        /* 5. Send ATTACH_OK (76 bytes) to D */
        ac = malloc( sizeof(*ac) );
        if (!ac)
        {
            TerminateProcess( pi.hProcess, 1 );
            WaitForSingleObject( pi.hProcess, 5000 );
            CloseHandle( pi.hProcess );
            CloseHandle( to_d_write );
            CloseHandle( from_d_read );
            if (secure) secure_leave();
            return ERROR_NOT_ENOUGH_MEMORY;
        }
        memset( ac, 0, sizeof(*ac) );
        ac->to_d_write = to_d_write;
        if (locked) e1r8_put_header( ac->attach_buf, sizeof(ac->attach_buf), E1R8_TYPE_LOCKED, 76, &tuple );
        else e1r8_encode_attach_ok( ac->attach_buf, sizeof(ac->attach_buf), &tuple );
        if (!(attach_thread = CreateThread( NULL, 0, r8_attach_worker_thread, ac, 0, NULL )))
        {
            free( ac );
            TerminateProcess( pi.hProcess, 1 );
            WaitForSingleObject( pi.hProcess, 5000 );
            CloseHandle( pi.hProcess );
            CloseHandle( to_d_write );
            CloseHandle( from_d_read );
            if (secure) secure_leave();
            return ERROR_NOT_ENOUGH_MEMORY;
        }

        elapsed = GetTickCount64() - start;
        handles[0] = attach_thread;
        handles[1] = pi.hProcess;
        handles[2] = req->caller;
        handles[3] = stop_event;
        handles[4] = req->lease_event;
        ret = WaitForMultipleObjects( 5, handles, FALSE, elapsed >= timeout ? 0 : (DWORD)(timeout - elapsed) );
        /* A dialog that took the lockout frame may be gone before the writer thread has ended. */
        if (locked && ret == WAIT_OBJECT_0 + 1 && WaitForSingleObject( attach_thread, 2000 ) == WAIT_OBJECT_0)
            ret = WAIT_OBJECT_0;

        if (ret != WAIT_OBJECT_0 || !ac->write_ok)
        {
            BOOL joined = TRUE;
            if (ret == WAIT_OBJECT_0)
            {
                TerminateProcess( pi.hProcess, 1 );
                WaitForSingleObject( pi.hProcess, 5000 );
            }
            else joined = r8_stop_worker( attach_thread, pi.hProcess );
            CloseHandle( pi.hProcess );
            if (joined)
            {
                CloseHandle( attach_thread );
                free( ac );
                CloseHandle( to_d_write );
                CloseHandle( from_d_read );
            }
            if (secure) secure_leave();
            if (ret == WAIT_TIMEOUT) return ERROR_TIMEOUT;
            if (ret == WAIT_OBJECT_0 + 4) return ERROR_FILE_CORRUPT;
            return ERROR_CANCELLED;
        }
        CloseHandle( attach_thread );
        free( ac );

        if (locked)
        {
            /* D tells the person and leaves by itself; the request ends, there is no further attempt. */
            r8_trace_line( "r8: locked sent" );
            WaitForSingleObject( pi.hProcess, 10000 );
            TerminateProcess( pi.hProcess, 1 );
            WaitForSingleObject( pi.hProcess, 5000 );
            CloseHandle( pi.hProcess );
            CloseHandle( to_d_write );
            CloseHandle( from_d_read );
            if (secure) secure_leave();
            return ERROR_ACCOUNT_LOCKED_OUT;
        }

        r8_trace_line( "r8: attach_ok sent" );

        /* R8 steps 5-8 (docs/E1_S18E_R8_SPEC.md sections 7-9, lab). From here every failure ends D. The
         * password never passes through this process: it goes from D to the worker on their own socket. */
        {
            uint8_t frame[E1S6_R8_FRAME_MAX], out[264], challenge[32], fresh_key[16], ref[16];
            static const uint8_t zero32[32];
            struct e1r8_result_v vres;
            struct e1r8_answer ans;
            size_t flen = 0;
            /* These steps run on the attempt's admission deadline, not the click-consent timeout: the
             * password check alone may take two seconds (the fixed release time of a refusal). */
            ULONGLONG r8_start = GetTickCount64();
            DWORD r8_timeout = r8_remaining( req );
            BOOL stuck = FALSE, refused = FALSE;
            const char *step;

            err = ERROR_ACCESS_DENIED;
            /* No record of an earlier attempt survives into this one. */
            s_r8_evidence_set = FALSE;
            e1s18_wipe( s_r8_evidence, sizeof(s_r8_evidence) );
            e1s18_wipe( s_r8_evidence_ref, sizeof(s_r8_evidence_ref) );

            /* DELEGATED: the endorsed dialog attached and the worker acknowledged it. */
            step = "delegated";
            if (r8_v_recv( req, frame, &flen, r8_left( r8_start, r8_timeout ) ) ||
                e1r8_decode_delegated( frame, flen, &tuple )) goto r8_fail;
            r8_trace_line( "r8: delegated" );

            /* READY after a server recheck of D. */
            step = "ready";
            if (!ops->r8_recheck || ops->r8_recheck( req, tuple.dialog_id )) goto r8_fail;
            flen = 76;
            if (e1r8_encode_ready( out, 76, &tuple )) goto r8_fail;
            if ((err = r8_pipe_transfer( req, pi.hProcess, to_d_write, TRUE, out, &flen, 0, 0, r8_start, r8_timeout, &stuck )))
                goto r8_fail;
            err = ERROR_ACCESS_DENIED;
            r8_trace_line( "r8: ready sent" );

            /* The verifier's RESULT after the worker's PAM conversation. */
            step = "verifier result";
            if (r8_v_recv( req, frame, &flen, r8_left( r8_start, r8_timeout ) ) ||
                e1r8_decode_result_v( frame, flen, &tuple, &vres )) goto r8_fail;
            if (vres.status != E1R8_RESULT_AUTHENTICATED)
            {
                r8_trace_line( "r8: result refused" );
                refused = TRUE;
                err = ERROR_LOGON_FAILURE;
                step = "refused result";
                flen = 116;
                /* A refusal that cannot be told to the dialog (it is gone) ends the request: only a
                 * dialog that took the refusal leads to a further attempt. */
                if (e1r8_encode_result( out, 116, &tuple, E1R8_RESULT_REFUSED, (const uint8_t *)"", 0, zero32 ) ||
                    r8_pipe_transfer( req, pi.hProcess, to_d_write, TRUE, out, &flen, 0, 0, r8_start, r8_timeout, &stuck ))
                { refused = FALSE; err = ERROR_CANCELLED; goto r8_fail; }
                flen = 80;
                if (e1r8_encode_done( out, 80, &tuple, E1R8_DONE_CANCELLED ) ||
                    r8_pipe_transfer( req, pi.hProcess, to_d_write, TRUE, out, &flen, 0, 0, r8_start, r8_timeout, &stuck ))
                { refused = FALSE; err = ERROR_CANCELLED; goto r8_fail; }
                /* D tells the person and leaves by itself (bounded by the attempt deadline). */
                WaitForSingleObject( pi.hProcess, r8_left( r8_start, r8_timeout ) );
                goto r8_fail;
            }
            memcpy( ref, vres.evidence_ref, 16 );

            /* Section 8: the authenticated account is the selected one, resolved again. */
            step = "final account";
            if (vres.account_len != sel_account_len || memcmp( vres.account, sel_account, sel_account_len ) ||
                e1s18_is_all_zero( sel_key, 16 ) || memcmp( vres.account_key, sel_key, 16 )) goto r8_fail;
            e1s18_resolve_account_s4( sel_account, fresh_key );
            if (memcmp( fresh_key, sel_key, 16 )) goto r8_fail;
            if (!ops->r8_recheck || ops->r8_recheck( req, tuple.dialog_id )) goto r8_fail;

            /* RESULT with a one-use challenge, then D's Confirm for exactly that challenge. */
            step = "result";
            if (!e1s18_fill_random( challenge, 32 ) || e1s18_is_all_zero( challenge, 32 )) goto r8_fail;
            flen = 116 + sel_account_len;
            if (e1r8_encode_result( out, flen, &tuple, E1R8_RESULT_AUTHENTICATED,
                                    (const uint8_t *)sel_account, (uint32_t)sel_account_len, challenge )) goto r8_fail;
            if ((err = r8_pipe_transfer( req, pi.hProcess, to_d_write, TRUE, out, &flen, 0, 0, r8_start, r8_timeout, &stuck )))
                goto r8_fail;
            r8_trace_line( "r8: result sent" );
            step = "confirm";
            flen = 0;
            if ((err = r8_pipe_transfer( req, pi.hProcess, from_d_read, FALSE, out, &flen, 264, 264, r8_start, r8_timeout, &stuck )))
                goto r8_fail;
            err = ERROR_ACCESS_DENIED;
            if (e1r8_decode_answer( out, flen, &tuple, E1S1_ALL_REQUIRED_CAPS, &ans )) goto r8_fail;
            if (ans.answer_kind != E1R8_S1_ANSWER_CONFIRM) { err = ERROR_CANCELLED; goto r8_fail; }
            if (memcmp( ans.challenge, challenge, 32 ) || e1s18_is_answer_replayed( request_id, nonce )) goto r8_fail;
            e1s18_record_answer( request_id, nonce );
            if (!ops->r8_recheck || ops->r8_recheck( req, tuple.dialog_id )) goto r8_fail;
            r8_trace_line( "r8: confirm ok" );

            /* Redemption (lab frames 28/29): the verifier hands over the evidence record once. */
            step = "redeem";
            if (e1r8_put_header( frame, sizeof(frame), 28, 92, &tuple )) goto r8_fail;
            memcpy( frame + 76, ref, 16 );
            if (r8_v_send( req, frame, 92 ) ||
                r8_v_recv( req, frame, &flen, r8_left( r8_start, r8_timeout ) ) ||
                flen != 296 || e1r8_get_header( frame, 296, 29, &tuple, NULL )) goto r8_fail;
            memcpy( s_r8_evidence, frame + 76, sizeof(s_r8_evidence) );
            memcpy( s_r8_evidence_ref, ref, 16 );
            s_r8_evidence_set = TRUE;
            s_r8_attempt = attempt;
            r8_trace_line( "r8: redeemed" );

            /* DONE: D leaves; then RELEASE retires the attempt at the verifier. (Lab order: DONE is written
             * before the launch commit, so the secure desktop is free while the program starts.) */
            step = "done";
            flen = 80;
            if (e1r8_encode_done( out, 80, &tuple, E1R8_DONE_COMPLETED ) ||
                (err = r8_pipe_transfer( req, pi.hProcess, to_d_write, TRUE, out, &flen, 0, 0, r8_start, r8_timeout, &stuck )))
            {
                DWORD code = 0;
                char text[64];
                GetExitCodeProcess( pi.hProcess, &code );
                snprintf( text, sizeof(text), "r8: done: dialog exit code %lu", (unsigned long)code );
                r8_trace_line( text );
                if (!err) err = ERROR_ACCESS_DENIED;
                s_r8_evidence_set = FALSE; e1s18_wipe( s_r8_evidence, sizeof(s_r8_evidence) ); goto r8_fail;
            }
            WaitForSingleObject( pi.hProcess, 10000 );
            err = ERROR_SUCCESS;
            r8_trace_line( "r8: done sent" );

        r8_fail:
            if (err && !refused) r8_trace_err( step, err );
            /* RELEASE is best effort: closing the channel retires the attempt as well. */
            if ((!err || refused) && !e1r8_encode_release( frame, 80, &tuple, err ? E1R8_RELEASE_CANCELLED
                                                                                 : E1R8_RELEASE_COMPLETED ) &&
                !r8_v_send( req, frame, 80 ) && !r8_v_recv( req, frame, &flen, 5000 ) &&
                !e1r8_decode_released( frame, flen, &tuple ))
                r8_trace_line( "r8: released" );
            TerminateProcess( pi.hProcess, 1 );
            WaitForSingleObject( pi.hProcess, 5000 );
            CloseHandle( pi.hProcess );
            if (!stuck)
            {
                CloseHandle( to_d_write );
                CloseHandle( from_d_read );
            }
            if (secure) secure_leave();
            e1s18_wipe( frame, sizeof(frame) );
            e1s18_wipe( out, sizeof(out) );
            e1s18_wipe( challenge, sizeof(challenge) );
            return err;
        }
    }
#endif
}
#endif

#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
/* WO-25: the S6 transport export (wexhost.dll), as in the redeem path. */
typedef DWORD (WINAPI *e1s18_pfn_WexE1s6Transport)( HANDLE, DWORD, void *, DWORD, UINT64 * );

static e1s18_pfn_WexE1s6Transport e1s18_get_s6_transport( void )
{
    HMODULE wexhost = GetModuleHandleW( L"wexhost.dll" );
    if (!wexhost) wexhost = LoadLibraryW( L"wexhost.dll" );
    if (!wexhost) return NULL;
    return (e1s18_pfn_WexE1s6Transport)(void *)GetProcAddress( wexhost, "WexE1s6Transport" );
}

/* WO-25: the extended caller-binding query (ntdll.dll). The native-layout
 * structures mirror wine/dlls/ntdll/sec.c (WEX_PIPE_CALLER_BINDING[_EXT]); no
 * pointer crosses the server transport. */
typedef struct _e1s18_pipe_caller_binding
{
    ULONG Size;
    ULONG Version;
    HANDLE Process;
    HANDLE Token;
    HANDLE WriterProcess;
    HANDLE WriterToken;
    ULONG Pid;
    LARGE_INTEGER StartTime;
    LUID TokenId;
    LUID ModifiedId;
    LUID WriterTokenId;
    LUID WriterModifiedId;
    BOOL Impersonated;
    BOOL TokenCurrent;
    ULONG WriterFlags;
} e1s18_pipe_caller_binding;

typedef struct _e1s18_pipe_caller_binding_ext
{
    e1s18_pipe_caller_binding binding;
    const void *input;
    ULONG in_size;
    void *output;
    ULONG *out_size;
} e1s18_pipe_caller_binding_ext;

typedef NTSTATUS (WINAPI *e1s18_pfn_QueryWexPipeCallerBindingEx)( HANDLE, e1s18_pipe_caller_binding * );

static e1s18_pfn_QueryWexPipeCallerBindingEx e1s18_get_binding_query( void )
{
    HMODULE ntdll = GetModuleHandleW( L"ntdll.dll" );
    if (!ntdll) return NULL;
    return (e1s18_pfn_QueryWexPipeCallerBindingEx)(void *)GetProcAddress( ntdll, "NtQueryWexPipeCallerBindingEx" );
}

/* WO-25: close the four handles the extended binding query returns on success. */
static void e1s18_close_binding_handles( e1s18_pipe_caller_binding *b )
{
    if (b->Process) CloseHandle( b->Process );
    if (b->Token) CloseHandle( b->Token );
    if (b->WriterProcess) CloseHandle( b->WriterProcess );
    if (b->WriterToken) CloseHandle( b->WriterToken );
    memset( b, 0, sizeof(*b) );
}
#endif

#ifdef WEXELEVATE_E1S18
DWORD e1s18_transport_redeem_live( struct request *req,
                                   const char *account_name,
                                   const uint8_t nonce[32],
                                   const uint8_t binding_digest[32],
                                   const uint8_t request_digest[32],
                                   uint8_t *out_evidence_buf,
                                   size_t *out_evidence_len,
                                   uint8_t out_evidence_ref[16] )
{
    (void)account_name; (void)binding_digest; (void)request_digest;

    if (!req || !req->pipe || !nonce || !out_evidence_buf || !out_evidence_len ||
        !out_evidence_ref)
        return ERROR_INVALID_PARAMETER;

#if defined(_WIN32)
    /* R8: the one redemption happened inside the dialog step, on the attempt's retained S6 channel
     * (frames 28/29 in e1s18_spawn_consent_dialog); a second OPEN would replace the broker generation.
     * Hand that record over exactly once. The S12 storage generation is not produced here; the broker
     * asks the server for it via WEX_HKU_STORAGE_QUERY (op 12) after S10 issuance. */
    if (!s_r8_evidence_set || *out_evidence_len < sizeof(s_r8_evidence)) return ERROR_ACCESS_DENIED;
    s_r8_evidence_set = FALSE;
    memcpy( out_evidence_buf, s_r8_evidence, sizeof(s_r8_evidence) );
    *out_evidence_len = sizeof(s_r8_evidence);
    memcpy( out_evidence_ref, s_r8_evidence_ref, 16 );
    e1s18_wipe( s_r8_evidence, sizeof(s_r8_evidence) );
    e1s18_wipe( s_r8_evidence_ref, sizeof(s_r8_evidence_ref) );
    return ERROR_SUCCESS;
#else
    return ERROR_NOT_SUPPORTED;
#endif
}
#endif

#if defined(_WIN32) && defined(WEXELEVATE_E1S18)

#define R9A_FRAME_MAX   2400

static BOOL e1r9_is_enabled( void )
{
    HKEY key;
    DWORD val = 0, sz = sizeof(val), type = 0;
    if (RegOpenKeyExW( HKEY_LOCAL_MACHINE, L"Software\\WineExplorer\\Elevation", 0, KEY_READ, &key ) == ERROR_SUCCESS)
    {
        if (RegQueryValueExW( key, L"R9", NULL, &type, (BYTE *)&val, &sz ) == ERROR_SUCCESS && type == REG_DWORD)
        {
            RegCloseKey( key );
            return val == 1;
        }
        RegCloseKey( key );
    }
    return FALSE;
}

static void r9_send_cancel( HANDLE pipe, const struct e1r8_tuple *t, uint32_t reason )
{
    uint8_t buf[80];
    DWORD done = 0;
    if (pipe == INVALID_HANDLE_VALUE) return;
    if (!e1r8_encode_cancel_r9( buf, 80, t, reason ))
        pipe_io( pipe, buf, 80, TRUE, 2000, &done );
}

struct r9_d_frame_ctx
{
    HANDLE pipe;
    uint8_t buf[340];
    uint32_t len;
    uint16_t type;
    int ok;
};

static DWORD WINAPI r9_d_reader_thread( void *param )
{
    struct r9_d_frame_ctx *ctx = param;
    size_t off = 0;
    DWORD got;
    uint32_t total;
    uint16_t type;
    BOOL valid = FALSE;

    while (off < 76)
    {
        if (!ReadFile( ctx->pipe, ctx->buf + off, (DWORD)(76 - off), &got, NULL ) || !got)
        {
            ctx->ok = -2;
            return 0;
        }
        off += got;
    }

    total = e1r8_get_u32( ctx->buf + 8 );
    type = e1r8_get_u16( ctx->buf + 6 );

    if (type == E1R8_TYPE_ACK && total == 156) valid = TRUE;
    else if (type == E1R8_TYPE_SELECT && total >= 81 && total <= 143) valid = TRUE;
    else if (type == E1R8_TYPE_ANSWER && total == 264) valid = TRUE;

    if (!valid || total > sizeof(ctx->buf))
    {
        ctx->ok = -1;
        return 0;
    }

    while (off < (size_t)total)
    {
        if (!ReadFile( ctx->pipe, ctx->buf + off, (DWORD)(total - off), &got, NULL ) || !got)
        {
            ctx->ok = -2;
            return 0;
        }
        off += got;
    }

    ctx->len = total;
    ctx->type = type;
    ctx->ok = 1;
    return 0;
}

DWORD e1r9_agent_dialog( struct request *req,
                         const uint8_t nonce[32],
                         const uint8_t binding_digest[32],
                         const uint8_t request_digest[32],
                         char *out_account,
                         size_t account_max )
{
    HANDLE hpipe = INVALID_HANDLE_VALUE;
    HANDLE b0_event = NULL;
    HANDLE d_thread = NULL;
    struct r9_d_frame_ctx *d_ctx = NULL;
    OVERLAPPED ov_b0;
    struct e1r8_tuple mine, tuple;
    struct r8_dialog dlg;
    uint8_t req_buf[1792];
    uint8_t b0_buf[R9A_FRAME_MAX];
    uint32_t img_len = 0;
    size_t req_total = 0;
    DWORD mode, done = 0, err = ERROR_ACCESS_DENIED;
    BOOL b0_pending = FALSE;
    BOOL done_relayed = FALSE;   /* the dialog has its last frame (DONE or the lockout notice): it leaves by itself */
    BOOL locked = FALSE;         /* that last frame was the lockout notice */
    BOOL stuck = FALSE;
    BOOL secure = TRUE;
    uint32_t cancel_reason = 0;
    const struct e1s18_ops *ops = e1s18_get_ops();

    if (!req || !nonce || !binding_digest || !request_digest || !out_account || account_max == 0)
        return ERROR_INVALID_PARAMETER;

    /* Step 1: e1s18_r8_prepare(req) if no channel is open */
    if (!s_r8_channel)
    {
        if ((err = e1s18_r8_prepare( req )))
            return err;
    }
    if (!e1s18_r8_context_complete( &s_r8_context ))
        return ERROR_NOT_SUPPORTED;

    /* Step 2: Open pipe to B0 */
    hpipe = CreateFileW( L"\\\\.\\pipe\\WexSession0\\wexelevate.r9",
                         GENERIC_READ | GENERIC_WRITE,
                         0,
                         NULL,
                         OPEN_EXISTING,
                         FILE_FLAG_OVERLAPPED,
                         NULL );
    if (hpipe == INVALID_HANDLE_VALUE)
    {
        r8_trace_err( "r9a open", GetLastError() );
        return ERROR_ACCESS_DENIED;
    }

    mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState( hpipe, &mode, NULL, NULL ))
    {
        r8_trace_err( "r9a pipe mode", GetLastError() );
        CloseHandle( hpipe );
        return ERROR_ACCESS_DENIED;
    }

    if (!(b0_event = CreateEventW( NULL, TRUE, FALSE, NULL )))
    {
        CloseHandle( hpipe );
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    /* Fill "mine" tuple */
    memset( &mine, 0, sizeof(mine) );
    mine.request_id = req->id;
    mine.attempt = 0;
    memcpy( mine.nonce, nonce, 32 );
    memcpy( mine.binding_digest, binding_digest, 32 );
    memcpy( mine.request_digest, request_digest, 32 );
    mine.deadline = E1S18_DEADLINE( req );
    mine.root_gen = s_r8_context.root_gen;
    mine.server_gen = s_r8_context.server_gen;
    mine.broker_gen = s_r8_context.broker_gen;
    mine.verifier_gen = s_r8_context.verifier_gen;
    mine.map_gen = s_r8_context.map_gen;
    mine.policy_gen = s_r8_context.policy_gen;
    mine.caller_session = s_r8_context.caller_session;
    memcpy( mine.desktop_route, s_r8_context.desktop_route, 16 );
    mine.desktop_mode = E1R8_DESKTOP_MODE_SECURE;

    if (!req->image || !req->image[0] || e1s18_display_string_len( req->image, &img_len ))
    {
        CloseHandle( b0_event );
        CloseHandle( hpipe );
        return ERROR_ACCESS_DENIED;
    }

    req_total = 232 + img_len;
    if (req_total > sizeof(req_buf) ||
        e1r8_encode_request( req_buf, req_total, &mine, E1R8_REQUEST_KIND_RUNAS, 1,
                             NULL, 0, (const uint8_t *)req->image, img_len, NULL, 0 ))
    {
        r8_trace_err( "r9a encode request", ERROR_INVALID_DATA );
        CloseHandle( b0_event );
        CloseHandle( hpipe );
        return ERROR_ACCESS_DENIED;
    }

    if ((err = pipe_io( hpipe, req_buf, (DWORD)req_total, TRUE, 10000, &done )))
    {
        r8_trace_err( "r9a request", err );
        CloseHandle( b0_event );
        CloseHandle( hpipe );
        return ERROR_ACCESS_DENIED;
    }
    r8_trace_line( "r9a: request sent" );

    /* Step 3: Read DECISION */
    if ((err = pipe_io( hpipe, b0_buf, sizeof(b0_buf), FALSE, 10000, &done )) || done < 84)
    {
        r8_trace_err( "r9a decision", err );
        CloseHandle( b0_event );
        CloseHandle( hpipe );
        return ERROR_ACCESS_DENIED;
    }

    {
        uint32_t status = e1r8_get_u32( b0_buf + 76 );
        if (status == E1R8_DECISION_DENY)
        {
            uint32_t reason = e1r8_get_u32( b0_buf + 80 );
            DWORD mapped_err = ERROR_ACCESS_DENIED;
            if (reason == 1) mapped_err = ERROR_ACCESS_DENIED;
            else if (reason == 2) mapped_err = ERROR_ACCOUNT_LOCKED_OUT;
            else if (reason == 3) mapped_err = ERROR_BUSY;

            /* Expect END */
            if (!pipe_io( hpipe, b0_buf, sizeof(b0_buf), FALSE, 5000, &done ))
            {
                struct e1r8_end em;
                if (!e1r8_decode_end( b0_buf, done, &mine, &em ))
                    r8_trace_line( "r9a: end" );
            }
            CloseHandle( b0_event );
            CloseHandle( hpipe );
            return mapped_err;
        }
        else if (status == E1R8_DECISION_PROMPT)
        {
            r8_trace_line( "r9a: decision prompt" );
            if (done != 284 || memcmp( b0_buf, "E1R8", 4 ) != 0 ||
                e1r8_get_u16( b0_buf + 4 ) != 1 || e1r8_get_u16( b0_buf + 6 ) != E1R8_TYPE_DECISION)
            {
                r9_send_cancel( hpipe, &mine, E1R8_CANCEL_R9_PROTOCOL );
                CloseHandle( b0_event );
                CloseHandle( hpipe );
                return ERROR_ACCESS_DENIED;
            }
            if (e1r8_decode_pre_endorsement_tuple( b0_buf + 80, E1R8_PRE_ENDORSEMENT_SIZE, &tuple ))
            {
                r9_send_cancel( hpipe, &mine, E1R8_CANCEL_R9_PROTOCOL );
                CloseHandle( b0_event );
                CloseHandle( hpipe );
                return ERROR_ACCESS_DENIED;
            }
            if (tuple.request_id != mine.request_id ||
                memcmp( tuple.nonce, mine.nonce, 32 ) != 0 ||
                memcmp( tuple.binding_digest, mine.binding_digest, 32 ) != 0 ||
                memcmp( tuple.request_digest, mine.request_digest, 32 ) != 0 ||
                tuple.root_gen != mine.root_gen ||
                tuple.server_gen != mine.server_gen ||
                tuple.broker_gen != mine.broker_gen ||
                tuple.verifier_gen != mine.verifier_gen ||
                tuple.map_gen != mine.map_gen ||
                tuple.policy_gen != mine.policy_gen ||
                memcmp( tuple.desktop_route, mine.desktop_route, 16 ) != 0 ||
                tuple.caller_session != mine.caller_session ||
                tuple.attempt != 1 ||
                tuple.deadline > E1S18_DEADLINE( req ) ||
                (tuple.desktop_mode != E1R8_DESKTOP_MODE_SECURE && tuple.desktop_mode != E1R8_DESKTOP_MODE_INSECURE) ||
                e1r8_is_zero16( tuple.channel_id ))
            {
                r9_send_cancel( hpipe, &tuple, E1R8_CANCEL_R9_PROTOCOL );
                CloseHandle( b0_event );
                CloseHandle( hpipe );
                return ERROR_ACCESS_DENIED;
            }
        }
        else
        {
            r9_send_cancel( hpipe, &mine, E1R8_CANCEL_R9_PROTOCOL );
            CloseHandle( b0_event );
            CloseHandle( hpipe );
            return ERROR_ACCESS_DENIED;
        }
    }

attempt_step4:
    /* Step 4: Create dialog D */
    secure = (tuple.desktop_mode == E1R8_DESKTOP_MODE_SECURE);
    memset( &dlg, 0, sizeof(dlg) );
    done_relayed = FALSE;
    b0_pending = FALSE;
    cancel_reason = 0;

    err = r8_dialog_create( req, &tuple, req->id, tuple.attempt, tuple.channel_id, secure, &dlg );
    if (err)
    {
        uint8_t el_buf[96];
        uint8_t zero_dlg[16] = { 0 };
        if (!e1r8_encode_endorsed_local( el_buf, 96, &tuple, zero_dlg, E1R8_ENDORSED_LOCAL_REFUSED ))
            pipe_io( hpipe, el_buf, 96, TRUE, 5000, &done );
        CloseHandle( b0_event );
        CloseHandle( hpipe );
        return ERROR_ACCESS_DENIED;
    }

    if (ResumeThread( dlg.thread ) == (DWORD)-1)
    {
        uint8_t el_buf[96];
        uint8_t zero_dlg[16] = { 0 };
        TerminateProcess( dlg.process, 1 );
        WaitForSingleObject( dlg.process, 5000 );
        CloseHandle( dlg.process );
        CloseHandle( dlg.thread );
        CloseHandle( dlg.to_d_write );
        CloseHandle( dlg.from_d_read );
        if (secure) secure_leave();
        if (!e1r8_encode_endorsed_local( el_buf, 96, &tuple, zero_dlg, E1R8_ENDORSED_LOCAL_REFUSED ))
            pipe_io( hpipe, el_buf, 96, TRUE, 5000, &done );
        CloseHandle( b0_event );
        CloseHandle( hpipe );
        return ERROR_ACCESS_DENIED;
    }
    CloseHandle( dlg.thread );
    dlg.thread = NULL;

    {
        uint8_t el_buf[96];
        if (e1r8_encode_endorsed_local( el_buf, 96, &tuple, dlg.dialog_id, E1R8_ENDORSED_LOCAL_ENDORSED ) ||
            pipe_io( hpipe, el_buf, 96, TRUE, 5000, &done ))
        {
            TerminateProcess( dlg.process, 1 );
            WaitForSingleObject( dlg.process, 5000 );
            CloseHandle( dlg.process );
            CloseHandle( dlg.to_d_write );
            CloseHandle( dlg.from_d_read );
            if (secure) secure_leave();
            CloseHandle( b0_event );
            CloseHandle( hpipe );
            return ERROR_ACCESS_DENIED;
        }
    }
    memcpy( tuple.dialog_id, dlg.dialog_id, 16 );
    r8_trace_line( "r9a: endorsed" );

    /* Step 5: Relay loop */
    for (;;)
    {
        DWORD wait_ms, wr, wait_count = 0;
        HANDLE wait_handles[6];
        DWORD idx_b0, idx_d = (DWORD)-1, idx_proc, idx_caller, idx_stop, idx_lease;

        if (!b0_pending)
        {
            memset( &ov_b0, 0, sizeof(ov_b0) );
            ov_b0.hEvent = b0_event;
            ResetEvent( b0_event );
            if (ReadFile( hpipe, b0_buf, sizeof(b0_buf), &done, &ov_b0 ))
            {
                b0_pending = TRUE;
            }
            else
            {
                DWORD le = GetLastError();
                if (le == ERROR_IO_PENDING)
                {
                    b0_pending = TRUE;
                }
                else
                {
                    err = ERROR_CANCELLED;
                    cancel_reason = E1R8_CANCEL_R9_PEER_GONE;
                    goto relay_fail;
                }
            }
        }

        if (!d_thread && !done_relayed)
        {
            d_ctx = calloc( 1, sizeof(*d_ctx) );
            if (!d_ctx)
            {
                err = ERROR_NOT_ENOUGH_MEMORY;
                cancel_reason = E1R8_CANCEL_R9_PROTOCOL;
                goto relay_fail;
            }
            d_ctx->pipe = dlg.from_d_read;
            d_thread = CreateThread( NULL, 0, r9_d_reader_thread, d_ctx, 0, NULL );
            if (!d_thread)
            {
                free( d_ctx );
                d_ctx = NULL;
                err = ERROR_NOT_ENOUGH_MEMORY;
                cancel_reason = E1R8_CANCEL_R9_PROTOCOL;
                goto relay_fail;
            }
        }

        idx_b0 = wait_count;
        wait_handles[wait_count++] = b0_event;

        if (d_thread)
        {
            idx_d = wait_count;
            wait_handles[wait_count++] = d_thread;
        }

        /* After DONE (or the lockout notice) the dialog leaves by itself: its exit is expected and must not
         * wake this wait again and again while the broker's next message is on its way. */
        idx_proc = (DWORD)-1;
        if (!done_relayed)
        {
            idx_proc = wait_count;
            wait_handles[wait_count++] = dlg.process;
        }

        idx_caller = wait_count;
        wait_handles[wait_count++] = req->caller;

        idx_stop = wait_count;
        wait_handles[wait_count++] = stop_event;

        idx_lease = wait_count;
        wait_handles[wait_count++] = req->lease_event;

        wait_ms = r8_remaining( req );
        if (!wait_ms)
        {
            err = ERROR_TIMEOUT;
            cancel_reason = E1R8_CANCEL_R9_DEADLINE;
            goto relay_fail;
        }

        wr = WaitForMultipleObjects( wait_count, wait_handles, FALSE, wait_ms );
        if (wr == WAIT_TIMEOUT)
        {
            err = ERROR_TIMEOUT;
            cancel_reason = E1R8_CANCEL_R9_DEADLINE;
            goto relay_fail;
        }

        if (wr == WAIT_OBJECT_0 + idx_b0)
        {
            DWORD b0_got = 0;
            if (!GetOverlappedResult( hpipe, &ov_b0, &b0_got, FALSE ))
            {
                b0_pending = FALSE;
                err = ERROR_CANCELLED;
                cancel_reason = E1R8_CANCEL_R9_PEER_GONE;
                goto relay_fail;
            }
            b0_pending = FALSE;

            if (b0_got < E1R8_HEADER_SIZE)
            {
                err = ERROR_ACCESS_DENIED;
                cancel_reason = E1R8_CANCEL_R9_PROTOCOL;
                goto relay_fail;
            }

            {
                uint16_t msg_type = e1r8_get_u16( b0_buf + 6 );
                if (msg_type == E1R8_TYPE_CANCEL_R9)
                {
                    err = ERROR_CANCELLED;
                    cancel_reason = 0;
                    goto relay_fail;
                }
                else if (msg_type == E1R8_TYPE_END)
                {
                    struct e1r8_end em;
                    r8_trace_line( "r9a: end" );
                    if (!e1r8_decode_end( b0_buf, b0_got, &tuple, &em ) && em.outcome == E1R8_END_DENIED)
                        err = ERROR_ACCESS_DENIED;
                    else if (done_relayed && !locked)
                        err = ERROR_LOGON_FAILURE;   /* DONE then END: the last attempt's password was refused */
                    else
                        err = ERROR_CANCELLED;
                    cancel_reason = 0;
                    goto relay_fail;
                }
                else if (msg_type == E1R8_TYPE_TO_D)
                {
                    struct e1r8_to_d td;
                    size_t flen;
                    char tr[64];

                    if (e1r8_decode_to_d( b0_buf, b0_got, &tuple, &td ))
                    {
                        err = ERROR_ACCESS_DENIED;
                        cancel_reason = E1R8_CANCEL_R9_PROTOCOL;
                        goto relay_fail;
                    }
                    snprintf( tr, sizeof(tr), "r9a: to_d %u", (unsigned int)td.type );
                    r8_trace_line( tr );

                    if (td.type == E1R8_TYPE_DONE || td.type == E1R8_TYPE_LOCKED)
                        done_relayed = TRUE;
                    if (td.type == E1R8_TYPE_LOCKED) locked = TRUE;

                    flen = td.frame_len;
                    err = r8_pipe_transfer( req, dlg.process, dlg.to_d_write, TRUE,
                                            (uint8_t *)td.frame, &flen, 0, 0,
                                            GetTickCount64(), r8_remaining( req ), &stuck );
                    if (err)
                    {
                        cancel_reason = E1R8_CANCEL_R9_D_GONE;
                        goto relay_fail;
                    }
                    continue;
                }
                else if (msg_type == E1R8_TYPE_RECHECK)
                {
                    struct e1r8_recheck rc;
                    char tr[64];
                    int rc_ok;
                    uint32_t rc_stat;
                    uint8_t rc_buf[84];

                    if (e1r8_decode_recheck( b0_buf, b0_got, &tuple, &rc ))
                    {
                        err = ERROR_ACCESS_DENIED;
                        cancel_reason = E1R8_CANCEL_R9_PROTOCOL;
                        goto relay_fail;
                    }
                    snprintf( tr, sizeof(tr), "r9a: recheck %u", (unsigned int)rc.step );
                    r8_trace_line( tr );

                    rc_ok = (ops->r8_recheck && ops->r8_recheck( req, tuple.dialog_id ) == 0);
                    rc_stat = rc_ok ? E1R8_RECHECKED_OK : E1R8_RECHECKED_REFUSED;
                    if (!e1r8_encode_rechecked( rc_buf, 84, &tuple, rc.step, rc_stat ))
                        pipe_io( hpipe, rc_buf, 84, TRUE, 5000, &done );

                    if (!rc_ok)
                    {
                        err = ERROR_ACCESS_DENIED;
                        cancel_reason = E1R8_CANCEL_R9_RECHECK;
                        goto relay_fail;
                    }
                    continue;
                }
                else if (msg_type == E1R8_TYPE_NEXT_ATTEMPT)
                {
                    struct e1r8_tuple next_t;
                    size_t flen;
                    struct e1r8_to_d td;

                    r8_trace_line( "r9a: next attempt" );
                    if (b0_got != 280 ||
                        e1r8_decode_pre_endorsement_tuple( b0_buf + 76, E1R8_PRE_ENDORSEMENT_SIZE, &next_t ))
                    {
                        err = ERROR_ACCESS_DENIED;
                        cancel_reason = E1R8_CANCEL_R9_PROTOCOL;
                        goto relay_fail;
                    }
                    if (next_t.attempt != tuple.attempt + 1 || next_t.attempt > 3 ||
                        next_t.request_id != mine.request_id ||
                        memcmp( next_t.nonce, mine.nonce, 32 ) != 0 ||
                        memcmp( next_t.binding_digest, mine.binding_digest, 32 ) != 0 ||
                        memcmp( next_t.request_digest, mine.request_digest, 32 ) != 0 ||
                        next_t.root_gen != mine.root_gen ||
                        next_t.server_gen != mine.server_gen ||
                        next_t.broker_gen != mine.broker_gen ||
                        next_t.verifier_gen != mine.verifier_gen ||
                        next_t.map_gen != mine.map_gen ||
                        next_t.policy_gen != mine.policy_gen ||
                        memcmp( next_t.desktop_route, mine.desktop_route, 16 ) != 0 ||
                        next_t.caller_session != mine.caller_session ||
                        next_t.deadline > E1S18_DEADLINE( req ) ||
                        (next_t.desktop_mode != E1R8_DESKTOP_MODE_SECURE && next_t.desktop_mode != E1R8_DESKTOP_MODE_INSECURE) ||
                        e1r8_is_zero16( next_t.channel_id ))
                    {
                        err = ERROR_ACCESS_DENIED;
                        cancel_reason = E1R8_CANCEL_R9_PROTOCOL;
                        goto relay_fail;
                    }

                    /* Retry state: the old dialog's DONE comes next, still under the old attempt's header. The
                     * dialog has the refusal: it tells the person and leaves by itself, as with the R8 broker
                     * (bounded by the request's deadline); only then is anything of it stopped. */
                    if (pipe_io( hpipe, b0_buf, sizeof(b0_buf), FALSE, 5000, &b0_got ) ||
                        e1r8_decode_to_d( b0_buf, b0_got, &tuple, &td ) || td.type != E1R8_TYPE_DONE)
                    {
                        err = ERROR_ACCESS_DENIED;
                        cancel_reason = E1R8_CANCEL_R9_PROTOCOL;
                        goto relay_fail;
                    }
                    r8_trace_line( "r9a: to_d 8" );
                    flen = td.frame_len;
                    if (!r8_pipe_transfer( req, dlg.process, dlg.to_d_write, TRUE,
                                           (uint8_t *)td.frame, &flen, 0, 0,
                                           GetTickCount64(), 5000, &stuck ))
                        WaitForSingleObject( dlg.process, r8_remaining( req ) );
                    if (stuck)
                    {
                        err = ERROR_CANCELLED;
                        cancel_reason = E1R8_CANCEL_R9_D_GONE;
                        goto relay_fail;
                    }
                    TerminateProcess( dlg.process, 1 );
                    WaitForSingleObject( dlg.process, 5000 );
                    if (d_thread)
                    {
                        /* the dialog is gone, so its pipe is broken and the reader ends */
                        if (!r8_stop_worker( d_thread, dlg.process ))
                        {
                            d_thread = NULL;   /* kept with its context and the pipe handles */
                            d_ctx = NULL;
                            stuck = TRUE;
                            err = ERROR_CANCELLED;
                            cancel_reason = E1R8_CANCEL_R9_D_GONE;
                            goto relay_fail;
                        }
                        CloseHandle( d_thread );
                        d_thread = NULL;
                        free( d_ctx );
                        d_ctx = NULL;
                    }
                    CloseHandle( dlg.process );
                    CloseHandle( dlg.to_d_write );
                    CloseHandle( dlg.from_d_read );
                    if (secure) secure_leave();

                    tuple = next_t;
                    goto attempt_step4;
                }
                else if (msg_type == E1R8_TYPE_LAUNCH)
                {
                    struct e1r8_launch lnch;
                    uint8_t r_frame[E1S6_R8_FRAME_MAX];
                    size_t r_flen = 0;
                    char acct[64];
                    size_t alen;

                    if (e1r8_decode_launch( b0_buf, b0_got, &tuple, &lnch ))
                    {
                        err = ERROR_ACCESS_DENIED;
                        cancel_reason = E1R8_CANCEL_R9_PROTOCOL;
                        goto relay_fail;
                    }
                    r8_trace_line( "r9a: launch" );

                    /* Redemption on A's own S6 channel */
                    if (e1r8_put_header( r_frame, sizeof(r_frame), 28, 92, &tuple ))
                    {
                        err = ERROR_ACCESS_DENIED;
                        cancel_reason = E1R8_CANCEL_R9_PROTOCOL;
                        goto relay_fail;
                    }
                    memcpy( r_frame + 76, lnch.evidence_ref, 16 );
                    if (r8_v_send( req, r_frame, 92 ) ||
                        r8_v_recv( req, r_frame, &r_flen, r8_remaining( req ) ) ||
                        r_flen != 296 || e1r8_get_header( r_frame, 296, 29, &tuple, NULL ))
                    {
                        err = ERROR_ACCESS_DENIED;
                        cancel_reason = E1R8_CANCEL_R9_PROTOCOL;
                        goto relay_fail;
                    }

                    memcpy( s_r8_evidence, r_frame + 76, sizeof(s_r8_evidence) );
                    memcpy( s_r8_evidence_ref, lnch.evidence_ref, 16 );
                    s_r8_evidence_set = TRUE;
                    s_r8_attempt = tuple.attempt;
                    r8_trace_line( "r9a: redeemed" );

                    alen = lnch.account_len < sizeof(acct) - 1 ? lnch.account_len : sizeof(acct) - 1;
                    memcpy( acct, lnch.account, alen );
                    acct[alen] = '\0';
                    lstrcpynA( req->account_name, acct, sizeof(req->account_name) );
                    if (out_account && account_max)
                        lstrcpynA( out_account, acct, (int)account_max );

                    e1s18_resolve_account_s4( acct, req->account_key );
                    if (e1s18_is_all_zero( req->account_key, 16 ))
                    {
                        err = ERROR_ACCESS_DENIED;
                        cancel_reason = E1R8_CANCEL_R9_PROTOCOL;
                        goto relay_fail;
                    }

                    /* Clean up D: it had DONE and leaves by itself first (as with the R8 broker) */
                    if (done_relayed)
                        WaitForSingleObject( dlg.process, 10000 );
                    TerminateProcess( dlg.process, 1 );
                    WaitForSingleObject( dlg.process, 5000 );
                    if (d_thread)
                    {
                        if (r8_stop_worker( d_thread, dlg.process ))
                        {
                            CloseHandle( d_thread );
                            free( d_ctx );
                        }
                        else stuck = TRUE;   /* the reader keeps its context and the pipe handles */
                        d_thread = NULL;
                        d_ctx = NULL;
                    }
                    CloseHandle( dlg.process );
                    if (!stuck)
                    {
                        CloseHandle( dlg.to_d_write );
                        CloseHandle( dlg.from_d_read );
                    }
                    if (secure) secure_leave();

                    CloseHandle( b0_event );
                    s_r9_pipe = hpipe;
                    s_r9_tuple = tuple;
                    return ERROR_SUCCESS;
                }
                else
                {
                    err = ERROR_ACCESS_DENIED;
                    cancel_reason = E1R8_CANCEL_R9_PROTOCOL;
                    goto relay_fail;
                }
            }
        }
        else if (d_thread && wr == WAIT_OBJECT_0 + idx_d)
        {
            if (d_ctx->ok == 1)
            {
                uint8_t fd_buf[R9A_FRAME_MAX];
                size_t fd_len = E1R8_HEADER_SIZE + d_ctx->len;
                char tr[64];

                if (e1r8_encode_from_d( fd_buf, fd_len, &tuple, d_ctx->buf, d_ctx->len ) ||
                    pipe_io( hpipe, fd_buf, (DWORD)fd_len, TRUE, 5000, &done ))
                {
                    err = ERROR_ACCESS_DENIED;
                    cancel_reason = E1R8_CANCEL_R9_PROTOCOL;
                    goto relay_fail;
                }
                snprintf( tr, sizeof(tr), "r9a: from_d %u", (unsigned int)d_ctx->type );
                r8_trace_line( tr );

                CloseHandle( d_thread );
                d_thread = NULL;
                free( d_ctx );
                d_ctx = NULL;
                continue;
            }
            else if (d_ctx->ok == -1)
            {
                err = ERROR_ACCESS_DENIED;
                cancel_reason = E1R8_CANCEL_R9_PROTOCOL;
                goto relay_fail;
            }
            else
            {
                CloseHandle( d_thread );
                d_thread = NULL;
                free( d_ctx );
                d_ctx = NULL;

                if (done_relayed)
                    continue;

                err = ERROR_CANCELLED;
                cancel_reason = E1R8_CANCEL_R9_D_GONE;
                goto relay_fail;
            }
        }
        else if (idx_proc != (DWORD)-1 && wr == WAIT_OBJECT_0 + idx_proc)
        {
            err = ERROR_CANCELLED;
            cancel_reason = E1R8_CANCEL_R9_D_GONE;
            goto relay_fail;
        }
        else if (wr == WAIT_OBJECT_0 + idx_caller || wr == WAIT_OBJECT_0 + idx_stop)
        {
            err = ERROR_CANCELLED;
            cancel_reason = E1R8_CANCEL_R9_PEER_GONE;
            goto relay_fail;
        }
        else if (wr == WAIT_OBJECT_0 + idx_lease)
        {
            err = ERROR_FILE_CORRUPT;
            cancel_reason = E1R8_CANCEL_R9_PEER_GONE;
            goto relay_fail;
        }
    }

relay_fail:
    if (b0_pending)
    {
        CancelIo( hpipe );
        GetOverlappedResult( hpipe, &ov_b0, &done, TRUE );
    }
    if (cancel_reason != 0)
        r9_send_cancel( hpipe, &tuple, cancel_reason );

    /* A dialog that has its last frame (the refusal with DONE, or the lockout notice) tells the person and leaves
     * by itself; the request is over either way, so this only delays the cleanup, never a decision. */
    if (done_relayed && !stuck &&
        WaitForSingleObject( stop_event, 0 ) == WAIT_TIMEOUT)
        WaitForSingleObject( dlg.process, locked ? 10000 : r8_remaining( req ) );
    TerminateProcess( dlg.process, 1 );
    WaitForSingleObject( dlg.process, 5000 );
    if (d_thread)
    {
        if (r8_stop_worker( d_thread, dlg.process ))
        {
            CloseHandle( d_thread );
            free( d_ctx );
        }
        else stuck = TRUE;   /* the reader keeps its context and the pipe handles */
    }
    CloseHandle( dlg.process );
    if (!stuck)
    {
        CloseHandle( dlg.to_d_write );
        CloseHandle( dlg.from_d_read );
    }
    if (secure) secure_leave();
    CloseHandle( b0_event );
    CloseHandle( hpipe );
    if (locked) err = ERROR_ACCOUNT_LOCKED_OUT;
    return err;
}

void e1r9_agent_report( struct request *req, DWORD status, DWORD win32 )
{
    HANDLE pipe = s_r9_pipe;
    struct e1r8_tuple tuple = s_r9_tuple;
    uint8_t buf[R9A_FRAME_MAX];
    struct e1r8_end end_msg;
    DWORD done = 0;

    (void)req;
    if (pipe == INVALID_HANDLE_VALUE) return;
    s_r9_pipe = INVALID_HANDLE_VALUE;
    memset( &s_r9_tuple, 0, sizeof(s_r9_tuple) );

    if (!e1r8_encode_launched( buf, 84, &tuple, status, win32 ))
    {
        if (!pipe_io( pipe, buf, 84, TRUE, 5000, &done ))
        {
            r8_trace_line( "r9a: launched" );
            if (!pipe_io( pipe, buf, sizeof(buf), FALSE, 5000, &done ))
            {
                if (!e1r8_decode_end( buf, done, &tuple, &end_msg ))
                    r8_trace_line( "r9a: end" );
            }
        }
    }
    CloseHandle( pipe );
}

#else

DWORD e1r9_agent_dialog( struct request *req,
                         const uint8_t nonce[32],
                         const uint8_t binding_digest[32],
                         const uint8_t request_digest[32],
                         char *out_account,
                         size_t account_max )
{
    (void)req; (void)nonce; (void)binding_digest; (void)request_digest;
    (void)out_account; (void)account_max;
    return ERROR_NOT_SUPPORTED;
}

void e1r9_agent_report( struct request *req, DWORD status, DWORD win32 )
{
    (void)req; (void)status; (void)win32;
}

#endif

static DWORD default_prompt_dialog( struct request *req, const uint8_t nonce[32],
                                    const uint8_t binding_digest[32], const uint8_t request_digest[32],
                                    char *out_account, size_t account_max )
{
    const struct e1s18_boundary_table *boundary = e1s18_get_boundary_table();
    struct wex_prompt_queue_entry *q_entry = NULL;
    uint8_t channel_token[16];
    uint8_t answer_buf[WEX_S1_ANSWER_LENGTH];
    size_t answer_len = 0;
    uint64_t caller_logon = 0;
    uint64_t deadline = 0;
    int q_res;
    DWORD err = ERROR_SUCCESS;
    BOOL secure = TRUE; /* desktop_mode 1 unless an insecure desktop is explicitly configured */

    if (!req || !nonce || !binding_digest || !request_digest || !out_account || account_max == 0)
        return ERROR_INVALID_PARAMETER;

    e1s18_init_prompt_queue();

    caller_logon = ((uint64_t)req->caller_stats.AuthenticationId.HighPart << 32) |
                   (uint32_t)req->caller_stats.AuthenticationId.LowPart;
    /* R4: the channel id is 16 independent random bytes, not derived from the
     * nonce. RNG failure or an all-zero id denies before any dialog starts. */
    if (!e1s18_fill_random( channel_token, 16 ))
        return ERROR_ACCESS_DENIED;
    if (e1s18_is_all_zero( channel_token, 16 ))
        return ERROR_ACCESS_DENIED;

    /* WO-16: use the admission deadline set once at the start of the
     * standard-request handling; the prompt step must not extend it. */
    deadline = E1S18_DEADLINE( req );

    /* Step 1: Enqueue in S17 serialized prompt queue */
    q_res = wex_prompt_queue_enqueue( &s_prompt_queue, req->id, req->caller_pid,
                                      caller_logon, req->caller_session,
                                      secure ? "WinSta0\\Winlogon" : "WinSta0\\Default",
                                      deadline, &q_entry );
    if (q_res != WEX_STATUS_OK)
    {
        return ERROR_BUSY;
    }

    /* Wait for our turn in FIFO order for this desktop/session */
    if (!q_entry->is_active)
    {
        q_res = wex_prompt_queue_wait_active( &s_prompt_queue, q_entry, 120000 );
        if (q_res != WEX_STATUS_OK)
        {
            wex_prompt_queue_release( &s_prompt_queue, q_entry );
            return (q_res == WEX_STATUS_TIMED_OUT) ? ERROR_TIMEOUT : ERROR_CANCELLED;
        }
    }

    memset( answer_buf, 0, sizeof(answer_buf) );

    /* Step 2: Dialog conversation execution */
    if (boundary && boundary->dialog_exec)
    {
        err = boundary->dialog_exec( req, req->id, nonce, binding_digest, request_digest,
                                     channel_token, secure, answer_buf, &answer_len,
                                     out_account, account_max );
        if (err == ERROR_SUCCESS)
        {
            /* R15: the dialog answer must be exactly WEX_S1_ANSWER_LENGTH bytes. */
            if (answer_len == WEX_S1_ANSWER_LENGTH)
            {
                if (!wex_verify_answer_record( answer_buf, answer_len,
                                               req->id, 1, 0, 0,
                                               WEX_ANSWER_KIND_CONFIRM,
                                               nonce, binding_digest, request_digest, channel_token ))
                {
                    const struct wex_s1_answer_record *rec = (const struct wex_s1_answer_record *)answer_buf;
                    if (rec->answer_kind == WEX_ANSWER_KIND_CANCEL) err = ERROR_CANCELLED;
                    else err = ERROR_ACCESS_DENIED;
                }
                else
                {
                    if (e1s18_is_answer_replayed( req->id, nonce ))
                    {
                        err = ERROR_ACCESS_DENIED;
                    }
                    else
                    {
                        e1s18_record_answer( req->id, nonce );
                        if (!wex_valid_account_name( out_account ))
                        {
                            err = ERROR_ACCESS_DENIED;
                        }
                        else
                        {
                            err = ERROR_SUCCESS;
                        }
                    }
                }
            }
            else
            {
                err = ERROR_CANCELLED;
            }
        }
    }
#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
    else
    {
        if (e1r9_is_enabled())
        {
            err = e1r9_agent_dialog( req, nonce, binding_digest, request_digest, out_account, account_max );
        }
        else
        {
            /* Spec section 1: attempts 1..3 per request, each with a fresh dialog and channel id; a
             * refused password is the only outcome that leads to a further attempt, and the admission
             * deadline is never extended. */
            uint32_t attempt;
            for (attempt = 1; ; attempt++)
            {
                err = e1s18_spawn_consent_dialog( req, req->id, attempt, nonce, binding_digest, request_digest,
                                                  channel_token, secure, out_account, account_max );
                if (err != ERROR_LOGON_FAILURE || attempt >= 3 || r8_remaining( req ) < 10000) break;
                if (!e1s18_fill_random( channel_token, 16 ) || e1s18_is_all_zero( channel_token, 16 )) break;
                r8_trace_line( "r8: further attempt" );
            }
        }
    }
#else
    else
    {
        err = ERROR_NOT_SUPPORTED;
    }
#endif

    /* Step 3: Dequeue and release prompt queue slot */
    if (q_entry)
    {
        wex_prompt_queue_release( &s_prompt_queue, q_entry );
    }

    wex_secure_wipe( answer_buf, sizeof(answer_buf) );
    wex_secure_wipe( channel_token, sizeof(channel_token) );
    if (err != ERROR_SUCCESS)
    {
        out_account[0] = 0;
    }
    return err;
}

static DWORD default_redeem_evidence( struct request *req, const char *account_name,
                                      const uint8_t nonce[32], const uint8_t binding_digest[32],
                                      const uint8_t request_digest[32],
                                      uint8_t out_account_key[16], uint8_t out_evidence_ref[16] )
{
    const struct e1s18_boundary_table *boundary = e1s18_get_boundary_table();
    uint8_t evidence_buf[256];
    size_t evidence_len = sizeof(evidence_buf);
    uint8_t evidence_ref[16];
    uint64_t expected_host_uid;
    uint64_t now = e1s18_get_monotonic_time();
    const struct wex_s1_evidence_record *ev = NULL;
    DWORD err = ERROR_SUCCESS;

    if (!req || !account_name || !nonce || !binding_digest || !request_digest ||
        !out_account_key || !out_evidence_ref)
        return ERROR_INVALID_PARAMETER;

    if (now == 0)
        return ERROR_INVALID_DATA;

    if (!req->caller_pid)
        return ERROR_INVALID_PARAMETER;

    expected_host_uid = (uint64_t)req->caller_pid;

    memset( evidence_buf, 0, sizeof(evidence_buf) );
    memset( evidence_ref, 0, sizeof(evidence_ref) );

    if (boundary && boundary->transport_redeem)
    {
        err = boundary->transport_redeem( req, account_name, nonce, binding_digest, request_digest,
                                          evidence_buf, &evidence_len, evidence_ref );
        if (err != ERROR_SUCCESS)
        {
            e1s18_wipe( evidence_buf, sizeof(evidence_buf) );
            return err;
        }
    }
#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
    else
    {
        err = e1s18_transport_redeem_live( req, account_name, nonce, binding_digest, request_digest,
                                           evidence_buf, &evidence_len, evidence_ref );
        if (err != ERROR_SUCCESS)
        {
            e1s18_wipe( evidence_buf, sizeof(evidence_buf) );
            return err;
        }
    }
#else
    else
    {
        return ERROR_NOT_SUPPORTED;
    }
#endif

    /* R4: take the clock again after the transport call returns, so evidence
     * that expires during the call is rejected. */
    now = e1s18_get_monotonic_time();
    if (now == 0)
        return ERROR_INVALID_DATA;

    /* R15: the evidence buffer must be exactly one S1 Kind 3 record, no more. */
    if (evidence_len != sizeof(struct wex_s1_evidence_record))
    {
        e1s18_wipe( evidence_buf, sizeof(evidence_buf) );
        return ERROR_INVALID_DATA;
    }

    ev = (const struct wex_s1_evidence_record *)evidence_buf;

    /* Verify S1 Kind 3 Evidence Record (220 B) */
    if (ev->kind != 3 || ev->version != 1 || ev->length != 220 ||
        (ev->required_caps & E1S1_ALL_REQUIRED_CAPS) != E1S1_ALL_REQUIRED_CAPS ||
        ev->request_id != req->id ||
        ev->attempt < 1 || ev->attempt > 3 ||
        ev->method != 1 || ev->service != 1 ||
        ev->map_gen == 0)
    {
        e1s18_wipe( evidence_buf, sizeof(evidence_buf) );
        return ERROR_ACCESS_DENIED;
    }

    /* Verify digests and nonce */
    if (memcmp( ev->request_digest, request_digest, 32 ) != 0 ||
        memcmp( ev->binding_digest, binding_digest, 32 ) != 0 ||
        memcmp( ev->nonce, nonce, 32 ) != 0)
    {
        e1s18_wipe( evidence_buf, sizeof(evidence_buf) );
        return ERROR_ACCESS_DENIED;
    }

    /* Verify host UID. On the live path the record's host uid is the selected administrator's (never the
     * requester's), so it is only required to be set; the record must also carry the selected account's key
     * and this attempt's server, broker and verifier generations. */
#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
    if (!(boundary && boundary->transport_redeem))
    {
        if (!ev->host_uid || memcmp( ev->account_key, req->account_key, 16 ) ||
            ev->server_gen != s_r8_context.server_gen || ev->broker_gen != s_r8_context.broker_gen ||
            ev->verifier_gen != s_r8_context.verifier_gen)
        {
            e1s18_wipe( evidence_buf, sizeof(evidence_buf) );
            return ERROR_ACCESS_DENIED;
        }
    }
    else
#endif
    if (ev->host_uid != expected_host_uid)
    {
        e1s18_wipe( evidence_buf, sizeof(evidence_buf) );
        return ERROR_ACCESS_DENIED;
    }

    /* Verify expiry. WO-16: the evidence expiry must not extend the
     * admission deadline; the effective expiry is the earlier of the two.
     * A zero deadline (direct test call) retains the old behaviour. */
    {
        uint64_t expiry;
        if (E1S18_DEADLINE( req ) == 0)
            expiry = ev->expiry;
        else
        {
            expiry = E1S18_DEADLINE( req );
            if (ev->expiry != 0 && ev->expiry < expiry)
                expiry = ev->expiry;
        }
        if (expiry != 0 && now != 0 && now >= expiry)
        {
            e1s18_wipe( evidence_buf, sizeof(evidence_buf) );
            return ERROR_ACCESS_DENIED;
        }
    }

    /* Single-use burn verification */
    if (e1s18_is_evidence_burned( evidence_ref ))
    {
        e1s18_wipe( evidence_buf, sizeof(evidence_buf) );
        return ERROR_ACCESS_DENIED;
    }
    e1s18_burn_evidence( evidence_ref );

    /* Populate struct request fields */
#ifdef WEXELEVATE_E1S18
    memset( req->account_name, 0, sizeof(req->account_name) );
    e1s18_safe_copy_str( req->account_name, account_name, sizeof(req->account_name) );
    memcpy( req->account_key, ev->account_key, 16 );
    req->map_generation = ev->map_gen;
    /* The S12 storage generation is no longer produced by Stage 2; the
     * broker asks the server for it via WEX_HKU_STORAGE_QUERY (op 12) in
     * query_storage, after S10 issuance and before LOAD. */
#endif

    memcpy( out_account_key, ev->account_key, 16 );
    memcpy( out_evidence_ref, evidence_ref, 16 );

    e1s18_wipe( evidence_buf, sizeof(evidence_buf) );
    return ERROR_SUCCESS;
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

#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
static DWORD default_r8_receipt( struct request *req, HANDLE context );
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
    HANDLE negotiate_token = NULL;
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
    /* Fail closed if Stage 2 fields are unset. The S12 storage generation is
     * not a Stage 2 field: it is queried from the server (op 12) after S10
     * issuance, so it is not checked here. */
    if (is_zero_key( req->account_key ) || req->map_generation == 0)
        return ERROR_INVALID_DATA;
#endif

    /* Step 3a: NEGOTIATE. The ntdll export rejects a NULL token output with
     * STATUS_INVALID_PARAMETER before reaching the server, so pass a real
     * output slot and deny if the server ever fills it. */
    status = exports->pNtCreateWexAccountToken( WEX_TOKEN_NEGOTIATE, &context, NULL, &caps, NULL, 0, &negotiate_token, NULL, &size );
    if (status != 0 || !context || caps != WEX_TOKEN_CAPS || size != 0)
    {
        if (context && ops && ops->close_handle) ops->close_handle( context );
        return ERROR_ACCESS_DENIED;
    }
    if (negotiate_token)
    {
        if (ops && ops->close_handle) ops->close_handle( negotiate_token );
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
#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
    wex_token_put32( in + 120, s_r8_attempt ? s_r8_attempt : 1 ); /* the redeemed attempt */
#else
    wex_token_put32( in + 120, 1 );                          /* attempt: 1 */
#endif
#ifdef WEXELEVATE_E1S18
    memcpy( in + 124, req->account_key, 16 );               /* account_key from Stage 2 */
    /* The attempt's generations (R8 context, set by prepare); 1 only where a test left it unset. */
    wex_token_put64( in + 140, s_r8_context.root_gen ? s_r8_context.root_gen : 1 );       /* root_gen */
    wex_token_put64( in + 148, s_r8_context.server_gen ? s_r8_context.server_gen : 1 );   /* server_gen */
    wex_token_put64( in + 156, s_r8_context.broker_gen ? s_r8_context.broker_gen : 1 );   /* broker_gen */
    wex_token_put64( in + 164, req->map_generation );        /* map_gen from Stage 2 */
#else
    memcpy( in + 124, account_key, 16 );                     /* account_key */
    wex_token_put64( in + 140, 1 );                          /* root_gen */
    wex_token_put64( in + 148, 1 );                          /* server_gen */
    wex_token_put64( in + 156, 1 );                          /* broker_gen */
    wex_token_put64( in + 164, 1 );                          /* map_gen */
#endif
#ifdef WEXELEVATE_E1S18
    wex_token_put64( in + 172, s_r8_context.policy_gen ? s_r8_context.policy_gen : 1 );   /* policy_gen */
#else
    wex_token_put64( in + 172, 1 );                          /* policy_gen */
#endif
    wex_token_put32( in + 180, req->caller_session );        /* session_id */
    wex_token_put32( in + 184, 0 );                          /* reserved */
    memcpy( in + 188, evidence_ref, 16 );                    /* envelope evidence_ref */

#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
    /* R10: the server issues only for a consumed record that the verifier vouched for. The verifier sent
     * its receipt to the server when it handed the evidence over (before its answer reached this process);
     * this call only names the S10 context, and the server builds the record from that receipt and its own
     * sealed tuple. Nothing the broker holds is installed. One attempt: a refusal burns the context. */
    if (default_r8_receipt( req, context ))
    {
        r8_trace_err( "receipt install", ERROR_ACCESS_DENIED );
        if (context && ops && ops->close_handle) ops->close_handle( context );
        e1s18_wipe( in, sizeof(in) );
        return ERROR_ACCESS_DENIED;
    }
    r8_trace_line( "r8: receipt installed" );
#endif

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

    /* Verify Alice's mapped SID: exactly the fixed 28-byte mapped-SID size the
     * ntdll wrapper enforces (wine/dlls/ntdll/sec.c), and a valid SID on Wine.
     * Native builds check the length only. */
    sid_len = wex_token_u32( out + 72 );
    if (sid_len != 28)
    {
        goto verify_fail;
    }
#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
    if (!IsValidSid( (PSID)(out + 76) ))
        goto verify_fail;
#endif
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
    wex_token_put64( record + 32, req->storage_generation ); /* storage_generation from the op 12 query */
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

    if (status != 0 || !key || managed != 1 || generation == 0)
    {
#ifdef WEXELEVATE_E1S18
        if (ops && ops->close_handle) ops->close_handle( req->s10_context );
        req->s10_context = NULL;
#endif
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

/*
 * Stage 3b: S12 storage generation query (WEX_HKU_STORAGE_QUERY, op 12).
 *
 * Called after S10 issuance and before LOAD. The record is built exactly like
 * the LOAD record in default_load_profile but with the storage generation
 * field (offset 32) zero: op 12 is the only operation where 0 is accepted.
 * Success only when status is 0, key is NULL, managed is 1 and generation is
 * nonzero; then the generation is stored in req->storage_generation for LOAD
 * to compare exactly. Anything else denies. Does not consume the one-shot
 * LOAD attempt.
 */
static DWORD default_query_storage( struct request *req, HANDLE token )
{
    const struct e1s18_export_table *exports = e1s18_get_export_table();
    const struct e1s18_ops *ops = e1s18_get_ops();
    HANDLE context = NULL;
    uint8_t record[WEX_HKU_RECORD_SIZE]; /* 48 bytes */
    HANDLE key = NULL;
    ULONG managed = 0;
    ULONGLONG generation = 0;
    NTSTATUS status;

    if (!exports || !exports->pNtLoadWexProfileEx) return ERROR_NOT_SUPPORTED;
    if (!req || !token) return ERROR_INVALID_PARAMETER;

#ifdef WEXELEVATE_E1S18
    /* Fail closed if Stage 2 fields are unset */
    if (is_zero_key( req->account_key ) || req->map_generation == 0)
        return ERROR_INVALID_DATA;

    context = req->s10_context;
#endif
    if (!context) return ERROR_ACCESS_DENIED;

    /* Build the LOAD record (48 bytes) with the storage generation field
     * (offset 32) zero. */
    memset( record, 0, sizeof(record) );
    wex_token_put32( record + 0, 1 );                    /* version: 1 */
    wex_token_put32( record + 4, WEX_HKU_RECORD_SIZE );   /* size: 48 */
#ifdef WEXELEVATE_E1S18
    memcpy( record + 8, req->account_key, 16 );          /* account_key from Stage 2 */
    wex_token_put64( record + 24, req->map_generation ); /* map_generation from Stage 2 */
    wex_token_put64( record + 32, 0 );                   /* storage_generation: 0 (query) */
#else
    wex_token_put64( record + 24, 1 );                   /* map_generation: 1 */
    wex_token_put64( record + 32, 0 );                   /* storage_generation: 0 (query) */
#endif
    wex_token_put64( record + 40, 0 );                   /* reserved: 0 */

    status = exports->pNtLoadWexProfileEx( WEX_HKU_STORAGE_QUERY, context, token,
                                           0 /* access: 0 */,
                                           record, sizeof(record),
                                           &key, &managed, &generation );

    /* Success only when status is 0, key is NULL, managed is 1 and
     * generation is nonzero. */
    if (status != 0 || key != NULL || managed != 1 || generation == 0)
    {
        if (key && ops && ops->close_handle) ops->close_handle( key );
        return ERROR_ACCESS_DENIED;
    }

#ifdef WEXELEVATE_E1S18
    req->storage_generation = generation;
#endif
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

#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
    /* Live broker (lab): the environment is the issued token's own default block, built by userenv from the
     * managed profile the server published at LOAD (S15); nothing of the caller's environment is used. The
     * working directory stays the request's frozen, validated one. The descriptor-anchored preparation
     * below is a host-side function (file descriptors, Unix paths) and cannot run inside this PE process:
     * its working-directory containment rule is not applied on this path yet (R11 remainder). */
    {
        void *block = NULL;
        const WCHAR *p;
        size_t size;
        char *copy;

        if (!CreateEnvironmentBlock( &block, token, FALSE )) return GetLastError() ? GetLastError() : ERROR_ACCESS_DENIED;
        for (p = block; *p; p += lstrlenW( p ) + 1) ;
        size = (size_t)((const char *)(p + 1) - (const char *)block);
        if (!(copy = malloc( size ))) { DestroyEnvironmentBlock( block ); return ERROR_NOT_ENOUGH_MEMORY; }
        memcpy( copy, block, size );
        DestroyEnvironmentBlock( block );
        if (out_win_env) *out_win_env = copy; else free( copy );
        if (out_win_env_sz) *out_win_env_sz = size;
        (void)params; (void)result; (void)st; (void)cwd_buf; (void)tmp_fd;
        return ERROR_SUCCESS;
    }
#endif

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
    e1s18_safe_copy_str( params.profile.account_name, "alice", sizeof(params.profile.account_name) );
#endif
    e1s18_safe_copy_str( params.profile.sid, "S-1-5-21-999-1001", sizeof(params.profile.sid) );
    e1s18_safe_copy_str( params.profile.windows_path, "C:\\users\\.e1s\\admin", sizeof(params.profile.windows_path) );
    e1s18_safe_copy_str( params.profile.unix_relative_path, "drive_c/users/.e1s/admin", sizeof(params.profile.unix_relative_path) );

    /* Working directory: convert the caller's UTF-16 cwd to UTF-8 exactly.
     * Unpaired surrogates, truncation, or a result that does not fit deny. */
    if (req->cwd)
    {
        if (wex_utf16le_to_utf8( (const uint16_t *)req->cwd, cwd_buf, sizeof(cwd_buf) ) != 0)
            return ERROR_ACCESS_DENIED;
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
    STARTUPINFOW si;
    WCHAR *cmdline = NULL;
    DWORD err = ERROR_SUCCESS;

    (void)win_env_sz;
    (void)cwd;
    if (!out_pi) return ERROR_INVALID_PARAMETER;
    memset( out_pi, 0, sizeof(*out_pi) );
    memset( &si, 0, sizeof(si) );
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = (WORD)req->show;

    cmdline = (WCHAR *)malloc( (lstrlenW( req->cmdline ) + 1) * sizeof(WCHAR) );
    if (!cmdline)
    {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    lstrcpyW( cmdline, req->cmdline );

    /*
     * R13 remediation: do NOT designate Bob (req->caller) as parent process.
     * Designating Bob imports Bob's debug port (debug_obj) and job membership (job)
     * into Alice's child process. The child is created with the broker (SYSTEM)
     * as parent, and caller handle/PID are returned via the broker reply.
     */
    if (!CreateProcessAsUserW( token, req->image, cmdline, NULL, NULL, FALSE,
                               CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_CONSOLE | CREATE_SUSPENDED,
                               (void *)win_env, req->cwd, &si, out_pi ))
    {
        err = GetLastError();
    }

    free( cmdline );
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
    uint8_t record[WEX_HKU_COMMIT_RECORD_SIZE];
    HANDLE context = NULL;

    if (!exports || !exports->pNtLoadWexProfileEx) return ERROR_NOT_SUPPORTED;
    if (!req || !lease_id || !process || !token) return ERROR_INVALID_PARAMETER;
#ifdef WEXELEVATE_E1S18
    context = req->s10_context;
#endif
    if (!context) return ERROR_ACCESS_DENIED;
    /* The server compares this original LOAD context's private lease binding;
     * lease_id is the broker's cache generation, not the S1 128-bit lease ID. */
    wex_token_put32( record, (uint32_t)(uintptr_t)process );
    status = exports->pNtLoadWexProfileEx( WEX_HKU_COMMIT_PROCESS, context, token, 0,
                                          record, sizeof(record), &unused, &managed, &generation );
    if (status != 0 || unused || managed != 1 || generation)
    {
        const struct e1s18_ops *ops = e1s18_get_ops();
        if (unused && ops && ops->close_handle) ops->close_handle( unused );
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

static BOOL default_terminate_child( HANDLE process )
{
#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
    if (process)
    {
        /* TerminateProcess failure alone does not decide: the process may
         * have already exited. Death is confirmed only by the wait. */
        TerminateProcess( process, 1 );
        return WaitForSingleObject( process, 5000 ) == WAIT_OBJECT_0;
    }
    return FALSE;
#else
    (void)process;
    return FALSE;
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

/* WO-25: default R8 ops. The Win32 implementations use the S6 transport and
 * the extended caller-binding query; the native stubs fail closed. */
static DWORD default_r8_open( struct request *req, unsigned char reg[E1S6_OPEN_SIZE], UINT64 *channel )
{
#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
    e1s18_pfn_WexE1s6Transport transport;
    DWORD err;
    if (!req || !req->pipe || !reg || !channel) return ERROR_INVALID_PARAMETER;
    transport = e1s18_get_s6_transport();
    if (!transport) return ERROR_NOT_SUPPORTED;
    *channel = 0;
    err = transport( req->pipe, E1S6_OPEN, reg, E1S6_OPEN_SIZE, channel );
    if (err || !*channel) { e1s18_wipe( reg, E1S6_OPEN_SIZE ); return err ? err : ERROR_ACCESS_DENIED; }
    return ERROR_SUCCESS;
#else
    (void)req; (void)reg; (void)channel;
    return ERROR_NOT_SUPPORTED;
#endif
}

static DWORD default_r8_owner( struct request *req, UINT64 channel, char owner[256] )
{
#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
    e1s18_pfn_WexE1s6Transport transport;
    DWORD err;
    if (!req || !req->pipe || !channel || !owner) return ERROR_INVALID_PARAMETER;
    transport = e1s18_get_s6_transport();
    if (!transport) return ERROR_NOT_SUPPORTED;
    memset( owner, 0, 256 );
    err = transport( req->pipe, E1S6_OWNER_QUERY, owner, 256, &channel );
    if (err) return err;
    return ERROR_SUCCESS;
#else
    (void)req; (void)channel; (void)owner;
    return ERROR_NOT_SUPPORTED;
#endif
}

static DWORD default_r8_binding( struct request *req, unsigned char out[132] )
{
#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
    e1s18_pfn_QueryWexPipeCallerBindingEx query;
    e1s18_pipe_caller_binding_ext ext;
    unsigned char payload[8];
    unsigned char out_buf[132];
    ULONG out_len = sizeof(out_buf);
    NTSTATUS status;
    if (!req || !req->pipe || !out) return ERROR_INVALID_PARAMETER;
    query = e1s18_get_binding_query();
    if (!query) return ERROR_NOT_SUPPORTED;
    memset( payload, 0, sizeof(payload) );
    memcpy( payload, "E1R8", 4 );
    payload[4] = 1; payload[5] = 0; /* version 1 */
    payload[6] = 1; payload[7] = 0; /* op 1 (BINDING) */
    memset( out_buf, 0, sizeof(out_buf) );
    memset( &ext, 0, sizeof(ext) );
    ext.binding.Size = sizeof(ext);
    ext.input = payload;
    ext.in_size = sizeof(payload);
    ext.output = out_buf;
    ext.out_size = &out_len;
    status = query( req->pipe, &ext.binding );
    if (status) return ERROR_ACCESS_DENIED;
    if (out_len != 132) { e1s18_close_binding_handles( &ext.binding ); return ERROR_ACCESS_DENIED; }
    memcpy( out, out_buf, 132 );
    e1s18_close_binding_handles( &ext.binding );
    return ERROR_SUCCESS;
#else
    (void)req; (void)out;
    return ERROR_NOT_SUPPORTED;
#endif
}

static DWORD default_r8_endorse( struct request *req, HANDLE dialog, const unsigned char tuple[204],
                                 unsigned char dialog_id[16] )
{
#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
    e1s18_pfn_QueryWexPipeCallerBindingEx query;
    e1s18_pipe_caller_binding_ext ext;
    unsigned char payload[216];
    unsigned char out_buf[16];
    ULONG out_len = sizeof(out_buf);
    NTSTATUS status;
    if (!req || !req->pipe || !dialog || !tuple || !dialog_id) return ERROR_INVALID_PARAMETER;
    query = e1s18_get_binding_query();
    if (!query) return ERROR_NOT_SUPPORTED;
    memset( payload, 0, sizeof(payload) );
    memcpy( payload, "E1R8", 4 );
    payload[4] = 1; payload[5] = 0; /* version 1 */
    payload[6] = 2; payload[7] = 0; /* op 2 (ENDORSE_DIALOG) */
    e1s6_put32( payload + 8, (unsigned int)(uintptr_t)dialog );
    memcpy( payload + 12, tuple, 204 );
    memset( out_buf, 0, sizeof(out_buf) );
    memset( &ext, 0, sizeof(ext) );
    ext.binding.Size = sizeof(ext);
    ext.input = payload;
    ext.in_size = sizeof(payload);
    ext.output = out_buf;
    ext.out_size = &out_len;
    status = query( req->pipe, &ext.binding );
    if (status) return ERROR_ACCESS_DENIED;
    if (out_len != 16 || e1s18_is_all_zero( out_buf, 16 ))
    { e1s18_close_binding_handles( &ext.binding ); return ERROR_ACCESS_DENIED; }
    memcpy( dialog_id, out_buf, 16 );
    e1s18_close_binding_handles( &ext.binding );
    return ERROR_SUCCESS;
#else
    (void)req; (void)dialog; (void)tuple; (void)dialog_id;
    return ERROR_NOT_SUPPORTED;
#endif
}

static DWORD default_r8_recheck( struct request *req, const unsigned char dialog_id[16] )
{
#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
    e1s18_pfn_QueryWexPipeCallerBindingEx query;
    e1s18_pipe_caller_binding_ext ext;
    unsigned char payload[24];
    NTSTATUS status;
    if (!req || !req->pipe || !dialog_id) return ERROR_INVALID_PARAMETER;
    query = e1s18_get_binding_query();
    if (!query) return ERROR_NOT_SUPPORTED;
    memset( payload, 0, sizeof(payload) );
    memcpy( payload, "E1R8", 4 );
    payload[4] = 1; payload[5] = 0; /* version 1 */
    payload[6] = 3; payload[7] = 0; /* op 3 (RECHECK) */
    memcpy( payload + 8, dialog_id, 16 );
    memset( &ext, 0, sizeof(ext) );
    ext.binding.Size = sizeof(ext);
    ext.input = payload;
    ext.in_size = sizeof(payload);
    /* No reply data for RECHECK. */
    status = query( req->pipe, &ext.binding );
    if (status) return ERROR_ACCESS_DENIED;
    e1s18_close_binding_handles( &ext.binding );
    return ERROR_SUCCESS;
#else
    (void)req; (void)dialog_id;
    return ERROR_NOT_SUPPORTED;
#endif
}

#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
/* R10: E1R8 server op 4 on the request's pipe: install the verifier's receipt into this S10 context. */
static DWORD default_r8_receipt( struct request *req, HANDLE context )
{
    e1s18_pfn_QueryWexPipeCallerBindingEx query;
    e1s18_pipe_caller_binding_ext ext;
    unsigned char payload[12];
    NTSTATUS status;
    if (!req || !req->pipe || !context) return ERROR_INVALID_PARAMETER;
    query = e1s18_get_binding_query();
    if (!query) return ERROR_NOT_SUPPORTED;
    memset( payload, 0, sizeof(payload) );
    memcpy( payload, "E1R8", 4 );
    payload[4] = 1; payload[5] = 0; /* version 1 */
    payload[6] = 4; payload[7] = 0; /* op 4 (INSTALL_RECEIPT) */
    e1s6_put32( payload + 8, (unsigned int)(uintptr_t)context );
    memset( &ext, 0, sizeof(ext) );
    ext.binding.Size = sizeof(ext);
    ext.input = payload;
    ext.in_size = sizeof(payload);
    status = query( req->pipe, &ext.binding );
    if (status) return ERROR_ACCESS_DENIED;
    e1s18_close_binding_handles( &ext.binding );
    return ERROR_SUCCESS;
}
#endif

static DWORD default_r8_close( struct request *req, UINT64 channel )
{
#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
    e1s18_pfn_WexE1s6Transport transport;
    if (!req || !req->pipe || !channel) return ERROR_INVALID_PARAMETER;
    transport = e1s18_get_s6_transport();
    if (!transport) return ERROR_NOT_SUPPORTED;
    return transport( req->pipe, E1S6_CLOSE, NULL, 0, &channel );
#else
    (void)req; (void)channel;
    return ERROR_NOT_SUPPORTED;
#endif
}

static DWORD default_r8_start( struct request *req, UINT64 channel, const unsigned char *start_pkt, size_t start_len,
                               unsigned char ready_pkt[76] )
{
#if defined(_WIN32) && defined(WEXELEVATE_E1S18)
    e1s18_pfn_WexE1s6Transport transport;
    unsigned char buf[160];
    DWORD err;
    if (!req || !req->pipe || !channel || !start_pkt || start_len < 97 || start_len > 159 || !ready_pkt)
        return ERROR_INVALID_PARAMETER;
    transport = e1s18_get_s6_transport();
    if (!transport) return ERROR_NOT_SUPPORTED;
    memset( buf, 0, sizeof(buf) );
    memcpy( buf, start_pkt, start_len );
    err = transport( req->pipe, E1S6_R8_START, buf, (DWORD)start_len, &channel );
    if (err) return err;
    memcpy( ready_pkt, buf, 76 );
    return ERROR_SUCCESS;
#else
    (void)req; (void)channel; (void)start_pkt; (void)start_len; (void)ready_pkt;
    return ERROR_NOT_SUPPORTED;
#endif
}

static struct e1s18_ops s_default_ops = {
    default_get_capabilities,
    default_is_policy_enabled,
    default_prompt_dialog,
    default_redeem_evidence,
    default_issue_token,
    default_load_profile,
    default_query_storage,
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
    default_free_buffer,
    default_r8_open,
    default_r8_owner,
    default_r8_binding,
    default_r8_endorse,
    default_r8_recheck,
    default_r8_close,
    default_r8_start
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

/* S1 contract: token/auth LUIDs encode low 32 bits then high 32 bits as a u64. */
static uint64_t e1s18_luid_to_u64( const LUID *luid )
{
    return ((uint64_t)(uint32_t)luid->HighPart << 32) | (uint64_t)(uint32_t)luid->LowPart;
}

/*
 * WO-14: Canonical digest calculation for the standard-user branch.
 *
 * The S1 caller-binding record (kind 1) and the frozen request record are
 * built through the shared encoder (e1s_encode.h) and hashed with the existing
 * sha256_* and the S1 domain tags ("E1S1-BIND\0", "E1S1-REQ\0", "E1S1-ENV\0").
 * The endorsed identities/generations come from the S1 context (zero in
 * production); the caller token/auth/session/SID and the image identity come
 * from the request. The nonce is not part of these records.
 */
static void e1s18_compute_digests( const struct request *req,
                                   const uint8_t nonce[32],
                                   uint8_t out_binding_digest[32],
                                   uint8_t out_request_digest[32],
                                   uint8_t out_env_digest[32] )
{
    const struct e1s18_s1_context *c = &s_s1_context;
    struct sha256_ctx ctx;
    uint32_t env_policy = 1; /* 1 = fresh managed environment */
    uint8_t *buf;
    struct e1s_bytes b;

    (void)nonce; /* the nonce is not part of the S1 binding/request records */

    buf = malloc( E1S_ENCODE_MAX_RECORD );
    if (!buf)
    {
        memset( out_binding_digest, 0, 32 );
        memset( out_request_digest, 0, 32 );
        memset( out_env_digest, 0, 32 );
        return;
    }

    /* 1. Caller-binding record (kind 1): SHA256("E1S1-BIND\0" || record) */
    b.data = buf; b.length = 0; b.capacity = E1S_ENCODE_MAX_RECORD; b.failed = 0;
    e1s_encode_binding( &b,
        c->server_gen, c->root_gen, c->broker_gen,
        c->prefix, c->host_uid,
        c->host_process, c->opener, c->writer,
        e1s18_luid_to_u64( &req->caller_stats.TokenId ),
        e1s18_luid_to_u64( &req->caller_stats.ModifiedId ),
        e1s18_luid_to_u64( &req->caller_stats.AuthenticationId ),
        req->caller_session,
        req->caller_sid, req->caller_sid_len,
        c->desktop );
    sha256_init( &ctx );
    sha256_update( &ctx, "E1S1-BIND\0", 10 );
    sha256_update( &ctx, buf, b.length );
    sha256_final( &ctx, out_binding_digest );

    /* 2. Frozen request record: SHA256("E1S1-REQ\0" || record) */
    b.data = buf; b.length = 0; b.capacity = E1S_ENCODE_MAX_RECORD; b.failed = 0;
    e1s_encode_request( &b,
        req->id, out_binding_digest,
        c->volume_serial, c->file_index, c->file_size, c->last_write,
        req->hash,
        (const uint16_t *)req->image, (const uint16_t *)req->cmdline, (const uint16_t *)req->cwd,
        env_policy, req->show, req->flags, c->deadline );
    sha256_init( &ctx );
    sha256_update( &ctx, "E1S1-REQ\0", 9 );
    sha256_update( &ctx, buf, b.length );
    sha256_final( &ctx, out_request_digest );

    /* 3. Environment Digest: SHA256("E1S1-ENV\0" || env_policy) */
    sha256_init( &ctx );
    sha256_update( &ctx, "E1S1-ENV\0", 9 );
    sha256_update( &ctx, &env_policy, sizeof(env_policy) );
    sha256_final( &ctx, out_env_digest );

    free( buf );
}

/* Launch preparation hands over the Unix environment; free it on every exit. */
static void e1s18_free_unix_env( char **env, size_t count )
{
    size_t i;
    if (!env) return;
    for (i = 0; i < count; i++) free( env[i] );
    free( env );
}

/*
 * WO-15: Broker-wide "standard request in flight" slot.
 * The S6 transport in wexhost is process-global, so two standard-user
 * requests must never overlap. The slot is taken at the start of
 * e1s18_handle_standard_request (before the prompt queue) and released on
 * every exit; a second request while it is taken returns ERROR_BUSY at once
 * (no waiting, no dialog, no transport call).
 */
#if defined(_WIN32)
static volatile LONG s_standard_in_flight = 0;

static BOOL e1s18_standard_slot_try_take( void )
{
    return InterlockedCompareExchange( &s_standard_in_flight, 1, 0 ) == 0;
}

static void e1s18_standard_slot_release( void )
{
    InterlockedExchange( &s_standard_in_flight, 0 );
}
#else
static volatile int s_standard_in_flight = 0;

static BOOL e1s18_standard_slot_try_take( void )
{
    return __sync_bool_compare_and_swap( &s_standard_in_flight, 0, 1 );
}

static void e1s18_standard_slot_release( void )
{
    __sync_lock_release( &s_standard_in_flight );
}
#endif

/*
 * Main standard-user request sequencing function.
 * Coordinates all steps and rolls back completely on any intermediate failure.
 * Runs with the WO-15 standard-request slot already taken (see the wrapper
 * e1s18_handle_standard_request below).
 */
static DWORD e1s18_handle_standard_request_locked( struct request *req, struct wexelevate_reply *reply )
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
    BOOL child_unconfirmed = FALSE;
#ifdef WEXELEVATE_E1S18
    BOOL committed = FALSE;
#endif
    const char *stage = "admission";

    memset( &pi, 0, sizeof(pi) );
    memset( account_name, 0, sizeof(account_name) );
    memset( account_key, 0, sizeof(account_key) );
    memset( evidence_ref, 0, sizeof(evidence_ref) );

    if (!ops) return ERROR_NOT_SUPPORTED;

    /* WO-16: set the absolute deadline once at admission (inside the WO-15
     * slot, before the prompt queue). The 120 s prompt deadline and the
     * evidence expiry checks use this same value and must not extend it.
     * Clock failure denies. */
    {
        uint64_t now = e1s18_get_monotonic_time();
        if (now == 0)
            return ERROR_INVALID_DATA;
#ifdef WEXELEVATE_E1S18
        req->deadline = now + 120000000000ULL; /* 120s */
#endif
    }

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

    /* R4: the attempt nonce is 32 random bytes. RNG failure or an all-zero
     * nonce denies the request before any dialog starts. */
    if (!e1s18_fill_random( nonce, 32 ))
        return ERROR_ACCESS_DENIED;
    if (e1s18_is_all_zero( nonce, 32 ))
        return ERROR_ACCESS_DENIED;

#ifdef WEXELEVATE_E1S18
    /* WO-27: run prepare before the S1 gate, populating live context and opening S6 channel. */
    if (s_r8_channel) return ERROR_BUSY;
    if ((err = e1s18_r8_prepare( req ))) goto fail;
#endif

    /* WO-14: the canonical S1 binding/request records need endorsed
     * identities and generations. Any zero required input denies the request
     * with ERROR_NOT_SUPPORTED before a dialog starts (zero in production). */
    if (!e1s18_s1_context_complete( &s_s1_context ))
        return ERROR_NOT_SUPPORTED;

    e1s18_compute_digests( req, nonce, binding_digest, request_digest, env_digest );

    /* Step 1: Serialized prompt queue and bound credential dialog (S17) */
    stage = "prompt_dialog";
    err = ops->prompt_dialog( req, nonce, binding_digest, request_digest, account_name, sizeof(account_name) );
    if (err) goto fail;

    /* Step 2: Single-use evidence verification and redemption (S8/S7) */
    stage = "redeem_evidence";
    err = ops->redeem_evidence( req, account_name, nonce, binding_digest, request_digest, account_key, evidence_ref );
    if (err) goto fail;

#ifdef WEXELEVATE_E1S18
    /* Step 3: Server-issued account token (S10) */
    stage = "issue_token";
    err = ops->issue_token( req, req->account_key, evidence_ref, nonce, binding_digest, request_digest, &issued_token );
    if (err) goto fail;

    /* Step 3b: S12 storage generation query (WEX_HKU_STORAGE_QUERY, op 12) */
    stage = "query_storage";
    err = ops->query_storage( req, issued_token );
    if (err) goto fail;

    /* Step 4: Profile load & lease acquisition (S13/S14) */
    stage = "load_profile";
    err = ops->load_profile( req, issued_token, req->account_key, &lease_id, &hkey );
    if (err) goto fail;
#else
    /* Step 3: Server-issued account token (S10) */
    stage = "issue_token";
    err = ops->issue_token( req, account_key, evidence_ref, nonce, binding_digest, request_digest, &issued_token );
    if (err) goto fail;

    /* Step 3b: S12 storage generation query (WEX_HKU_STORAGE_QUERY, op 12) */
    stage = "query_storage";
    err = ops->query_storage( req, issued_token );
    if (err) goto fail;

    /* Step 4: Profile load & lease acquisition (S13/S14) */
    stage = "load_profile";
    err = ops->load_profile( req, issued_token, account_key, &lease_id, &hkey );
    if (err) goto fail;
#endif

    /* Step 5: Launch preparation (S15) */
    stage = "prep_launch";
    err = ops->prep_launch( req, issued_token, &win_env, &win_env_sz, &unix_env, &unix_env_cnt, &cwd );
    if (err) goto fail;

    /* Step 6: Image re-verification before creation */
    stage = "verify_image";
    err = ops->verify_image( req );
    if (err) goto fail;

    /* Step 7: Suspended child creation in caller's server and session */
    stage = "create_process_suspended";
    err = ops->create_process_suspended( req, issued_token, win_env, win_env_sz, cwd, &pi );
    if (err) goto fail;

    /* Step 8: Verify installed child identity & session */
    stage = "verify_child";
    err = ops->verify_child( req, issued_token, &pi );
    if (err) goto fail;

    /* Step 9: Arm image read lease on child PID */
    if (req->lease)
    {
        stage = "arm_lease";
        err = ops->arm_lease( req->lease, pi.dwProcessId );
        if (err) goto fail;
#if defined(_WIN32)
        /* R6 remediation: keep image protection until TARGET process exits by transferring ownership to guard thread */
        {
            HANDLE guard_proc = NULL;
            if (DuplicateHandle( GetCurrentProcess(), pi.hProcess, GetCurrentProcess(), &guard_proc,
                                 SYNCHRONIZE | PROCESS_TERMINATE, FALSE, 0 ))
            {
                if (!start_guard( req, guard_proc ))
                {
                    CloseHandle( guard_proc );
                    err = ERROR_NOT_ENOUGH_MEMORY;
                    goto fail;
                }
            }
            else
            {
                err = GetLastError();
                goto fail;
            }
        }
#endif
    }

    /* Step 10: Duplicate limited handle to caller */
    if (req->flags & WEXELEVATE_WANT_HANDLE)
    {
        stage = "duplicate_handle";
        err = ops->duplicate_handle( req, pi.hProcess, &remote_handle );
        if (err) goto fail;
    }

    /* Step 11: Transfer profile lease to process tracking */
    stage = "transfer_lease";
    err = ops->transfer_lease( req, lease_id, pi.hProcess, issued_token );
    if (err) goto fail;

    /* Step 12: Pre-resume binding checks */
    stage = "check_pre_resume";
    err = ops->check_pre_resume( req );
    if (err) goto fail;

    /* Step 13: Guarded resume (broker resumes child once after all checks pass) */
    stage = "resume_thread";
    err = ops->resume_thread( req, pi.hThread );
    if (err) goto fail;

#ifdef WEXELEVATE_E1S18
    committed = TRUE;
    r8_trace_line( "r8: launched" );
    e1r9_agent_report( req, E1R8_LAUNCHED_COMMITTED, ERROR_SUCCESS );
#endif

    /* Commit */
    if (reply)
    {
        reply->pid = pi.dwProcessId;
        reply->tid = pi.dwThreadId;
        reply->handle = (ULONGLONG)(uintptr_t)remote_handle;
    }

    /* Clean up broker local process/thread handles and temporary buffers */
    if (issued_token) ops->close_handle( issued_token );
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
    e1s18_free_unix_env( unix_env, unix_env_cnt );
    e1s18_wipe( nonce, sizeof(nonce) );
    e1s18_wipe( account_name, sizeof(account_name) );
    e1s18_wipe( account_key, sizeof(account_key) );
    e1s18_wipe( evidence_ref, sizeof(evidence_ref) );
#ifdef WEXELEVATE_E1S18
    e1s18_wipe( req->account_name, sizeof(req->account_name) );
    e1s18_wipe( req->account_key, sizeof(req->account_key) );
#endif
    return ERROR_SUCCESS;

fail:
    /* Explicit cleanup / rollback at any failure stage */
#ifdef WEXELEVATE_E1S18
    r8_trace_err( stage, (unsigned long)err );
    e1r9_agent_report( req, committed ? E1R8_LAUNCHED_FAILED_AFTER : E1R8_LAUNCHED_FAILED_BEFORE, err );
#else
    (void)stage;
#endif
    if (remote_handle)
    {
        ops->close_remote_handle( req->caller, remote_handle );
        remote_handle = NULL;
    }
    if (pi.hProcess)
    {
        if (ops->terminate_child( pi.hProcess ))
        {
            if (pi.hThread) ops->close_handle( pi.hThread );
            ops->close_handle( pi.hProcess );
            pi.hProcess = NULL;
            pi.hThread = NULL;
        }
        else
        {
            /* ponytail: termination could not be confirmed; the child may still
             * be alive. Keep the process and thread references for a
             * supervisor-owned reaper (R7 design follow-up) and skip the
             * profile unload below: the child may still use the hive. */
            child_unconfirmed = TRUE;
        }
    }
    if (!child_unconfirmed && (lease_id || hkey))
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
    e1s18_free_unix_env( unix_env, unix_env_cnt );
    unix_env = NULL;
    e1s18_wipe( nonce, sizeof(nonce) );
    e1s18_wipe( account_name, sizeof(account_name) );
    e1s18_wipe( account_key, sizeof(account_key) );
    e1s18_wipe( evidence_ref, sizeof(evidence_ref) );
#ifdef WEXELEVATE_E1S18
    e1s18_wipe( req->account_name, sizeof(req->account_name) );
    e1s18_wipe( req->account_key, sizeof(req->account_key) );
#endif

    if (reply)
    {
        reply->pid = 0;
        reply->tid = 0;
        reply->handle = 0;
    }
    if (child_unconfirmed)
        return ERROR_CHILD_NOT_COMPLETE;
    return err ? err : ERROR_ACCESS_DENIED;
}

/*
 * WO-15: Public entry point. Takes the broker-wide standard-request slot
 * before the prompt queue and releases it on every exit, so a second
 * standard-user request while one is in flight returns ERROR_BUSY at once.
 */
DWORD e1s18_handle_standard_request( struct request *req, struct wexelevate_reply *reply )
{
    const struct e1s18_ops *ops = e1s18_get_ops();
    DWORD result;

    if (!ops) return ERROR_NOT_SUPPORTED;
    if (!e1s18_standard_slot_try_take())
        return ERROR_BUSY;
    result = e1s18_handle_standard_request_locked( req, reply );
#ifdef WEXELEVATE_E1S18
    e1s18_r8_release( req );
#endif
    e1s18_standard_slot_release();
    return result;
}
