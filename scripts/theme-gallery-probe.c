/*
 * WineExplorer Desktop Environment - visual style gallery probe (DWM P13)
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
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

/* Applies the visual style named by the first argument (a .msstyles path; "-" keeps the current one), then shows
 * two windows of common controls at 40,40 (active) and 520,40, 440x420 each, for 40 s. A second argument names a log
 * file. Build: x86_64-w64-mingw32-gcc -municode -mwindows -o theme-gallery-probe.exe theme-gallery-probe.c
 *              -luxtheme -lcomctl32 -lgdi32 -luser32 -ladvapi32 */

#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <stdio.h>

typedef HRESULT (WINAPI *OPEN)( LPCWSTR, LPCWSTR, LPCWSTR, HANDLE *, DWORD );
typedef HRESULT (WINAPI *APPLY)( HANDLE, char *, HWND );

static HWND child( HWND parent, const WCHAR *cls, const WCHAR *text, DWORD style, int x, int y, int w, int h )
{
    HWND hwnd = CreateWindowExW( 0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent, NULL, NULL, NULL );
    SendMessageW( hwnd, WM_SETFONT, (WPARAM)GetStockObject( DEFAULT_GUI_FONT ), FALSE );
    return hwnd;
}

static void fill( HWND hwnd, BOOL disabled )
{
    HWND h;
    TCITEMW tab = { TCIF_TEXT };
    LVCOLUMNW col = { LVCF_TEXT | LVCF_WIDTH };
    int i;

    child( hwnd, L"BUTTON", L"Button", BS_PUSHBUTTON, 10, 10, 90, 26 );
    child( hwnd, L"BUTTON", L"Default", BS_DEFPUSHBUTTON, 110, 10, 90, 26 );
    h = child( hwnd, L"BUTTON", L"Check box", BS_AUTOCHECKBOX, 10, 46, 120, 20 );
    SendMessageW( h, BM_SETCHECK, BST_CHECKED, 0 );
    h = child( hwnd, L"BUTTON", L"Radio", BS_AUTORADIOBUTTON, 140, 46, 100, 20 );
    SendMessageW( h, BM_SETCHECK, BST_CHECKED, 0 );
    child( hwnd, L"BUTTON", L"Group", BS_GROUPBOX, 250, 6, 170, 64 );
    child( hwnd, L"EDIT", L"Edit text", WS_BORDER | ES_AUTOHSCROLL, 10, 76, 190, 24 );
    h = child( hwnd, L"COMBOBOX", NULL, CBS_DROPDOWNLIST | WS_VSCROLL, 210, 76, 140, 200 );
    SendMessageW( h, CB_ADDSTRING, 0, (LPARAM)L"Combo box" );
    SendMessageW( h, CB_SETCURSEL, 0, 0 );
    h = child( hwnd, PROGRESS_CLASSW, NULL, 0, 10, 110, 410, 18 );
    SendMessageW( h, PBM_SETPOS, 60, 0 );
    h = child( hwnd, TRACKBAR_CLASSW, NULL, TBS_AUTOTICKS, 10, 134, 200, 30 );
    SendMessageW( h, TBM_SETPOS, TRUE, 40 );
    child( hwnd, L"SCROLLBAR", NULL, SBS_HORZ, 220, 140, 200, 17 );
    h = child( hwnd, WC_TABCONTROLW, NULL, 0, 10, 170, 410, 70 );
    tab.pszText = (WCHAR *)L"General";
    SendMessageW( h, TCM_INSERTITEMW, 0, (LPARAM)&tab );
    tab.pszText = (WCHAR *)L"Advanced";
    SendMessageW( h, TCM_INSERTITEMW, 1, (LPARAM)&tab );
    h = child( hwnd, WC_LISTVIEWW, NULL, LVS_REPORT | WS_BORDER, 10, 250, 410, 110 );
    for (i = 0; i < 2; i++)
    {
        col.pszText = (WCHAR *)(i ? L"Size" : L"Name");
        col.cx = 150;
        SendMessageW( h, LVM_INSERTCOLUMNW, i, (LPARAM)&col );
    }
    SendMessageW( h, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_FULLROWSELECT, LVS_EX_FULLROWSELECT );
    {
        LVITEMW item = { LVIF_TEXT | LVIF_STATE, 0, 0, LVIS_SELECTED, LVIS_SELECTED, (WCHAR *)L"Selected item" };
        SendMessageW( h, LVM_INSERTITEMW, 0, (LPARAM)&item );
    }
    if (disabled) EnableWindow( child( hwnd, L"BUTTON", L"Disabled", BS_PUSHBUTTON, 10, 366, 90, 26 ), FALSE );
}

