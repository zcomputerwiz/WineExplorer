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
 * S toggles a class-shadow popup, B blends a popup in/out over 900 ms, U blends
 * the existing 50% alpha window, 0 blends with zero duration, P opens
 * a TrackPopupMenu (automatic menu fade is deferred), D toggles the drop-shadow
 * SPI and remaps the popup. K captures through a compatible bitmap, J through
 * a DIB; T captures a half-size StretchBlt through a compatible bitmap. All
 * save Z:\\tmp\\capture.bmp and show a preview. The screen DC stays
 * open across compositor toggles. Esc exits. Clicking solid pixels logs a click; a color-key hole must let a
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

static HWND controls, ramp, uniform, keyed, shadow_popup, fader, capture_view;
static HDC capture_dc;
static HBITMAP capture_bitmap;
static int capture_width, capture_height;
static BOOL original_shadow, have_original_shadow;
static BOOL moving = TRUE, large_ramp, fullscreen, own_compositor;
static unsigned int tick, cycle, clicks, failures;
static DWORD (WINAPI *set_composition)( BOOL );
static DWORD (WINAPI *get_composition)( BOOL * );
static void (WINAPI *stop_wm)( void );

static void blend_popup( HWND window, DWORD duration )
{
    BOOL hide = IsWindowVisible( window ), had_attributes, attributes_restored = TRUE;
    LONG style = GetWindowLongW( window, GWL_EXSTYLE );
    COLORREF key, actual_key;
    BYTE alpha, actual_alpha;
    DWORD flags, actual_flags;
    DWORD start = GetTickCount(), elapsed;
    BOOL result;
    had_attributes = GetLayeredWindowAttributes( window, &key, &alpha, &flags );
    result = AnimateWindow( window, duration, AW_BLEND | (hide ? AW_HIDE : 0) );
    elapsed = GetTickCount() - start;
    if (had_attributes)
        attributes_restored = GetLayeredWindowAttributes( window, &actual_key, &actual_alpha, &actual_flags ) &&
                              key == actual_key && alpha == actual_alpha && flags == actual_flags;
    if (!result || IsWindowVisible( window ) == hide || GetWindowLongW( window, GWL_EXSTYLE ) != style ||
        !attributes_restored)
        failures++;
    printf( "blend window=%s hide=%u result=%u duration=%lu elapsed=%lu visible=%u style_restored=%u attributes_restored=%u\n",
            window == uniform ? "uniform" : "popup", hide, result, duration, elapsed,
            IsWindowVisible( window ), GetWindowLongW( window, GWL_EXSTYLE ) == style, attributes_restored );
    fflush( stdout );
}

static void popup_menu(void)
{
    HMENU menu = CreatePopupMenu();
    POINT position;
    BOOL animation = FALSE, fade = FALSE;
    UINT command;
    AppendMenuW( menu, MF_STRING, 1, L"Menu shadow and fade observation" );
    AppendMenuW( menu, MF_STRING, 2, L"Second item" );
    GetCursorPos( &position );
    SystemParametersInfoW( SPI_GETMENUANIMATION, 0, &animation, 0 );
    SystemParametersInfoW( SPI_GETMENUFADE, 0, &fade, 0 );
    command = TrackPopupMenu( menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, position.x, position.y, 0, controls, NULL );
    printf( "menu command=%u animation=%u fade=%u (automatic fade deferred)\n", command, animation, fade );
    fflush( stdout );
    DestroyMenu( menu );
}

