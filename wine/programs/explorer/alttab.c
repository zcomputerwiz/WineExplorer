/*
 * WineExplorer Desktop Environment - Alt+Tab task switcher
 *
 * Based on the ReactOS task switcher (win32ss/user/user32/controls/appswitch.c)
 * Copyright Johannes Anderwald (johannes.anderwald@reactos.org)
 * Copyright David Quintana (gigaherz@gmail.com)
 * Copyright Katayama Hirofumi MZ (katayama.hirofumi.mz@gmail.com)
 * Copyright 2026 WineExplorer Desktop Environment Project
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

/* Windows XP's "CoolSwitch": while Alt is held, Tab steps through the windows (a grid of their icons with the
 * selected title below) and releasing Alt switches.  Wine has none (SC_NEXTWINDOW is a FIXME), so the shell's
 * low-level keyboard hook drives it, for Windows programs and, through wexhost's replay, for Linux windows.
 * ReactOS runs it as a modal loop inside user32; here the hook feeds it key by key. */

#include <stdlib.h>
#include <string.h>

#include "alttab.h"
#include "shellres.h"
#include "taskband.h"

#define DIALOG_MARGIN   8
#define CX_ICON         32
#define CY_ICON         32
#define ICON_MARGIN     4
#define CX_ITEM         (CX_ICON + 2 * ICON_MARGIN)
#define CY_ITEM         (CY_ICON + 2 * ICON_MARGIN)
#define ITEM_MARGIN     4
#define CX_ITEM_SPACE   (CX_ITEM + 2 * ITEM_MARGIN)
#define CY_ITEM_SPACE   (CY_ITEM + 2 * ITEM_MARGIN)
#define CY_TEXT_MARGIN  4
#define MAX_WINDOWS     120

static const WCHAR switch_class[] = L"WineExplorerTaskSwitch";
static const DWORD switch_style = WS_POPUP | WS_BORDER | WS_DISABLED;
static const DWORD switch_ex_style = WS_EX_TOPMOST | WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW;

static HWND switch_hwnd;
static HWND windows[MAX_WINDOWS];
static HICON icons[MAX_WINDOWS];
static int count, selected;
static int columns = 7, rows = 3, items, shown_columns, shown_rows, items_width, items_height, total_width, total_height;
static int font_height;

static int get_reg_int( HKEY key, const WCHAR *name, int def )
{
    DWORD value, size = sizeof(value), type;
    WCHAR buffer[16];

    size = sizeof(buffer);
    if (RegQueryValueExW( key, name, NULL, &type, (BYTE *)buffer, &size )) return def;
    if (type == REG_DWORD && size == sizeof(DWORD)) { memcpy( &value, buffer, sizeof(value) ); return value; }
    if (type == REG_SZ && size < sizeof(buffer)) { buffer[size / sizeof(WCHAR)] = 0; return _wtoi( buffer ); }
    return def;
}

/* CoolSwitch, CoolSwitchRows, CoolSwitchColumns: the same settings as Windows */
static BOOL load_settings(void)
{
    BOOL enabled = TRUE;
    HKEY key;

    if (!RegOpenKeyExW( HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, KEY_READ, &key ))
    {
        enabled = get_reg_int( key, L"CoolSwitch", TRUE );
        rows = get_reg_int( key, L"CoolSwitchRows", 3 );
        columns = get_reg_int( key, L"CoolSwitchColumns", 7 );
        RegCloseKey( key );
    }
    if (rows * columns < 3 || rows <= 0 || columns <= 0) { rows = 3; columns = 7; }
    return enabled;
}

static HICON window_icon( HWND hwnd )
{
    HICON icon = NULL;

    SendMessageTimeoutW( hwnd, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, (DWORD_PTR *)&icon );
    if (!icon) icon = (HICON)GetClassLongPtrW( hwnd, GCLP_HICON );
    if (!icon) SendMessageTimeoutW( hwnd, WM_GETICON, ICON_SMALL2, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, (DWORD_PTR *)&icon );
    if (icon) return CopyIcon( icon );
    return shellres_icon( 3, CX_ICON );   /* the generic program icon */
}

static BOOL CALLBACK add_window( HWND hwnd, LPARAM lparam )
{
    if (count == MAX_WINDOWS) return FALSE;
    if (!taskband_is_task_window( hwnd )) return TRUE;
    windows[count] = hwnd;
    icons[count] = window_icon( hwnd );
    count++;
    return TRUE;
}

static void free_list(void)
{
    while (count) if (icons[--count]) DestroyIcon( icons[count] );
}

