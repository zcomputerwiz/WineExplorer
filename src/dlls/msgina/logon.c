/*
 * The classic "Log On to Windows" dialog
 *
 * XP's Welcome screen switches to it when Ctrl+Alt+Delete is pressed twice (logonui.exe calls logon_show_dialog).
 * The account's name is typed, so accounts without a tile log on here too: those hidden by SpecialAccounts\UserList
 * and the built-in Administrator in a normal start (docs/SAFE_MODE_WORKORDER.md C4 and C7). The dialog only asks;
 * logonui.exe does the logon through LightDM and posts the result back (wexlogon.h).
 *
 * The layout is XP's, measured on the XP VM; the template and the messages follow ReactOS's msgina
 * (dll/win32/msgina/gui.c and lang/en-US.rc, Copyright ReactOS Team, GPL-2.0-or-later). The banner is WineExplorer's
 * own, drawn here: no Windows art (CLAUDE.md).
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

#include <stdarg.h>
#include <stdio.h>

#include <windows.h>

#include "wine/debug.h"
#include "wexlogon.h"
#include "wexpower.h"
#include "resource.h"

WINE_DEFAULT_DEBUG_CHANNEL(msgina);

extern HINSTANCE msgina_instance;

/* XP's dialog stands above the screen's middle: its top is at 95 on an 800 x 600 screen (the one size measured),
 * which is where a template 411 pixels high would be centred. XP's template has rows a workgroup machine does not
 * show ("Log on to" and the like), so XP seems to centre the full template and then take those rows away. */
#define CENTRED_HEIGHT 411
#define BANNER_STRIPE  5      /* the banner's last rows: a stripe, light in the middle */
#define PASSWORD_MAX   127

struct logon
{
    WEX_LOGON_DIALOG *params;
    BOOL busy;
    HFONT product_font, small_font;
};

static void hgradient( HDC hdc, int left, int top, int right, int bottom, COLORREF from, COLORREF to )
{
    TRIVERTEX v[2];
    GRADIENT_RECT gr = { 0, 1 };

    v[0].x = left;
    v[0].y = top;
    v[0].Red = GetRValue( from ) << 8;
    v[0].Green = GetGValue( from ) << 8;
    v[0].Blue = GetBValue( from ) << 8;
    v[0].Alpha = 0;
    v[1].x = right;
    v[1].y = bottom;
    v[1].Red = GetRValue( to ) << 8;
    v[1].Green = GetGValue( to ) << 8;
    v[1].Blue = GetBValue( to ) << 8;
    v[1].Alpha = 0;
    GdiGradientFill( hdc, v, 2, &gr, 1, GRADIENT_FILL_RECT_H );
}

/* WineExplorer's banner: the Welcome screen's blues, Wine's logo (the interim logo, as on the Welcome screen), the
 * product's name and a copyright line where XP has its own */
static void draw_banner( const DRAWITEMSTRUCT *item, const struct logon *data )
{
    static const COLORREF dark = RGB(74, 110, 206), light = RGB(150, 186, 242), glow = RGB(240, 152, 76);
    HDC hdc = item->hDC;
    RECT rc = item->rcItem, text_rc;
    int w = rc.right - rc.left, h = rc.bottom - rc.top, field = h - BANNER_STRIPE, logo_x = rc.left + w * 27 / 100;
    WCHAR product[64], copyright[128], line[160];
    HICON logo;
    HGDIOBJ old;

    hgradient( hdc, rc.left, rc.top, rc.right, rc.top + field, dark, light );
    hgradient( hdc, rc.left, rc.top + field, rc.left + w / 2, rc.bottom, dark, glow );
    hgradient( hdc, rc.left + w / 2, rc.top + field, rc.right, rc.bottom, glow, light );

    if ((logo = LoadImageW( NULL, (const WCHAR *)IDI_WINLOGO, IMAGE_ICON, 48, 48, 0 )))
    {
        DrawIconEx( hdc, logo_x, rc.top + (field - 48) / 2 - 6, logo, 48, 48, 0, NULL, DI_NORMAL );
        DestroyIcon( logo );
    }
    SetBkMode( hdc, TRANSPARENT );
    SetTextColor( hdc, RGB(255, 255, 255) );
    LoadStringW( msgina_instance, IDS_LOGON_PRODUCT, product, ARRAY_SIZE(product) );
    SetRect( &text_rc, logo_x + 58, rc.top, rc.right - 8, rc.top + field - 12 );
    old = SelectObject( hdc, data->product_font );
    DrawTextW( hdc, product, -1, &text_rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX );
    LoadStringW( msgina_instance, IDS_LOGON_COPYRIGHT, copyright, ARRAY_SIZE(copyright) );
    swprintf( line, ARRAY_SIZE(line), L"Copyright \x00a9 2026\n%s", copyright );
    SetRect( &text_rc, rc.left + 5, rc.top + field - 32, rc.right - 8, rc.top + field - 3 );
    SelectObject( hdc, data->small_font );
    DrawTextW( hdc, line, -1, &text_rc, DT_LEFT | DT_BOTTOM | DT_NOPREFIX );
    SelectObject( hdc, old );
}

