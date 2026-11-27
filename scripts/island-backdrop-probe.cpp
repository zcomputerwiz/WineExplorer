/*
 * Probe of a WinUI window's bars: system backdrop, extended frame and child islands without a redirection bitmap
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

/* Does what Windows Notepad (WinUI 3) does with its window, without WinUI: DwmSetWindowAttribute 38 (system backdrop) =
 * ISLAND_BACKDROP (default 4, Mica Alt), 20 (dark mode) = ISLAND_DARK (default 0), DwmExtendFrameIntoClientArea top 75,
 * bottom 32, and a WM_PAINT that fills those strips with BLACK_BRUSH and the rest (the editor) with 0xf9f9f9. Two child
 * islands, WS_CHILD | WS_EX_NOREDIRECTIONBITMAP, each with a DirectComposition target whose visual shows a composition
 * swapchain cleared to transparent with an opaque dark-grey (0x404040) square in its middle, as XAML draws text on a
 * transparent bar: a menu row (1000x33 at 0,42) and a status bar (1000x32 at 0,bottom-32). Window 1016x600 at 40,40.
 *
 * What Windows shows: the strips are the backdrop (Mica Alt: about 0xf3f3f3 light, 0x202020 dark, with the wallpaper
 * tinting it), the islands' transparent pixels show the strip under them, the squares are dark grey. B-notepad-bars: on
 * Wine the strips and the islands are black, so the squares (the text) cannot be seen.
 *
 * Build: x86_64-w64-mingw32-g++ -O2 -municode -o island-backdrop-probe.exe island-backdrop-probe.cpp \
 *            -ld3d11 -ldxgi -ldcomp -ldwmapi -lgdi32 -luser32 -mwindows -static */

#include <windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <stdlib.h>

#define WIDTH 1016
#define HEIGHT 600
#define TOP 75
#define BOTTOM 32

static ID3D11Device *device;
static ID3D11DeviceContext *context;
static IDCompositionDevice *dcomp;

static LRESULT CALLBACK main_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint( hwnd, &ps );
        HBRUSH editor = CreateSolidBrush( RGB(0xf9, 0xf9, 0xf9) );
        /* ISLAND_RED_STRIPS=1 (diagnosis): red strips instead of black, so the islands' transparent pixels show */
        HBRUSH strip = getenv( "ISLAND_RED_STRIPS" ) ? CreateSolidBrush( RGB(255, 0, 0) ) :
                       (HBRUSH)GetStockObject( BLACK_BRUSH );
        RECT client, rect;
        GetClientRect( hwnd, &client );
        SetRect( &rect, 0, 0, client.right, TOP );
        FillRect( hdc, &rect, strip );
        SetRect( &rect, 0, client.bottom - BOTTOM, client.right, client.bottom );
        FillRect( hdc, &rect, strip );
        SetRect( &rect, 0, TOP, client.right, client.bottom - BOTTOM );
        FillRect( hdc, &rect, editor );
        DeleteObject( editor );
        EndPaint( hwnd, &ps );
        return 0;
    }
    case WM_TIMER:
        PostQuitMessage( 0 );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

