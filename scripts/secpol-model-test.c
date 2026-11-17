/*
 * Native test of src/dlls/secpol/template.c and settings.c, run by scripts/test_secpol_model.sh
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

#include "../src/dlls/secpol/settings.h"
#include "../src/dlls/secpol/template.h"

static int failures;
#define CHECK( cond, ... ) do { if (!(cond)) { printf( "FAIL %s:%d: ", __FILE__, __LINE__ ); printf( __VA_ARGS__ ); printf( "\n" ); failures++; } } while (0)

#define SYSTEM "MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System\\"

static const struct sec_setting *by_key( const char *key )
{
    unsigned int i;

    for (i = 0; i < sec_setting_count; i++)
        if (sec_settings[i].key[0] && strstr( sec_settings[i].key, key )) return &sec_settings[i];
    printf( "FAIL: no setting %s\n", key );
    failures++;
    return &sec_settings[0];
}

static struct tmpl *load( const char *text )
{
    struct tmpl *t = tmpl_new();

    CHECK( !tmpl_load( t, text, strlen( text ) ), "load" );
    return t;
}

static char *saved( const struct tmpl *t )
{
    char *text;
    size_t length;

    CHECK( !tmpl_save( t, &text, &length ), "save" );
    CHECK( strlen( text ) == length, "length %zu of %zu", length, strlen( text ) );
    return text;
}

static void test_template(void)
{
    static const char sample[] =
        "\xef\xbb\xbf[Unicode]\r\nUnicode=yes\r\n[System Access]\r\nLockoutBadCount = 5\r\n  ResetLockoutCount=30  \r\n\r\n"
        "[Registry Values]\r\n" SYSTEM "ConsentPromptBehaviorAdmin=4,2\r\n"
        "[Privilege Rights]\r\nSeShutdownPrivilege = *S-1-5-32-544\r\n; a comment = not a value\r\n"
        "[Version]\r\nsignature=\"$CHICAGO$\"\r\nRevision=1\r\n";
    struct tmpl *t = load( sample );
    char value[64], *text, *other;

    CHECK( tmpl_get( t, "System Access", "LockoutBadCount", value, sizeof(value) ) && !strcmp( value, "5" ), "get: %s", value );
    CHECK( tmpl_get( t, "system access", "lockoutbadcount", value, sizeof(value) ) && !strcmp( value, "5" ), "case" );
    CHECK( tmpl_get( t, "System Access", "ResetLockoutCount", value, sizeof(value) ) && !strcmp( value, "30" ), "blanks: '%s'", value );
    CHECK( tmpl_get( t, "Registry Values", SYSTEM "ConsentPromptBehaviorAdmin", value, sizeof(value) ) && !strcmp( value, "4,2" ), "registry: %s", value );
    CHECK( !tmpl_get( t, "System Access", "LockoutDuration", value, sizeof(value) ), "missing name" );
    CHECK( !tmpl_get( t, "Registry Values", "LockoutBadCount", value, sizeof(value) ), "name of another section" );
    CHECK( !tmpl_get( t, "Privilege Rights", "; a comment", value, sizeof(value) ), "a comment is not a line" );
    CHECK( !tmpl_get( t, "Nothing", "LockoutBadCount", value, sizeof(value) ), "missing section" );

    /* a line that is there is replaced and keeps its name's spelling; nothing else moves */
    CHECK( !tmpl_set( t, "System Access", "lockoutbadcount", "7" ), "set" );
    CHECK( tmpl_get( t, "System Access", "LockoutBadCount", value, sizeof(value) ) && !strcmp( value, "7" ), "replaced" );
    text = saved( t );
    CHECK( strstr( text, "\r\nlockoutbadcount = 7\r\n" ) || strstr( text, "\r\nLockoutBadCount = 7\r\n" ), "name kept" );
    CHECK( strstr( text, "[Privilege Rights]\r\nSeShutdownPrivilege = *S-1-5-32-544\r\n; a comment = not a value\r\n" ), "other sections kept" );
    CHECK( !strncmp( text, "[Unicode]\r\n", 11 ), "the byte order mark is not kept in the text: %.12s", text );
    free( text );

    /* a new line goes at the end of its section, before the blank line that separates it from the next */
    CHECK( !tmpl_set( t, "System Access", "LockoutDuration", "-1" ), "add" );
    text = saved( t );
    CHECK( strstr( text, "ResetLockoutCount=30  \r\nLockoutDuration = -1\r\n\r\n[Registry Values]" ), "placed: %s", text );
    free( text );

    /* a new section is made before [Version] */
    CHECK( !tmpl_set( t, "Event Audit", "AuditSystemEvents", "0" ), "new section" );
    text = saved( t );
    CHECK( strstr( text, "AuditSystemEvents = 0\r\n[Version]" ) && strstr( text, "[Event Audit]\r\nAuditSystemEvents = 0" ), "section before Version: %s", text );
    /* the text read again gives the same */
    other = NULL;
    {
        struct tmpl *again = load( text );

        other = saved( again );
        tmpl_free( again );
    }
    CHECK( !strcmp( text, other ), "round trip" );
    free( text );
    free( other );

    /* arguments that would break the file are refused */
    CHECK( tmpl_set( t, "System Access", "A=B", "1" ), "= in a name" );
    CHECK( tmpl_set( t, "System Access", "A", "1\r\n[X]" ), "line break in a value" );
    CHECK( tmpl_set( t, "System]Access", "A", "1" ), "bracket in a section" );
    CHECK( tmpl_set( t, "", "A", "1" ), "empty section" );
    CHECK( tmpl_set( t, "System Access", "", "1" ), "empty name" );
    tmpl_free( t );

    /* without [Version] a new section goes last; LF line ends are read; no final line end */
    t = load( "[System Access]\nLockoutBadCount = 1" );
    CHECK( !tmpl_set( t, "Registry Values", "x", "4,1" ), "append" );
    text = saved( t );
    CHECK( !strcmp( text, "[System Access]\r\nLockoutBadCount = 1\r\n[Registry Values]\r\nx = 4,1\r\n" ), "appended: %s", text );
    free( text );
    tmpl_free( t );

    /* setup merges the sections of one name and applies the lines in order: the last one is in force, and a change has to
     * reach every one of them */
    t = load( "[Unicode]\r\nUnicode=yes\r\n[System Access]\r\nLockoutBadCount = 5\r\n[Registry Values]\r\nA\\B=4,2\r\n"
              "[System Access]\r\nLockoutBadCount = 9 ; the later one wins\r\nResetLockoutCount = 10\r\n"
              "[Registry Values]\r\nA\\B=4,5\r\n[Version]\r\nsignature=\"$CHICAGO$\"\r\n" );
    CHECK( tmpl_get( t, "System Access", "LockoutBadCount", value, sizeof(value) ) && !strcmp( value, "9" ), "last line, comment cut: '%s'", value );
    CHECK( tmpl_get( t, "Registry Values", "A\\B", value, sizeof(value) ) && !strcmp( value, "4,5" ), "last section: '%s'", value );
    CHECK( !tmpl_set( t, "System Access", "LockoutBadCount", "3" ) && !tmpl_set( t, "Registry Values", "A\\B", "4,2" ), "set duplicates" );
    text = saved( t );
    CHECK( !strstr( text, "= 5" ) && !strstr( text, "= 9" ) && !strstr( text, "=4,5" ), "an old line stayed: %s", text );
    CHECK( strstr( text, "LockoutBadCount = 3\r\nResetLockoutCount" ) && strstr( text, "A\\B = 4,2\r\n[Version]" ), "both replaced in place: %s", text );
    /* a new name goes to the end of the last section of that name */
    CHECK( !tmpl_set( t, "Registry Values", "C", "4,1" ), "new name" );
    free( text );
    text = saved( t );
    CHECK( strstr( text, "A\\B = 4,2\r\nC = 4,1\r\n[Version]" ), "after the last section: %s", text );
    free( text );
    tmpl_free( t );
    t = load( "[S]\r\nk = \"a;b\" ; c\r\nq = \r\n" );
    CHECK( tmpl_get( t, "S", "k", value, sizeof(value) ) && !strcmp( value, "\"a;b\"" ), "a ; in quotes is kept: '%s'", value );
    CHECK( tmpl_get( t, "S", "q", value, sizeof(value) ) && !strcmp( value, "" ), "empty value" );
    tmpl_free( t );

    /* what setup reads differently from a list of lines is refused, not edited around */
    {
        static const char * const refused[] = { "[S]\r\nk = a \\\r\nj = b\r\n", "[S]\r\nk = a\x1a\r\n", "[S]\r\nk = a  \\  \r\n" };
        unsigned int i;

        for (i = 0; i < sizeof(refused) / sizeof(refused[0]); i++)
        {
            t = tmpl_new();
            CHECK( tmpl_load( t, refused[i], strlen( refused[i] ) ) == -2, "refused %u", i );
            tmpl_free( t );
        }
        t = tmpl_new();
        CHECK( tmpl_load( t, "[S]\r\nk = a\0b\r\n", 14 ) == -2, "a NUL is refused" );
        tmpl_free( t );
        t = tmpl_new();
        CHECK( !tmpl_load( t, "[S]\r\npath = C:\\x\\y\r\n", 20 ), "a backslash inside a line is no continuation" );
        tmpl_free( t );
    }

    /* a first setting in no template at all makes the minimal one around it */
    t = tmpl_new();
    CHECK( !tmpl_set( t, "System Access", "LockoutBadCount", "3" ), "set in nothing" );
    text = saved( t );
    CHECK( !strcmp( text, "[Unicode]\r\nUnicode=yes\r\n[System Access]\r\nLockoutBadCount = 3\r\n[Version]\r\nsignature=\"$CHICAGO$\"\r\nRevision=1\r\n" ),
           "first setting: %s", text );
    free( text );
    tmpl_free( t );

    /* nothing at all: the minimal template */
    t = tmpl_new();
    text = saved( t );
    CHECK( strstr( text, "[Unicode]" ) && strstr( text, "[Version]" ), "header: %s", text );
    free( text );
    tmpl_free( t );
}

