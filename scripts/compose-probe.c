/*
 * WineExplorer - layered window composition probe
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

/* Build in the Proton lab (copy this file separately from the wine archive):
 * ~/open/inst/bin/winegcc -m64 -Wall -I ~/open/wine/include \
 *     -o compose-probe.exe compose-probe.c -luser32 -lgdi32
 * DISPLAY=:7 WINEPREFIX=~/open/prefix ~/open/inst/bin/wine ./compose-probe.exe
 * Run on the existing shell with Composition=0 and =1, then compare root
 * screenshots. The red ramp uses premultiplied per-pixel alpha, the blue
 * rectangle uses LWA_ALPHA=128, and the green ring has a magenta color key.
 * M pauses/resumes motion, R resizes/remaps the ramp, F tests opaque fullscreen,
 * Esc exits. Clicking solid pixels logs a click; a color-key hole must let a
 * desktop click through. stdout is a machine-readable observation log.
 *
 * To exercise the API in the same process that owns the Unix compositor, use
 * a separate Xvfb/prefix with ManageLinuxWindows=0 and Composition=0, launch
 * explorer /desktop=Probe,1024x768 ./compose-probe.exe --own-compositor.
 * This mode starts WexWmStart itself; C toggles composition and 5 performs five
 * disable/enable cycles at 700 ms intervals, checking query results. Do not
 * use this mode alongside an Explorer instance managing Linux windows.
 * Capture with DISPLAY=:7 import -window root output.png; send keys/clicks
 * with DISPLAY=:7 xdotool. No VM start/stop commands are needed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <wexhost.h>

static HWND controls, ramp, uniform, keyed;
static BOOL moving = TRUE, large_ramp, fullscreen, own_compositor;
static unsigned int tick, cycle, clicks, failures;
static DWORD (WINAPI *set_composition)( BOOL );
static DWORD (WINAPI *get_composition)( BOOL * );
static void (WINAPI *stop_wm)( void );

static void report_composition( BOOL enable )
{
    BOOL actual = FALSE;
    DWORD set_err = set_composition( enable ), get_err = get_composition( &actual );
    if (set_err || get_err || actual != enable) failures++;
    printf( "composition requested=%u actual=%u set=%lu query=%lu\n", enable, actual, set_err, get_err );
    fflush( stdout );
}

static void update_ramp(void)
{
    int width = large_ramp ? 360 : 280, height = large_ramp ? 210 : 160;
    BITMAPINFO info = { 0 };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    HDC screen = GetDC( NULL ), memory = CreateCompatibleDC( screen );
    HBITMAP bitmap, previous;
    DWORD *pixels;
    SIZE size = { width, height };
    POINT source = { 0, 0 }, position;
    RECT rect;
    int x, y;
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    bitmap = CreateDIBSection( screen, &info, DIB_RGB_COLORS, (void **)&pixels, NULL, 0 );
    if (!bitmap) { fprintf( stderr, "CreateDIBSection failed: %lu\n", GetLastError() ); failures++; goto done; }
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
        {
            unsigned int alpha = x * 255 / (width - 1);
            pixels[y * width + x] = (alpha << 24) | (alpha << 16);
        }
    previous = SelectObject( memory, bitmap );
    GetWindowRect( ramp, &rect );
    position.x = rect.left;
    position.y = rect.top;
    if (!UpdateLayeredWindow( ramp, screen, &position, &size, memory, &source, 0, &blend, ULW_ALPHA ))
    {
        fprintf( stderr, "UpdateLayeredWindow failed: %lu\n", GetLastError() );
        failures++;
    }
    SelectObject( memory, previous );
    DeleteObject( bitmap );
done:
    DeleteDC( memory );
    ReleaseDC( NULL, screen );
}

static void toggle_fullscreen(void)
{
    fullscreen = !fullscreen;
    ShowWindow( ramp, fullscreen ? SW_HIDE : SW_SHOWNOACTIVATE );
    ShowWindow( uniform, fullscreen ? SW_HIDE : SW_SHOWNOACTIVATE );
    ShowWindow( keyed, fullscreen ? SW_HIDE : SW_SHOWNOACTIVATE );
    SetWindowLongW( controls, GWL_STYLE, fullscreen ? WS_POPUP : WS_OVERLAPPEDWINDOW );
    SetWindowPos( controls, HWND_TOP, fullscreen ? 0 : 110, fullscreen ? 0 : 500,
                  fullscreen ? GetSystemMetrics( SM_CXSCREEN ) : 800,
                  fullscreen ? GetSystemMetrics( SM_CYSCREEN ) : 160,
                  SWP_FRAMECHANGED | SWP_SHOWWINDOW );
    InvalidateRect( controls, NULL, TRUE );
    printf( "fullscreen=%u\n", fullscreen );
    fflush( stdout );
}

static LRESULT CALLBACK probe_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint( hwnd, &paint );
        RECT rect;
        HBRUSH brush;
        GetClientRect( hwnd, &rect );
        if (hwnd == keyed)
        {
            brush = CreateSolidBrush( RGB( 0, 220, 70 ) );
            FillRect( dc, &rect, brush );
            DeleteObject( brush );
            InflateRect( &rect, -45, -45 );
            brush = CreateSolidBrush( RGB( 255, 0, 255 ) );
            FillRect( dc, &rect, brush );
            DeleteObject( brush );
        }
        else if (hwnd == uniform)
        {
            brush = CreateSolidBrush( RGB( 0, 80, 255 ) );
            FillRect( dc, &rect, brush );
            DeleteObject( brush );
        }
        else if (hwnd == controls)
        {
            char text[256];
            FillRect( dc, &rect, (HBRUSH)(COLOR_WINDOW + 1) );
            snprintf( text, sizeof(text),
                      "Red: alpha ramp. Blue: 50%% opacity. Green: color-key ring.\n"
                      "M motion | R resize/remap | F fullscreen | Esc exit | clicks: %u%s",
                      clicks, own_compositor ? " | C toggle | 5 cycles" : "" );
            SetBkMode( dc, TRANSPARENT );
            DrawTextA( dc, text, -1, &rect, DT_LEFT | DT_WORDBREAK );
        }
        EndPaint( hwnd, &paint );
        return 0;
    }
    case WM_LBUTTONDOWN:
        clicks++;
        printf( "click=%u window=%s x=%d y=%d\n", clicks,
                hwnd == ramp ? "ramp" : hwnd == uniform ? "uniform" : hwnd == keyed ? "keyed" : "controls",
                (short)LOWORD(lp), (short)HIWORD(lp) );
        fflush( stdout );
        InvalidateRect( controls, NULL, TRUE );
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) DestroyWindow( controls );
        else if (wp == 'M') moving = !moving;
        else if (wp == 'R')
        {
            ShowWindow( ramp, SW_HIDE );
            large_ramp = !large_ramp;
            update_ramp();
            ShowWindow( ramp, SW_SHOWNOACTIVATE );
            printf( "resize=%u\n", large_ramp );
            fflush( stdout );
        }
        else if (wp == 'F') toggle_fullscreen();
        else if (wp == 'C' && own_compositor)
        {
            BOOL enable = FALSE;
            get_composition( &enable );
            report_composition( !enable );
        }
        else if (wp == '5' && own_compositor)
        {
            report_composition( TRUE );
            cycle = 0;
            SetTimer( controls, 2, 700, NULL );
        }
        return 0;
    case WM_TIMER:
        if (wp == 1 && moving && !fullscreen)
        {
            int offset = tick++ % 160;
            if (offset > 80) offset = 160 - offset;
            SetWindowPos( ramp, NULL, 180 + offset, 170, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
        }
        else if (wp == 2)
        {
            report_composition( cycle++ % 2 );
            if (cycle == 10) { KillTimer( controls, 2 ); puts( "cycles=5 complete" ); fflush( stdout ); }
        }
        return 0;
    case WM_DESTROY:
        if (hwnd == controls) PostQuitMessage( 0 );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

static void CALLBACK wm_callback( const WEX_WM_EVENT *event, void *context )
{
    /* The isolated probe display has no foreign applications to manage. */
    if (event->type == WEX_WM_MAP) printf( "unexpected foreign window=%llu\n", event->window );
}

