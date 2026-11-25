/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe: which interfaces a DirectComposition surface and virtual surface answer. The Notepad compositor asks
 * Wine's surface for {790a45f7} (IDXGISwapChain1, the public interface): does a Windows surface answer it?
 * Build (mingw): x86_64-w64-mingw32-g++ -O2 -static -o dcomp-surface-qi-probe.exe dcomp-surface-qi-probe.cpp -ld3d11 -ldcomp -luuid
 */
#include <stdio.h>
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>

static const GUID IID_SwapChain1_ = {0x790a45f7, 0x0d42, 0x4876, {0x98, 0x3a, 0x0a, 0x55, 0xcf, 0xe6, 0xf4, 0xaa}};
static const GUID IID_SwapChain_ = {0x310d36a0, 0xd2e7, 0x4c0a, {0xaa, 0x04, 0x6a, 0x9d, 0x23, 0xb8, 0x88, 0x6a}};
static const GUID IID_Surface_ = {0xbb8a4953, 0x2c99, 0x4f5a, {0x96, 0xf5, 0x48, 0x19, 0x02, 0x7f, 0xa3, 0xac}};
static const GUID IID_VirtualSurface_ = {0xae471c51, 0x5f53, 0x4a24, {0x8d, 0x3e, 0xd0, 0xc3, 0x9c, 0x30, 0xb3, 0xf0}};
static const GUID IID_Device_ = {0xc37ea93a, 0xe7aa, 0x450d, {0xb1, 0x6f, 0x97, 0x46, 0xcb, 0x04, 0x07, 0xf3}};
static const GUID IID_DXGIDevice_ = {0x54ec77fa, 0x1377, 0x44e6, {0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c}};
static const GUID IID_DXGISurface_ = {0xcafcb56c, 0x6ac3, 0x4889, {0xbf, 0x47, 0x9e, 0x23, 0xbb, 0xd2, 0x60, 0xec}};
static const GUID IID_Unknown_ = {0x00000000, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};

static void try_qi(const char *what, IUnknown *obj)
{
    static const struct { const char *name; const GUID *iid; } iids[] =
    {
        { "IUnknown", &IID_Unknown_ }, { "IDXGISwapChain1 {790a45f7}", &IID_SwapChain1_ },
        { "IDXGISwapChain", &IID_SwapChain_ }, { "IDCompositionSurface", &IID_Surface_ },
        { "IDCompositionVirtualSurface", &IID_VirtualSurface_ }, { "IDXGISurface", &IID_DXGISurface_ },
    };

    for (unsigned int i = 0; i < ARRAYSIZE(iids); ++i)
    {
        IUnknown *const marker = (IUnknown *)(ULONG_PTR)0xdeadbeef;
        IUnknown *out = marker;
        HRESULT hr = obj->QueryInterface(*iids[i].iid, (void **)&out);
        printf("%s QI %s: hr %#lx out %s\n", what, iids[i].name, (unsigned long)hr,
               out == marker ? "untouched" : out ? "set" : "NULL");
        if (SUCCEEDED(hr) && out && out != marker) out->Release();
    }
}

int main(void)
{
    ID3D11Device *d3d = NULL;
    IDXGIDevice *dxgi = NULL;
    IDCompositionDevice *dcomp = NULL;
    IDCompositionSurface *surface = NULL;
    IDCompositionVirtualSurface *virt = NULL;
    HRESULT hr;

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                           D3D11_SDK_VERSION, &d3d, NULL, NULL);
    if (FAILED(hr))
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                               D3D11_SDK_VERSION, &d3d, NULL, NULL);
    printf("D3D11CreateDevice: %#lx\n", (unsigned long)hr);
    if (FAILED(hr)) return 1;
    hr = d3d->QueryInterface(IID_DXGIDevice_, (void **)&dxgi);
    printf("IDXGIDevice: %#lx\n", (unsigned long)hr);
    hr = DCompositionCreateDevice(dxgi, IID_Device_, (void **)&dcomp);
    printf("DCompositionCreateDevice: %#lx\n", (unsigned long)hr);
    if (FAILED(hr)) return 1;

    hr = dcomp->CreateSurface(64, 64, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &surface);
    printf("CreateSurface: %#lx\n", (unsigned long)hr);
    if (SUCCEEDED(hr)) try_qi("surface", surface);
    hr = dcomp->CreateVirtualSurface(64, 64, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &virt);
    printf("CreateVirtualSurface: %#lx\n", (unsigned long)hr);
    if (SUCCEEDED(hr)) try_qi("virtual surface", virt);

    if (virt) virt->Release();
    if (surface) surface->Release();
    dcomp->Release();
    dxgi->Release();
    d3d->Release();
    return 0;
}