static HFONT make_font( int height, int weight )
{
    LOGFONTW lf = {0};

    lf.lfHeight = height;
    lf.lfWeight = weight;
    lf.lfQuality = ANTIALIASED_QUALITY;
    lstrcpyW( lf.lfFaceName, L"Tahoma" );
    return CreateFontIndirectW( &lf );
}

static void move_button( HWND dlg, int id, int x )   /* x in dialog units, on the buttons' row */
{
    RECT rc = { x, 108, x + 50, 122 };

    MapDialogRect( dlg, &rc );
    SetWindowPos( GetDlgItem( dlg, id ), NULL, rc.left, rc.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
}

/* Options >> shows Shut Down... where Cancel was and moves OK and Cancel one place left; Options << undoes it */
static void set_expanded( HWND dlg, BOOL expanded )
{
    WCHAR label[64];

    move_button( dlg, IDOK, expanded ? 55 : 109 );
    move_button( dlg, IDCANCEL, expanded ? 109 : 163 );
    ShowWindow( GetDlgItem( dlg, IDC_LOGON_SHUTDOWN ), expanded ? SW_SHOW : SW_HIDE );
    LoadStringW( msgina_instance, expanded ? IDS_LOGON_FEWER : IDS_LOGON_OPTIONS, label, ARRAY_SIZE(label) );
    SetDlgItemTextW( dlg, IDC_LOGON_OPTIONS, label );
}

static void place( HWND dlg )
{
    RECT rc;
    int cx = GetSystemMetrics( SM_CXSCREEN ), cy = GetSystemMetrics( SM_CYSCREEN ), w, h, y;

    GetWindowRect( dlg, &rc );
    w = rc.right - rc.left;
    h = rc.bottom - rc.top;
    y = (cy - CENTRED_HEIGHT) / 2;
    if (y < 0 || y + h > cy) y = (cy - h) / 2;
    SetWindowPos( dlg, NULL, (cx - w) / 2, max( y, 0 ), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
}

static void focus( HWND dlg, int id )
{
    SendMessageW( dlg, WM_NEXTDLGCTL, (WPARAM)GetDlgItem( dlg, id ), TRUE );
}

static void set_busy( HWND dlg, struct logon *data, BOOL busy )
{
    static const int ids[] = { IDC_LOGON_USER, IDC_LOGON_PASSWORD, IDOK, IDCANCEL, IDC_LOGON_SHUTDOWN, IDC_LOGON_OPTIONS };
    unsigned int i;

    data->busy = busy;
    for (i = 0; i < ARRAY_SIZE(ids); i++) EnableWindow( GetDlgItem( dlg, ids[i] ), !busy );
    SetCursor( LoadCursorW( NULL, (const WCHAR *)(busy ? IDC_WAIT : IDC_ARROW) ) );
}

/* XP's "Logon Message" (ReactOS: MB_ICONEXCLAMATION); the password is cleared for the next try */
static void failed( HWND dlg, struct logon *data, DWORD err )
{
    WCHAR title[64], text[512];
    BOOL wrong = err == ERROR_LOGON_FAILURE;

    set_busy( dlg, data, FALSE );
    LoadStringW( msgina_instance, IDS_LOGON_TITLE, title, ARRAY_SIZE(title) );
    LoadStringW( msgina_instance, wrong ? IDS_LOGON_FAILED : IDS_LOGON_SESSION, text, ARRAY_SIZE(text) );
    MessageBoxW( dlg, text, title, MB_OK | (wrong ? MB_ICONEXCLAMATION : MB_ICONERROR) );
    SetDlgItemTextW( dlg, IDC_LOGON_PASSWORD, L"" );
    focus( dlg, IDC_LOGON_PASSWORD );
}

static void begin( HWND dlg, struct logon *data )
{
    WCHAR user[WEX_LOGON_NAME_MAX], password[PASSWORD_MAX + 1];
    DWORD err;

    if (data->busy) return;
    GetDlgItemTextW( dlg, IDC_LOGON_USER, user, ARRAY_SIZE(user) );
    GetDlgItemTextW( dlg, IDC_LOGON_PASSWORD, password, ARRAY_SIZE(password) );
    err = data->params->logon( data->params->context, dlg, user, password );
    SecureZeroMemory( password, sizeof(password) );
    if (err) failed( dlg, data, err );
    else set_busy( dlg, data, TRUE );
}

static INT_PTR CALLBACK logon_proc( HWND dlg, UINT msg, WPARAM wparam, LPARAM lparam )
{
    struct logon *data = (struct logon *)GetWindowLongPtrW( dlg, DWLP_USER );

    switch (msg)
    {
    case WM_INITDIALOG:
        data = (struct logon *)lparam;
        SetWindowLongPtrW( dlg, DWLP_USER, (LONG_PTR)data );
        data->product_font = make_font( -30, FW_BOLD );
        data->small_font = make_font( -11, FW_NORMAL );
        SendDlgItemMessageW( dlg, IDC_LOGON_USER, EM_LIMITTEXT, WEX_LOGON_NAME_MAX - 1, 0 );
        SendDlgItemMessageW( dlg, IDC_LOGON_PASSWORD, EM_LIMITTEXT, PASSWORD_MAX, 0 );
        SetDlgItemTextW( dlg, IDC_LOGON_USER, data->params->user );
        set_expanded( dlg, FALSE );
        place( dlg );
        /* the last user's name is filled in and the password has the focus, as on XP */
        focus( dlg, data->params->user[0] ? IDC_LOGON_PASSWORD : IDC_LOGON_USER );
        return FALSE;

    case WM_DRAWITEM:
        if (wparam == IDC_LOGON_BANNER && data)
        {
            draw_banner( (const DRAWITEMSTRUCT *)lparam, data );
            return TRUE;
        }
        break;

    case WM_SETCURSOR:
        if (data && data->busy)
        {
            SetCursor( LoadCursorW( NULL, (const WCHAR *)IDC_WAIT ) );
            SetWindowLongPtrW( dlg, DWLP_MSGRESULT, TRUE );
            return TRUE;
        }
        break;

    case WM_WEX_LOGON_RESULT:
        if (!data || !data->busy) return TRUE;
        if (!wparam) EndDialog( dlg, IDOK );   /* the session is starting */
        else failed( dlg, data, (DWORD)wparam );
        return TRUE;

    case WM_COMMAND:
        if (HIWORD( wparam ) != BN_CLICKED) break;
        switch (LOWORD( wparam ))
        {
        case IDOK:
            begin( dlg, data );
            return TRUE;
        case IDCANCEL:   /* also Esc: back to the Welcome screen */
            if (!data->busy) EndDialog( dlg, IDCANCEL );
            return TRUE;
        case IDC_LOGON_OPTIONS:
            set_expanded( dlg, !IsWindowVisible( GetDlgItem( dlg, IDC_LOGON_SHUTDOWN ) ) );
            return TRUE;
        case IDC_LOGON_SHUTDOWN:
            power_mgmt_show_shutdown_dialog( dlg );
            return TRUE;
        }
        break;

    case WM_DESTROY:
        if (data)
        {
            DeleteObject( data->product_font );
            DeleteObject( data->small_font );
        }
        break;
    }
    return FALSE;
}

INT_PTR __cdecl logon_show_dialog( HWND owner, WEX_LOGON_DIALOG *params )
{
    struct logon data = { params };

    if (!params || params->size < sizeof(*params) || !params->logon) return -1;
    TRACE( "owner %p user %s\n", owner, debugstr_w( params->user ) );
    return DialogBoxParamW( msgina_instance, MAKEINTRESOURCEW( IDD_LOGON ), owner, logon_proc, (LPARAM)&data );
}
