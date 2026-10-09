/*
 * WineExplorer Desktop Environment - windows of Linux programs
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

/* Every window of a program outside our wineserver (native Linux programs, Steam, Proton games) gets a Win32
 * window of ours, the host, and wexhost places the program's X window into the host's client area.  From then on
 * the host is an ordinary window: taskbar button, Alt+Tab, frame and caption from the visual style, z-order,
 * minimize and maximize.  The host passes what happens to it on to the program (size, close, states) and the
 * program's requests (title, icon, size, fullscreen, activation) come back as WEX_WM_EVENTs.
 * See docs/LINUX_WINDOWS.md. */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <ntuser.h>
#include <wexhost.h>

#include "wine/debug.h"
#include "wine/list.h"
#include "linux_windows.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer);

#define WM_LINUX_EVENT  (WM_APP + 0x4c57)
#define IDT_MANAGE      1
#define MANAGE_RETRIES  40              /* 50 ms each: the host's X window reaches the server with Wine's next flush */
#define ICON_PIXELS     (256 * 256)

static const WCHAR host_class[] = L"WineExplorerLinuxWindow";
static const WCHAR events_class[] = L"WineExplorerLinuxWindows";
static const WCHAR whole_window_prop[] = L"__wine_x11_whole_window";

struct linux_window
{
    struct list entry;
    UINT64 window;          /* the program's X window */
    HWND   host;
    BOOL   managed;         /* placed into the host */
    BOOL   gone;            /* the program let it go: nothing to release */
    int    retries;
    DWORD  window_type;
    DWORD  flags;
    DWORD  initial_states;
    SIZE   min_size, max_size;
    BOOL   fullscreen;
    LONG   saved_style;
    RECT   saved_rect;
    HICON  icon, small_icon;
};

static struct list windows = LIST_INIT( windows );
static HWND events_hwnd;

static struct linux_window *find_window( UINT64 window )
{
    struct linux_window *lw;
    LIST_FOR_EACH_ENTRY( lw, &windows, struct linux_window, entry ) if (lw->window == window) return lw;
    return NULL;
}

static void host_style( const struct linux_window *lw, BOOL owned, DWORD *style, DWORD *ex_style )
{
    *ex_style = 0;
    if ((lw->flags & WEX_WM_NO_BORDER) || lw->window_type == WEX_WM_TYPE_SPLASH) *style = WS_POPUP;
    else if (lw->window_type == WEX_WM_TYPE_DIALOG) *style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME;
    else if (lw->window_type == WEX_WM_TYPE_UTILITY)
    {
        *style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME;
        *ex_style = WS_EX_TOOLWINDOW;
    }
    else *style = WS_OVERLAPPEDWINDOW;

    if (lw->min_size.cx && lw->min_size.cx == lw->max_size.cx && lw->min_size.cy == lw->max_size.cy)
        *style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    /* a program's own main window gets a taskbar button even without a frame (Steam draws its own) */
    if (lw->window_type == WEX_WM_TYPE_SPLASH) *ex_style |= WS_EX_TOOLWINDOW;
    else if (!owned && (*style & WS_POPUP)) *ex_style |= WS_EX_APPWINDOW;
}

/* The host's client area, relative to the host's X window and on the screen.  In the virtual desktop Wine draws
 * the frame itself, so the X window covers the whole window rectangle. */
static void client_geometry( const struct linux_window *lw, RECT *client, RECT *screen )
{
    RECT window, rc;
    POINT origin = { 0, 0 };

    GetWindowRect( lw->host, &window );
    GetClientRect( lw->host, &rc );
    ClientToScreen( lw->host, &origin );
    SetRect( screen, origin.x, origin.y, origin.x + rc.right, origin.y + rc.bottom );
    *client = *screen;
    OffsetRect( client, -window.left, -window.top );
}

static void send_states( struct linux_window *lw, BOOL focused )
{
    DWORD states = 0;

    if (IsIconic( lw->host )) states |= WEX_WM_STATE_MINIMIZED;
    else if (lw->fullscreen) states |= WEX_WM_STATE_FULLSCREEN;
    else if (IsZoomed( lw->host )) states |= WEX_WM_STATE_MAXIMIZED;
    if (GetWindowLongW( lw->host, GWL_EXSTYLE ) & WS_EX_TOPMOST) states |= WEX_WM_STATE_ABOVE;
    if (focused) states |= WEX_WM_STATE_FOCUSED;
    WexWmSetState( lw->window, states );
}

