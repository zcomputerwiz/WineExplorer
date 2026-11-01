/*
 * Streaming SHA-256 (FIPS 180-4), clean-room implementation.
 *
 * Copyright 2026 WineExplorer contributors
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "sha256.h"

#include <string.h>

/* First 32 bits of the fractional parts of the cube roots of the first 64 primes (FIPS 180-4, 4.2.2). */
static const uint32_t K[64] =
{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static inline uint32_t rotr( uint32_t x, unsigned n )
{
    return (x >> n) | (x << (32 - n));
}

static void sha256_transform( sha256_ctx *ctx, const uint8_t block[64] )
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    int t;

    for (t = 0; t < 16; t++)
        w[t] = ((uint32_t)block[t * 4] << 24) |
               ((uint32_t)block[t * 4 + 1] << 16) |
               ((uint32_t)block[t * 4 + 2] << 8) |
               (uint32_t)block[t * 4 + 3];

    for (t = 16; t < 64; t++)
    {
        uint32_t sig0 = rotr( w[t - 15], 7 ) ^ rotr( w[t - 15], 18 ) ^ (w[t - 15] >> 3);
        uint32_t sig1 = rotr( w[t - 2], 17 ) ^ rotr( w[t - 2], 19 ) ^ (w[t - 2] >> 10);
        w[t] = w[t - 16] + sig0 + w[t - 7] + sig1;
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (t = 0; t < 64; t++)
    {
        uint32_t SIG1 = rotr( e, 6 ) ^ rotr( e, 11 ) ^ rotr( e, 25 );
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = h + SIG1 + ch + K[t] + w[t];
        uint32_t SIG0 = rotr( a, 2 ) ^ rotr( a, 13 ) ^ rotr( a, 22 );
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = SIG0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void sha256_init( sha256_ctx *ctx )
{
    /* First 32 bits of the fractional parts of the square roots of the first 8 primes (FIPS 180-4, 5.3.3). */
    ctx->state[0] = 0x6a09e667u;
    ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u;
    ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu;
    ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu;
    ctx->state[7] = 0x5be0cd19u;
    ctx->bitlen = 0;
    ctx->datalen = 0;
}

void sha256_update( sha256_ctx *ctx, const void *data, size_t len )
{
    const uint8_t *p = data;

    ctx->bitlen += (uint64_t)len * 8;

    /* Top up the partial block, if any. */
    if (ctx->datalen)
    {
        size_t n = 64 - ctx->datalen;
        if (n > len) n = len;
        memcpy( ctx->data + ctx->datalen, p, n );
        ctx->datalen += n;
        p += n;
        len -= n;
        if (ctx->datalen == 64)
        {
            sha256_transform( ctx, ctx->data );
            ctx->datalen = 0;
        }
    }

    /* Process whole 64-byte blocks directly from the input buffer. */
    while (len >= 64)
    {
        sha256_transform( ctx, p );
        p += 64;
        len -= 64;
    }

    /* Buffer the remaining tail. */
    if (len)
    {
        memcpy( ctx->data, p, len );
        ctx->datalen = len;
    }
}

void sha256_final( sha256_ctx *ctx, uint8_t out[32] )
{
    uint64_t bitlen = ctx->bitlen;
    uint8_t lenbuf[8];
    int i;

    /* Message length in bits, big-endian. */
    for (i = 0; i < 8; i++)
        lenbuf[i] = (uint8_t)(bitlen >> (56 - 8 * i));

    /* Append the 0x80 byte, then zero padding so the padded message is a multiple of 64 bytes with room
     * for the 8 length bytes. */
    ctx->data[ctx->datalen++] = 0x80;
    if (ctx->datalen > 56)
    {
        while (ctx->datalen < 64)
            ctx->data[ctx->datalen++] = 0;
        sha256_transform( ctx, ctx->data );
        ctx->datalen = 0;
    }
    while (ctx->datalen < 56)
        ctx->data[ctx->datalen++] = 0;
    memcpy( ctx->data + 56, lenbuf, 8 );
    sha256_transform( ctx, ctx->data );

    for (i = 0; i < 8; i++)
    {
        out[i * 4]     = (uint8_t)(ctx->state[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        out[i * 4 + 3] = (uint8_t)ctx->state[i];
    }
}

static const char hexdigits[] = "0123456789abcdef";

void sha256_hex( const uint8_t digest[32], char out[65] )
{
    int i;
    for (i = 0; i < 32; i++)
    {
        out[i * 2]     = hexdigits[digest[i] >> 4];
        out[i * 2 + 1] = hexdigits[digest[i] & 0x0f];
    }
    out[64] = '\0';
}
