/*
 * The classic "Log On to Windows" dialog (msgina.dll) and its caller, the Welcome screen (logonui.exe)
 *
 * Copyright 2026 WineExplorer contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __WEX_LOGON_H
#define __WEX_LOGON_H

#include <windows.h>

#define WEX_LOGON_NAME_MAX 256

/* As on XP, msgina.dll owns the dialog and the caller does the logon (here logonui.exe, which speaks LightDM's greeter
 * protocol). OK calls logon(); a result of 0 means the logon has started and the dialog waits, disabled, for
 * WM_WEX_LOGON_RESULT. Another result fails the logon at once (ERROR_LOGON_FAILURE: XP's "Logon Message").
 * Shut Down... opens msgina's own shutdown dialog. */
typedef struct WEX_LOGON_DIALOG
{
    DWORD size;                       /* sizeof(WEX_LOGON_DIALOG) */
    WCHAR user[WEX_LOGON_NAME_MAX];   /* in: the User name box's text (the last user, as Winlogon's DefaultUserName) */
    void *context;
    DWORD (CALLBACK *logon)( void *context, HWND dialog, const WCHAR *user, const WCHAR *password );
} WEX_LOGON_DIALOG;

/* posted to the dialog by the caller: wparam 0, the session is starting (the caller ends the dialog with its process
 * or by WM_QUIT); otherwise a Win32 error, which shows XP's message for it and lets the user try again */
#define WM_WEX_LOGON_RESULT (WM_APP + 0x40)

/* IDOK when the session started, IDCANCEL for Cancel (back to the Welcome screen) */
typedef INT_PTR (__cdecl *WEX_LOGON_SHOW_DIALOG)( HWND owner, WEX_LOGON_DIALOG *params );
INT_PTR __cdecl logon_show_dialog( HWND owner, WEX_LOGON_DIALOG *params );

#endif /* __WEX_LOGON_H */