static void layout(void)
{
    RECT rect;
    HDC hdc;
    TEXTMETRICW tm;
    HFONT old;

    hdc = GetDC( 0 );
    old = SelectObject( hdc, GetStockObject( DEFAULT_GUI_FONT ));
    GetTextMetricsW( hdc, &tm );
    SelectObject( hdc, old );
    ReleaseDC( 0, hdc );
    font_height = tm.tmHeight;

    items = count;
    shown_columns = columns;
    shown_rows = (items + columns - 1) / columns;
    if (shown_rows > rows)
    {
        shown_rows = rows;
        items = shown_rows * shown_columns;
    }
    items_width = shown_columns * CX_ITEM_SPACE;
    items_height = shown_rows * CY_ITEM_SPACE;
    total_width = items_width + 2 * DIALOG_MARGIN;
    total_height = items_height + 2 * DIALOG_MARGIN + font_height + 2 * CY_TEXT_MARGIN;

    SystemParametersInfoW( SPI_GETWORKAREA, 0, &rect, 0 );
    SetRect( &rect, (rect.left + rect.right - total_width) / 2, (rect.top + rect.bottom - total_height) / 2,
             (rect.left + rect.right + total_width) / 2, (rect.top + rect.bottom + total_height) / 2 );
    AdjustWindowRectEx( &rect, switch_style, FALSE, switch_ex_style );
    SetWindowPos( switch_hwnd, HWND_TOPMOST, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                  SWP_NOACTIVATE | SWP_SHOWWINDOW );
}

static void paint( HWND hwnd )
{
    PAINTSTRUCT ps;
    RECT rect, text_rect;
    WCHAR text[1024];
    int i, x, y, shift, len;
    HFONT old_font;
    HPEN pen, old_pen;
    HDC hdc = BeginPaint( hwnd, &ps );

    GetClientRect( hwnd, &rect );
    FillRect( hdc, &rect, GetSysColorBrush( COLOR_3DFACE ));
    if (!items) { EndPaint( hwnd, &ps ); return; }

    /* more windows than places: the selection scrolls the list */
    shift = selected >= items ? selected - items + 1 : 0;
    for (i = 0; i < items && i + shift < count; i++)
    {
        x = DIALOG_MARGIN + CX_ITEM_SPACE * (i % shown_columns) + ITEM_MARGIN;
        y = DIALOG_MARGIN + CY_ITEM_SPACE * (i / shown_columns) + ITEM_MARGIN;
        if (count < shown_columns) x += (items_width - count * CX_ITEM_SPACE) / 2;

        if (selected == i + shift)
        {
            pen = CreatePen( PS_SOLID, 1, GetSysColor( COLOR_HIGHLIGHT ));
            old_pen = SelectObject( hdc, pen );
            SelectObject( hdc, GetStockObject( NULL_BRUSH ));
            Rectangle( hdc, x, y, x + CX_ITEM, y + CY_ITEM );
            Rectangle( hdc, x + 1, y + 1, x + CX_ITEM - 1, y + CY_ITEM - 1 );
            SelectObject( hdc, old_pen );
            DeleteObject( pen );
        }
        if (icons[i + shift])
            DrawIconEx( hdc, x + ICON_MARGIN, y + ICON_MARGIN, icons[i + shift], CX_ICON, CY_ICON, 0, NULL, DI_NORMAL );
    }

    /* the selected window's title in a sunken field */
    SetRect( &text_rect, DIALOG_MARGIN, DIALOG_MARGIN + items_height, total_width - DIALOG_MARGIN,
             total_height - DIALOG_MARGIN );
    DrawFrameControl( hdc, &text_rect, DFC_BUTTON, DFCS_BUTTONPUSH | DFCS_PUSHED );
    len = GetWindowTextW( windows[selected], text, ARRAY_SIZE(text) );
    old_font = SelectObject( hdc, GetStockObject( DEFAULT_GUI_FONT ));
    SetTextColor( hdc, GetSysColor( COLOR_BTNTEXT ));
    SetBkMode( hdc, TRANSPARENT );
    DrawTextW( hdc, text, len, &text_rect, DT_CENTER | DT_VCENTER | DT_END_ELLIPSIS | DT_SINGLELINE | DT_NOPREFIX );
    SelectObject( hdc, old_font );
    EndPaint( hwnd, &ps );
}

#define VK_MASK     0xe8          /* unassigned virtual key */
#define WM_MASK_ALT (WM_APP + 1)

