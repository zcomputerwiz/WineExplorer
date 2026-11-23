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
#include "wexpreg.h"
#include "wexsectmpl.h"

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

/* The setup library that applies the template reads a few things that this editor does not edit around: a line that
 * ends in a backslash goes on in the next one (a line added at the end of such a file would be swallowed), Ctrl-Z ends
 * the file, and a NUL ends a line. A template that has any of them is refused (-2): the caller leaves it as it is. */
int tmpl_load( struct tmpl *t, const char *text, size_t length )
{
    size_t start = 0, end, n, i;
    int quoted;

    if (length >= 3 && !memcmp( text, "\xef\xbb\xbf", 3 )) { text += 3; length -= 3; }
    for (i = 0; i < length; i++) if (!text[i] || text[i] == 0x1a) return -2;
    while (start < length)
    {
        for (end = start; end < length && text[end] != '\n'; end++) {}
        n = end - start;
        if (n && text[start + n - 1] == '\r') n--;
        /* the end of what setup reads of the line: before a ";" comment (outside quotes), without the blanks */
        for (i = 0, quoted = 0; i < n; i++)
        {
            if (text[start + i] == '"') quoted = !quoted;
            else if (text[start + i] == ';' && !quoted) break;
        }
        for (; i && (text[start + i - 1] == ' ' || text[start + i - 1] == '\t'); i--) {}
        if (i && text[start + i - 1] == '\\') return -2;
        if (insert_line( t, t->count, text + start, n )) return -1;
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

/* The setup library merges every section of one name and applies the lines in order, so the last line of a name is the
 * one in force. The scan gives the index of the first and the last line of NAME in SECTION (-1 when there is none), and
 * where a new line of that section goes: after the last line that is not blank of the last section of that name; *found
 * says whether there is such a section. */
struct scan
{
    long first, last;
    size_t after;
    int found;
};

/* whether the name of a line (length bytes) is name: without case; in [Registry Values] as the registry reads the
 * path (wexpreg.h, as wexpolicy applies it), so a doubled backslash or a dotless i is the same line */
static int same_name( const char *section, const char *line_name, size_t length, const char *name )
{
    char want[512];
    size_t i, n = strlen( name );

    if (icmp( section, "Registry Values", (size_t)-1 ) || n >= sizeof(want))
        return length == n && !icmp( line_name, name, n );
    /* the canonical form: lower case, single backslashes */
    for (i = 0, n = 0; name[i]; i++)
    {
        char c = name[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c == '\\' && n && want[n - 1] == '\\') continue;
        want[n++] = c;
    }
    want[n] = 0;
    return preg_utf8_name_is( line_name, length, want );
}

static void scan_lines( const struct tmpl *t, const char *section, const char *name, struct scan *s )
{
    char current[128];
    size_t i, start, length;
    const char *value;
    int in = 0;

    s->first = s->last = -1;
    s->after = t->count;
    s->found = 0;
    for (i = 0; i < t->count; i++)
    {
        if (section_of( t->line[i], current, sizeof(current) ))
        {
            in = !icmp( current, section, (size_t)-1 );
            if (in) { s->found = 1; s->after = i + 1; }
            continue;
        }
        if (!in) continue;
        if (*skip_blanks( t->line[i] )) s->after = i + 1;
        if (split_line( t->line[i], &start, &length, &value ) && same_name( section, t->line[i] + start, length, name ))
        {
            if (s->first < 0) s->first = (long)i;
            s->last = (long)i;
        }
    }
}

/* the value of a line: after the "=", without the comment that the setup library cuts off (a ";" outside quotes) and
 * without the blanks around it */
int tmpl_get( const struct tmpl *t, const char *section, const char *name, char *value, size_t room )
{
    struct scan s;
    size_t start, length, n, i;
    const char *v;
    int quoted = 0;

    scan_lines( t, section, name, &s );
    if (s.last < 0 || !room) return 0;
    split_line( t->line[s.last], &start, &length, &v );
    for (n = 0; v[n]; n++)
    {
        if (v[n] == '"') quoted = !quoted;
        else if (v[n] == ';' && !quoted) break;
    }
    while (n && (v[n - 1] == ' ' || v[n - 1] == '\t' || v[n - 1] == '\r')) n--;
    if (n >= room) n = room - 1;
    for (i = 0; i < n; i++) value[i] = v[i];
    value[n] = 0;
    return 1;
}

/* what a template with nothing in it is: setup reads a file without a [Version] as no template */
static const char empty_template[] = WEX_SECTMPL_EMPTY;

int tmpl_set( struct tmpl *t, const char *section, const char *name, const char *value )
{
    char *text;
    struct scan s;
    size_t n = strlen( name ), at, version, i, start, length;
    const char *v;
    char current[128];

    if (!t->count && tmpl_load( t, empty_template, sizeof(empty_template) - 1 )) return -1;
    if (!*section || !n || strpbrk( section, "\r\n[]" ) || strpbrk( name, "\r\n=" ) || strpbrk( value, "\r\n" )) return -1;
    scan_lines( t, section, name, &s );
    if (s.first >= 0)
    {
        /* every line of the name, in every section of that name, gets the value (the name keeps the spelling it had) */
        int in = 0;

        for (i = 0; i < t->count; i++)
        {
            if (section_of( t->line[i], current, sizeof(current) )) { in = !icmp( current, section, (size_t)-1 ); continue; }
            if (!in || !split_line( t->line[i], &start, &length, &v ) || !same_name( section, t->line[i] + start, length, name )) continue;
            if (!(text = malloc( length + strlen( value ) + 4 ))) return -1;
            sprintf( text, "%.*s = %s", (int)length, t->line[i] + start, value );
            free( t->line[i] );
            t->line[i] = text;
        }
        return 0;
    }
    if (!(text = malloc( n + strlen( value ) + 4 ))) return -1;
    sprintf( text, "%s = %s", name, value );
    if (!s.found)
    {
        char header[160];

        if (strlen( section ) > 120) { free( text ); return -1; }
        /* a new section goes before [Version], which is last in the templates of Windows */
        at = t->count;
        for (version = 0; version < t->count; version++)
            if (section_of( t->line[version], current, sizeof(current) ) && !icmp( current, "Version", (size_t)-1 )) { at = version; break; }
        sprintf( header, "[%s]", section );
        if (insert_line( t, at, header, strlen( header ) )) { free( text ); return -1; }
        s.after = at + 1;
    }
    n = insert_line( t, s.after, text, strlen( text ) );
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
