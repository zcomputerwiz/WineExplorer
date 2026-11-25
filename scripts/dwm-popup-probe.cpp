/*
 * WineExplorer Desktop Environment - probe for a translucent popup that Chromium and Edge draw (DWM P7b slice 2)
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

/* A plain orange popup fills the screen. Over it, at (100,100), a 400x300 popup the way Chromium builds one: a top-level
 * window that is a glass sheet (DwmExtendFrameIntoClientArea, margins -1) with a child window as its client, and a
 * DirectComposition target on that child: a premultiplied composition swap chain with a white 360x260 card at (20,20)
 * and a soft shadow around it (black, 30 % at the card's edge down to nothing over 12 pixels). One Commit, then a Present
 * every 100 ms; ends after 60 s or when C:\popup.stop exists.
 * The screen then shows (checked by test_sysrun_server.sh, section c4_dwm_popup, from an X screenshot):
 *   inside the card (300,250 on the screen)     white
 *   3 pixels below the card (300,383)           darker than the orange with a compositor (the shadow), orange
 *                                               without one (the window is shaped to the card)
 *   14 pixels below the card (300,394)          the orange, always
 * It writes C:\popup.log and says "popup ready" in it when the first frame is presented.
 * Build: x86_64-w64-mingw32-g++ -municode -mwindows -o dwm-popup-probe.exe dwm-popup-probe.cpp
 *        -ld3d11 -ldxgi -ldcomp -luser32 -lgdi32 */

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <stdio.h>

#define POPUP_W 400
#define POPUP_H 300
#define CARD_X  20
#define CARD_Y  20
#define CARD_W  360
#define CARD_H  260
#define SHADOW  12

typedef struct { int left, right, top, bottom; } margins_t;

static void card_pixels( DWORD *pixels )
{
    for (int y = 0; y < POPUP_H; y++)
        for (int x = 0; x < POPUP_W; x++)
        {
            int dx = x < CARD_X ? CARD_X - x : x >= CARD_X + CARD_W ? x - (CARD_X + CARD_W) + 1 : 0;
            int dy = y < CARD_Y ? CARD_Y - y : y >= CARD_Y + CARD_H ? y - (CARD_Y + CARD_H) + 1 : 0;
            int d = dx > dy ? dx : dy;   /* 0 inside the card, then the distance to its edge */

            if (!d) pixels[y * POPUP_W + x] = 0xffffffff;
            else if (d <= SHADOW) pixels[y * POPUP_W + x] = (DWORD)(77 * (SHADOW - d + 1) / (SHADOW + 1)) << 24;   /* 30 % */
            else pixels[y * POPUP_W + x] = 0;   /* premultiplied: black with alpha 0 */
        }
}

/* like Chromium's host window: it erases nothing and paints nothing (the DirectComposition target is its content) */
static LRESULT CALLBACK sheet_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        BeginPaint( hwnd, &ps );
        EndPaint( hwnd, &ps );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

