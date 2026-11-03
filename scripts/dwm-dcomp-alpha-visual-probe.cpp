/*
 * WineExplorer Desktop Environment - visual probe for transparent DirectComposition targets (DWM P7b)
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

/* A striped popup fills the screen; over it a 400x300 WS_EX_NOREDIRECTIONBITMAP popup at (100,100) presents through
 * DirectComposition: a premultiplied composition swap chain holding an opaque white bar at the top, red at 50 % on
 * the left and nothing on the right. With DwmAlpha the stripes must show through, reddened on the left and unchanged
 * on the right. One Commit, then a Present every 100 ms. An argument names a log file.
 * Build: x86_64-w64-mingw32-g++ -municode -mwindows -o dwm-dcomp-alpha-visual-probe.exe
 *        dwm-dcomp-alpha-visual-probe.cpp -ld3d11 -ldxgi -ldcomp -luser32 -lgdi32 */

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <stdio.h>

static LRESULT CALLBACK stripes_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        RECT rc, band;
        HDC dc = BeginPaint( hwnd, &ps );
        GetClientRect( hwnd, &rc );
        FillRect( dc, &rc, (HBRUSH)GetStockObject( WHITE_BRUSH ) );
        for (int x = 0; x < rc.right; x += 64)
        {
            HBRUSH brush = CreateSolidBrush( (x / 64) & 1 ? RGB(0, 160, 0) : RGB(0, 0, 200) );
            SetRect( &band, x, 0, x + 32, rc.bottom );
            FillRect( dc, &band, brush );
            DeleteObject( brush );
        }
        EndPaint( hwnd, &ps );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wp, lp );
}

int WINAPI wWinMain( HINSTANCE inst, HINSTANCE prev, LPWSTR cmdline, int show )
{
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
    HWND stripes, hwnd;
    HRESULT hr;
    DWORD deadline;

    if (cmdline && *cmdline) _wfreopen( cmdline, L"w", stdout );
    wc.lpfnWndProc   = stripes_proc;
    wc.hInstance     = inst;
    wc.lpszClassName = L"DwmDCompStripes";
    RegisterClassW( &wc );
    stripes = CreateWindowExW( 0, wc.lpszClassName, L"Stripes", WS_POPUP, 0, 0, 1024, 740, NULL, NULL, inst, NULL );
    ShowWindow( stripes, SW_SHOWNOACTIVATE );
    UpdateWindow( stripes );

    wc.lpfnWndProc   = DefWindowProcW;
    wc.lpszClassName = L"DwmDCompAlpha";
    RegisterClassW( &wc );
    hwnd = CreateWindowExW( WS_EX_NOREDIRECTIONBITMAP, wc.lpszClassName, L"DComp alpha", WS_POPUP | WS_VISIBLE,
                            100, 100, 400, 300, NULL, NULL, inst, NULL );

    hr = D3D11CreateDevice( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                            D3D11_SDK_VERSION, &device, NULL, &context );
    printf( "D3D11CreateDevice hr %08lx\n", hr );
    if (FAILED(hr)) return 1;
    device->QueryInterface( __uuidof(IDXGIDevice), (void **)&dxgi_device );
    dxgi_device->GetAdapter( &adapter );
    adapter->GetParent( __uuidof(IDXGIFactory2), (void **)&factory );

    desc.Width = 400;
    desc.Height = 300;
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
    hr = dcomp->CreateTargetForHwnd( hwnd, TRUE, &target );
    printf( "CreateTargetForHwnd hr %08lx\n", hr );
    dcomp->CreateVisual( &visual );
    visual->SetContent( swapchain );
    target->SetRoot( visual );
    hr = dcomp->Commit();
    printf( "Commit hr %08lx\n", hr );
    fflush( stdout );

    deadline = GetTickCount() + 40000;
    while (GetTickCount() < deadline)
    {
        static DWORD pixels[300][400];
        MSG msg;
        ID3D11Texture2D *buffer = NULL;

        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE )) DispatchMessageW( &msg );
        /* premultiplied BGRA: left half red at 50 %, right half clear, a white opaque bar at the top */
        for (int y = 0; y < 300; y++)
            for (int x = 0; x < 400; x++)
                pixels[y][x] = y < 30 ? 0xffffffff : x < 200 ? 0x80800000 : 0;
        swapchain->GetBuffer( 0, __uuidof(ID3D11Texture2D), (void **)&buffer );
        context->UpdateSubresource( buffer, 0, NULL, pixels, sizeof(pixels[0]), 0 );
        buffer->Release();
        swapchain->Present( 1, 0 );
        Sleep( 100 );
    }
    return 0;
}