static void update_title( struct linux_window *lw )
{
    WCHAR title[512];
    if (!WexWmGetTitle( lw->window, title, ARRAY_SIZE(title) )) SetWindowTextW( lw->host, title );
}

static HICON create_icon( const DWORD *pixels, DWORD width, DWORD height )
{
    BITMAPINFO info = { { sizeof(BITMAPINFOHEADER) } };
    ICONINFO icon = { TRUE };
    HICON ret = NULL;
    void *bits;

    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -(LONG)height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    if (!(icon.hbmColor = CreateDIBSection( NULL, &info, DIB_RGB_COLORS, &bits, NULL, 0 ))) return NULL;
    memcpy( bits, pixels, width * height * 4 );   /* _NET_WM_ICON is ARGB, as a 32-bit icon with alpha */
    if ((icon.hbmMask = CreateBitmap( width, height, 1, 1, NULL ))) ret = CreateIconIndirect( &icon );
    DeleteObject( icon.hbmColor );
    if (icon.hbmMask) DeleteObject( icon.hbmMask );
    return ret;
}

static HICON get_icon( UINT64 window, DWORD size )
{
    DWORD *pixels = malloc( ICON_PIXELS * sizeof(DWORD) ), width, height;
    HICON ret = NULL;

    if (pixels && !WexWmGetIcon( window, size, pixels, ICON_PIXELS, &width, &height ))
        ret = create_icon( pixels, width, height );
    free( pixels );
    return ret;
}

static void update_icon( struct linux_window *lw )
{
    HICON icon = get_icon( lw->window, GetSystemMetrics( SM_CXICON ) );
    HICON small_icon = get_icon( lw->window, GetSystemMetrics( SM_CXSMICON ) );

    SendMessageW( lw->host, WM_SETICON, ICON_BIG, (LPARAM)icon );
    SendMessageW( lw->host, WM_SETICON, ICON_SMALL, (LPARAM)small_icon );
    if (lw->icon) DestroyIcon( lw->icon );
    if (lw->small_icon) DestroyIcon( lw->small_icon );
    lw->icon = icon;
    lw->small_icon = small_icon;
}

static void set_fullscreen( struct linux_window *lw, BOOL fullscreen )
{
    MONITORINFO info = { sizeof(info) };

    if (fullscreen == lw->fullscreen) return;
    if (fullscreen)
    {
        if (IsZoomed( lw->host ) || IsIconic( lw->host )) ShowWindow( lw->host, SW_RESTORE );
        lw->saved_style = GetWindowLongW( lw->host, GWL_STYLE );
        GetWindowRect( lw->host, &lw->saved_rect );
        GetMonitorInfoW( MonitorFromWindow( lw->host, MONITOR_DEFAULTTONEAREST ), &info );
        lw->fullscreen = TRUE;
        SetWindowLongW( lw->host, GWL_STYLE, (lw->saved_style & ~(WS_CAPTION | WS_THICKFRAME)) | WS_POPUP );
        /* above the taskbar, as Windows does for a full screen window */
        SetWindowPos( lw->host, HWND_TOPMOST, info.rcMonitor.left, info.rcMonitor.top,
                      info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top,
                      SWP_FRAMECHANGED );
    }
    else
    {
        lw->fullscreen = FALSE;
        SetWindowLongW( lw->host, GWL_STYLE, lw->saved_style );
        SetWindowPos( lw->host, HWND_NOTOPMOST, lw->saved_rect.left, lw->saved_rect.top,
                      lw->saved_rect.right - lw->saved_rect.left, lw->saved_rect.bottom - lw->saved_rect.top,
                      SWP_FRAMECHANGED );
    }
}

