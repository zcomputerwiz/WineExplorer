/*
 * Windows Logon User Interface Host (logonui.exe): the Welcome screen, as LightDM's greeter.
 *
 * LightDM checks the password (PAM) and starts the session; this program only asks.  It runs as the unprivileged
 * lightdm account with its own Wine prefix, started by wex-greeter.sh (docs/WINLOGON_AND_DWM.md, phases W2 and W3).
 * The protocol and the account list come from wexhost (WexGreeter*, WexUsersList).
 *
 * The screen follows Windows XP's Welcome screen: a dark band at the top and the bottom, the panel in between with
 * the logo and "To begin, click your user name" on the left and one tile per account on the right.  Clicking a tile
 * starts the logon at once; when PAM asks for a password the tile opens a password box (accounts without a
 * password log on directly, as on XP).  A wrong password shows XP's balloon.  The frame pictures are ReactOS's
 * (NT5design), the rest come from scripts/open_assets.py.
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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windef.h>
#include <winbase.h>
#include <winnls.h>
#include <wingdi.h>
#include <winuser.h>
#include <winreg.h>
#include <commctrl.h>
#include <objbase.h>
#include <gdiplus.h>

#include "wexhost.h"
#include "wexpower.h"
#include "resource.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(logonui);

#define WM_GREETER_EVENT (WM_APP + 1)   /* lparam: malloc'ed event text, freed by the receiver */
#define MAX_USERS 32

/* layout, in pixels, as measured on XP's Welcome screen */
#define TOP_BAND        78
#define BOTTOM_BAND     96
#define TILE_HEIGHT     72
#define TILE_ASK_HEIGHT 86
#define TILE_GAP        8
#define KEY_COLOR       RGB(255, 0, 255)
#define PANEL_COLOR     RGB(90, 126, 220)
#define BAND_COLOR      RGB(0, 48, 156)

enum state { IDLE, CHECKING, ASKING };
enum hot { HOT_NONE, HOT_TILE, HOT_GO, HOT_HELP, HOT_TURNOFF };

struct user
{
    char name[64];
    char session[64];    /* the session LightDM saved for the account (AccountsService's XSession), may be empty */
    WCHAR display[128];
    BOOL logged_on;
    HBITMAP picture;     /* 48 x 48, NULL: the default picture */
    RECT rect;           /* the tile */
};

static HINSTANCE instance;
static HWND main_window, password_edit, balloon, session_list;
static struct user users[MAX_USERS];
static unsigned int user_count;
static int selected = -1, hot_user = -1;
static enum hot hot;
static enum state state;
static char pending_user[64];
static WCHAR texts[16][256];
static HFONT font_begin, font_name, font_small, font_small_bold, font_turnoff, font_edit;
static HBITMAP bitmaps[128];
static ULONG_PTR gdiplus_token;
static RECT go_rect, help_rect, turnoff_rect;

static const WCHAR *text( UINT id )
{
    WCHAR *buf = texts[id - IDS_TITLE];
    if (!buf[0]) LoadStringW( instance, id, buf, ARRAY_SIZE(texts[0]) );
    return buf;
}

/* ------------------------------------------------------------------ pictures */

static HBITMAP bitmap( UINT id )
{
    if (!bitmaps[id - 100])
    {
        bitmaps[id - 100] = LoadImageW( instance, MAKEINTRESOURCEW(id), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION );
        /* 32-bit pictures (XP's own, when a local build has them) carry alpha: premultiply once for AlphaBlend */
        if (bitmaps[id - 100])
        {
            DIBSECTION ds;
            if (GetObjectW( bitmaps[id - 100], sizeof(ds), &ds ) && ds.dsBm.bmBitsPixel == 32 && ds.dsBm.bmBits)
            {
                DWORD *px = ds.dsBm.bmBits;
                int i, n = ds.dsBm.bmWidth * abs( ds.dsBm.bmHeight );
                for (i = 0; i < n; i++)
                {
                    DWORD a = px[i] >> 24;
                    px[i] = (a << 24) | ((((px[i] >> 16) & 255) * a / 255) << 16) |
                            ((((px[i] >> 8) & 255) * a / 255) << 8) | ((px[i] & 255) * a / 255);
                }
            }
        }
    }
    return bitmaps[id - 100];
}

/* draws a picture at x, y (stretched to w x h when they are not 0): alpha when it has it, else magenta is clear;
 * an opacity below 255 blends a picture without alpha over what is there */