/* The program saw Alt go down but never sees the Tab we take: when Alt goes up DefWindowProc would enter its menu
 * bar (and keep the activation).  A key of no meaning in between tells it Alt was part of a combination.  Sent
 * after the keyboard hook returns: input sent from inside the hook would wait for that same hook. */
static void mask_alt(void)
{
    INPUT input[2] = { { INPUT_KEYBOARD }, { INPUT_KEYBOARD } };

    input[0].ki.wVk = input[1].ki.wVk = VK_MASK;
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput( 2, input, sizeof(INPUT) );
}

static LRESULT CALLBACK switch_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    switch (msg)
    {
    case WM_PAINT:
        paint( hwnd );
        return 0;
    case WM_MASK_ALT:
        mask_alt();
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

/* Wine's SwitchToThisWindow only brings the window to the top, which activates it within its own thread.  The
 * keyboard went to the foreground window's thread, so join its input to be allowed to change the foreground. */
static void switch_to( HWND hwnd )
{
    DWORD thread = GetWindowThreadProcessId( GetForegroundWindow(), NULL ), self = GetCurrentThreadId();
    BOOL attached;

    if (!hwnd) return;
    attached = thread && thread != self && AttachThreadInput( self, thread, TRUE );
    if (IsIconic( hwnd )) ShowWindow( hwnd, SW_RESTORE );
    SetForegroundWindow( GetLastActivePopup( hwnd ));
    if (attached) AttachThreadInput( self, thread, FALSE );
}

static void complete( BOOL do_switch )
{
    HWND target = do_switch && selected < count ? windows[selected] : NULL;

    ShowWindow( switch_hwnd, SW_HIDE );
    free_list();
    selected = 0;
    switch_to( target );
}

static void open_switcher( BOOL backwards )
{
    WNDCLASSW cls = { 0 };

    if (!load_settings()) return;
    free_list();
    EnumWindows( add_window, 0 );
    if (count < 2)
    {
        if (count) switch_to( windows[0] );
        free_list();
        return;
    }
    if (!switch_hwnd)
    {
        cls.lpfnWndProc = switch_proc;
        cls.hInstance = GetModuleHandleW( NULL );
        cls.hCursor = LoadCursorW( NULL, (const WCHAR *)IDC_ARROW );
        cls.lpszClassName = switch_class;
        RegisterClassW( &cls );
        switch_hwnd = CreateWindowExW( switch_ex_style, switch_class, L"", switch_style, 0, 0, 0, 0, NULL, NULL,
                                       GetModuleHandleW( NULL ), NULL );
        if (!switch_hwnd) { free_list(); return; }
    }
    /* the first window is the active one: start at the next (or the last one with Shift) */
    selected = backwards ? count - 1 : 1;
    layout();
    InvalidateRect( switch_hwnd, NULL, TRUE );
    PostMessageW( switch_hwnd, WM_MASK_ALT, 0, 0 );
}

static void move_selection( int step )
{
    selected = (selected + step + count) % count;
    InvalidateRect( switch_hwnd, NULL, TRUE );
}

static void move_row( int step )
{
    int row = selected / shown_columns + step, column = selected % shown_columns;

    if (row < 0) row = shown_rows - 1;
    if (row >= shown_rows) row = 0;
    selected = min( row * shown_columns + column, count - 1 );
    InvalidateRect( switch_hwnd, NULL, TRUE );
}

BOOL alttab_key( DWORD vk, BOOL down )
{
    BOOL is_open = switch_hwnd && IsWindowVisible( switch_hwnd );
    BOOL shift = GetAsyncKeyState( VK_SHIFT ) < 0;

    if (vk == VK_TAB && down && GetAsyncKeyState( VK_MENU ) < 0)
    {
        if (!is_open) open_switcher( shift );
        else move_selection( shift ? -1 : 1 );
        return TRUE;   /* never a Tab for the program */
    }
    if (!is_open) return FALSE;
    if (!down)
    {
        /* releasing Alt switches; the release itself goes on to the window that becomes active */
        if (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU) complete( TRUE );
        return vk == VK_TAB;
    }
    switch (vk)
    {
    case VK_ESCAPE: complete( FALSE ); return TRUE;
    case VK_RETURN: complete( TRUE ); return TRUE;
    case VK_LEFT:   move_selection( -1 ); return TRUE;
    case VK_RIGHT:  move_selection( 1 ); return TRUE;
    case VK_UP:     move_row( -1 ); return TRUE;
    case VK_DOWN:   move_row( 1 ); return TRUE;
    }
    return FALSE;
}
