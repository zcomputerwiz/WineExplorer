/*
 * gpedit: the settings of a Registry.pol, in memory
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

/* See model.h. The entries are kept as the bytes of the file, from "[" to "]". */

#include <stdlib.h>
#include <string.h>

#include "../../programs/wexpolicy/preg.h"
#include "model.h"

#define REG_DWORD_TYPE 4

struct entry
{
    uint8_t *bytes;          /* the whole entry, as in the file */
    size_t size;
    char *key;               /* ASCII copies of the names, NULL when a name has other characters */
    char *value;
    uint32_t type;
    uint32_t data_size;
    uint32_t number;         /* a DWORD's value */
};

struct model
{
    struct entry *entries;
    size_t count, capacity;
};

struct model *model_new(void)
{
    return calloc( 1, sizeof(struct model) );
}

static void entry_clear( struct entry *e )
{
    free( e->bytes );
    free( e->key );
    free( e->value );
}

static void model_clear( struct model *model )
{
    size_t i;

    for (i = 0; i < model->count; i++) entry_clear( &model->entries[i] );
    free( model->entries );
    model->entries = NULL;
    model->count = model->capacity = 0;
}

void model_free( struct model *model )
{
    if (!model) return;
    model_clear( model );
    free( model );
}

/* UTF-16LE names to ASCII; NULL when a character is not ASCII */
static char *ascii_name( const uint8_t *name, uint32_t chars )
{
    char *out = malloc( chars + 1 );
    uint32_t i;

    if (!out) return NULL;
    for (i = 0; i < chars; i++)
    {
        uint16_t c = preg_u16( name + 2 * i );
        if (c >= 0x80) { free( out ); return NULL; }
        out[i] = (char)c;
    }
    out[chars] = 0;
    return out;
}

static int add_entry( struct model *model, const uint8_t *bytes, size_t size, const struct preg_entry *e )
{
    struct entry *entry;

    if (model->count == model->capacity)
    {
        size_t capacity = model->capacity ? model->capacity * 2 : 16;
        struct entry *entries = realloc( model->entries, capacity * sizeof(*entries) );
        if (!entries) return -1;
        model->entries = entries;
        model->capacity = capacity;
    }
    entry = &model->entries[model->count];
    memset( entry, 0, sizeof(*entry) );
    if (!(entry->bytes = malloc( size ))) return -1;
    memcpy( entry->bytes, bytes, size );
    entry->size = size;
    entry->key = ascii_name( e->key, e->key_len );
    entry->value = ascii_name( e->value, e->value_len );
    entry->type = e->type;
    entry->data_size = e->size;
    if (e->type == REG_DWORD_TYPE && e->size == 4) entry->number = preg_u32( e->data );
    model->count++;
    return 0;
}

int model_load( struct model *model, const uint8_t *data, size_t size )
{
    struct preg_entry e;
    size_t off, start;
    int ret;

    model_clear( model );
    if (!size) return 0;
    if ((ret = preg_begin( data, size, &off )) != PREG_OK) return ret;
    for (;;)
    {
        start = off;
        if ((ret = preg_next( data, size, &off, &e )) == PREG_END) return 0;
        if (ret != PREG_OK || add_entry( model, data + start, off - start, &e ))
        {
            model_clear( model );
            return ret != PREG_OK ? ret : PREG_ERR_SIZE;
        }
    }
}

int model_save( const struct model *model, uint8_t **data, size_t *size )
{
    size_t total = PREG_HEADER_SIZE, i, off;
    uint8_t *out;

    *data = NULL;
    *size = 0;
    if (!model->count) return 0;
    for (i = 0; i < model->count; i++) total += model->entries[i].size;
    if (!(out = malloc( total ))) return -1;
    off = preg_put_header( out );
    for (i = 0; i < model->count; i++)
    {
        memcpy( out + off, model->entries[i].bytes, model->entries[i].size );
        off += model->entries[i].size;
    }
    *data = out;
    *size = total;
    return 0;
}

