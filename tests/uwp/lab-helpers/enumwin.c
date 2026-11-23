/*
 * Copyright 2026 WineExplorer contributors
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later
 * version. SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Lab tool: every top-level window of the desktop, with its children one level down: class, title, rectangle, visibility,
 * owning process and thread. usage: enumwin
 */
#include <windows.h>
#include <stdio.h>

static void show( HWND hwnd, int depth )
{
    WCHAR cls[128] = L"", title[128] = L"";
    RECT rect = { 0 };
    DWORD pid = 0, tid;

    GetClassNameW( hwnd, cls, 128 );
    GetWindowTextW( hwnd, title, 128 );
    GetWindowRect( hwnd, &rect );
    tid = GetWindowThreadProcessId( hwnd, &pid );
    printf( "%*s%p  %-6s pid %lu tid %lu  [%ls] \"%ls\"  %ld,%ld %ldx%ld style %08lx ex %08lx\n", depth * 2, "", hwnd,
            IsWindowVisible( hwnd ) ? "SHOWN" : "hidden", pid, tid, cls, title, rect.left, rect.top,
            rect.right - rect.left, rect.bottom - rect.top, (DWORD)GetWindowLongW( hwnd, GWL_STYLE ),
            (DWORD)GetWindowLongW( hwnd, GWL_EXSTYLE ) );
}

static BOOL CALLBACK child( HWND hwnd, LPARAM lparam )
{
    show( hwnd, 1 );
    return TRUE;
}

static BOOL CALLBACK top( HWND hwnd, LPARAM lparam )
{
    show( hwnd, 0 );
    EnumChildWindows( hwnd, child, 0 );
    return TRUE;
}

int main(void)
{
    EnumWindows( top, 0 );
    return 0;
}