/* a composition swapchain on a DirectComposition target for the island: transparent, with an opaque square */
static void make_island( HWND parent, int x, int y, int cx, int cy )
{
    HWND island = CreateWindowExW( WS_EX_NOREDIRECTIONBITMAP, L"Static", NULL, WS_CHILD | WS_VISIBLE,
                                   x, y, cx, cy, parent, NULL, NULL, NULL );
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    IDXGIDevice *dxgi;
    IDXGIAdapter *adapter;
    IDXGIFactory2 *factory;
    IDXGISwapChain1 *swapchain;
    ID3D11Texture2D *buffer;
    ID3D11RenderTargetView *view;
    IDCompositionTarget *target;
    IDCompositionVisual *visual;
    const float clear[4] = { 0, 0, 0, 0 };
    /* the square is written with UpdateSubresource (Wine's ClearView with rectangles is a stub) */
    D3D11_BOX square = { (UINT)(cx / 2 - cy / 2), 4, 0, (UINT)(cx / 2 + cy / 2), (UINT)(cy - 4), 1 };
    UINT square_w = square.right - square.left, square_h = square.bottom - square.top;
    UINT *pixels = (UINT *)malloc( square_w * square_h * 4 );

    device->QueryInterface( __uuidof(IDXGIDevice), (void **)&dxgi );
    dxgi->GetAdapter( &adapter );
    adapter->GetParent( __uuidof(IDXGIFactory2), (void **)&factory );
    desc.Width = cx;
    desc.Height = cy;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    if (FAILED(factory->CreateSwapChainForComposition( device, &desc, NULL, &swapchain ))) return;
    swapchain->GetBuffer( 0, __uuidof(ID3D11Texture2D), (void **)&buffer );
    device->CreateRenderTargetView( buffer, NULL, &view );
    context->ClearRenderTargetView( view, clear );
    for (UINT i = 0; i < square_w * square_h; i++) pixels[i] = 0xff404040;   /* opaque dark grey, BGRA */
    context->UpdateSubresource( buffer, 0, &square, pixels, square_w * 4, 0 );
    free( pixels );
    swapchain->Present( 0, 0 );

    dcomp->CreateTargetForHwnd( island, TRUE, &target );
    dcomp->CreateVisual( &visual );
    visual->SetContent( swapchain );
    target->SetRoot( visual );
    dcomp->Commit();
}

int WINAPI wWinMain( HINSTANCE instance, HINSTANCE prev, WCHAR *cmdline, int show )
{
    WNDCLASSW cls = {};
    const char *backdrop_env = getenv( "ISLAND_BACKDROP" ), *dark_env = getenv( "ISLAND_DARK" );
    const char *seconds = getenv( "ISLAND_SECONDS" );
    DWORD backdrop = backdrop_env ? atoi( backdrop_env ) : 4, dark = dark_env ? atoi( dark_env ) : 0;
    MARGINS margins = { 0, 0, TOP, BOTTOM };
    IDXGIDevice *dxgi;
    HWND hwnd;
    RECT client;
    MSG msg;

    cls.lpfnWndProc = main_proc;
    cls.hInstance = instance;
    cls.hCursor = LoadCursorW( NULL, (const WCHAR *)IDC_ARROW );
    cls.lpszClassName = L"IslandBackdropProbe";
    RegisterClassW( &cls );
    hwnd = CreateWindowExW( 0, L"IslandBackdropProbe", L"Island backdrop probe", WS_OVERLAPPEDWINDOW,
                            40, 40, WIDTH, HEIGHT, NULL, NULL, instance, NULL );
    DwmSetWindowAttribute( hwnd, 20, &dark, sizeof(dark) );       /* DWMWA_USE_IMMERSIVE_DARK_MODE */
    DwmSetWindowAttribute( hwnd, 38, &backdrop, sizeof(backdrop) ); /* DWMWA_SYSTEMBACKDROP_TYPE */
    DwmExtendFrameIntoClientArea( hwnd, &margins );
    ShowWindow( hwnd, SW_SHOW );
    UpdateWindow( hwnd );

    if (SUCCEEDED(D3D11CreateDevice( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                                     D3D11_SDK_VERSION, &device, NULL, &context )))
    {
        device->QueryInterface( __uuidof(IDXGIDevice), (void **)&dxgi );
        if (SUCCEEDED(DCompositionCreateDevice( dxgi, __uuidof(IDCompositionDevice), (void **)&dcomp )))
        {
            GetClientRect( hwnd, &client );
            make_island( hwnd, 0, 42, client.right, 33 );
            make_island( hwnd, 0, client.bottom - BOTTOM, client.right, BOTTOM );
        }
    }
    SetTimer( hwnd, 1, (seconds ? atoi( seconds ) : 60) * 1000, NULL );
    while (GetMessageW( &msg, NULL, 0, 0 ))
    {
        TranslateMessage( &msg );
        DispatchMessageW( &msg );
    }
    return 0;
}