int WINAPI wWinMain( HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show )
{
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES };
    HMODULE ux = LoadLibraryW( L"uxtheme.dll" );
    OPEN open = (OPEN)GetProcAddress( ux, MAKEINTRESOURCEA(2) );
    APPLY apply = (APPLY)GetProcAddress( ux, MAKEINTRESOURCEA(4) );
    WCHAR style[MAX_PATH] = L"", *log = NULL, *p;
    WCHAR name[MAX_PATH], color[64], size[64];
    HANDLE theme = NULL;
    HRESULT hr = S_FALSE;
    WNDCLASSW wc = { 0 };
    DWORD deadline, switch_at;
    WCHAR value[16];
    MSG msg;
    HWND a, b;

    /* arguments: style path (quoted or not), then an optional log path */
    while (*cmdline == ' ') cmdline++;
    if (*cmdline == '"') { cmdline++; if ((p = wcschr( cmdline, '"' ))) *p++ = 0; else p = cmdline + wcslen( cmdline ); }
    else if ((p = wcschr( cmdline, ' ' ))) *p++ = 0; else p = cmdline + wcslen( cmdline );
    lstrcpynW( style, cmdline, MAX_PATH );
    while (*p == ' ') p++;
    if (*p) log = p;
    if (log) _wfreopen( log, L"w", stdout );

    {
        /* themed controls need comctl32 6: activate it from a manifest written next to the temporary files */
        static const char manifest[] =
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\"><dependency>"
            "<dependentAssembly><assemblyIdentity type=\"win32\" name=\"Microsoft.Windows.Common-Controls\" "
            "version=\"6.0.0.0\" processorArchitecture=\"*\" publicKeyToken=\"6595b64144ccf1df\" language=\"*\"/>"
            "</dependentAssembly></dependency></assembly>";
        WCHAR path[MAX_PATH];
        ACTCTXW ctx = { sizeof(ctx) };
        ULONG_PTR cookie;
        HANDLE file, actctx;
        DWORD written;
        GetTempPathW( MAX_PATH, path );
        lstrcatW( path, L"theme-gallery.manifest" );
        file = CreateFileW( path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL );
        WriteFile( file, manifest, sizeof(manifest) - 1, &written, NULL );
        CloseHandle( file );
        ctx.lpSource = path;
        if ((actctx = CreateActCtxW( &ctx )) != INVALID_HANDLE_VALUE) ActivateActCtx( actctx, &cookie );
    }
    InitCommonControlsEx( &icc );
    if (style[0] && wcscmp( style, L"-" ) && open && apply)
    {
        hr = open( style, NULL, NULL, &theme, 0 );
        printf( "open %ls: %08lx\n", style, hr );
        if (SUCCEEDED(hr)) printf( "apply: %08lx\n", hr = apply( theme, NULL, NULL ) );
    }
    if (SUCCEEDED(GetCurrentThemeName( name, MAX_PATH, color, 64, size, 64 )))
        printf( "current: %ls %ls %ls\n", name, color, size );
    else printf( "current: none\n" );
    fflush( stdout );

    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = inst;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hCursor = LoadCursorW( NULL, (LPCWSTR)IDC_ARROW );
    wc.lpszClassName = L"ThemeGallery";
    RegisterClassW( &wc );
    b = CreateWindowExW( 0, wc.lpszClassName, L"Inactive gallery", WS_OVERLAPPEDWINDOW, 520, 40, 440, 420,
                         NULL, NULL, inst, NULL );
    fill( b, TRUE );
    ShowWindow( b, SW_SHOWNORMAL );
    a = CreateWindowExW( 0, wc.lpszClassName, L"Theme gallery", WS_OVERLAPPEDWINDOW, 40, 40, 440, 420,
                         NULL, NULL, inst, NULL );
    fill( a, FALSE );
    ShowWindow( a, SW_SHOWNORMAL );
    SetForegroundWindow( a );

    /* THEME_SWITCH=n in the environment: after 6 s set DwmFrameStyle to n and apply the style again (a live
     * switch between the Windows 10/11 and the Aero frame) */
    switch_at = GetEnvironmentVariableW( L"THEME_SWITCH", value, ARRAYSIZE(value) ) ? GetTickCount() + 6000 : 0;
    for (deadline = GetTickCount() + 40000; GetTickCount() < deadline; Sleep( 30 ))
    {
        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE ))
        {
            TranslateMessage( &msg );
            DispatchMessageW( &msg );
        }
        if (switch_at && GetTickCount() >= switch_at && theme)
        {
            DWORD style = _wtoi( value );
            RegSetKeyValueW( HKEY_CURRENT_USER, L"Software\\WineExplorer\\Shell", L"DwmFrameStyle", REG_DWORD,
                             &style, sizeof(style) );
            printf( "switch to frame style %lu: %08lx\n", style, apply( theme, NULL, NULL ) );
            fflush( stdout );
            switch_at = 0;
        }
    }
    return 0;
}
