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
#include <wexhost.h>
#include <dwmapi.h>
#include "shellres.h"
#include "taskband.h"
#include "module.h"


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

/* DWM P11b: a live thumbnail of the selected window above the icons, only with composition and
 * HKCU\Software\WineExplorer\Shell\AltTabThumbnails non-zero; otherwise the XP switcher is unchanged.
 * A minimized window shows the last picture the compositor kept of it. */
#define PREVIEW_HEIGHT 160
static int preview_height;
static HTHUMBNAIL preview_thumb;
static HBITMAP preview_bitmap;   /* the window's own iconic picture (P9) */
static RECT preview_bitmap_rect;
static DWORD preview_flags;      /* its DWM_SIT_* flags */

static BOOL previews_enabled(void)
{
    DWORD enabled = 0, size = sizeof(enabled);
    BOOL composition = FALSE;

    if (WexCompositionGetEnabled( &composition ) || !composition) return FALSE;
    RegGetValueW( HKEY_CURRENT_USER, L"Software\\WineExplorer\\Shell", L"AltTabThumbnails", RRF_RT_REG_DWORD,
                  NULL, &enabled, &size );
    return enabled != 0;
}

static void release_preview(void)
{
    if (preview_thumb) DwmUnregisterThumbnail( preview_thumb );
    preview_thumb = NULL;
    if (preview_bitmap) DeleteObject( preview_bitmap );
    preview_bitmap = NULL;
}

