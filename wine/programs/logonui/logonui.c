/*
 * Windows Logon User Interface Host (logonui.exe): the logon screen, as LightDM's greeter.
 *
 * LightDM checks the password (PAM) and starts the session; this program only asks.  It runs as the unprivileged
 * lightdm account with its own Wine prefix, started by wex-greeter.sh (docs/WINLOGON_AND_DWM.md, phase W2).  The
 * protocol and the account list come from wexhost (WexGreeter*, WexUsersList).
 *
 * This first version is plain: a list of the accounts, a password box, a status line and Turn off computer.  The
 * Welcome screen comes in phase W3.
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

#include "wexhost.h"
#include "resource.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(logonui);

#define WM_GREETER_EVENT (WM_APP + 1)   /* lparam: malloc'ed event text, freed by the receiver */
#define MAX_USERS 64

struct user { char name[64]; WCHAR display[128]; };

static HINSTANCE instance;
static HWND main_window, dialog;
static struct user users[MAX_USERS];
static unsigned int user_count;
static char pending_user[64];
static char pending_secret[512];   /* the typed password, kept only until LightDM asks for it */
static BOOL busy;
static WCHAR locked_user[64];

static void set_status( const WCHAR *text )
{
    SetDlgItemTextW( dialog, IDC_STATUS, text );
}

static void set_status_id( UINT id )
{
    WCHAR text[512];
    LoadStringW( instance, id, text, ARRAY_SIZE(text) );
    set_status( text );
}

static void set_busy( BOOL value )
{
    busy = value;
    EnableWindow( GetDlgItem( dialog, IDC_USERS ), !value );
    EnableWindow( GetDlgItem( dialog, IDC_PASSWORD ), !value );
    EnableWindow( GetDlgItem( dialog, IDOK ), !value );
}

static void forget_secret( void )
{
    SecureZeroMemory( pending_secret, sizeof(pending_secret) );
}

static void load_users( void )
{
    char *buffer = NULL, *line, *next;
    DWORD needed = 0, size = 4096;

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
    for (line = buffer; *line && user_count < MAX_USERS; line = next)
    {
        char *real, *end;
        struct user *u = &users[user_count];

        if ((next = strchr( line, '\n' ))) *next++ = 0;
        else next = line + strlen( line );
        if (!(real = strchr( line, '\t' ))) continue;
        *real++ = 0;
        if ((end = strchr( real, '\t' ))) *end = 0;
        lstrcpynA( u->name, line, sizeof(u->name) );
        MultiByteToWideChar( CP_UTF8, 0, *real ? real : line, -1, u->display, ARRAY_SIZE(u->display) );
        user_count++;
    }
    free( buffer );
}

static int find_user( const char *name )
{
    unsigned int i;
    for (i = 0; i < user_count; i++) if (!strcmp( users[i].name, name )) return i;
    return -1;
}

static void select_user( const char *name )
{
    int i = find_user( name );
    if (i < 0) return;
    SendDlgItemMessageW( dialog, IDC_USERS, LB_SETCURSEL, i, 0 );
    SetFocus( GetDlgItem( dialog, IDC_PASSWORD ) );
}

/* "key=value" lines after the first line of a connected/reset event */
static void apply_hints( char *text )
{
    char *line, *next, *value;
    char select[64] = "";
    BOOL lock = FALSE;

    for (line = strchr( text, '\n' ); line && *++line; line = next)
    {
        if ((next = strchr( line, '\n' ))) *next = 0;
        if ((value = strchr( line, '=' )))
        {
            *value++ = 0;
            if (!strcmp( line, "select-user" )) lstrcpynA( select, value, sizeof(select) );
            else if (!strcmp( line, "lock-screen" )) lock = !strcmp( value, "true" );
        }
        if (!next) break;
    }
    if (select[0]) select_user( select );
    if (lock && select[0])
    {
        WCHAR format[256], text[512];
        int i = find_user( select );
        MultiByteToWideChar( CP_UTF8, 0, select, -1, locked_user, ARRAY_SIZE(locked_user) );
        LoadStringW( instance, IDS_LOCKED, format, ARRAY_SIZE(format) );
        swprintf( text, ARRAY_SIZE(text), format, i >= 0 ? users[i].display : locked_user );
        set_status( text );
    }
}

static void start_logon( void )
{
    WCHAR password[256];
    int sel = SendDlgItemMessageW( dialog, IDC_USERS, LB_GETCURSEL, 0, 0 );
    DWORD err, sequence;

    if (busy || sel < 0 || sel >= user_count) return;
    GetDlgItemTextW( dialog, IDC_PASSWORD, password, ARRAY_SIZE(password) );
    WideCharToMultiByte( CP_UTF8, 0, password, -1, pending_secret, sizeof(pending_secret), NULL, NULL );
    SecureZeroMemory( password, sizeof(password) );
    SetDlgItemTextW( dialog, IDC_PASSWORD, L"" );
    lstrcpynA( pending_user, users[sel].name, sizeof(pending_user) );
    set_busy( TRUE );
    set_status_id( IDS_CHECKING );
    if ((err = WexGreeterAuthenticate( pending_user, &sequence )))
    {
        WARN( "authenticate failed: %lu\n", err );
        forget_secret();
        set_busy( FALSE );
        set_status_id( IDS_FAILED );
    }
}