static void apply_state( struct linux_window *lw, DWORD state, BOOL on )
{
    switch (state)
    {
    case WEX_WM_STATE_FULLSCREEN:
        set_fullscreen( lw, on );
        break;
    case WEX_WM_STATE_MAXIMIZED:
        if (!lw->fullscreen) ShowWindow( lw->host, on ? SW_MAXIMIZE : SW_RESTORE );
        break;
    case WEX_WM_STATE_MINIMIZED:
        ShowWindow( lw->host, on ? SW_MINIMIZE : SW_RESTORE );
        break;
    case WEX_WM_STATE_ABOVE:
        SetWindowPos( lw->host, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE );
        break;
    }
}

static BOOL has_state( struct linux_window *lw, DWORD state )
{
    switch (state)
    {
    case WEX_WM_STATE_FULLSCREEN: return lw->fullscreen;
    case WEX_WM_STATE_MAXIMIZED: return IsZoomed( lw->host );
    case WEX_WM_STATE_MINIMIZED: return IsIconic( lw->host );
    case WEX_WM_STATE_ABOVE: return !!(GetWindowLongW( lw->host, GWL_EXSTYLE ) & WS_EX_TOPMOST);
    }
    return FALSE;
}

static void manage( struct linux_window *lw )
{
    UINT64 parent = (ULONG_PTR)GetPropW( lw->host, whole_window_prop );
    RECT client, screen;
    DWORD state;

    client_geometry( lw, &client, &screen );
    if (parent && !WexWmManage( lw->window, parent, &client, &screen ))
    {
        lw->managed = TRUE;
        for (state = WEX_WM_STATE_FULLSCREEN; state <= WEX_WM_STATE_ABOVE; state <<= 1)
            if (lw->initial_states & state) apply_state( lw, state, TRUE );
        send_states( lw, GetForegroundWindow() == lw->host );
        return;
    }
    if (++lw->retries < MANAGE_RETRIES)
    {
        SetTimer( lw->host, IDT_MANAGE, 50, NULL );
        return;
    }
    ERR( "could not place window %I64x into %p, showing it without a frame\n", lw->window, lw->host );
    lw->gone = TRUE;
    WexWmManage( lw->window, 0, NULL, NULL );
    DestroyWindow( lw->host );
}

static void map_window( const WEX_WM_EVENT *event )
{
    struct linux_window *lw, *owner = event->owner ? find_window( event->owner ) : NULL;
    DWORD style, ex_style;
    RECT rect = event->rect, work;
    int x, y, width, height;

    if (find_window( event->window ) || !(lw = calloc( 1, sizeof(*lw) ))) return;
    lw->window = event->window;
    lw->window_type = event->window_type;
    lw->flags = event->flags;
    lw->initial_states = event->states;
    lw->min_size = event->min_size;
    lw->max_size = event->max_size;
    host_style( lw, owner != NULL, &style, &ex_style );

    AdjustWindowRectEx( &rect, style, FALSE, ex_style );
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
    if (event->flags & WEX_WM_EXISTING)
    {
        /* already on the screen: the program's content stays where it is */
        x = rect.left;
        y = rect.top;
    }
    else if (event->flags & WEX_WM_POSITION)
    {
        /* X11 window managers put the frame's top-left corner where the program asked (NorthWest gravity) */
        x = event->rect.left;
        y = event->rect.top;
    }
    else if (owner)
    {
        GetWindowRect( owner->host, &work );
        x = (work.left + work.right - width) / 2;
        y = (work.top + work.bottom - height) / 2;
    }
    else if (!(style & WS_POPUP))
    {
        x = CW_USEDEFAULT;
        y = 0;
    }
    else
    {
        SystemParametersInfoW( SPI_GETWORKAREA, 0, &work, 0 );
        x = (work.left + work.right - width) / 2;
        y = (work.top + work.bottom - height) / 2;
    }

    list_add_tail( &windows, &lw->entry );
    if (!CreateWindowExW( ex_style, host_class, L"", style, x, y, width, height, owner ? owner->host : NULL, 0,
                          GetModuleHandleW( NULL ), lw ))
    {
        ERR( "could not create a host for window %I64x\n", event->window );
        list_remove( &lw->entry );
        free( lw );
        WexWmManage( event->window, 0, NULL, NULL );
        return;
    }
    TRACE( "window %I64x host %p\n", lw->window, lw->host );
    /* Wine paints the host's surface with IncludeInferiors, over the program's window.  A client surface
     * (format -1, as for Vulkan) keeps the client area out of the host's surface, as for OpenGL windows.
     * SendMessageTimeout, because SendMessage within a thread calls the window procedure from user32 directly
     * and only win32u handles Wine's internal messages. */
    SendMessageTimeoutW( lw->host, WM_WINE_SETPIXELFORMAT, (WPARAM)-1, TRUE, SMTO_NORMAL, 1000, NULL );
    update_title( lw );
    update_icon( lw );
    ShowWindow( lw->host, (event->flags & WEX_WM_EXISTING) ? SW_SHOWNOACTIVATE : SW_SHOWNORMAL );
    manage( lw );
}