static void draw_blended( HDC hdc, HBITMAP bmp, int x, int y, int w, int h, BYTE opacity )
{
    BITMAP bm;
    HDC mem;
    HGDIOBJ old;

    if (!bmp || !GetObjectW( bmp, sizeof(bm), &bm )) return;
    if (!w) w = bm.bmWidth;
    if (!h) h = bm.bmHeight;
    mem = CreateCompatibleDC( hdc );
    old = SelectObject( mem, bmp );
    if (bm.bmBitsPixel == 32)
    {
        BLENDFUNCTION blend = { AC_SRC_OVER, 0, opacity, AC_SRC_ALPHA };
        GdiAlphaBlend( hdc, x, y, w, h, mem, 0, 0, bm.bmWidth, bm.bmHeight, blend );
    }
    else if (opacity < 255)
    {
        BLENDFUNCTION blend = { AC_SRC_OVER, 0, opacity, 0 };
        GdiAlphaBlend( hdc, x, y, w, h, mem, 0, 0, bm.bmWidth, bm.bmHeight, blend );
    }
    else GdiTransparentBlt( hdc, x, y, w, h, mem, 0, 0, bm.bmWidth, bm.bmHeight, KEY_COLOR );
    SelectObject( mem, old );
    DeleteDC( mem );
}

static void draw( HDC hdc, HBITMAP bmp, int x, int y, int w, int h )
{
    draw_blended( hdc, bmp, x, y, w, h, 255 );
}

/* the account picture (AccountsService IconFile, usually a PNG) scaled to 48 x 48 */
static HBITMAP load_picture( const char *unix_path )
{
    WCHAR path[MAX_PATH + 16] = L"\\\\?\\unix";
    GpBitmap *src = NULL, *dst = NULL;
    GpGraphics *g = NULL;
    HBITMAP result = NULL;
    int len = lstrlenW( path );

    if (!unix_path[0] || unix_path[0] != '/') return NULL;
    MultiByteToWideChar( CP_UTF8, 0, unix_path, -1, path + len, ARRAY_SIZE(path) - len );
    for (WCHAR *p = path + len; *p; p++) if (*p == '/') *p = '\\';
    if (GdipCreateBitmapFromFile( path, &src ) != Ok) return NULL;
    if (GdipCreateBitmapFromScan0( 48, 48, 0, PixelFormat32bppARGB, NULL, &dst ) == Ok &&
        GdipGetImageGraphicsContext( (GpImage *)dst, &g ) == Ok)
    {
        GdipSetInterpolationMode( g, InterpolationModeHighQualityBicubic );
        GdipDrawImageRectI( g, (GpImage *)src, 0, 0, 48, 48 );
        GdipDeleteGraphics( g );
        GdipCreateHBITMAPFromBitmap( dst, &result, 0xff000000 );
    }
    if (dst) GdipDisposeImage( (GpImage *)dst );
    GdipDisposeImage( (GpImage *)src );
    return result;
}

/* ------------------------------------------------------------------ accounts */

/* The accounts XP's Welcome screen leaves out: a REG_DWORD value of 0 named after the account (1 shows it) */
static const WCHAR special_accounts[] =
    L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon\\SpecialAccounts\\UserList";

/* Safe Mode: SM_CLEANBOOT once Wine reports it (docs/SAFE_MODE_WORKORDER.md B2); until then wex-greeter.sh passes
 * the mode in WEXMODE, as the session scripts do for Explorer */
static BOOL safe_mode( void )
{
    WCHAR mode[16];

    if (GetSystemMetrics( SM_CLEANBOOT )) return TRUE;
    if (!GetEnvironmentVariableW( L"WEXMODE", mode, ARRAY_SIZE(mode) )) return FALSE;
    return !lstrcmpiW( mode, L"safe" ) || !lstrcmpiW( mode, L"safenet" ) || !lstrcmpiW( mode, L"safecmd" );
}

/* A value in SpecialAccounts\UserList decides. Without one, the built-in Administrator (the account named
 * administrator, docs/SAFE_MODE_WORKORDER.md C4) shows only in Safe Mode, as on XP Professional. */
static BOOL user_hidden( HKEY list, const char *name, BOOL safe )
{
    WCHAR wname[64];
    DWORD value = 1, size = sizeof(value), type;

    if (list && MultiByteToWideChar( CP_UTF8, 0, name, -1, wname, ARRAY_SIZE(wname) ) &&
        !RegQueryValueExW( list, wname, NULL, &type, (BYTE *)&value, &size ) && type == REG_DWORD &&
        size == sizeof(value))
        return value == 0;
    return !safe && !lstrcmpiA( name, "administrator" );
}