/* one event from LightDM (see dlls/wexhost/greeter.c for the format) */
static void handle_event( char *text )
{
    char *body = strchr( text, '\n' );
    unsigned int seq, result;
    char user[64];

    TRACE( "%s\n", debugstr_a( text ) );
    if (!strncmp( text, "connected", 9 ) || !strncmp( text, "reset", 5 ))
    {
        apply_hints( text );
        return;
    }
    if (!strncmp( text, "prompt ", 7 ))
    {
        char *line = body ? body + 1 : NULL;
        BOOL answered = FALSE;
        while (line && *line)
        {
            char *next = strchr( line, '\n' ), *msg = strchr( line, '\t' );
            unsigned int style = atoi( line );
            if (next) *next++ = 0;
            msg = msg ? msg + 1 : (char *)"";
            if (style == 1 && !answered)          /* secret: the password typed before */
            {
                WexGreeterRespond( pending_secret );
                forget_secret();
                answered = TRUE;
            }
            else if (style == 2 && !answered)     /* visible: PAM asks for the user name again */
            {
                WexGreeterRespond( pending_user );
                answered = TRUE;
            }
            else if (style == 3 || style == 4)    /* error or information */
            {
                WCHAR wide[512];
                MultiByteToWideChar( CP_UTF8, 0, msg, -1, wide, ARRAY_SIZE(wide) );
                set_status( wide );
            }
            line = next;
        }
        return;
    }
    if (sscanf( text, "end %u %u %63s", &seq, &result, user ) >= 2)
    {
        forget_secret();
        if (result == 0)
        {
            WexGreeterStartSession( NULL );   /* the user's own default session */
            return;
        }
        set_busy( FALSE );
        set_status_id( IDS_FAILED );
        SetFocus( GetDlgItem( dialog, IDC_PASSWORD ) );
        return;
    }
    if (sscanf( text, "session %u", &result ) == 1)
    {
        if (result == 0) PostQuitMessage( 0 );   /* LightDM takes over the display */
        else
        {
            set_busy( FALSE );
            set_status_id( IDS_SESSION_FAILED );
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

static INT_PTR CALLBACK dialog_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        unsigned int i;
        DWORD can = WEX_POWER_NO;
        dialog = hwnd;
        load_users();
        for (i = 0; i < user_count; i++) SendDlgItemMessageW( hwnd, IDC_USERS, LB_ADDSTRING, 0, (LPARAM)users[i].display );
        if (user_count == 1) SendDlgItemMessageW( hwnd, IDC_USERS, LB_SETCURSEL, 0, 0 );
        WexPowerCan( WEX_POWER_SHUTDOWN, &can );
        EnableWindow( GetDlgItem( hwnd, IDC_TURNOFF ), can != WEX_POWER_NO );
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case IDOK:
            start_logon();
            return TRUE;
        case IDC_USERS:
            if (HIWORD(wparam) == LBN_SELCHANGE || HIWORD(wparam) == LBN_DBLCLK)
            {
                SetDlgItemTextW( hwnd, IDC_STATUS, L"" );
                SetFocus( GetDlgItem( hwnd, IDC_PASSWORD ) );
            }
            return TRUE;
        case IDC_TURNOFF:
            WexPowerDo( main_window, WEX_POWER_SHUTDOWN );
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static LRESULT CALLBACK main_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    switch (msg)
    {
    case WM_GREETER_EVENT:
        handle_event( (char *)lparam );
        free( (char *)lparam );
        return 0;
    case WM_SIZE:
        if (dialog)
        {
            RECT rc, dr;
            GetClientRect( hwnd, &rc );
            GetWindowRect( dialog, &dr );
            SetWindowPos( dialog, NULL, (rc.right - (dr.right - dr.left)) / 2, (rc.bottom - (dr.bottom - dr.top)) / 2,
                          0, 0, SWP_NOSIZE | SWP_NOZORDER );
        }
        return 0;
    case WM_ERASEBKGND:
    {
        RECT rc;
        HBRUSH brush = CreateSolidBrush( RGB(90, 126, 220) );   /* the Welcome screen's blue */
        GetClientRect( hwnd, &rc );
        FillRect( (HDC)wparam, &rc, brush );
        DeleteObject( brush );
        return 1;
    }
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
        return (LRESULT)GetSysColorBrush( COLOR_BTNFACE );
    case WM_DESTROY:
        forget_secret();
        PostQuitMessage( 0 );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

int WINAPI wWinMain( HINSTANCE inst, HINSTANCE prev, WCHAR *cmdline, int show )
{
    WNDCLASSW wc = {0};
    WCHAR title[64];
    MSG msg;
    DWORD err;

    instance = inst;
    LoadStringW( inst, IDS_TITLE, title, ARRAY_SIZE(title) );
    if ((err = WexGreeterConnect()))
    {
        WCHAR text[256];
        LoadStringW( inst, IDS_NO_GREETER, text, ARRAY_SIZE(text) );
        ERR( "not started by LightDM (%lu)\n", err );
        MessageBoxW( NULL, text, title, MB_OK | MB_ICONERROR );
        return 1;
    }

    wc.lpfnWndProc = main_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW( NULL, (const WCHAR *)IDC_ARROW );
    wc.lpszClassName = L"WexLogonUI";
    RegisterClassW( &wc );
    main_window = CreateWindowExW( 0, wc.lpszClassName, title, WS_POPUP | WS_CLIPCHILDREN, 0, 0,
                                   GetSystemMetrics( SM_CXSCREEN ), GetSystemMetrics( SM_CYSCREEN ), NULL, NULL, inst, NULL );
    CreateDialogW( inst, MAKEINTRESOURCEW(IDD_LOGON), main_window, dialog_proc );
    ShowWindow( main_window, SW_SHOW );
    SendMessageW( main_window, WM_SIZE, 0, 0 );
    CloseHandle( CreateThread( NULL, 0, reader_thread, NULL, 0, NULL ) );

    while (GetMessageW( &msg, NULL, 0, 0 ))
    {
        if (IsDialogMessageW( dialog, &msg )) continue;
        TranslateMessage( &msg );
        DispatchMessageW( &msg );
    }
    forget_secret();
    return 0;
}
