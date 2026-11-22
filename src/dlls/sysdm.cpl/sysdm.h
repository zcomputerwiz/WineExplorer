/*
 * WineExplorer Desktop Environment - sysdm.cpl Internal Header
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#ifndef __WINE_SYSDM_H
#define __WINE_SYSDM_H

#include <windows.h>
#include <commctrl.h>

#include "resource.h"

extern HINSTANCE g_hInstance;

INT_PTR CALLBACK general_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
INT_PTR CALLBACK compname_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
INT_PTR CALLBACK hardware_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
INT_PTR CALLBACK advanced_page_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

void show_performance_dialog(HWND parent);
void show_envvars_dialog(HWND parent);
void show_startrec_dialog(HWND parent);

/* the Computer Name page's join (compname.c) and the Network Identification Wizard (netid.c), which shares it */
extern WCHAR g_member[256];
extern BOOL g_joined;
extern BOOL g_reopen_elevated;
void read_membership(void);
void engine_message(WCHAR *text, DWORD chars);
void show_join_error(HWND owner, const WCHAR *caption, const WCHAR *domain, DWORD status, int what);
void offer_elevation(HWND owner, const WCHAR *caption);
void show_netid_wizard(HWND parent);

#endif /* __WINE_SYSDM_H */