/* by the name on the tile, ignoring case as XP does, then by account name */
static int __cdecl compare_users( const void *a, const void *b )
{
    const struct user *x = a, *y = b;
    int order = CompareStringW( LOCALE_USER_DEFAULT, NORM_IGNORECASE, x->display, -1, y->display, -1 ) - CSTR_EQUAL;

    return order ? order : lstrcmpiA( x->name, y->name );
}

static void load_users( void )
{
    char *buffer = NULL, *line, *next;
    DWORD needed = 0, size = 4096;
    HKEY list = NULL;
    BOOL safe = safe_mode();

    user_count = 0;
    for (;;)
    {
        DWORD err;
        free( buffer );
        if (!(buffer = malloc( size ))) return;
        err = WexUsersList( buffer, size, &needed );
        if (err == ERROR_MORE_DATA && needed > size) { size = needed; continue; }
        if (err) { WARN( "no account list: %lu\n", err ); free( buffer ); return; }
        break;
    }
    if (RegOpenKeyExW( HKEY_LOCAL_MACHINE, special_accounts, 0, KEY_READ, &list )) list = NULL;
    for (line = buffer; *line && user_count < MAX_USERS; line = next)
    {
        char *field[5] = { line }, *p = line;
        struct user *u = &users[user_count];
        int n = 1;

        if ((next = strchr( line, '\n' ))) *next++ = 0;
        else next = line + strlen( line );
        while (n < 5 && (p = strchr( p, '\t' ))) { *p++ = 0; field[n++] = p; }
        if (n < 3) continue;
        if (user_hidden( list, field[0], safe ))
        {
            TRACE( "%s is hidden (SpecialAccounts\\UserList or the Administrator rule)\n", debugstr_a(field[0]) );
            continue;
        }
        lstrcpynA( u->name, field[0], sizeof(u->name) );
        MultiByteToWideChar( CP_UTF8, 0, *field[1] ? field[1] : field[0], -1, u->display, ARRAY_SIZE(u->display) );
        u->logged_on = n > 3 && field[3][0] == '1';
        lstrcpynA( u->session, n > 4 ? field[4] : "", sizeof(u->session) );
        u->picture = load_picture( field[2] );
        user_count++;
    }
    if (list) RegCloseKey( list );
    free( buffer );
    /* XP lists the tiles alphabetically (measured 2026-11-11, docs/SAFE_MODE_WORKORDER.md C7); AccountsService
     * returns them in no set order */
    qsort( users, user_count, sizeof(users[0]), compare_users );
}

/* ------------------------------------------------------------------ layout */

static int middle_top( void ) { return TOP_BAND + 2; }
static int middle_bottom( int cy ) { return cy - BOTTOM_BAND; }

/* Screens narrower than 800 pixels, such as Safe Mode's 640x480, get XP's compact layout, measured on the XP VM at
 * 640x480 (2026-11-11): the logo and "To begin" above the list instead of beside it, the list from the left behind
 * a shorter divider, no shine, and the hint further left. The offsets below are XP's at 640x480. */
static BOOL compact( int cx ) { return cx < 800; }

