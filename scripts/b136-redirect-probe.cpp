/*
 * Probe of B136: ordinary GDI windows of a process that renders through Direct3D 11 or DirectComposition show black on X
 *
 * Copyright 2026 WineExplorer contributors
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/* A main window (green client area, 400x300 at 50,50) that, by B136_MODE, renders nothing else ("plain"), presents a
 * blue frame through a D3D11 flip-model swapchain on its HWND ("d3d"), or gets a DirectComposition target ("dcomp"), or
 * does either on a WS_CHILD | WS_EX_NOREDIRECTIONBITMAP child, as WinUI's islands do ("d3dchild", "dcompchild").
 * Then an ordinary GDI top-level window ("B136 GDI window", pure red client area, 300x200 at 600,100) and, with
 * B136_DIALOG=1, a Save As dialog (=2: the Vista-style IFileSaveDialog). On X the red window must show on the root (xwd -root, import -window root); B136 is
 * that it shows black in the d3d and dcomp modes. Runs for B136_SECONDS (default 60), then exits.
 *
 * Build: x86_64-w64-mingw32-g++ -O2 -municode -o b136-redirect-probe.exe b136-redirect-probe.cpp \
 *            -ld3d11 -ldxgi -ldcomp -lcomdlg32 -lole32 -luuid -lgdi32 -luser32 -mwindows -static */

#include <windows.h>
#include <commdlg.h>
#include <shobjidl.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <stdlib.h>
#include <wchar.h>

static COLORREF fill_colour( HWND hwnd )
{
    return (COLORREF)GetWindowLongPtrW( hwnd, GWLP_USERDATA );
}

static LRESULT CALLBACK wnd_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint( hwnd, &ps );
        HBRUSH brush = CreateSolidBrush( fill_colour( hwnd ) );
        RECT rect;
        GetClientRect( hwnd, &rect );
        FillRect( hdc, &rect, brush );
        DeleteObject( brush );
        EndPaint( hwnd, &ps );
        return 0;
    }
    case WM_TIMER:
        PostQuitMessage( 0 );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

static HWND make_window( const WCHAR *title, int x, int y, int cx, int cy, COLORREF colour, DWORD ex_style )
{
    HWND hwnd = CreateWindowExW( ex_style, L"B136Probe", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, x, y, cx, cy,
                                 NULL, NULL, GetModuleHandleW( NULL ), NULL );
    SetWindowLongPtrW( hwnd, GWLP_USERDATA, colour );
    InvalidateRect( hwnd, NULL, TRUE );
    UpdateWindow( hwnd );
    return hwnd;
}

static ID3D11Device *device;
static ID3D11DeviceContext *context;

static BOOL start_d3d( HWND hwnd )
{
    DXGI_SWAP_CHAIN_DESC desc = {};
    IDXGISwapChain *swapchain;
    ID3D11Texture2D *buffer;
    ID3D11RenderTargetView *view;
    const float blue[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
    RECT rect;

    GetClientRect( hwnd, &rect );
    desc.BufferDesc.Width = rect.right;
    desc.BufferDesc.Height = rect.bottom;
    desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.OutputWindow = hwnd;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (FAILED(D3D11CreateDeviceAndSwapChain( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION,
                                              &desc, &swapchain, &device, NULL, &context )))
        return FALSE;
    swapchain->GetBuffer( 0, __uuidof(ID3D11Texture2D), (void **)&buffer );
    device->CreateRenderTargetView( buffer, NULL, &view );
    context->ClearRenderTargetView( view, blue );
    swapchain->Present( 0, 0 );
    return TRUE;
}

static BOOL start_dcomp( HWND hwnd )
{
    IDXGIDevice *dxgi;
    IDCompositionDevice *dcomp;
    IDCompositionTarget *target;
    IDCompositionVisual *visual;

    if (FAILED(D3D11CreateDevice( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                                  D3D11_SDK_VERSION, &device, NULL, &context )))
        return FALSE;
    device->QueryInterface( __uuidof(IDXGIDevice), (void **)&dxgi );
    if (FAILED(DCompositionCreateDevice( dxgi, __uuidof(IDCompositionDevice), (void **)&dcomp ))) return FALSE;
    if (FAILED(dcomp->CreateTargetForHwnd( hwnd, TRUE, &target ))) return FALSE;
    dcomp->CreateVisual( &visual );
    target->SetRoot( visual );
    dcomp->Commit();
    return TRUE;
}

int WINAPI wWinMain( HINSTANCE instance, HINSTANCE prev, WCHAR *cmdline, int show )
{
    WNDCLASSW cls = {};
    const char *mode = getenv( "B136_MODE" ), *seconds = getenv( "B136_SECONDS" ), *dialog = getenv( "B136_DIALOG" );
    HWND main_window, gdi_window;
    BOOL ok = TRUE;
    MSG msg;

    cls.lpfnWndProc = wnd_proc;
    cls.hInstance = instance;
    cls.hCursor = LoadCursorW( NULL, (const WCHAR *)IDC_ARROW );
    cls.lpszClassName = L"B136Probe";
    RegisterClassW( &cls );

    /* in the top-level modes the main window has no redirection bitmap, as a WinUI window does */
    main_window = make_window( L"B136 main window", 50, 50, 400, 300, RGB(0, 160, 0),
                               mode && (!strcmp( mode, "d3d" ) || !strcmp( mode, "dcomp" )) ? WS_EX_NOREDIRECTIONBITMAP : 0 );
    if (mode && !strcmp( mode, "d3d" )) ok = start_d3d( main_window );
    else if (mode && !strcmp( mode, "dcomp" )) ok = start_dcomp( main_window );
    else if (mode && (!strcmp( mode, "d3dchild" ) || !strcmp( mode, "dcompchild" )))
    {
        /* as WinUI's islands: the swapchain or target on a child window without a redirection bitmap */
        HWND child = CreateWindowExW( WS_EX_NOREDIRECTIONBITMAP, L"B136Probe", NULL, WS_CHILD | WS_VISIBLE,
                                      10, 10, 300, 100, main_window, NULL, instance, NULL );
        ok = mode[1] == '3' ? start_d3d( child ) : start_dcomp( child );
    }
    if (!ok) MessageBoxW( NULL, L"Direct3D or DirectComposition could not be started", L"B136 probe", MB_OK );
    gdi_window = make_window( L"B136 GDI window", 600, 100, 300, 200, RGB(255, 0, 0), 0 );
    SetTimer( gdi_window, 1, (seconds ? atoi( seconds ) : 60) * 1000, NULL );

    if (dialog && atoi( dialog ) == 2)
    {
        /* the Vista-style dialog, as Notepad uses it (comdlg32's item dialog with an explorer browser) */
        IFileSaveDialog *save;
        CoInitializeEx( NULL, COINIT_APARTMENTTHREADED );
        if (SUCCEEDED(CoCreateInstance( CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER, IID_IFileSaveDialog,
                                        (void **)&save )))
        {
            save->Show( main_window );
            save->Release();
        }
    }
    else if (dialog && atoi( dialog ))
    {
        WCHAR file[MAX_PATH] = L"b136.txt";
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = main_window;
        ofn.lpstrFile = file;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_EXPLORER;
        GetSaveFileNameW( &ofn );
    }

    while (GetMessageW( &msg, NULL, 0, 0 ))
    {
        TranslateMessage( &msg );
        DispatchMessageW( &msg );
    }
    return 0;
}
