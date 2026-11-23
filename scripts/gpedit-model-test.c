/*
 * Native test of src/dlls/gpedit/policies.c and model.c, run by scripts/test_gpedit_model.sh
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
#include <string.h>

#include "../src/include/wexpreg.h"
#include "../src/dlls/gpedit/model.h"

static int failures;
#define CHECK( cond, ... ) do { if (!(cond)) { printf( "FAIL %s:%d: ", __FILE__, __LINE__ ); printf( __VA_ARGS__ ); printf( "\n" ); failures++; } } while (0)

static const struct policy *by_id( const char *id )
{
    unsigned int i;

    for (i = 0; i < policy_count; i++) if (!strcmp( policies[i].id, id )) return &policies[i];
    printf( "FAIL: no policy %s\n", id );
    failures++;
    return &policies[0];
}

static size_t put_name( uint8_t *out, const char *name )
{
    size_t i, n = strlen( name );
    for (i = 0; i < n; i++) preg_put_u16( out + 2 * i, (unsigned char)name[i] );
    return n;
}

/* an entry of a file, as another tool would write it */
static size_t foreign_entry( uint8_t *out, const char *key, const char *value, uint32_t type, const void *data, uint32_t size )
{
    uint8_t k[512], v[512];
    size_t kn = put_name( k, key ), vn = put_name( v, value );

    return preg_put_entry( out, k, (uint32_t)kn, v, (uint32_t)vn, type, data, size );
}

static struct model *loaded( const uint8_t *data, size_t size, int expect )
{
    struct model *m = model_new();
    int ret = model_load( m, data, size );

    CHECK( ret == expect, "load returned %d, wanted %d", ret, expect );
    return m;
}

static void roundtrip( struct model *m, struct model **out )
{
    uint8_t *data;
    size_t size, off = PREG_HEADER_SIZE;
    struct preg_entry e;
    int ret, n = 0;

    CHECK( !model_save( m, &data, &size ), "save" );
    if (size)
    {
        CHECK( preg_begin( data, size, &off ) == PREG_OK, "the saved file has no PReg header" );
        while ((ret = preg_next( data, size, &off, &e )) == PREG_OK) n++;
        CHECK( ret == PREG_END, "the saved file does not parse: %d", ret );
        CHECK( (size_t)n == model_entries( m ), "entries in the file %d, in the model %zu", n, model_entries( m ) );
    }
    *out = loaded( data, size, 0 );
    free( data );
}

static void table_is_sane(void)
{
    unsigned int i, j;

    for (i = 0; i < policy_count; i++)
    {
        const struct policy *p = &policies[i];

        CHECK( p->id && p->name && p->explain && p->key && p->value && p->category, "policy %u has an empty field", i );
        {
            static const char tree1[] = "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\", tree2[] = "Software\\Policies\\";

            CHECK( !strncmp( p->key, tree1, strlen( tree1 ) ) || !strncmp( p->key, tree2, strlen( tree2 ) ),
                   "%s is not in a policy tree", p->id );
        }
        if (p->kind == POLICY_FLAG) CHECK( p->enabled != p->disabled, "%s: enabled and disabled are the same", p->id );
        if (p->status == POLICY_NOT_APPLICABLE) CHECK( p->reason && p->reason[0], "%s is grayed out without a reason", p->id );
        else CHECK( !p->reason, "%s has a reason but applies", p->id );
        CHECK( policy_find( p->scope, p->key, p->value ) == p, "%s is not found by its value", p->id );
        for (j = i + 1; j < policy_count; j++) CHECK( strcmp( p->id, policies[j].id ), "%s twice", p->id );
    }
    CHECK( policy_count >= 30, "only %u policies", policy_count );
}