static void configure_window( struct linux_window *lw, const WEX_WM_EVENT *event )
{
    RECT rect = event->rect, window;
    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;

    if (!lw->managed || lw->fullscreen || IsZoomed( lw->host ) || IsIconic( lw->host )) return;
    GetWindowRect( lw->host, &window );
    AdjustWindowRectEx( &rect, GetWindowLongW( lw->host, GWL_STYLE ), FALSE, GetWindowLongW( lw->host, GWL_EXSTYLE ) );
    if (!(event->flags & WEX_WM_MOVE)) flags |= SWP_NOMOVE;
    if (!(event->flags & WEX_WM_SIZE)) flags |= SWP_NOSIZE;
    SetWindowPos( lw->host, NULL, event->rect.left, event->rect.top, rect.right - rect.left, rect.bottom - rect.top, flags );
}

static void handle_event( const WEX_WM_EVENT *event )
{
    struct linux_window *lw;
    DWORD state;

    if (event->type == WEX_WM_MAP)
    {
        map_window( event );
        return;
    }
    if (event->type == WEX_WM_KEY)
    {
        /* the Windows key and Ctrl+Esc reach the shell window, as on Windows */
        PostMessageW( GetShellWindow(), WM_SYSCOMMAND, SC_TASKLIST, 0 );
        return;
    }
    if (!(lw = find_window( event->window ))) return;

    switch (event->type)
    {
    case WEX_WM_WITHDRAW:
        lw->gone = TRUE;
        DestroyWindow( lw->host );
        break;
    case WEX_WM_CONFIGURE:
        configure_window( lw, event );
        break;
    case WEX_WM_TITLE:
        update_title( lw );
        break;
    case WEX_WM_ICON:
        update_icon( lw );
        break;
    case WEX_WM_STATE:
        for (state = WEX_WM_STATE_FULLSCREEN; state <= WEX_WM_STATE_ABOVE; state <<= 1)
        {
            if (!(event->states & state)) continue;
            apply_state( lw, state, event->value == 2 ? !has_state( lw, state ) : event->value == 1 );
        }
        send_states( lw, GetForegroundWindow() == lw->host );
        break;
    case WEX_WM_ACTIVATE:
        if (IsIconic( lw->host )) ShowWindow( lw->host, SW_RESTORE );
        SetForegroundWindow( lw->host );
        break;
    case WEX_WM_MINIMIZE:
        ShowWindow( lw->host, SW_MINIMIZE );
        break;
    case WEX_WM_HINTS:
        lw->min_size = event->min_size;
        lw->max_size = event->max_size;
        break;
    }
}

static void limit_size( HWND hwnd, SIZE client, POINT *limit )
{
    RECT rect = { 0, 0, client.cx, client.cy };
    if (!client.cx || !client.cy) return;
    AdjustWindowRectEx( &rect, GetWindowLongW( hwnd, GWL_STYLE ), FALSE, GetWindowLongW( hwnd, GWL_EXSTYLE ) );
    limit->x = rect.right - rect.left;
    limit->y = rect.bottom - rect.top;
}