static void layout( void )
{
    RECT rc;
    int cx, cy, total = 0, y, x;
    unsigned int i;

    GetClientRect( main_window, &rc );
    cx = rc.right;
    cy = rc.bottom;
    for (i = 0; i < user_count; i++)
        total += (i == selected && state == ASKING ? TILE_ASK_HEIGHT : TILE_HEIGHT) + (i ? TILE_GAP : 0);
    if (compact( cx ))
    {
        /* the first picture frame's white border at 165,244, then one tile every 80 rows */
        x = cx / 2 - 165;
        y = middle_top() + 155;
    }
    else
    {
        x = cx / 2 + 25;
        y = (middle_top() + middle_bottom( cy )) / 2 - total / 2;
        if (y < middle_top() + 12) y = middle_top() + 12;
    }
    for (i = 0; i < user_count; i++)
    {
        int h = i == selected && state == ASKING ? TILE_ASK_HEIGHT : TILE_HEIGHT;
        SetRect( &users[i].rect, x, y, cx, y + h );
        y += h + TILE_GAP;
    }
    SetRectEmpty( &go_rect );
    SetRectEmpty( &help_rect );
    if (selected >= 0 && state == ASKING)
    {
        RECT *t = &users[selected].rect;
        int ex = t->left + 74, ey = t->top + 52;
        SetRect( &go_rect, ex + 171 + 6, ey, ex + 171 + 6 + 26, ey + 26 );
        SetRect( &help_rect, go_rect.right + 6, ey, go_rect.right + 6 + 26, ey + 26 );
        MoveWindow( password_edit, ex + 6, ey + 5, 159, 16, FALSE );
    }
    SetRect( &turnoff_rect, 24, middle_bottom( cy ) + 22, 24 + 26 + 8 + 190, middle_bottom( cy ) + 22 + 26 );
    /* the session list (WineExplorer's own; XP has none): below "To begin", or in the compact layout's empty top
     * left corner, clear of the logo and the list */
    if (session_list && compact( cx ))
        SetWindowPos( session_list, NULL, 16, middle_top() + 16, 250, 180, SWP_NOZORDER | SWP_NOACTIVATE );
    else if (session_list)
        SetWindowPos( session_list, NULL, 24, middle_bottom( cy ) - 44, 250, 180, SWP_NOZORDER | SWP_NOACTIVATE );
}

/* ------------------------------------------------------------------ painting */

static void text_out( HDC hdc, HFONT font, const WCHAR *s, RECT *rc, UINT flags )
{
    HGDIOBJ old = SelectObject( hdc, font );
    DrawTextW( hdc, s, -1, rc, flags | DT_NOPREFIX );
    SelectObject( hdc, old );
}

static void paint_frame( HDC hdc, int cx, int cy )
{
    RECT rc;
    HBRUSH brush;
    BITMAP bm;
    TRIVERTEX vert[2];
    GRADIENT_RECT gr = { 0, 1 };
    int mb = middle_bottom( cy ), dy;

    SetRect( &rc, 0, 0, cx, TOP_BAND );
    FillRect( hdc, &rc, brush = CreateSolidBrush( BAND_COLOR ) );
    DeleteObject( brush );
    draw( hdc, bitmap( IDB_TOPSTRIP ), 0, TOP_BAND, cx, 2 );

    SetRect( &rc, 0, middle_top(), cx, mb );
    FillRect( hdc, &rc, brush = CreateSolidBrush( PANEL_COLOR ) );
    DeleteObject( brush );
    if (compact( cx ))   /* no shine; a fainter divider left of the list, rows 209 to 382 of 480, 145,174,238 at most */
        draw_blended( hdc, bitmap( IDB_DIVIDER ), cx / 2 - 192, middle_top() + 129, 1, mb - middle_top() - 130, 90 );
    else
    {
        draw( hdc, bitmap( IDB_SHINE ), 0, middle_top(), 0, 0 );
        if (GetObjectW( bitmap( IDB_DIVIDER ), sizeof(bm), &bm ))
        {
            int h = min( bm.bmHeight, mb - middle_top() - 20 );
            dy = middle_top() + (mb - middle_top() - h) / 2;
            draw( hdc, bitmap( IDB_DIVIDER ), cx / 2, dy, 1, h );
        }
    }

    draw( hdc, bitmap( IDB_BOTTOMSTRIP ), 0, mb, cx, 3 );
    vert[0].x = 0;  vert[0].y = mb + 3; vert[0].Red = 0x3900; vert[0].Green = 0x3400; vert[0].Blue = 0xae00; vert[0].Alpha = 0;
    vert[1].x = cx; vert[1].y = cy;     vert[1].Red = 0x0000; vert[1].Green = 0x3000; vert[1].Blue = 0x9600; vert[1].Alpha = 0;
    GdiGradientFill( hdc, vert, 2, &gr, 1, GRADIENT_FILL_RECT_H );
}

