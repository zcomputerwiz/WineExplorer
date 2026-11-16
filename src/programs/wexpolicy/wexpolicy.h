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

/* the service's control for a refresh, sent by gpupdate (a user-defined control, 128 to 255) */
#define GPSVC_CONTROL_REFRESH  128

/* HKLM: the serial numbers of the writer; Refreshed counts the finished refreshes (wexpolicy refreshed) */
#define MACHINE_SERIAL_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Group Policy\\WineExplorer Applied"

void say( const char *format, ... );
void bump_serial( HKEY root, BOOL user );
DWORD request_refresh(void);

/* sectmpl.c */
DWORD apply_security_templates( int count, WCHAR **paths );
DWORD check_security_template( const WCHAR *path );

/* store.c: the control code 129 (GPSVC_CONTROL_STORE, wexgpstore.h) of the service: the requests that the system group
 * left in the spool are checked and installed into the local store, and a refresh is asked for when one was */
DWORD process_spool(void);

/* gpolist.c */
DWORD record_gpo_list( const WCHAR *sid, const WCHAR *path );

#endif /* WEXPOLICY_H */
