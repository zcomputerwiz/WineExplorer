/*
 * Registry policy files (Registry.pol, the PReg format of MS-GPREG): reader and writer
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

/*
 * The file: "PReg", version 1 (both 32-bit little endian), then entries
 *     [ key NUL ; value NUL ; type ; size ; data ]
 * where the brackets, the semicolons, the key and the value name are UTF-16LE, type and size are 32-bit little
 * endian and data is size bytes. Portable C with no Windows types, so that the parser is tested natively
 * (test_preg_main.c); the file comes from a domain's SYSVOL and is not trusted.
 */

#ifndef WEXPOLICY_PREG_H
#define WEXPOLICY_PREG_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PREG_HEADER_SIZE   8
#define PREG_MAX_NAME      (16383 + 255)   /* characters: longer than any registry key path component chain we take */
#define PREG_MAX_DATA      (1024 * 1024)   /* bytes: the registry's own limit for a value */

#define PREG_OK            1
#define PREG_END           0
#define PREG_ERR_HEADER    (-1)
#define PREG_ERR_SYNTAX    (-2)
#define PREG_ERR_TRUNCATED (-3)
#define PREG_ERR_SIZE      (-4)

struct preg_entry
{
    const uint8_t *key;        /* UTF-16LE, not aligned, key_len characters, followed by a NUL character */
    uint32_t       key_len;
    const uint8_t *value;
    uint32_t       value_len;
    uint32_t       type;
    uint32_t       size;
    const uint8_t *data;
};

static inline uint16_t preg_u16( const uint8_t *p ) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t preg_u32( const uint8_t *p )
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline void preg_put_u16( uint8_t *p, uint16_t v ) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void preg_put_u32( uint8_t *p, uint32_t v )
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* the header; the offset of the first entry, or an error */
static inline int preg_begin( const uint8_t *buf, size_t len, size_t *off )
{
    if (len < PREG_HEADER_SIZE) return PREG_ERR_HEADER;
    if (memcmp( buf, "PReg", 4 ) || preg_u32( buf + 4 ) != 1) return PREG_ERR_HEADER;
    *off = PREG_HEADER_SIZE;
    return PREG_OK;
}

static inline int preg_char( const uint8_t *buf, size_t len, size_t *off, uint16_t ch )
{
    if (len - *off < 2) return PREG_ERR_TRUNCATED;
    if (preg_u16( buf + *off ) != ch) return PREG_ERR_SYNTAX;
    *off += 2;
    return PREG_OK;
}

/* a NUL-terminated UTF-16LE string of at most PREG_MAX_NAME characters */
static inline int preg_string( const uint8_t *buf, size_t len, size_t *off, const uint8_t **str, uint32_t *chars )
{
    size_t pos = *off;
    uint32_t n = 0;

    for (;;)
    {
        if (len - pos < 2) return PREG_ERR_TRUNCATED;
        if (!preg_u16( buf + pos )) break;
        if (++n > PREG_MAX_NAME) return PREG_ERR_SIZE;
        pos += 2;
    }
    *str = buf + *off;
    *chars = n;
    *off = pos + 2;
    return PREG_OK;
}

/* The next entry: PREG_OK, PREG_END at the end of the file, or an error. Nothing in the entry is interpreted. */
static inline int preg_next( const uint8_t *buf, size_t len, size_t *off, struct preg_entry *e )
{
    int ret;

    if (*off > len) return PREG_ERR_TRUNCATED;
    if (*off == len) return PREG_END;
    if ((ret = preg_char( buf, len, off, '[' )) != PREG_OK) return ret;
    if ((ret = preg_string( buf, len, off, &e->key, &e->key_len )) != PREG_OK) return ret;
    if ((ret = preg_char( buf, len, off, ';' )) != PREG_OK) return ret;
    if ((ret = preg_string( buf, len, off, &e->value, &e->value_len )) != PREG_OK) return ret;
    if ((ret = preg_char( buf, len, off, ';' )) != PREG_OK) return ret;
    if (len - *off < 4) return PREG_ERR_TRUNCATED;
    e->type = preg_u32( buf + *off ); *off += 4;
    if ((ret = preg_char( buf, len, off, ';' )) != PREG_OK) return ret;
    if (len - *off < 4) return PREG_ERR_TRUNCATED;
    e->size = preg_u32( buf + *off ); *off += 4;
    if ((ret = preg_char( buf, len, off, ';' )) != PREG_OK) return ret;
    if (e->size > PREG_MAX_DATA) return PREG_ERR_SIZE;
    if (len - *off < e->size) return PREG_ERR_TRUNCATED;
    e->data = buf + *off; *off += e->size;
    if ((ret = preg_char( buf, len, off, ']' )) != PREG_OK) return ret;
    if (!e->key_len) return PREG_ERR_SYNTAX;   /* an entry names a key */
    return PREG_OK;
}