static void paint_tile( HDC hdc, unsigned int i )
{
    struct user *u = &users[i];
    RECT *t = &u->rect, rc;
    BOOL sel = i == selected, lit = sel || i == hot_user;

    if (sel) draw( hdc, bitmap( IDB_TILE ), t->left, t->top, 0, t->bottom - t->top );
    draw( hdc, bitmap( lit ? IDB_FRAME_HOT : IDB_FRAME ), t->left + 8, t->top + 7, 0, 0 );
    draw( hdc, u->picture ? u->picture : bitmap( IDB_USER ), t->left + 13, t->top + 12, 48, 48 );

    SetRect( &rc, t->left + 74, t->top + 8, t->right - 8, t->top + 34 );
    text_out( hdc, font_name, u->display, &rc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS );
    if (sel && state == ASKING)
    {
        SetRect( &rc, t->left + 74, t->top + 35, t->right - 8, t->top + 50 );
        text_out( hdc, font_small, text( IDS_TYPE_PASSWORD ), &rc, DT_LEFT | DT_SINGLELINE );
        draw( hdc, bitmap( IDB_EDITFRAME ), t->left + 74, t->top + 52, 0, 0 );
        draw( hdc, bitmap( hot == HOT_GO ? IDB_GO_HOT : IDB_GO ), go_rect.left, go_rect.top, 0, 0 );
        draw( hdc, bitmap( hot == HOT_HELP ? IDB_HELP_HOT : IDB_HELP ), help_rect.left, help_rect.top, 0, 0 );
    }
    else if (u->logged_on)
    {
        SetRect( &rc, t->left + 74, t->top + 33, t->right - 8, t->top + 48 );
        text_out( hdc, font_small_bold, text( IDS_LOGGED_ON ), &rc, DT_LEFT | DT_SINGLELINE );
    }
}

static void paint( HDC hdc )
{
    RECT rc, client;
    BITMAP bm;
    int cx, cy, mb;
    unsigned int i;

    GetClientRect( main_window, &client );
    cx = client.right;
    cy = client.bottom;
    mb = middle_bottom( cy );
    paint_frame( hdc, cx, cy );
    SetBkMode( hdc, TRANSPARENT );
    SetTextColor( hdc, RGB(255, 255, 255) );

    /* the logo, and "To begin" 25 pixels left of its right edge and 20 below it: beside the list, or in the compact
     * layout above it, the logo's right edge at 490 and its bottom at 166 */
    if (compact( cx ))
    {
        if (GetObjectW( bitmap( IDB_LOGO ), sizeof(bm), &bm ))
            draw( hdc, bitmap( IDB_LOGO ), cx / 2 + 170 - bm.bmWidth, middle_top() + 86 - bm.bmHeight, 0, 0 );
        SetRect( &rc, 0, middle_top() + 106, cx / 2 + 146, middle_top() + 138 );
    }
    else
    {
        if (GetObjectW( bitmap( IDB_LOGO ), sizeof(bm), &bm ))
            draw( hdc, bitmap( IDB_LOGO ), cx / 2 - 20 - bm.bmWidth, cy / 2 + 18 - bm.bmHeight, 0, 0 );
        SetRect( &rc, 0, cy / 2 + 38, cx / 2 - 45, cy / 2 + 70 );
    }
    text_out( hdc, font_begin, text( IDS_BEGIN ), &rc, DT_RIGHT | DT_SINGLELINE );

    for (i = 0; i < user_count; i++) paint_tile( hdc, i );

    draw( hdc, bitmap( hot == HOT_TURNOFF ? IDB_TURNOFF_HOT : IDB_TURNOFF ), turnoff_rect.left, turnoff_rect.top, 0, 0 );
    SetRect( &rc, turnoff_rect.left + 34, turnoff_rect.top, turnoff_rect.right + 40, turnoff_rect.bottom );
    text_out( hdc, font_turnoff, text( IDS_TURNOFF ), &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE );
    SetRect( &rc, compact( cx ) ? cx / 2 - 10 : cx / 2 + 70, mb + 25, cx - 20, cy - 10 );
    text_out( hdc, font_small, text( IDS_ACCOUNTS_HINT ), &rc, DT_LEFT );
}

static void repaint( void )
{
    layout();
    InvalidateRect( main_window, NULL, FALSE );
}

/* ------------------------------------------------------------------ logon */

static void hide_balloon( void )
{
    TTTOOLINFOW ti = { sizeof(ti) };
    if (!balloon) return;
    ti.hwnd = main_window;
    ti.uId = (UINT_PTR)password_edit;
    SendMessageW( balloon, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti );
}

static void show_balloon( void )
{
    TTTOOLINFOW ti = { sizeof(ti) };
    RECT rc;

    if (!balloon) return;
    ti.hwnd = main_window;
    ti.uId = (UINT_PTR)password_edit;
    GetWindowRect( password_edit, &rc );
    SendMessageW( balloon, TTM_SETTITLEW, TTI_ERROR, (LPARAM)text( IDS_FORGOT_TITLE ) );
    ti.lpszText = (WCHAR *)text( IDS_FORGOT_TEXT );
    SendMessageW( balloon, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti );
    SendMessageW( balloon, TTM_TRACKPOSITION, 0, MAKELPARAM( rc.left + 20, rc.bottom ) );
    SendMessageW( balloon, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti );
}

