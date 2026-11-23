/*
 * WO-14 / design review, finding 3: the one S1 digest path. SHA-256 of a domain tag and an encoded record (e1s_encode.h),
 * checked at every step: an encoder failure, a missing buffer or a hash failure is a refusal, never a zero or partial
 * digest. The S5 broker (e1s.h) and the standard-user path (standard.c) both hash through here. In PE builds the hash
 * is the system's (BCrypt); the portable SHA-256 below exists only for the native unit tests, which have no BCrypt.
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef WEXELEVATE_E1S_DIGEST_H
#define WEXELEVATE_E1S_DIGEST_H

#include <stdint.h>
#include <string.h>
#include "e1s_encode.h"

#ifdef _WIN32

#include <windows.h>
#include <bcrypt.h>
#ifndef NT_SUCCESS
#define NT_SUCCESS(s) ((LONG)(s) >= 0)
#endif

/* SHA-256 of tag || data. FALSE on any failure (deny). */
static inline int e1s_sha256( const char *tag, uint32_t tag_len, const uint8_t *data, uint32_t len, uint8_t out[32] )
{
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    int ok = 0;
    if (NT_SUCCESS(BCryptOpenAlgorithmProvider( &alg, BCRYPT_SHA256_ALGORITHM, NULL, 0 )) &&
        NT_SUCCESS(BCryptCreateHash( alg, &hash, NULL, 0, NULL, 0, 0 )) &&
        NT_SUCCESS(BCryptHashData( hash, (UCHAR *)tag, tag_len, 0 )) &&
        NT_SUCCESS(BCryptHashData( hash, (UCHAR *)data, len, 0 )) &&
        NT_SUCCESS(BCryptFinishHash( hash, out, 32, 0 ))) ok = 1;
    if (hash) BCryptDestroyHash( hash );
    if (alg) BCryptCloseAlgorithmProvider( alg, 0 );
    if (!ok) memset( out, 0, 32 );
    return ok;
}

#else /* native unit tests */

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

static inline void sha256_transform( uint32_t state[8], const uint8_t data[64] )
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

static inline void sha256_init( struct sha256_ctx *ctx )
{
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
}

static inline void sha256_update( struct sha256_ctx *ctx, const void *data, size_t len )
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

static inline void sha256_final( struct sha256_ctx *ctx, uint8_t out[32] )
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

/* SHA-256 of tag || data. FALSE on any failure (deny). */
static inline int e1s_sha256( const char *tag, uint32_t tag_len, const uint8_t *data, uint32_t len, uint8_t out[32] )
{
    struct sha256_ctx ctx;
    if (!tag || (!data && len)) { memset( out, 0, 32 ); return 0; }
    sha256_init( &ctx );
    sha256_update( &ctx, tag, tag_len );
    sha256_update( &ctx, data, len );
    sha256_final( &ctx, out );
    return 1;
}

#endif /* _WIN32 */

/* SHA-256 of tag || the encoded record; FALSE (out zeroed) when the record could not be encoded or hashed. */
static inline int e1s_digest_record( const char *tag, uint32_t tag_len, const struct e1s_bytes *b, uint8_t out[32] )
{
    if (!b || !b->data || b->failed || !b->length)
    {
        memset( out, 0, 32 );
        return 0;
    }
    return e1s_sha256( tag, tag_len, b->data, b->length, out );
}

/* The environment digest: SHA-256 of "E1S1-ENV" and the policy as a little-endian 32-bit value. */
static inline int e1s_env_digest( uint32_t policy, uint8_t out[32] )
{
    uint8_t bytes[4];
    bytes[0] = (uint8_t)policy; bytes[1] = (uint8_t)(policy >> 8);
    bytes[2] = (uint8_t)(policy >> 16); bytes[3] = (uint8_t)(policy >> 24);
    return e1s_sha256( "E1S1-ENV", sizeof("E1S1-ENV"), bytes, sizeof(bytes), out );
}

#endif /* WEXELEVATE_E1S_DIGEST_H */
