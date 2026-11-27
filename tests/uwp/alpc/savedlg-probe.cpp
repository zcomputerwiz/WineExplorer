/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Probe (Wine lab): shows the common item Save dialog from a plain program, with the calls Notepad makes before Show, so a screenshot
 * can tell whether the dialog paints outside Notepad. Variants, to find what in Notepad's process keeps the dialog black:
 *   SAVEDLG_OWNER=1   an owner window of its own
 *   SAVEDLG_DPI=1     per-monitor (v2) DPI awareness, as Notepad's manifest asks
 *   SAVEDLG_D3D=1     the owner (WS_EX_NOREDIRECTIONBITMAP) gets a Direct3D 11 flip swapchain, cleared and presented
 *   SAVEDLG_DCOMP=1   the owner (WS_EX_NOREDIRECTIONBITMAP) gets a DirectComposition target with a composition swapchain
 *   SAVEDLG_THREAD=1  the dialog is shown from a second thread (no owner)
 * The dialog closes when the process is killed.
 */
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <shobjidl.h>
#include <stdio.h>
#include <stdlib.h>

static HWND owner;

static void present_d3d(bool dcomp)
{
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    static const float color[4] = {0.0f, 0.5f, 1.0f, 1.0f};
    ID3D11RenderTargetView *rtv;
    ID3D11DeviceContext *ctx;
    IDXGISwapChain1 *swapchain;
    ID3D11Texture2D *back;
    IDXGIFactory2 *factory;
    IDXGIAdapter *adapter;
    ID3D11Device *device;
    IDXGIDevice *dxgi;
    HRESULT hr;

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION,
                           &device, NULL, &ctx);
    printf("d3d11 hr %#lx\n", hr);
    if (FAILED(hr)) return;
    device->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgi);
    dxgi->GetAdapter(&adapter);
    adapter->GetParent(__uuidof(IDXGIFactory2), (void **)&factory);
    desc.Width = 800;
    desc.Height = 600;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    if (dcomp)
    {
        IDCompositionDevice *cdev;
        IDCompositionTarget *target;
        IDCompositionVisual *visual;

        desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        hr = factory->CreateSwapChainForComposition(device, &desc, NULL, &swapchain);
        printf("composition swapchain hr %#lx\n", hr);
        if (FAILED(hr)) return;
        hr = DCompositionCreateDevice(dxgi, __uuidof(IDCompositionDevice), (void **)&cdev);
        printf("dcomp device hr %#lx\n", hr);
        if (FAILED(hr)) return;
        cdev->CreateTargetForHwnd(owner, TRUE, &target);
        cdev->CreateVisual(&visual);
        visual->SetContent(swapchain);
        target->SetRoot(visual);
        cdev->Commit();
    }
    else
    {
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        hr = factory->CreateSwapChainForHwnd(device, owner, &desc, NULL, NULL, &swapchain);
        printf("hwnd swapchain hr %#lx\n", hr);
        if (FAILED(hr)) return;
    }
    swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&back);
    device->CreateRenderTargetView(back, NULL, &rtv);
    ctx->ClearRenderTargetView(rtv, color);
    hr = swapchain->Present(0, 0);
    printf("present hr %#lx\n", hr);
}

static DWORD WINAPI show(void *arg)
{
    static const COMDLG_FILTERSPEC types[2] = {{L"Text documents (*.txt)", L"*.txt"}, {L"All files (*.*)", L"*.*"}};
    IFileSaveDialog *dlg;
    FILEOPENDIALOGOPTIONS opts = 0;
    HRESULT hr;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    hr = CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER, IID_IFileSaveDialog, (void **)&dlg);
    printf("create hr %#lx\n", hr);
    if (FAILED(hr)) return 1;
    dlg->SetFileName(L"hello.txt");
    dlg->SetTitle(L"Save as");
    dlg->SetDefaultExtension(L"txt");
    dlg->SetFileTypes(2, types);
    dlg->GetOptions(&opts);
    dlg->SetOptions(0x884a);
    hr = dlg->Show(arg ? NULL : owner);
    printf("show hr %#lx\n", hr);
    dlg->Release();
    return 0;
}

int main(void)
{
    bool d3d = getenv("SAVEDLG_D3D") != NULL, dcomp = getenv("SAVEDLG_DCOMP") != NULL;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (getenv("SAVEDLG_DPI"))
        printf("dpi awareness %d\n", SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (getenv("SAVEDLG_OWNER") || d3d || dcomp)
    {
        owner = CreateWindowExW(d3d || dcomp ? WS_EX_NOREDIRECTIONBITMAP : 0, L"STATIC", L"owner",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 800, 600, NULL, NULL, NULL, NULL);
        UpdateWindow(owner);
        if (d3d || dcomp) present_d3d(dcomp);
    }
    if (getenv("SAVEDLG_DELAY"))
    {
        /* SAVEDLG_DELAY=ms: pump messages that long before the dialog (does the GL/D3D setup need to settle first?) */
        DWORD end = GetTickCount() + atoi(getenv("SAVEDLG_DELAY"));
        MSG msg;

        while ((int)(end - GetTickCount()) > 0)
        {
            MsgWaitForMultipleObjects(0, NULL, FALSE, 50, QS_ALLINPUT);
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        }
        printf("delayed %s ms\n", getenv("SAVEDLG_DELAY"));
    }
    if (getenv("SAVEDLG_THREAD"))
    {
        HANDLE t = CreateThread(NULL, 0, show, (void *)1, 0, NULL);
        MSG msg;
        while (MsgWaitForMultipleObjects(1, &t, FALSE, INFINITE, QS_ALLINPUT) != WAIT_OBJECT_0)
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        return 0;
    }
    return show(NULL);
}
