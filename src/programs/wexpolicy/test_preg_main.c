/*
 * Native test of the Registry.pol reader and writer (preg.h)
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include "preg.h"

static int failures, checks;
#define CHECK(cond, name) do { checks++; if (!(cond)) { failures++; printf( "FAIL: %s (line %d)\n", name, __LINE__ ); } } while (0)

/* ASCII to UTF-16LE without the NUL; the number of characters */
static uint32_t wide( uint8_t *out, const char *s )
{
    uint32_t n = 0;
    for (; *s; s++, n++) preg_put_u16( out + 2 * n, (uint16_t)(unsigned char)*s );
    return n;
}

static size_t add( uint8_t *buf, const char *key, const char *value, uint32_t type, const void *data, uint32_t size )
{
    uint8_t k[1024], v[1024];
    uint32_t kl = wide( k, key ), vl = wide( v, value );
    size_t n = preg_put_entry( buf, k, kl, v, vl, type, data, size );
    CHECK( n == preg_entry_size( kl, vl, size ), "entry size" );
    return n;
}

static int same( const uint8_t *w, uint32_t len, const char *s )
{
    uint8_t tmp[1024];
    return wide( tmp, s ) == len && !memcmp( tmp, w, 2 * (size_t)len );
}

int main(void)
{
    static uint8_t buf[4096], big[PREG_MAX_DATA + 4096];
    struct preg_entry e;
    size_t off, len, i;
    uint32_t name_off, dword = 0x11223344;
    uint8_t w[256];
    int ret;

    /* a file with three entries reads back */
    len = preg_put_header( buf );
    len += add( buf + len, "Software\\Policies\\Wex", "Number", 4, &dword, 4 );
    len += add( buf + len, "Software\\Policies\\Wex", "", 1, "a\0b\0\0\0", 6 );
    len += add( buf + len, "Software\\Policies\\Wex\\Sub", "**Del.Old", 1, " \0\0\0", 4 );
    CHECK( preg_begin( buf, len, &off ) == PREG_OK && off == 8, "header" );
    CHECK( preg_next( buf, len, &off, &e ) == PREG_OK, "entry 1" );
    CHECK( same( e.key, e.key_len, "Software\\Policies\\Wex" ) && same( e.value, e.value_len, "Number" ), "entry 1 names" );
    CHECK( e.type == 4 && e.size == 4 && preg_u32( e.data ) == 0x11223344, "entry 1 data" );
    CHECK( preg_classify( &e, &name_off ) == PREG_SET, "entry 1 is a set" );
    CHECK( preg_next( buf, len, &off, &e ) == PREG_OK && e.value_len == 0 && e.size == 6, "entry 2 (default value)" );
    CHECK( preg_next( buf, len, &off, &e ) == PREG_OK, "entry 3" );
    CHECK( preg_classify( &e, &name_off ) == PREG_DELETE_VALUE && name_off == 6, "entry 3 deletes a value" );
    CHECK( same( e.value + 2 * name_off, e.value_len - name_off, "Old" ), "entry 3 value name" );
    CHECK( preg_next( buf, len, &off, &e ) == PREG_END && off == len, "end" );

    /* every truncation of that file is an error or a clean end at an entry boundary, never a read past the end */
    for (i = 0; i < len; i++)
    {
        size_t o;
        int n = 0;
        if (preg_begin( buf, i, &o ) != PREG_OK) { CHECK( i < 8, "short header" ); continue; }
        while ((ret = preg_next( buf, i, &o, &e )) == PREG_OK) n++;
        CHECK( ret == PREG_END || ret == PREG_ERR_TRUNCATED, "truncated file" );
        CHECK( ret != PREG_END || o == i, "a clean end is at the end" );
        CHECK( n <= 2, "no entry appears from a cut file" );
    }

    /* header faults */
    memcpy( big, buf, len );
    big[0] = 'p'; CHECK( preg_begin( big, len, &off ) == PREG_ERR_HEADER, "wrong magic" );
    big[0] = 'P'; big[4] = 2; CHECK( preg_begin( big, len, &off ) == PREG_ERR_HEADER, "wrong version" );
    CHECK( preg_begin( buf, 7, &off ) == PREG_ERR_HEADER, "short file" );
    CHECK( preg_begin( buf, 8, &off ) == PREG_OK && preg_next( buf, 8, &off, &e ) == PREG_END, "empty file" );

    /* syntax faults: each delimiter replaced */
    for (i = 0; i < 6; i++)
    {
        /* offsets of '[', the three ';' after key, value, type, the ';' after size, and ']' of entry 1 */
        size_t kl = 21, vl = 6, at[6];
        at[0] = 8; at[1] = at[0] + 2 + (kl + 1) * 2; at[2] = at[1] + 2 + (vl + 1) * 2; at[3] = at[2] + 2 + 4;
        at[4] = at[3] + 2 + 4; at[5] = at[4] + 2 + 4;
        memcpy( big, buf, len );
        big[at[i]] = 'x';
        preg_begin( big, len, &off );
        CHECK( preg_next( big, len, &off, &e ) == PREG_ERR_SYNTAX, "a wrong delimiter" );
    }

    /* an entry without a key, a size beyond the limit, a size beyond the file, a name without end */
    len = preg_put_header( big );
    len += add( big + len, "", "v", 4, &dword, 4 );
    preg_begin( big, len, &off );
    CHECK( preg_next( big, len, &off, &e ) == PREG_ERR_SYNTAX, "no key" );
    len = preg_put_header( big );
    len += add( big + len, "k", "v", 3, big + 100000, 16 );
    preg_put_u32( big + 8 + 2 + 4 + 2 + 4 + 2 + 4 + 2, PREG_MAX_DATA + 1 );
    preg_begin( big, len, &off );
    CHECK( preg_next( big, len, &off, &e ) == PREG_ERR_SIZE, "data over the limit" );
    preg_put_u32( big + 8 + 2 + 4 + 2 + 4 + 2 + 4 + 2, 17 );
    preg_begin( big, len, &off );
    CHECK( preg_next( big, len, &off, &e ) != PREG_OK, "data longer than the file" );
    len = preg_put_header( big );
    preg_put_u16( big + len, '[' ); len += 2;
    for (i = 0; i < PREG_MAX_NAME + 2; i++, len += 2) preg_put_u16( big + len, 'a' );
    preg_begin( big, len, &off );
    ret = preg_next( big, len, &off, &e );
    CHECK( ret == PREG_ERR_SIZE || ret == PREG_ERR_TRUNCATED, "a name without end" );

    /* data of the largest size is accepted */
    len = preg_put_header( big );
    {
        uint8_t k[8], v[8];
        static uint8_t data[PREG_MAX_DATA];
        uint32_t kl = wide( k, "k" ), vl = wide( v, "v" );
        len += preg_put_entry( big + len, k, kl, v, vl, 3, data, PREG_MAX_DATA );
    }
    preg_begin( big, len, &off );
    CHECK( preg_next( big, len, &off, &e ) == PREG_OK && e.size == PREG_MAX_DATA, "largest data" );
    CHECK( preg_next( big, len, &off, &e ) == PREG_END, "end after the largest data" );

    /* the special names, without case */
    {
        static const struct { const char *name; enum preg_action action; uint32_t off; } names[] =
        {
            { "Value", PREG_SET, 0 }, { "*x", PREG_SET, 0 }, { "", PREG_SET, 0 },
            { "**DeleteValues", PREG_DELETE_VALUES, 0 }, { "**deletevalues", PREG_DELETE_VALUES, 0 },
            { "**DeleteKeys", PREG_DELETE_KEYS, 0 }, { "**DELETEKEYS", PREG_DELETE_KEYS, 0 },
            { "**DelVals.", PREG_DELETE_ALL_VALUES, 0 }, { "**delvals.", PREG_DELETE_ALL_VALUES, 0 },
            { "**Del.Name", PREG_DELETE_VALUE, 6 }, { "**del.n", PREG_DELETE_VALUE, 6 },
            { "**Soft.Name", PREG_SOFT_SET, 7 }, { "**SecureKey", PREG_SECURE_KEY, 0 },
            { "**Del.", PREG_SECURE_KEY, 0 }, { "**Soft.", PREG_SECURE_KEY, 0 }, { "**Unknown", PREG_SECURE_KEY, 0 },
            { "**DeleteValuesX", PREG_SECURE_KEY, 0 },
        };
        for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        {
            e.value = w; e.value_len = wide( w, names[i].name );
            CHECK( preg_classify( &e, &name_off ) == names[i].action && name_off == names[i].off, names[i].name );
        }
    }

    /* the two policy trees */
    {
        static const struct { const char *key; int in; } keys[] =
        {
            { "Software\\Policies", 1 }, { "software\\policies\\Microsoft\\Windows", 1 },
            { "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies", 1 },
            { "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", 1 },
            { "Software\\PoliciesX", 0 }, { "Software\\Policie", 0 }, { "Software", 0 },
            { "Software\\Microsoft\\Windows\\CurrentVersion\\PoliciesX\\A", 0 },
            { "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0 }, { "System\\CurrentControlSet", 0 },
            { "\\Software\\Policies", 0 },
        };
        for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
            CHECK( preg_key_in_policy_tree( w, wide( w, keys[i].key ) ) == keys[i].in, keys[i].key );
    }

    printf( "PREG checks=%d failures=%d\n", checks, failures );
    return failures != 0;
}