static void capture_screen( BOOL dib, BOOL scaled )
{
    BITMAPINFO info = { 0 };
    BITMAPFILEHEADER header = { 0 };
    HDC memory = CreateCompatibleDC( capture_dc );
    HBITMAP bitmap = NULL, compatible = NULL, previous;
    HANDLE file = INVALID_HANDLE_VALUE;
    void *pixels;
    DWORD bytes, written;
    BOOL result = FALSE;
    int width = GetSystemMetrics( SM_CXVIRTUALSCREEN ), height = GetSystemMetrics( SM_CYVIRTUALSCREEN );
    int source_width = width, source_height = height;
    int x = GetSystemMetrics( SM_XVIRTUALSCREEN ), y = GetSystemMetrics( SM_YVIRTUALSCREEN );
    if (capture_view) ShowWindow( capture_view, SW_HIDE );
    /* Allow the compositor to consume the preview's unmap before reading. */
    Sleep( 100 );
    if (scaled) { width = max( 1, width / 2 ); height = max( 1, height / 2 ); }
    if (!memory || width <= 0 || height <= 0 ||
        (ULONGLONG)width * height * 4 > MAXDWORD - sizeof(header) - sizeof(info.bmiHeader)) goto done;
    bytes = (DWORD)width * height * 4;
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    info.bmiHeader.biSizeImage = bytes;
    bitmap = CreateDIBSection( capture_dc, &info, DIB_RGB_COLORS, &pixels, NULL, 0 );
    if (!bitmap) goto done;
    if (!dib && !(compatible = CreateCompatibleBitmap( capture_dc, width, height ))) goto done;
    previous = SelectObject( memory, dib ? bitmap : compatible );
    result = scaled ? StretchBlt( memory, 0, 0, width, height, capture_dc, x, y, source_width, source_height, SRCCOPY ) :
                      BitBlt( memory, 0, 0, width, height, capture_dc, x, y, SRCCOPY );
    GdiFlush();
    SelectObject( memory, previous );
    if (!result) goto done;
    if (!dib) result = GetDIBits( capture_dc, compatible, 0, height, pixels, &info, DIB_RGB_COLORS ) == height;
    if (!result) goto done;
    result = FALSE;
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
    header.bfSize = header.bfOffBits + bytes;
    file = CreateFileW( L"Z:\\tmp\\capture.bmp", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL );
    if (file == INVALID_HANDLE_VALUE) goto done;
    if (!WriteFile( file, &header, sizeof(header), &written, NULL ) || written != sizeof(header) ||
        !WriteFile( file, &info.bmiHeader, sizeof(info.bmiHeader), &written, NULL ) || written != sizeof(info.bmiHeader) ||
        !WriteFile( file, pixels, bytes, &written, NULL ) || written != bytes) goto done;
    result = TRUE;
    if (capture_bitmap) DeleteObject( capture_bitmap );
    capture_bitmap = bitmap;
    bitmap = NULL;
    capture_width = width;
    capture_height = height;
    if (!capture_view)
        capture_view = CreateWindowW( L"WineExplorerCompositionProbe", L"Screen DC capture preview", WS_OVERLAPPEDWINDOW,
                                      30, 30, 480, 320, NULL, NULL, GetModuleHandleW( NULL ), NULL );
    ShowWindow( capture_view, SW_SHOWNOACTIVATE );
    InvalidateRect( capture_view, NULL, TRUE );
done:
    if (file != INVALID_HANDLE_VALUE) CloseHandle( file );
    if (bitmap) DeleteObject( bitmap );
    if (compatible) DeleteObject( compatible );
    if (memory) DeleteDC( memory );
    if (!result) failures++;
    printf( "capture path=%s scaled=%u result=%u size=%dx%d origin=%d,%d file=Z:\\tmp\\capture.bmp\n",
            dib ? "dib-readback" : "compatible-copy", scaled, result, width, height, x, y );
    fflush( stdout );
}

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
            char text[512];
            FillRect( dc, &rect, (HBRUSH)(COLOR_WINDOW + 1) );
            snprintf( text, sizeof(text),
                      "Red: alpha ramp. Blue: 50%% opacity. Green: color-key ring.\n"
                      "M motion | R resize/remap | F fullscreen | S shadow | B blend | U alpha blend | 0 instant | P menu\n"
                      "D shadow SPI | K capture copy | J capture DIB | T scaled capture | Esc exit | clicks: %u%s",
                      clicks, own_compositor ? " | C toggle | 5 cycles" : "" );
            SetBkMode( dc, TRANSPARENT );
            DrawTextA( dc, text, -1, &rect, DT_LEFT | DT_WORDBREAK );
        }
        else if (hwnd == shadow_popup || hwnd == fader)
        {
            FillRect( dc, &rect, (HBRUSH)(COLOR_INFOBK + 1) );
            DrawTextW( dc, hwnd == fader ? L"AW_BLEND: 900 ms in/out" : L"CS_DROPSHADOW: S hide/show, D toggle SPI",
                       -1, &rect, DT_CENTER | DT_WORDBREAK );
        }
        else if (hwnd == capture_view && capture_bitmap)
        {
            HDC memory = CreateCompatibleDC( dc );
            HBITMAP previous = SelectObject( memory, capture_bitmap );
            StretchBlt( dc, 0, 0, rect.right, rect.bottom, memory, 0, 0, capture_width, capture_height, SRCCOPY );
            SelectObject( memory, previous );
            DeleteDC( memory );
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
        else if (wp == 'B') blend_popup( fader, 900 );
        else if (wp == 'U') blend_popup( uniform, 900 );
        else if (wp == '0') blend_popup( fader, 0 );
        else if (wp == 'P') popup_menu();
        else if (wp == 'K' || wp == 'J' || wp == 'T') capture_screen( wp == 'J', wp == 'T' );
        else if (wp == 'S') ShowWindow( shadow_popup, IsWindowVisible( shadow_popup ) ? SW_HIDE : SW_SHOWNOACTIVATE );
        else if (wp == 'D')
        {
            BOOL shadow = FALSE;
            SystemParametersInfoW( SPI_GETDROPSHADOW, 0, &shadow, 0 );
            shadow = !shadow;
            if (!SystemParametersInfoW( SPI_SETDROPSHADOW, 0, (void *)(ULONG_PTR)shadow, SPIF_SENDCHANGE )) failures++;
            ShowWindow( shadow_popup, SW_HIDE );
            ShowWindow( shadow_popup, SW_SHOWNOACTIVATE );
            printf( "drop_shadow=%u\n", shadow );
            fflush( stdout );
        }
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
            SetWindowPos( shadow_popup, NULL, 650 + offset, 370, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE );
        }
        else if (wp == 2)
        {
            report_composition( cycle++ % 2 );
            if (cycle == 10) { KillTimer( controls, 2 ); puts( "cycles=5 complete" ); fflush( stdout ); }
        }
        return 0;
    case WM_DESTROY:
        if (hwnd == controls) PostQuitMessage( 0 );
        if (hwnd == capture_view) capture_view = NULL;
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
    have_original_shadow = SystemParametersInfoW( SPI_GETDROPSHADOW, 0, &original_shadow, 0 );
    capture_dc = GetDC( NULL );
    controls = CreateWindowW( cls.lpszClassName, L"Composition probe controls", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              110, 500, 800, 160, NULL, NULL, cls.hInstance, NULL );
    ramp = CreateWindowExW( WS_EX_LAYERED, cls.lpszClassName, L"Composition alpha ramp", WS_POPUP,
                            180, 170, 280, 160, NULL, NULL, cls.hInstance, NULL );
    uniform = CreateWindowExW( WS_EX_LAYERED, cls.lpszClassName, L"Composition uniform alpha", WS_POPUP,
                               330, 230, 280, 170, NULL, NULL, cls.hInstance, NULL );
    keyed = CreateWindowExW( WS_EX_LAYERED, cls.lpszClassName, L"Composition color key", WS_POPUP,
                             520, 150, 220, 200, NULL, NULL, cls.hInstance, NULL );
    fader = CreateWindowW( cls.lpszClassName, L"Blend fade popup", WS_POPUP | WS_BORDER,
                           160, 370, 300, 100, NULL, NULL, cls.hInstance, NULL );
    cls.lpszClassName = L"WineExplorerShadowProbe";
    cls.style = CS_DROPSHADOW;
    RegisterClassW( &cls );
    shadow_popup = CreateWindowW( cls.lpszClassName, L"Class shadow popup", WS_POPUP | WS_BORDER,
                                  650, 370, 260, 100, NULL, NULL, cls.hInstance, NULL );
    if (!controls || !ramp || !uniform || !keyed || !fader || !shadow_popup || !capture_dc)
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
    ShowWindow( shadow_popup, SW_SHOWNOACTIVATE );
    SetTimer( controls, 1, 30, NULL );
    puts( "probe ready" );
    fflush( stdout );
    while (GetMessageW( &msg, NULL, 0, 0 ) > 0) { TranslateMessage( &msg ); DispatchMessageW( &msg ); }
    DestroyWindow( ramp );
    DestroyWindow( uniform );
    DestroyWindow( keyed );
    DestroyWindow( shadow_popup );
    DestroyWindow( fader );
    if (capture_view) DestroyWindow( capture_view );
    if (capture_bitmap) DeleteObject( capture_bitmap );
    ReleaseDC( NULL, capture_dc );
    if (have_original_shadow)
        SystemParametersInfoW( SPI_SETDROPSHADOW, 0, (void *)(ULONG_PTR)original_shadow, SPIF_SENDCHANGE );
    if (own_compositor) { report_composition( FALSE ); stop_wm(); }
    if (host) FreeLibrary( host );
    return failures ? 1 : 0;
}
