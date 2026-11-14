/*
 * wexpolicy: what the parts of the program share
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

#ifndef WEXPOLICY_H
#define WEXPOLICY_H

#define MAX_FILES      64

void say( const char *format, ... );
void bump_serial( HKEY root, BOOL user );

/* sectmpl.c */
DWORD apply_security_templates( int count, WCHAR **paths );

#endif /* WEXPOLICY_H */
