/*
 * secpol: the text of a security template (GptTmpl.inf), edited line by line
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

/* See template.h. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "template.h"

struct tmpl
{
    char **line;          /* the lines without their line ends */
    size_t count, room;
};

struct tmpl *tmpl_new(void)
{
    return calloc( 1, sizeof(struct tmpl) );
}

void tmpl_free( struct tmpl *t )
{
    size_t i;

    if (!t) return;
    for (i = 0; i < t->count; i++) free( t->line[i] );
    free( t->line );
    free( t );
}

/* a line is inserted before index at */
static int insert_line( struct tmpl *t, size_t at, const char *text, size_t length )
{
    char *copy;

    if (t->count == t->room)
    {
        size_t room = t->room ? t->room * 2 : 32;
        char **lines = realloc( t->line, room * sizeof(*lines) );

        if (!lines) return -1;
        t->line = lines;
        t->room = room;
    }
    if (!(copy = malloc( length + 1 ))) return -1;
    memcpy( copy, text, length );
    copy[length] = 0;
    memmove( t->line + at + 1, t->line + at, (t->count - at) * sizeof(*t->line) );
    t->line[at] = copy;
    t->count++;
    return 0;
}

int tmpl_load( struct tmpl *t, const char *text, size_t length )
{
    size_t start = 0, end;

    if (length >= 3 && !memcmp( text, "\xef\xbb\xbf", 3 )) { text += 3; length -= 3; }
    while (start < length)
    {
        for (end = start; end < length && text[end] != '\n'; end++) {}
        if (insert_line( t, t->count, text + start, end > start && text[end - 1] == '\r' ? end - start - 1 : end - start )) return -1;
        start = end + 1;
    }
    return 0;
}

/* names compare without case, ASCII only (the locale is not looked at) */
static int icmp( const char *a, const char *b, size_t n )
{
    for (; n; a++, b++, n--)
    {
        int x = (unsigned char)*a, y = (unsigned char)*b;

        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return x < y ? -1 : 1;
        if (!x) return 0;
    }
    return 0;
}

static const char *skip_blanks( const char *s )
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/* the name of a section line "[Name]" (blanks around it dropped) into buffer; 0 when the line is not one */
static int section_of( const char *line, char *buffer, size_t room )
{
    const char *end;
    size_t n;

    line = skip_blanks( line );
    if (*line != '[' || !(end = strchr( line, ']' ))) return 0;
    line = skip_blanks( line + 1 );
    while (end > line && (end[-1] == ' ' || end[-1] == '\t')) end--;
    n = end - line;
    if (n >= room) n = room - 1;
    memcpy( buffer, line, n );
    buffer[n] = 0;
    return 1;
}

/* the name of a line "name = value" (without blanks), its value start; 0 for a line that has no "=" or is a comment */
static int split_line( const char *line, size_t *name_start, size_t *name_length, const char **value )
{
    const char *eq, *s = skip_blanks( line );
    size_t n;

    if (*s == ';' || *s == '[' || !(eq = strchr( s, '=' ))) return 0;
    for (n = eq - s; n && (s[n - 1] == ' ' || s[n - 1] == '\t'); n--) {}
    *name_start = s - line;
    *name_length = n;
    *value = skip_blanks( eq + 1 );
    return 1;
}

/* the index of the line of NAME in SECTION, or -1; end is the index after the last line of the section (or count when
 * the section is not there, with *found cleared) */
static long find_line( const struct tmpl *t, const char *section, const char *name, size_t *end, int *found )
{
    char current[128];
    size_t i, n = strlen( name );
    long hit = -1;
    int in = 0;

    *found = 0;
    *end = t->count;
    for (i = 0; i < t->count; i++)
    {
        if (section_of( t->line[i], current, sizeof(current) ))
        {
            if (in) { *end = i; return hit; }
            in = !icmp( current, section, (size_t)-1 );
            if (in) *found = 1;
            continue;
        }
        if (in)
        {
            size_t start, length;
            const char *value;

            if (hit < 0 && split_line( t->line[i], &start, &length, &value ) && length == n &&
                !icmp( t->line[i] + start, name, n ))
                hit = (long)i;
        }
    }
    return hit;
}

int tmpl_get( const struct tmpl *t, const char *section, const char *name, char *value, size_t room )
{
    size_t end, start, length, n;
    const char *v;
    int found;
    long hit = find_line( t, section, name, &end, &found );

    if (hit < 0 || !room) return 0;
    split_line( t->line[hit], &start, &length, &v );
    n = strlen( v );
    while (n && (v[n - 1] == ' ' || v[n - 1] == '\t' || v[n - 1] == '\r')) n--;
    if (n >= room) n = room - 1;
    memcpy( value, v, n );
    value[n] = 0;
    return 1;
}

/* what a template with nothing in it is: setup reads a file without a [Version] as no template */
static const char empty_template[] = "[Unicode]\r\nUnicode=yes\r\n[Version]\r\nsignature=\"$CHICAGO$\"\r\nRevision=1\r\n";

int tmpl_set( struct tmpl *t, const char *section, const char *name, const char *value )
{
    char *text;
    size_t end, n = strlen( name ), at, version;
    int found;
    long hit;

    if (!t->count && tmpl_load( t, empty_template, sizeof(empty_template) - 1 )) return -1;
    if (!*section || !n || strpbrk( section, "\r\n[]" ) || strpbrk( name, "\r\n=" ) || strpbrk( value, "\r\n" )) return -1;
    hit = find_line( t, section, name, &end, &found );
    if (!(text = malloc( n + strlen( value ) + 4 ))) return -1;
    if (hit >= 0)
    {
        /* the name keeps the spelling it had */
        size_t start, length;
        const char *v;

        split_line( t->line[hit], &start, &length, &v );
        sprintf( text, "%.*s = %s", (int)length, t->line[hit] + start, value );
        free( t->line[hit] );
        t->line[hit] = text;
        return 0;
    }
    sprintf( text, "%s = %s", name, value );
    if (!found)
    {
        char header[160], current[128];

        if (strlen( section ) > 120) { free( text ); return -1; }
        /* a new section goes before [Version], which is last in the templates of Windows */
        at = t->count;
        for (version = 0; version < t->count; version++)
            if (section_of( t->line[version], current, sizeof(current) ) && !icmp( current, "Version", (size_t)-1 )) { at = version; break; }
        sprintf( header, "[%s]", section );
        if (insert_line( t, at, header, strlen( header ) )) { free( text ); return -1; }
        end = at + 1;
    }
    else
    {
        /* after the last line of the section that is not blank */
        while (end > 0 && !*skip_blanks( t->line[end - 1] )) end--;
    }
    n = insert_line( t, end, text, strlen( text ) );
    free( text );
    return n ? -1 : 0;
}

int tmpl_save( const struct tmpl *t, char **text, size_t *length )
{
    size_t i, size = 1;
    char *out, *p;

    if (!t->count)
    {
        if (!(out = malloc( sizeof(empty_template) ))) return -1;
        memcpy( out, empty_template, sizeof(empty_template) );
        *text = out;
        *length = sizeof(empty_template) - 1;
        return 0;
    }
    for (i = 0; i < t->count; i++) size += strlen( t->line[i] ) + 2;
    if (!(out = malloc( size ))) return -1;
    for (p = out, i = 0; i < t->count; i++)
    {
        size_t n = strlen( t->line[i] );

        memcpy( p, t->line[i], n );
        p += n;
        *p++ = '\r';
        *p++ = '\n';
    }
    *p = 0;
    *text = out;
    *length = p - out;
    return 0;
}