static void set_state( enum state s )
{
    state = s;
    ShowWindow( password_edit, s == ASKING ? SW_SHOW : SW_HIDE );
    if (s == ASKING) SetFocus( password_edit );
    else SetFocus( main_window );
    if (s != ASKING) hide_balloon();
    repaint();
}

/* The session list follows the account clicked: its own saved session, as LightDM's greeters do. Without this a
 * logon took whatever the list showed (X11 at first), and LightDM then saved that as the account's choice. An account
 * with none saved gets the first entry, LightDM's user-session (X11). The list can still be changed before the
 * password goes. */
static void show_session( int i )
{
    if (session_list)
        SendMessageW( session_list, CB_SETCURSEL, !strcmp( users[i].session, "wine-explorer-wayland" ) ? 1 : 0, 0 );
}

/* starts LightDM's authentication of user i; PAM asks for the password (or does not, for an account without one) */
static void begin_logon( int i )
{
    DWORD sequence;

    if (state != IDLE) WexGreeterCancel();
    hide_balloon();
    selected = i;
    show_session( i );
    SetWindowTextW( password_edit, L"" );
    lstrcpynA( pending_user, users[i].name, sizeof(pending_user) );
    set_state( CHECKING );
    if (WexGreeterAuthenticate( pending_user, &sequence ))
    {
        WARN( "authenticate failed\n" );
        selected = -1;
        set_state( IDLE );
    }
}

static void cancel_logon( void )
{
    if (state != IDLE) WexGreeterCancel();
    selected = -1;
    set_state( IDLE );
}

static void send_password( void )
{
    WCHAR password[256];
    char secret[512];

    if (state != ASKING || !IsWindowEnabled( password_edit )) return;
    GetWindowTextW( password_edit, password, ARRAY_SIZE(password) );
    WideCharToMultiByte( CP_UTF8, 0, password, -1, secret, sizeof(secret), NULL, NULL );
    SecureZeroMemory( password, sizeof(password) );
    SetWindowTextW( password_edit, L"" );
    hide_balloon();
    EnableWindow( password_edit, FALSE );   /* the tile stays open while LightDM checks */
    WexGreeterRespond( secret );
    SecureZeroMemory( secret, sizeof(secret) );
}

/* one event from LightDM (see dlls/wexhost/greeter.c for the format) */
static void handle_event( char *msg )
{
    char *body = strchr( msg, '\n' );
    unsigned int seq, result;

    TRACE( "%s\n", debugstr_a( msg ) );
    if (!strncmp( msg, "prompt ", 7 ))
    {
        char *line = body ? body + 1 : NULL;
        while (line && *line)
        {
            char *next = strchr( line, '\n' );
            unsigned int style = atoi( line );
            if (next) *next++ = 0;
            if (style == 1)                        /* secret: show the password box */
            {
                EnableWindow( password_edit, TRUE );
                set_state( ASKING );
            }
            else if (style == 2) WexGreeterRespond( pending_user );   /* PAM asks for the user name again */
            line = next;
        }
        return;
    }
    if (sscanf( msg, "end %u %u", &seq, &result ) == 2)
    {
        if (result == 0)
        {
            WexGreeterStartSession( session_list ?
                (SendMessageW( session_list, CB_GETCURSEL, 0, 0 ) == 1 ?
                 "wine-explorer-wayland" : "wine-explorer") : NULL );
            return;
        }
        /* wrong password: XP keeps the tile open and shows its balloon; LightDM needs a new authentication */
        EnableWindow( password_edit, TRUE );
        if (selected >= 0)
        {
            DWORD sequence;
            WexGreeterAuthenticate( pending_user, &sequence );
            set_state( ASKING );
            show_balloon();
        }
        return;
    }
    if (sscanf( msg, "session %u", &result ) == 1)
    {
        if (result == 0) PostQuitMessage( 0 );   /* LightDM takes over the display */
        else
        {
            MessageBoxW( main_window, text( IDS_SESSION_FAILED ), text( IDS_TITLE ), MB_OK | MB_ICONERROR );
            cancel_logon();
        }
    }
}

