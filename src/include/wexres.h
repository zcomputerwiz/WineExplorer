/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 */
#ifndef WEX_RES_H
#define WEX_RES_H
#include <windows.h>

/* Immutable module identity for Win32 resource APIs; the DLL remains loaded. */
HMODULE WINAPI WexResModule(void);
HMODULE WINAPI WexResIconModule(void);
HICON shellres_icon(int id, int size);
HICON shellres_module_icon(const WCHAR *module, int id, int size);
HICON shellres_app_icon(const WCHAR *exe, int size);
HBITMAP shellres_bitmap(const WCHAR *module, int id, int fallback_id);
HBITMAP shellres_style_bitmap(int id);
void shellres_premultiply(HBITMAP bitmap);
HWND shellres_go_button(HWND parent, UINT id);
int shellres_go_width(HWND go);
void shellres_reset(void);
BOOL shellres_has_pack(const WCHAR *module);
BOOL shellres_sync_shell_icons(void);
void shellres_reload_shell_icons(void);
void shellfolder_open(int csidl, BOOL explore);
BOOL shellfolder_name(int csidl, WCHAR *name, int len);
HICON shellfolder_icon(int csidl, const WCHAR *state, int size);
const WCHAR *shellfolder_clsid(int csidl);
/* Style is caller state, never a pointer into the shell's settings. */
BOOL shellfolder_on_desktop_style(int csidl, int style);
void shellfolder_set_on_desktop_style(int csidl, BOOL show, int style);
int shellfolder_desktop_items_style(int *csidls, int max, int style);
void shellfolder_link_media_folders(void);
#endif