static int same_name( const char *a, const char *b )
{
    if (!a || !b) return 0;
    for (; *a && *b; a++, b++)
    {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return 0;
    }
    return *a == *b;
}

/* the entry of a policy: the key and the value name match without case, and it is a DWORD */
static struct entry *find_entry( const struct model *model, const struct policy *policy )
{
    size_t i;

    for (i = 0; i < model->count; i++)
    {
        struct entry *e = &model->entries[i];
        if (same_name( e->key, policy->key ) && same_name( e->value, policy->value )) return e;
    }
    return NULL;
}

void model_get( const struct model *model, const struct policy *policy, enum setting_state *state, uint32_t *number )
{
    struct entry *e = find_entry( model, policy );

    *number = 0;
    if (!e || e->type != REG_DWORD_TYPE || e->data_size != 4)
    {
        *state = SETTING_NOT_CONFIGURED;
        return;
    }
    *number = e->number;
    if (policy->kind == POLICY_FLAG && e->number == policy->disabled && policy->disabled != policy->enabled)
        *state = SETTING_DISABLED;
    else
        *state = SETTING_ENABLED;
}

static void remove_entry( struct model *model, struct entry *e )
{
    entry_clear( e );
    memmove( e, e + 1, (size_t)(model->entries + model->count - e - 1) * sizeof(*e) );
    model->count--;
}

/* UTF-16LE of an ASCII name, malloc'd, with its length in characters */
static uint8_t *utf16_name( const char *name, uint32_t *chars )
{
    size_t n = strlen( name ), i;
    uint8_t *out = malloc( n * 2 + 2 );

    if (!out) return NULL;
    for (i = 0; i < n; i++) preg_put_u16( out + 2 * i, (unsigned char)name[i] );
    *chars = (uint32_t)n;
    return out;
}

int model_set( struct model *model, const struct policy *policy, enum setting_state state, uint32_t number )
{
    struct entry *old = find_entry( model, policy );
    uint8_t value[4], *key, *name, *bytes;
    uint32_t key_chars, name_chars, data;
    struct preg_entry parsed;
    size_t size, off;
    int ret;

    if (state == SETTING_NOT_CONFIGURED)
    {
        if (old) remove_entry( model, old );
        return 0;
    }
    if (state == SETTING_DISABLED && policy->kind == POLICY_FLAG) data = policy->disabled;
    else if (policy->kind == POLICY_FLAG) data = policy->enabled;
    else data = number;
    preg_put_u32( value, data );
    if (!(key = utf16_name( policy->key, &key_chars )) || !(name = utf16_name( policy->value, &name_chars )))
    {
        free( key );
        return -1;
    }
    size = preg_entry_size( key_chars, name_chars, 4 );
    if (!(bytes = malloc( size )))
    {
        free( key );
        free( name );
        return -1;
    }
    size = preg_put_entry( bytes, key, key_chars, name, name_chars, REG_DWORD_TYPE, value, 4 );
    free( key );
    free( name );
    /* the new entry replaces the old one in place (the order of the file stays), or is added at the end */
    if (old)
    {
        free( old->bytes );
        old->bytes = bytes;
        old->size = size;
        old->type = REG_DWORD_TYPE;
        old->data_size = 4;
        old->number = data;
        return 0;
    }
    off = 0;
    ret = preg_next( bytes, size, &off, &parsed );
    if (ret != PREG_OK || add_entry( model, bytes, size, &parsed ))
    {
        free( bytes );
        return -1;
    }
    free( bytes );
    return 0;
}

size_t model_entries( const struct model *model )
{
    return model->count;
}

size_t model_foreign_entries( const struct model *model )
{
    size_t i, n = 0;

    for (i = 0; i < model->count; i++)
    {
        struct entry *e = &model->entries[i];
        unsigned int p;
        int known = 0;

        for (p = 0; p < policy_count && !known; p++)
            known = same_name( e->key, policies[p].key ) && same_name( e->value, policies[p].value );
        if (!known) n++;
    }
    return n;
}