static void test_settings(void)
{
    const struct sec_setting *bad = by_key( "LockoutBadCount" ), *duration = by_key( "LockoutDuration" ), *reset = by_key( "ResetLockoutCount" );
    const struct sec_setting *admin = by_key( "ConsentPromptBehaviorAdmin" ), *secure = by_key( "PromptOnSecureDesktop" );
    char out[40];
    int ok;
    unsigned int i;

    CHECK( !bad->reason && !duration->reason && !reset->reason && !admin->reason && !secure->reason, "the five that apply" );
    CHECK( sec_from_template( bad, "5", &ok ) == 5 && ok, "threshold" );
    CHECK( sec_from_template( duration, "-1", &ok ) == 0 && ok, "forever in the template" );
    CHECK( sec_from_template( duration, "99999", &ok ) == 0 && ok, "forever as the longest time" );
    CHECK( sec_from_template( duration, "45", &ok ) == 45 && ok, "minutes" );
    CHECK( sec_from_template( reset, "0", &ok ) == 99999 && ok, "a reset time of 0 is applied as the longest" );
    CHECK( sec_from_template( reset, "-1", &ok ) == 99999 && ok, "a reset time of -1 too" );
    CHECK( sec_from_template( reset, "45", &ok ) == 45 && ok, "a reset time" );
    CHECK( sec_from_template( bad, "0", &ok ) == 0 && ok, "a threshold of 0 stays 0" );
    CHECK( sec_from_template( admin, "4,5", &ok ) == 5 && ok, "registry value" );
    sec_from_template( admin, "5", &ok );
    CHECK( !ok, "a registry value without its type is not read" );
    sec_from_template( admin, "1,5", &ok );
    CHECK( !ok, "a string value is not read" );
    sec_from_template( bad, "x", &ok );
    CHECK( !ok, "text" );
    sec_from_template( bad, "", &ok );
    CHECK( !ok, "empty" );
    CHECK( sec_from_registry( duration, 99999 ) == 0 && sec_from_registry( duration, 10 ) == 10 && sec_from_registry( bad, 99999 ) == 99999, "registry" );
    sec_to_template( duration, 0, out, sizeof(out) );
    CHECK( !strcmp( out, "-1" ), "forever to the template: %s", out );
    sec_to_template( duration, 12, out, sizeof(out) );
    CHECK( !strcmp( out, "12" ), "minutes to the template: %s", out );
    sec_to_template( bad, 0, out, sizeof(out) );
    CHECK( !strcmp( out, "0" ), "threshold 0: %s", out );
    sec_to_template( admin, 2, out, sizeof(out) );
    CHECK( !strcmp( out, "4,2" ), "registry to the template: %s", out );
    CHECK( sec_to_registry( duration, 0 ) == 99999 && sec_to_registry( duration, 7 ) == 7 && sec_to_registry( bad, 0 ) == 0, "to the registry" );
    CHECK( sec_number_valid( bad, 999 ) && !sec_number_valid( bad, 1000 ), "threshold range" );
    CHECK( sec_number_valid( reset, 1 ) && !sec_number_valid( reset, 0 ) && sec_number_valid( reset, 99999 ) && !sec_number_valid( reset, 100000 ), "reset range" );
    CHECK( sec_number_valid( duration, 0 ) && sec_number_valid( duration, 99999 ), "duration range" );
    CHECK( sec_number_valid( admin, 2 ) && sec_number_valid( admin, 5 ) && !sec_number_valid( admin, 0 ) && !sec_number_valid( admin, 3 ), "choices" );
    CHECK( sec_number_valid( secure, 0 ) && sec_number_valid( secure, 1 ) && !sec_number_valid( secure, 2 ), "flag" );

    /* every setting that applies names a template line and (for the registry) a key the writer takes */
    for (i = 0; i < sec_setting_count; i++)
    {
        const struct sec_setting *s = &sec_settings[i];

        CHECK( s->name && s->node && s->explain && *s->explain, "setting %u is complete", i );
        if (s->reason) continue;
        CHECK( s->key[0] && (!strcmp( s->section, "System Access" ) || !strcmp( s->section, "Registry Values" )), "%s: section", s->name );
        if (!strcmp( s->section, "Registry Values" ))
            CHECK( !strncmp( s->key, "MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\", 57 ), "%s: key in a policy tree", s->name );
        if (s->kind == SEC_NUMBER) CHECK( s->label && s->unit && s->min <= s->def && s->def <= s->max, "%s: number", s->name );
        if (s->kind == SEC_CHOICE) CHECK( s->choices && s->choices->text && sec_number_valid( s, s->def ), "%s: choices", s->name );
    }
}

int main(void)
{
    test_template();
    test_settings();
    if (failures) { printf( "%d failure(s)\n", failures ); return 1; }
    printf( "ok\n" );
    return 0;
}