/* Writer: the bytes one entry takes, and the entry itself (the caller provides the room). Names are UTF-16LE
 * without their NUL. */
static inline size_t preg_entry_size( uint32_t key_len, uint32_t value_len, uint32_t size )
{
    return 2 + ((size_t)key_len + 1) * 2 + 2 + ((size_t)value_len + 1) * 2 + 2 + 4 + 2 + 4 + 2 + size + 2;
}

static inline size_t preg_put_header( uint8_t *buf )
{
    memcpy( buf, "PReg", 4 );
    preg_put_u32( buf + 4, 1 );
    return PREG_HEADER_SIZE;
}

static inline size_t preg_put_entry( uint8_t *buf, const uint8_t *key, uint32_t key_len, const uint8_t *value,
                                     uint32_t value_len, uint32_t type, const uint8_t *data, uint32_t size )
{
    uint8_t *p = buf;

    preg_put_u16( p, '[' ); p += 2;
    memcpy( p, key, (size_t)key_len * 2 ); p += (size_t)key_len * 2;
    preg_put_u16( p, 0 ); p += 2;
    preg_put_u16( p, ';' ); p += 2;
    if (value_len) memcpy( p, value, (size_t)value_len * 2 );
    p += (size_t)value_len * 2;
    preg_put_u16( p, 0 ); p += 2;
    preg_put_u16( p, ';' ); p += 2;
    preg_put_u32( p, type ); p += 4;
    preg_put_u16( p, ';' ); p += 2;
    preg_put_u32( p, size ); p += 4;
    preg_put_u16( p, ';' ); p += 2;
    if (size) memcpy( p, data, size );
    p += size;
    preg_put_u16( p, ']' ); p += 2;
    return (size_t)(p - buf);
}

/*
 * The special value names of MS-GPREG (compared without case):
 *   **DeleteValues    data: value names separated by semicolons, to delete in the key
 *   **Del.NAME        delete the value NAME
 *   **DelVals.        delete every value of the key
 *   **DeleteKeys      data: subkey names separated by semicolons, to delete below the key
 *   **SecureKey       ignored (the key's protection is not the file's to set here)
 *   **Soft.NAME       set NAME only if it does not exist
 */
enum preg_action
{
    PREG_SET, PREG_DELETE_VALUES, PREG_DELETE_VALUE, PREG_DELETE_ALL_VALUES, PREG_DELETE_KEYS, PREG_SECURE_KEY,
    PREG_SOFT_SET
};

static inline int preg_prefix( const uint8_t *value, uint32_t len, const char *prefix )
{
    uint32_t i, n = (uint32_t)strlen( prefix );

    if (len < n) return 0;
    for (i = 0; i < n; i++)
    {
        uint16_t ch = preg_u16( value + 2 * i );
        if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
        if (ch != (uint16_t)(unsigned char)prefix[i]) return 0;
    }
    return 1;
}

/* what the entry asks for; *name_off is where the value name proper starts (PREG_DELETE_VALUE, PREG_SOFT_SET) */
static inline enum preg_action preg_classify( const struct preg_entry *e, uint32_t *name_off )
{
    *name_off = 0;
    if (!preg_prefix( e->value, e->value_len, "**" )) return PREG_SET;
    if (e->value_len == 14 && preg_prefix( e->value, e->value_len, "**deletevalues" )) return PREG_DELETE_VALUES;
    if (e->value_len == 12 && preg_prefix( e->value, e->value_len, "**deletekeys" )) return PREG_DELETE_KEYS;
    if (e->value_len == 11 && preg_prefix( e->value, e->value_len, "**securekey" )) return PREG_SECURE_KEY;
    if (preg_prefix( e->value, e->value_len, "**delvals." )) return PREG_DELETE_ALL_VALUES;
    if (preg_prefix( e->value, e->value_len, "**del." ) && e->value_len > 6) { *name_off = 6; return PREG_DELETE_VALUE; }
    if (preg_prefix( e->value, e->value_len, "**soft." ) && e->value_len > 7) { *name_off = 7; return PREG_SOFT_SET; }
    return PREG_SECURE_KEY;   /* an unknown ** name: nothing to do, never a value of that name */
}

/* Is the key one of the two policy trees (relative to the hive root), or below one? Without case. */
static inline int preg_key_in_policy_tree( const uint8_t *key, uint32_t len )
{
    static const char * const trees[] =
    {
        "software\\policies",
        "software\\microsoft\\windows\\currentversion\\policies",
    };
    unsigned int t;

    for (t = 0; t < sizeof(trees) / sizeof(trees[0]); t++)
    {
        uint32_t n = (uint32_t)strlen( trees[t] );
        if (!preg_prefix( key, len, trees[t] )) continue;
        if (len == n || preg_u16( key + 2 * n ) == '\\') return 1;
    }
    return 0;
}

#endif /* WEXPOLICY_PREG_H */
