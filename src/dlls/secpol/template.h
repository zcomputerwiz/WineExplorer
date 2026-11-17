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

/*
 * docs/GROUP_POLICY.md, "The local security settings editor". The template is kept as the lines it was read from, so
 * that changing one setting never drops a section or a line that the editor does not know (privilege rights, audit,
 * another tool's): only the line of the setting is replaced or added. Plain C on UTF-8 text, tested natively
 * (scripts/test_secpol_model.sh); the window converts to and from the file's UTF-16.
 */

#ifndef SECPOL_TEMPLATE_H
#define SECPOL_TEMPLATE_H

#include <stddef.h>

struct tmpl;

struct tmpl *tmpl_new(void);
void tmpl_free( struct tmpl *t );

/* the text of a template (lines ended by CR LF or LF); a byte order mark in front is skipped. 0, or -1 for memory. */
int tmpl_load( struct tmpl *t, const char *text, size_t length );

/* the value of NAME in [SECTION] (after the "=", without blanks around it; names and sections compare without case):
 * 1 when the line is there, else 0. A value longer than room is cut. */
int tmpl_get( const struct tmpl *t, const char *section, const char *name, char *value, size_t room );

/* the line "NAME = VALUE" in [SECTION]: replaced when it is there (the name keeps the spelling it had), else added at
 * the end of the section, which is made (before [Version], else at the end) when it is not. NAME and VALUE hold no
 * line break and NAME no "=". 0, or -1 for memory or a bad argument. */
int tmpl_set( struct tmpl *t, const char *section, const char *name, const char *value );

/* the template as text with CR LF line ends, malloc'd; a template with no line at all gets the minimal header
 * ([Unicode], [Version]) so that it is a template */
int tmpl_save( const struct tmpl *t, char **text, size_t *length );

#endif