int main(void)
{
    const struct policy *run = by_id( "user:NoRun" ), *secure = by_id( "computer:DisableBkGndGroupPolicy" );
    const struct policy *drives = by_id( "user:NoDrives" ), *tray = by_id( "user:NoTrayItemsDisplay" );
    struct model *m = model_new(), *r;
    enum setting_state state;
    uint32_t number;
    uint8_t file[4096], *data;
    size_t size, off;

    table_is_sane();

    /* an empty model: everything Not Configured, and no file */
    model_get( m, run, &state, &number );
    CHECK( state == SETTING_NOT_CONFIGURED && !number, "an empty model is not Not Configured" );
    CHECK( !model_save( m, &data, &size ) && !size && !data, "an empty model saves as zero bytes" );

    /* flags: Enabled and Disabled write the policy's numbers, and read back */
    CHECK( !model_set( m, run, SETTING_ENABLED, 0 ), "set" );
    model_get( m, run, &state, &number );
    CHECK( state == SETTING_ENABLED && number == 1, "NoRun enabled: %d %u", state, number );
    CHECK( !model_set( m, secure, SETTING_DISABLED, 0 ), "set" );
    model_get( m, secure, &state, &number );
    CHECK( state == SETTING_DISABLED && number == 0, "DisableBkGndGroupPolicy disabled: %d %u", state, number );
    CHECK( !model_set( m, secure, SETTING_ENABLED, 0 ), "set" );
    model_get( m, secure, &state, &number );
    CHECK( state == SETTING_ENABLED && number == 1, "DisableBkGndGroupPolicy enabled again: %d %u", state, number );
    CHECK( model_entries( m ) == 2, "entries %zu", model_entries( m ) );
    /* a number policy takes the number */
    CHECK( !model_set( m, drives, SETTING_ENABLED, 0x0c ), "set" );
    model_get( m, drives, &state, &number );
    CHECK( state == SETTING_ENABLED && number == 0x0c, "NoDrives: %d %u", state, number );
    roundtrip( m, &r );
    model_get( r, drives, &state, &number );
    CHECK( state == SETTING_ENABLED && number == 0x0c, "NoDrives after a round trip: %d %u", state, number );
    model_get( r, secure, &state, &number );
    CHECK( state == SETTING_ENABLED && number == 1, "DisableBkGndGroupPolicy after a round trip" );
    CHECK( model_entries( r ) == 3, "entries after a round trip %zu", model_entries( r ) );
    model_free( r );

    /* Not Configured removes the entry; the last one gives zero bytes again */
    CHECK( !model_set( m, run, SETTING_NOT_CONFIGURED, 0 ), "set" );
    model_get( m, run, &state, &number );
    CHECK( state == SETTING_NOT_CONFIGURED && model_entries( m ) == 2, "NoRun not configured: %d, %zu entries", state, model_entries( m ) );
    CHECK( !model_set( m, secure, SETTING_NOT_CONFIGURED, 0 ) && !model_set( m, drives, SETTING_NOT_CONFIGURED, 0 ), "set" );
    CHECK( !model_save( m, &data, &size ) && !size, "all removed: zero bytes" );
    model_free( m );

    /* entries that no policy describes are kept, in their place, and the known ones are edited in the same file */
    {
        const char text[] = "a text";
        uint32_t seven = 7;
        uint16_t wide[8] = { 'a', 'b', 0 };

        off = preg_put_header( file );
        off += foreign_entry( file + off, "Software\\Policies\\Other", "Setting", 1, wide, 6 );
        off += foreign_entry( file + off, "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", "NoRun",
                              4, &seven, 4 );
        off += foreign_entry( file + off, "Software\\Policies\\Other", "**Del.Old", 1, text, 2 );
        off += foreign_entry( file + off, "SOFTWARE\\policies\\Other\\Sub", "N", 4, &seven, 4 );
        m = loaded( file, off, 0 );
        CHECK( model_entries( m ) == 4 && model_foreign_entries( m ) == 3, "foreign entries: %zu of %zu", model_foreign_entries( m ), model_entries( m ) );
        model_get( m, run, &state, &number );
        CHECK( state == SETTING_ENABLED && number == 7, "an existing NoRun=7 is Enabled with its number: %d %u", state, number );
        CHECK( !model_set( m, run, SETTING_DISABLED, 0 ), "set" );
        CHECK( !model_set( m, tray, SETTING_ENABLED, 0 ), "set" );
        roundtrip( m, &r );
        CHECK( model_entries( r ) == 5 && model_foreign_entries( r ) == 3, "after editing: %zu entries, %zu foreign", model_entries( r ), model_foreign_entries( r ) );
        model_get( r, run, &state, &number );
        CHECK( state == SETTING_DISABLED && number == 0, "NoRun disabled in place: %d %u", state, number );
        /* the order is kept: the file starts with the first foreign entry, and the edited NoRun is second */
        CHECK( !model_save( r, &data, &size ), "save" );
        {
            struct preg_entry e;
            size_t o = PREG_HEADER_SIZE;
            CHECK( preg_next( data, size, &o, &e ) == PREG_OK && e.value_len == 7, "the first entry is not the foreign one" );
            CHECK( preg_next( data, size, &o, &e ) == PREG_OK && e.value_len == 5 && e.size == 4 && preg_u32( e.data ) == 0, "NoRun is not second, or not 0" );
        }
        free( data );
        model_free( r );
        model_free( m );
    }

    /* an entry of another type is not a setting: Not Configured, and Enabled replaces it by a DWORD */
    {
        uint16_t wide[4] = { '1', 0 };

        off = preg_put_header( file );
        off += foreign_entry( file + off, "Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", "NoRun", 1, wide, 4 );
        m = loaded( file, off, 0 );
        model_get( m, run, &state, &number );
        CHECK( state == SETTING_NOT_CONFIGURED, "a string NoRun is a setting" );
        CHECK( !model_set( m, run, SETTING_ENABLED, 0 ), "set" );
        model_get( m, run, &state, &number );
        CHECK( state == SETTING_ENABLED && number == 1 && model_entries( m ) == 1, "the string entry was not replaced" );
        model_free( m );
    }

    /* what is not a policy file is refused, and leaves an empty model */
    off = preg_put_header( file );
    off += foreign_entry( file + off, "Software\\Policies\\Other", "Setting", 4, &off, 4 );
    m = loaded( file, off - 3, PREG_ERR_TRUNCATED );
    CHECK( model_entries( m ) == 0, "a truncated file left entries" );
    model_free( m );
    memcpy( file, "PReX", 4 );
    m = loaded( file, off, PREG_ERR_HEADER );
    model_free( m );
    m = loaded( (const uint8_t *)"PReg", 4, PREG_ERR_HEADER );
    model_free( m );
    memcpy( file, "PReg", 4 );
    preg_put_u32( file + 4, 2 );
    m = loaded( file, off, PREG_ERR_HEADER );
    model_free( m );

    if (failures) { printf( "%d failures\n", failures ); return 1; }
    printf( "gpedit model: all checks passed\n" );
    return 0;
}