/* fit the selected window, keeping its aspect ratio, centered in the band; called whenever the selection moves */
static void update_preview(void)
{
    DWM_THUMBNAIL_PROPERTIES props = { 0 };
    BITMAP bitmap;
    RECT source;
    int band_w = total_width - 2 * DIALOG_MARGIN, band_h = preview_height - DIALOG_MARGIN, w, h, x, y;

    release_preview();
    if (!preview_height || selected >= count) return;
    if ((preview_bitmap = iconic_thumbnail( windows[selected], band_w, band_h, &preview_flags )))
    {
        /* the window's own picture (P9): fit it into the band, never enlarge it */
        GetObjectW( preview_bitmap, sizeof(bitmap), &bitmap );
        w = min( bitmap.bmWidth, band_w );
        h = MulDiv( w, bitmap.bmHeight, bitmap.bmWidth );
        if (h > band_h)
        {
            h = band_h;
            w = MulDiv( h, bitmap.bmWidth, bitmap.bmHeight );
        }
        x = DIALOG_MARGIN + (band_w - w) / 2;
        y = DIALOG_MARGIN + (band_h - h) / 2;
        SetRect( &preview_bitmap_rect, x, y, x + w, y + h );
        InvalidateRect( switch_hwnd, &preview_bitmap_rect, TRUE );
        return;
    }
    if (!window_frame_rect( windows[selected], &source ) || source.right <= source.left || source.bottom <= source.top ||
        FAILED( DwmRegisterThumbnail( switch_hwnd, windows[selected], &preview_thumb ) )) return;
    w = band_w;
    h = MulDiv( w, source.bottom - source.top, source.right - source.left );
    if (h > band_h)
    {
        h = band_h;
        w = MulDiv( h, source.right - source.left, source.bottom - source.top );
    }
    x = DIALOG_MARGIN + (band_w - w) / 2;
    y = DIALOG_MARGIN + (band_h - h) / 2;
    /* the destination rectangle is relative to the switcher's window, which has a border */
    GetWindowRect( switch_hwnd, &source );
    MapWindowPoints( NULL, switch_hwnd, (POINT *)&source, 1 );
    x -= source.left;
    y -= source.top;
    props.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE;
    SetRect( &props.rcDestination, x, y, x + w, y + h );
    props.fVisible = TRUE;
    DwmUpdateThumbnailProperties( preview_thumb, &props );
}

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
    preview_height = previews_enabled() ? PREVIEW_HEIGHT : 0;
    total_width = items_width + 2 * DIALOG_MARGIN;
    total_height = preview_height + items_height + 2 * DIALOG_MARGIN + font_height + 2 * CY_TEXT_MARGIN;

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

    if (preview_bitmap)
        draw_iconic( hdc, preview_bitmap, &preview_bitmap_rect, windows[selected],
                     preview_flags & DWM_SIT_DISPLAYFRAME, GetSystemMetrics( SM_CYSMCAPTION ) );

    /* more windows than places: the selection scrolls the list */
    shift = selected >= items ? selected - items + 1 : 0;
    for (i = 0; i < items && i + shift < count; i++)
    {
        x = DIALOG_MARGIN + CX_ITEM_SPACE * (i % shown_columns) + ITEM_MARGIN;
        y = preview_height + DIALOG_MARGIN + CY_ITEM_SPACE * (i / shown_columns) + ITEM_MARGIN;
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
    SetRect( &text_rect, DIALOG_MARGIN, preview_height + DIALOG_MARGIN + items_height, total_width - DIALOG_MARGIN,
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
#define WM_SWITCH_TO (WM_APP + 2)
static BOOL closing;

static void switch_to( HWND hwnd );

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
    case WM_SWITCH_TO:
        release_preview();
        ShowWindow( hwnd, SW_HIDE );
        free_list();
        selected = 0;
        closing = FALSE;
        if (IsWindow( (HWND)lp )) switch_to( (HWND)lp );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

/* Wine's SwitchToThisWindow only brings the window to the top, which activates it within its own thread.  The
 * keyboard went to the foreground window's thread, so join its input to be allowed to change the foreground. */
static void switch_to( HWND hwnd )
{
    DWORD thread = GetWindowThreadProcessId( GetForegroundWindow(), NULL ), self = GetCurrentThreadId();
    BOOL attached, activated;

    if (!hwnd) return;
    /* Keep the shell's queue when detaching: Wine creates a fresh queue for
     * the first argument, dropping its active window and input timestamp. */
    attached = thread && thread != self && AttachThreadInput( thread, self, TRUE );
    if (IsIconic( hwnd )) ShowWindow( hwnd, SW_RESTORE );
    hwnd = GetLastActivePopup( hwnd );
    activated = SetForegroundWindow( hwnd );
    if (attached) AttachThreadInput( thread, self, FALSE );
    /* The session WM owns X keyboard focus across Wine and foreign clients. */
    if (activated || GetForegroundWindow() == hwnd)
        WexWmFocus( (ULONG_PTR)GetPropW( hwnd, L"__wine_x11_whole_window" ) );
}

static void complete( BOOL do_switch )
{
    HWND target = do_switch && selected < count ? windows[selected] : NULL;

    /* Cross-thread activation must run after the low-level keyboard hook has
     * returned, so the target's input thread can process the activation. */
    closing = TRUE;
    PostMessageW( switch_hwnd, WM_SWITCH_TO, 0, (LPARAM)target );
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
        cls.hInstance = explorer_hInstance;
        cls.hCursor = LoadCursorW( NULL, (const WCHAR *)IDC_ARROW );
        cls.lpszClassName = switch_class;
        RegisterClassW( &cls );
        switch_hwnd = CreateWindowExW( switch_ex_style, switch_class, L"", switch_style, 0, 0, 0, 0, NULL, NULL,
                                       explorer_hInstance, NULL );
        if (!switch_hwnd) { free_list(); return; }
    }
    /* the first window is the active one: start at the next (or the last one with Shift) */
    selected = backwards ? count - 1 : 1;
    layout();
    update_preview();
    InvalidateRect( switch_hwnd, NULL, TRUE );
    PostMessageW( switch_hwnd, WM_MASK_ALT, 0, 0 );
}

static void move_selection( int step )
{
    selected = (selected + step + count) % count;
    update_preview();
    InvalidateRect( switch_hwnd, NULL, TRUE );
}

static void move_row( int step )
{
    int row = selected / shown_columns + step, column = selected % shown_columns;

    if (row < 0) row = shown_rows - 1;
    if (row >= shown_rows) row = 0;
    selected = min( row * shown_columns + column, count - 1 );
    update_preview();
    InvalidateRect( switch_hwnd, NULL, TRUE );
}

BOOL alttab_key( DWORD vk, BOOL down )
{
    BOOL is_open = switch_hwnd && IsWindowVisible( switch_hwnd ) && !closing;
    BOOL shift = GetAsyncKeyState( VK_SHIFT ) < 0;

    if (closing) return vk == VK_TAB;

    if (vk == VK_TAB && down && GetAsyncKeyState( VK_MENU ) < 0)
    {
        if (!is_open) open_switcher( shift );
        else move_selection( shift ? -1 : 1 );
        return TRUE;   /* never a Tab for the program */
    }
    if (!is_open) return FALSE;
    if (!down)
    {
        /* The mask key prevents the old window entering its menu while the
         * switch is queued. Let Alt up through to release its keyboard state. */
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