static DWORD WINAPI reader_thread( void *arg )
{
    char buffer[8192];
    for (;;)
    {
        DWORD err = WexGreeterRead( buffer, sizeof(buffer), INFINITE );
        if (err == ERROR_SUCCESS || err == ERROR_MORE_DATA)
        {
            char *copy = strdup( buffer );
            if (copy && !PostMessageW( main_window, WM_GREETER_EVENT, 0, (LPARAM)copy )) free( copy );
            continue;
        }
        WARN( "greeter connection ended: %lu\n", err );
        PostMessageW( main_window, WM_CLOSE, 0, 0 );
        return 0;
    }
}

static void turn_off( HWND owner )
{
    HMODULE module = LoadLibraryW(L"msgina.dll");
    WEX_POWER_SHOW_DIALOG show = module ?
        (WEX_POWER_SHOW_DIALOG)GetProcAddress(module, "power_mgmt_show_shutdown_dialog") : NULL;
    if (show) show(owner);
    if (module) FreeLibrary(module);
}

/* ------------------------------------------------------------------ window */

static enum hot hit_test( POINT pt, int *user )
{
    unsigned int i;
    *user = -1;
    if (PtInRect( &go_rect, pt )) return HOT_GO;
    if (PtInRect( &help_rect, pt )) return HOT_HELP;
    if (PtInRect( &turnoff_rect, pt )) return HOT_TURNOFF;
    for (i = 0; i < user_count; i++)
        if (PtInRect( &users[i].rect, pt )) { *user = i; return HOT_TILE; }
    return HOT_NONE;
}

static LRESULT CALLBACK edit_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    WNDPROC old = (WNDPROC)GetWindowLongPtrW( hwnd, GWLP_USERDATA );
    if (msg == WM_KEYDOWN && wparam == VK_RETURN) { send_password(); return 0; }
    if (msg == WM_KEYDOWN && wparam == VK_ESCAPE) { cancel_logon(); return 0; }
    if (msg == WM_CHAR && (wparam == '\r' || wparam == 27)) return 0;
    if (msg == WM_CHAR) hide_balloon();
    return CallWindowProcW( old, hwnd, msg, wparam, lparam );
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

