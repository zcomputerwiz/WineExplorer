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

#ifndef WEXPREFETCHD_SHA256_H
#define WEXPREFETCHD_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  data[64];
    uint32_t datalen;
} sha256_ctx;

void sha256_init( sha256_ctx *ctx );
void sha256_update( sha256_ctx *ctx, const void *data, size_t len );
void sha256_final( sha256_ctx *ctx, uint8_t out[32] );
void sha256_hex( const uint8_t digest[32], char out[65] );

#endif /* WEXPREFETCHD_SHA256_H */
