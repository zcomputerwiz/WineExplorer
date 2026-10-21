// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
// Original process-boundary diagnostic. Uses existing public-API probe helpers.
#define main g4_composition_probe_unused_main
#include "test_g4_composition_probe.cpp"
#undef main

static int produce(HWND hwnd, bool gdi, bool full = false)
{
    DWORD owner = 0;
    GetWindowThreadProcessId(hwnd, &owner);
    printf("PRODUCER pid=%lu root_owner=%lu foreign_root=%u mode=%s root=%p\n", GetCurrentProcessId(), owner,
            owner != GetCurrentProcessId(), gdi ? "gdi" : "dcomp", (void *)hwnd);
    // Chromium's GPU process owns the rendering child, but the containing
    // top-level belongs to the browser process. Direct foreign-root DComp is
    // a different API case and is not a valid substitute for this topology.
    HWND child_window = NULL;
    if (!gdi && owner != GetCurrentProcessId())
    {
        child_window = create_window("G4 foreign-root rendering child", full ? 64 : 256, full ? 64 : 256, hwnd);
        if (!child_window) return 2;
        hwnd = child_window;
    }
    fflush(stdout);
    printf("RENDER_TARGET hwnd=%p root=%p visible=%u style=%08lx\n", (void *)hwnd,
            (void *)GetAncestor(hwnd,GA_ROOT), IsWindowVisible(hwnd), (unsigned long)GetWindowLongW(hwnd,GWL_STYLE));
    fflush(stdout);
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    IDCompositionDevice *comp = NULL;
    IDCompositionTarget *target = NULL;
    IDCompositionVisual *visual = NULL;
    IDXGISwapChain1 *swapchain = NULL;
    ID3D11Texture2D *buffer = NULL;
    ID3D11RenderTargetView *rtv = NULL;
    bool ok = true;
    if (!gdi)
    {
        ok = create_d3d_and_dcomp(hwnd, &device, &context, &comp, &target)
                && (swapchain = create_comp_swapchain(device, 64, 64))
                && SUCCEEDED(swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&buffer))
                && SUCCEEDED(device->CreateRenderTargetView(buffer, NULL, &rtv))
                && SUCCEEDED(comp->CreateVisual(&visual)) && SUCCEEDED(visual->SetContent(swapchain))
                && SUCCEEDED(target->SetRoot(visual)) && SUCCEEDED(comp->Commit());
    }
    unsigned int frames = 0;
    ULONGLONG deadline = GetTickCount64() + 8000;
    while (ok && GetTickCount64() < deadline)
    {
        flush_messages();
        if (gdi)
        {
            RECT rect = {0, 0, 64, 64};
            HDC dc = GetDC(hwnd);
            HBRUSH brush = CreateSolidBrush(RGB(0,255,0));
            ok = dc && brush && FillRect(dc, &rect, brush);
            if (brush) DeleteObject(brush);
            if (dc) ReleaseDC(hwnd, dc);
            GdiFlush();
        }
        else
        {
            const float red[] = {1,0,0,1};
            context->ClearRenderTargetView(rtv, red);
            context->Flush();
            ok = swapchain->Present(0,0) == S_OK;
        }
        if (!frames) { printf("FRAME_SENT ok=%u\n", ok); fflush(stdout); }
        ++frames;
        Sleep(50);
    }
    printf("PRODUCER_COMPLETE ok=%u frames=%u\n", ok, frames);
    fflush(stdout);
    if (target) target->SetRoot(NULL);
    if (comp) comp->Commit();
    release(rtv); release(buffer); release(visual); release(swapchain);
    release(target); release(comp); release(context); release(device);
    if (child_window) DestroyWindow(child_window);
    return ok && frames >= 20 ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc == 4 && !strcmp(argv[1], "--producer"))
        return produce((HWND)(UINT_PTR)strtoull(argv[2], NULL, 16), !strcmp(argv[3], "gdi"), !strcmp(argv[3], "full"));
    bool full = argc == 2 && !strcmp(argv[1], "--foreign-full");
    bool foreign = argc == 2 && (!strcmp(argv[1], "--foreign") || full);
    bool gdi = argc == 2 && !strcmp(argv[1], "--foreign-gdi");
    HWND hwnd = create_window("G4 owned presentation diagnostic", 300, 300, NULL);
    if (!hwnd) return 2;
    HDC dc = GetDC(hwnd); RECT client;
    GetClientRect(hwnd, &client);
    FillRect(dc, &client, (HBRUSH)GetStockObject(WHITE_BRUSH));
    ReleaseDC(hwnd, dc); GdiFlush();
    if (!foreign && !gdi)
    {
        int result = produce(hwnd, false);
        DestroyWindow(hwnd);
        return result;
    }
    char executable[MAX_PATH], command[2 * MAX_PATH];
    STARTUPINFOA startup = {}; PROCESS_INFORMATION child = {};
    GetModuleFileNameA(NULL, executable, sizeof(executable));
    snprintf(command, sizeof(command), "\"%s\" --producer %llx %s", executable,
             (unsigned long long)(UINT_PTR)hwnd, gdi ? "gdi" : full ? "full" : "dcomp");
    startup.cb = sizeof(startup);
    if (!CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &child)) return 3;
    ULONGLONG deadline = GetTickCount64() + 15000;
    while (WaitForSingleObject(child.hProcess, 0) == WAIT_TIMEOUT && GetTickCount64() < deadline)
    {
        flush_messages();
        Sleep(10);
    }
    DWORD code = 4;
    if (WaitForSingleObject(child.hProcess, 0) == WAIT_TIMEOUT) TerminateProcess(child.hProcess, 4);
    else GetExitCodeProcess(child.hProcess, &code);
    CloseHandle(child.hThread); CloseHandle(child.hProcess);
    DestroyWindow(hwnd);
    return code;
}