static LRESULT CALLBACK main_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    switch (msg)
    {
    case WM_CREATE:
    {
        TTTOOLINFOW ti = { sizeof(ti) };
        main_window = hwnd;
        if (GetFileAttributesW( L"Z:\\usr\\share\\wayland-sessions\\wine-explorer-wayland.desktop" ) != INVALID_FILE_ATTRIBUTES)
        {
            session_list = CreateWindowExW( 0, WC_COMBOBOXW, NULL,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                0, 0, 250, 180, hwnd, NULL, instance, NULL );
            SendMessageW( session_list, WM_SETFONT, (WPARAM)font_edit, FALSE );
            SendMessageW( session_list, CB_ADDSTRING, 0, (LPARAM)L"WineExplorer (X11)" );
            SendMessageW( session_list, CB_ADDSTRING, 0, (LPARAM)L"WineExplorer (Wayland)" );
            SendMessageW( session_list, CB_SETCURSEL, 0, 0 );
        }
        password_edit = CreateWindowExW( 0, L"EDIT", NULL, WS_CHILD | ES_PASSWORD | ES_AUTOHSCROLL, 0, 0, 10, 10, hwnd,
                                         NULL, instance, NULL );
        SendMessageW( password_edit, WM_SETFONT, (WPARAM)font_edit, FALSE );
        SetWindowLongPtrW( password_edit, GWLP_USERDATA,
                           SetWindowLongPtrW( password_edit, GWLP_WNDPROC, (LONG_PTR)edit_proc ) );
        balloon = CreateWindowExW( WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_BALLOON | TTS_NOPREFIX,
                                   0, 0, 0, 0, hwnd, NULL, instance, NULL );
        ti.uFlags = TTF_IDISHWND | TTF_TRACK | TTF_ABSOLUTE;
        ti.hwnd = hwnd;
        ti.uId = (UINT_PTR)password_edit;
        ti.lpszText = (WCHAR *)L"";
        SendMessageW( balloon, TTM_ADDTOOLW, 0, (LPARAM)&ti );
        SendMessageW( balloon, TTM_SETMAXTIPWIDTH, 0, 300 );
        return 0;
    }
    case WM_GREETER_EVENT:
        handle_event( (char *)lparam );
        free( (char *)lparam );
        return 0;
    case WM_SIZE:
        layout();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        RECT rc;
        HDC hdc = BeginPaint( hwnd, &ps ), mem;
        HBITMAP buf;
        HGDIOBJ old;
        GetClientRect( hwnd, &rc );
        mem = CreateCompatibleDC( hdc );
        buf = CreateCompatibleBitmap( hdc, rc.right, rc.bottom );
        old = SelectObject( mem, buf );
        paint( mem );
        BitBlt( hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY );
        SelectObject( mem, old );
        DeleteObject( buf );
        DeleteDC( mem );
        EndPaint( hwnd, &ps );
        return 0;
    }
    case WM_CTLCOLOREDIT:
        SetBkColor( (HDC)wparam, RGB(255, 255, 255) );
        return (LRESULT)GetStockObject( WHITE_BRUSH );
    case WM_MOUSEMOVE:
    {
        POINT pt = { (short)LOWORD(lparam), (short)HIWORD(lparam) };
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd };
        int u;
        enum hot h = hit_test( pt, &u );
        if (h != hot || u != hot_user)
        {
            hot = h;
            hot_user = u;
            InvalidateRect( hwnd, NULL, FALSE );
        }
        SetCursor( LoadCursorW( NULL, (const WCHAR *)(h != HOT_NONE ? IDC_HAND : IDC_ARROW) ) );
        TrackMouseEvent( &tme );
        return 0;
    }
    case WM_MOUSELEAVE:
        hot = HOT_NONE;
        hot_user = -1;
        InvalidateRect( hwnd, NULL, FALSE );
        return 0;
    case WM_LBUTTONUP:
    {
        POINT pt = { (short)LOWORD(lparam), (short)HIWORD(lparam) };
        int u;
        switch (hit_test( pt, &u ))
        {
        case HOT_GO:      send_password(); break;
        case HOT_HELP:    show_balloon(); break;
        case HOT_TURNOFF: turn_off(hwnd); break;
        case HOT_TILE:    if (u != selected) begin_logon( u ); break;
        case HOT_NONE:    cancel_logon(); break;
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) cancel_logon();
        else if ((wparam == VK_DOWN || wparam == VK_UP) && user_count)
            begin_logon( selected < 0 ? 0 : (selected + (wparam == VK_DOWN ? 1 : user_count - 1)) % user_count );
        else if (wparam == VK_RETURN && selected < 0 && user_count) begin_logon( 0 );
        return 0;
    case WM_DESTROY:
        PostQuitMessage( 0 );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

int WINAPI wWinMain( HINSTANCE inst, HINSTANCE prev, WCHAR *cmdline, int show )
{
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_WIN95_CLASSES };
    struct GdiplusStartupInput gsi = { 1 };
    WNDCLASSW wc = {0};
    MSG msg;
    DWORD err;

    instance = inst;
    if ((err = WexGreeterConnect()))
    {
        ERR( "not started by LightDM (%lu)\n", err );
        MessageBoxW( NULL, text( IDS_NO_GREETER ), text( IDS_TITLE ), MB_OK | MB_ICONERROR );
        return 1;
    }
    InitCommonControlsEx( &icc );
    GdiplusStartup( &gdiplus_token, &gsi, NULL );
    font_begin = make_font( -19, FW_NORMAL );
    font_name = make_font( -19, FW_NORMAL );
    font_small = make_font( -11, FW_NORMAL );
    font_small_bold = make_font( -11, FW_BOLD );
    font_turnoff = make_font( -19, FW_NORMAL );
    font_edit = make_font( -13, FW_NORMAL );
    load_users();

    wc.lpfnWndProc = main_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW( NULL, (const WCHAR *)IDC_ARROW );
    wc.lpszClassName = L"WexLogonUI";
    RegisterClassW( &wc );
    CreateWindowExW( 0, wc.lpszClassName, text( IDS_TITLE ), WS_POPUP | WS_CLIPCHILDREN, 0, 0,
                     GetSystemMetrics( SM_CXSCREEN ), GetSystemMetrics( SM_CYSCREEN ), NULL, NULL, inst, NULL );
    layout();
    ShowWindow( main_window, SW_SHOW );
    SetForegroundWindow( main_window );
    SetFocus( main_window );
    CloseHandle( CreateThread( NULL, 0, reader_thread, NULL, 0, NULL ) );

    while (GetMessageW( &msg, NULL, 0, 0 ))
    {
        TranslateMessage( &msg );
        DispatchMessageW( &msg );
    }
    GdiplusShutdown( gdiplus_token );
    return 0;
}