static LRESULT CALLBACK host_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    struct linux_window *lw = (struct linux_window *)GetWindowLongPtrW( hwnd, GWLP_USERDATA );
    RECT client, screen;
    PAINTSTRUCT ps;

    switch (msg)
    {
    case WM_NCCREATE:
        lw = ((CREATESTRUCTW *)lp)->lpCreateParams;
        lw->host = hwnd;
        SetWindowLongPtrW( hwnd, GWLP_USERDATA, (LONG_PTR)lw );
        break;
    case WM_WINDOWPOSCHANGED:
        if (lw && lw->managed && !IsIconic( hwnd ))
        {
            client_geometry( lw, &client, &screen );
            WexWmConfigure( lw->window, &client, &screen );
        }
        break;
    case WM_SIZE:
        if (lw && lw->managed) send_states( lw, GetForegroundWindow() == hwnd );
        break;
    case WM_ACTIVATE:
        if (lw && lw->managed) send_states( lw, LOWORD(wp) != WA_INACTIVE );
        break;
    case WM_CLOSE:
        /* the program decides: it closes the window (and we hear about it) or asks to save first */
        if (lw && !lw->gone) WexWmClose( lw->window );
        return 0;
    case WM_GETMINMAXINFO:
        if (lw)
        {
            MINMAXINFO *info = (MINMAXINFO *)lp;
            limit_size( hwnd, lw->min_size, &info->ptMinTrackSize );
            limit_size( hwnd, lw->max_size, &info->ptMaxTrackSize );
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        /* the client area is the program's */
        BeginPaint( hwnd, &ps );
        EndPaint( hwnd, &ps );
        return 0;
    case WM_TIMER:
        if (wp == IDT_MANAGE)
        {
            KillTimer( hwnd, IDT_MANAGE );
            if (lw && !lw->managed && !lw->gone) manage( lw );
            return 0;
        }
        break;
    case WM_DESTROY:
        /* the host goes before the program's window (the shell exits): give it back to the root window */
        if (lw && !lw->gone)
        {
            client_geometry( lw, &client, &screen );
            WexWmRelease( lw->window, &screen );
        }
        break;
    case WM_NCDESTROY:
        if (lw)
        {
            list_remove( &lw->entry );
            if (lw->icon) DestroyIcon( lw->icon );
            if (lw->small_icon) DestroyIcon( lw->small_icon );
            free( lw );
            SetWindowLongPtrW( hwnd, GWLP_USERDATA, 0 );
        }
        break;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

static LRESULT CALLBACK events_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_LINUX_EVENT)
    {
        handle_event( (const WEX_WM_EVENT *)lp );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

/* on wexhost's thread: the hosts belong to the desktop thread */
static void CALLBACK wm_callback( const WEX_WM_EVENT *event, void *context )
{
    SendMessageW( events_hwnd, WM_LINUX_EVENT, 0, (LPARAM)event );
}

void linux_windows_start(void)
{
    UINT64 desktop = (ULONG_PTR)GetPropW( GetDesktopWindow(), whole_window_prop );
    DWORD enabled = 1, size = sizeof(enabled), err;
    WNDCLASSW cls = { 0 };

    RegGetValueW( HKEY_CURRENT_USER, L"Software\\WineExplorer\\Shell", L"ManageLinuxWindows", RRF_RT_REG_DWORD,
                  NULL, &enabled, &size );
    if (!enabled || !desktop) return;   /* not an X11 virtual desktop */

    cls.lpfnWndProc = host_proc;
    cls.hInstance = GetModuleHandleW( NULL );
    cls.hCursor = LoadCursorW( NULL, (LPCWSTR)IDC_ARROW );
    cls.lpszClassName = host_class;
    RegisterClassW( &cls );
    cls.lpfnWndProc = events_proc;
    cls.lpszClassName = events_class;
    RegisterClassW( &cls );
    events_hwnd = CreateWindowExW( 0, events_class, NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, 0, GetModuleHandleW( NULL ), NULL );

    if ((err = WexWmStart( desktop, wm_callback, NULL )))
    {
        WARN( "not managing Linux windows, error %lu\n", err );
        DestroyWindow( events_hwnd );
        events_hwnd = NULL;
    }
}

void linux_windows_stop(void)
{
    struct linux_window *lw, *next;

    if (!events_hwnd) return;
    WexWmStop();
    LIST_FOR_EACH_ENTRY_SAFE( lw, next, &windows, struct linux_window, entry )
    {
        lw->gone = TRUE;   /* released by WexWmStop */
        DestroyWindow( lw->host );
    }
    DestroyWindow( events_hwnd );
    events_hwnd = NULL;
}
