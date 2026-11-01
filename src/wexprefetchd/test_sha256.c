/*
 * NIST test vectors for the SHA-256 implementation.
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

#include <stdio.h>
#include <string.h>

#include "sha256.h"

static int check( const char *name, const char *expected, const uint8_t *digest )
{
    char hex[65];
    int ok;

    sha256_hex( digest, hex );
    ok = (strcmp( hex, expected ) == 0);
    if (ok)
        printf( "PASS %s\n", name );
    else
        printf( "FAIL %s (expected %s, got %s)\n", name, expected, hex );
    return ok;
}

int main( void )
{
    sha256_ctx ctx;
    uint8_t digest[32];
    static char buf[1000000];
    size_t i;
    int ok = 1;

    for (i = 0; i < sizeof(buf); i++)
        buf[i] = 'a';

    /* Empty string. */
    sha256_init( &ctx );
    sha256_final( &ctx, digest );
    ok &= check( "empty string",
                 "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                 digest );

    /* "abc". */
    sha256_init( &ctx );
    sha256_update( &ctx, "abc", 3 );
    sha256_final( &ctx, digest );
    ok &= check( "'abc'",
                 "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                 digest );

    /* 56-byte two-block vector. */
    {
        const char *s = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        sha256_init( &ctx );
        sha256_update( &ctx, s, strlen( s ) );
        sha256_final( &ctx, digest );
        ok &= check( "56-byte two-block",
                     "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
                     digest );
    }

    /* One million 'a', fed in chunks of 1000. */
    sha256_init( &ctx );
    for (i = 0; i < 1000000; i += 1000)
        sha256_update( &ctx, buf + i, 1000 );
    sha256_final( &ctx, digest );
    ok &= check( "1M 'a' in 1000-byte chunks",
                 "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
                 digest );

    /* One million 'a', fed in odd chunk sizes (7, 63, 64, 65, 4096 repeating). */
    {
        static const size_t chunks[] = { 7, 63, 64, 65, 4096 };
        size_t off = 0;
        size_t ci = 0;
        sha256_init( &ctx );
        while (off < 1000000)
        {
            size_t n = chunks[ci % 5];
            if (off + n > 1000000)
                n = 1000000 - off;
            sha256_update( &ctx, buf + off, n );
            off += n;
            ci++;
        }
        sha256_final( &ctx, digest );
        ok &= check( "1M 'a' in odd chunks",
                     "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
                     digest );
    }

    return ok ? 0 : 1;
}