int WINAPI wWinMain( HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show )
{
    static DWORD pixels[POPUP_H * POPUP_W];
    WNDCLASSW wc = {};
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    IDXGIDevice *dxgi_device = NULL;
    IDXGIAdapter *adapter = NULL;
    IDXGIFactory2 *factory = NULL;
    IDXGISwapChain1 *swapchain = NULL;
    IDCompositionDevice *dcomp = NULL;
    IDCompositionTarget *target = NULL;
    IDCompositionVisual *visual = NULL;
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    HRESULT (WINAPI *extend)( HWND, const margins_t * );
    static const WCHAR *stop = L"C:\\popup.stop";
    HWND backdrop, popup, child;
    DWORD deadline;
    BOOL ready = FALSE;
    HRESULT hr;
    HMODULE dwmapi;

    _wfreopen( L"C:\\popup.log", L"w", stdout );
    wc.hInstance     = inst;
    wc.hbrBackground = CreateSolidBrush( RGB(255, 128, 0) );
    wc.lpfnWndProc   = DefWindowProcW;
    wc.lpszClassName = L"DwmPopupBackdrop";
    RegisterClassW( &wc );
    backdrop = CreateWindowExW( 0, wc.lpszClassName, L"Backdrop", WS_POPUP, 0, 0, 1024, 740, NULL, NULL, inst, NULL );
    ShowWindow( backdrop, SW_SHOWNOACTIVATE );
    UpdateWindow( backdrop );

    wc.hbrBackground = NULL;
    wc.lpfnWndProc   = sheet_proc;
    wc.lpszClassName = L"DwmPopupSheet";
    RegisterClassW( &wc );
    wc.lpfnWndProc   = DefWindowProcW;
    wc.lpszClassName = L"DwmPopupClient";
    RegisterClassW( &wc );
    popup = CreateWindowExW( WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP, L"DwmPopupSheet", L"Popup", WS_POPUP, 100, 100, POPUP_W, POPUP_H,
                             NULL, NULL, inst, NULL );
    child = CreateWindowExW( WS_EX_NOREDIRECTIONBITMAP, L"DwmPopupClient", L"Client", WS_CHILD | WS_VISIBLE, 0, 0, POPUP_W, POPUP_H,
                             popup, NULL, inst, NULL );
    /* the glass sheet: Chromium gives the whole window extended frame (margins of -1) */
    dwmapi = LoadLibraryW( L"dwmapi.dll" );
    extend = dwmapi ? (HRESULT (WINAPI *)( HWND, const margins_t * ))GetProcAddress( dwmapi, "DwmExtendFrameIntoClientArea" ) : NULL;
    if (extend)
    {
        margins_t m = { -1, -1, -1, -1 };
        printf( "DwmExtendFrameIntoClientArea hr %08lx\n", extend( popup, &m ) );
    }
    ShowWindow( popup, SW_SHOWNOACTIVATE );

    hr = D3D11CreateDevice( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                            D3D11_SDK_VERSION, &device, NULL, &context );
    printf( "D3D11CreateDevice hr %08lx\n", hr );
    if (FAILED(hr)) return 1;
    device->QueryInterface( __uuidof(IDXGIDevice), (void **)&dxgi_device );
    dxgi_device->GetAdapter( &adapter );
    adapter->GetParent( __uuidof(IDXGIFactory2), (void **)&factory );

    desc.Width = POPUP_W;
    desc.Height = POPUP_H;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    hr = factory->CreateSwapChainForComposition( device, &desc, NULL, &swapchain );
    printf( "CreateSwapChainForComposition hr %08lx\n", hr );
    if (FAILED(hr)) return 1;
    hr = DCompositionCreateDevice( dxgi_device, __uuidof(IDCompositionDevice), (void **)&dcomp );
    printf( "DCompositionCreateDevice hr %08lx\n", hr );
    if (FAILED(hr)) return 1;
    hr = dcomp->CreateTargetForHwnd( child, TRUE, &target );   /* a child window, as Chromium's */
    printf( "CreateTargetForHwnd hr %08lx\n", hr );
    dcomp->CreateVisual( &visual );
    visual->SetContent( swapchain );
    target->SetRoot( visual );
    hr = dcomp->Commit();
    printf( "Commit hr %08lx\n", hr );
    fflush( stdout );

    card_pixels( pixels );
    deadline = GetTickCount() + 60000;
    while (GetTickCount() < deadline)
    {
        MSG msg;
        ID3D11Texture2D *buffer = NULL;

        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE )) DispatchMessageW( &msg );
        swapchain->GetBuffer( 0, __uuidof(ID3D11Texture2D), (void **)&buffer );
        context->UpdateSubresource( buffer, 0, NULL, pixels, POPUP_W * 4, 0 );
        buffer->Release();
        hr = swapchain->Present( 1, 0 );
        if (!ready)
        {
            printf( "popup ready (Present hr %08lx)\n", hr );
            fflush( stdout );
            ready = TRUE;
        }
        if (GetFileAttributesW( stop ) != INVALID_FILE_ATTRIBUTES) break;
        Sleep( 100 );
    }
    return 0;
}