int main( int argc, char **argv )
{
    WNDCLASSW cls = { 0 };
    MSG msg;
    HMODULE host = NULL;
    if (argc > 1 && !strcmp( argv[1], "--own-compositor" ))
    {
        DWORD (WINAPI *start)( UINT64, WEX_WM_CALLBACK, void * );
        UINT64 desktop = 0;
        DWORD err;
        int tries;
        /* the desktop's X window appears once explorer has created the virtual desktop */
        /* from another process the property can read 0: then pass the X window ("xdotool search --name 'Wine desktop'") */
        if (argc > 2) desktop = strtoull( argv[2], NULL, 0 );
        for (tries = 0; tries < 50 && !desktop; tries++)
        {
            desktop = (ULONG_PTR)GetPropW( GetDesktopWindow(), L"__wine_x11_whole_window" );
            if (!desktop) Sleep( 100 );
        }
        host = LoadLibraryW( L"wexhost.dll" );
        if (!host) { fprintf( stderr, "no wexhost.dll\n" ); return 1; }
        start = (void *)GetProcAddress( host, "WexWmStart" );
        stop_wm = (void *)GetProcAddress( host, "WexWmStop" );
        set_composition = (void *)GetProcAddress( host, "WexCompositionSetEnabled" );
        get_composition = (void *)GetProcAddress( host, "WexCompositionGetEnabled" );
        if (!desktop || !start || !stop_wm || !set_composition || !get_composition)
        { fprintf( stderr, "no X11 desktop (%llx) or composition API (%p %p %p %p)\n", desktop, start, stop_wm,
                   set_composition, get_composition ); return 1; }
        if ((err = start( desktop, wm_callback, NULL )))
        { fprintf( stderr, "WexWmStart failed: %lu (use an isolated unmanaged display)\n", err ); return 1; }
        own_compositor = TRUE;
        report_composition( TRUE );
    }
    cls.lpfnWndProc = probe_proc;
    cls.hInstance = GetModuleHandleW( NULL );
    cls.hCursor = LoadCursorW( NULL, (LPCWSTR)IDC_ARROW );
    cls.lpszClassName = L"WineExplorerCompositionProbe";
    RegisterClassW( &cls );
    controls = CreateWindowW( cls.lpszClassName, L"Composition probe controls", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              110, 500, 800, 160, NULL, NULL, cls.hInstance, NULL );
    ramp = CreateWindowExW( WS_EX_LAYERED, cls.lpszClassName, L"Composition alpha ramp", WS_POPUP,
                            180, 170, 280, 160, NULL, NULL, cls.hInstance, NULL );
    uniform = CreateWindowExW( WS_EX_LAYERED, cls.lpszClassName, L"Composition uniform alpha", WS_POPUP,
                               330, 230, 280, 170, NULL, NULL, cls.hInstance, NULL );
    keyed = CreateWindowExW( WS_EX_LAYERED, cls.lpszClassName, L"Composition color key", WS_POPUP,
                             520, 150, 220, 200, NULL, NULL, cls.hInstance, NULL );
    if (!controls || !ramp || !uniform || !keyed)
    { fprintf( stderr, "CreateWindow failed: %lu\n", GetLastError() ); return 1; }
    update_ramp();
    if (!SetLayeredWindowAttributes( uniform, 0, 128, LWA_ALPHA ) ||
        !SetLayeredWindowAttributes( keyed, RGB( 255, 0, 255 ), 255, LWA_COLORKEY ))
    {
        fprintf( stderr, "SetLayeredWindowAttributes failed: %lu\n", GetLastError() );
        failures++;
    }
    ShowWindow( ramp, SW_SHOWNOACTIVATE );
    ShowWindow( uniform, SW_SHOWNOACTIVATE );
    ShowWindow( keyed, SW_SHOWNOACTIVATE );
    SetTimer( controls, 1, 30, NULL );
    puts( "probe ready" );
    fflush( stdout );
    while (GetMessageW( &msg, NULL, 0, 0 ) > 0) { TranslateMessage( &msg ); DispatchMessageW( &msg ); }
    DestroyWindow( ramp );
    DestroyWindow( uniform );
    DestroyWindow( keyed );
    if (own_compositor) { report_composition( FALSE ); stop_wm(); }
    if (host) FreeLibrary( host );
    return failures ? 1 : 0;
}
