// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
// Public HWND layering contracts, kept separate from the established controls.
// A failing diagnostic is an OPEN integration gate, never an expected pass.
#define WEX_G4_HELPERS_ONLY
#include "test_g4_correctness_probe.cpp"
#undef WEX_G4_HELPERS_ONLY

static LRESULT CALLBACK black_child_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(hwnd,&paint);
        FillRect(dc,&paint.rcPaint,(HBRUSH)GetStockObject(BLACK_BRUSH));
        EndPaint(hwnd,&paint);
        return 0;
    }
    if (message == WM_ERASEBKGND) return 1;
    return DefWindowProcA(hwnd,message,wp,lp);
}

static bool child_clip_order(bool topmost)
{
    Scene s;
    if (!s.init()) return false;
    if (!topmost)
    {
        release(s.target);
        if (FAILED(s.comp->CreateTargetForHwnd(s.hwnd,FALSE,&s.target))
                || FAILED(s.target->SetRoot(s.visual))) return false;
    }
    if (!paint_application_background(s,RGB(0,160,0))) return false;
    WNDCLASSA wc = {};
    wc.lpfnWndProc = black_child_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "WEX_G4_Black_Child";
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    HWND child = CreateWindowExA(0,wc.lpszClassName,"",WS_CHILD|WS_VISIBLE,
            8,8,16,16,s.hwnd,NULL,GetModuleHandleA(NULL),NULL);
    if (!child) return false;
    UpdateWindow(child);
    flush_messages();
    POINT child_point = {12,12};
    ClientToScreen(s.hwnd,&child_point);
    bool child_hit = WindowFromPoint(child_point) == child;
    bool base_pixel = s.pixel(4,4,RGB(0,160,0));
    bool child_pixel = s.pixel(12,12,RGB(0,0,0));
    bool preflight = child_hit && base_pixel && child_pixel;
    printf("HWND_CHILD preflight_hit=%d base=%d child=%d\n",child_hit,base_pixel,child_pixel);
    printf("HWND_CHILD mode=%s preflight=%s\n",topmost ? "top" : "bottom",preflight ? "PASS" : "FAIL");
    if (!preflight) { DestroyWindow(child); return false; }
    bool ok = SUCCEEDED(s.comp->Commit()) && s.present() && s.pixel(4,4,RGB(255,0,0))
            && s.pixel(12,12,topmost ? RGB(255,0,0) : RGB(0,0,0));
    printf("HWND_CHILD mode=%s first=%s\n",topmost ? "top" : "bottom",ok ? "PASS" : "FAIL");
    // A later native child paint must neither overpaint a top tree nor vanish
    // beneath a bottom tree. No new DComp Commit/producer Present is allowed.
    InvalidateRect(child,NULL,TRUE);
    UpdateWindow(child);
    GdiFlush();
    bool repainted = dispatch_application_idle(s.hwnd)
            && s.pixel(12,12,topmost ? RGB(255,0,0) : RGB(0,0,0));
    printf("HWND_CHILD mode=%s repaint=%s\n",topmost ? "top" : "bottom",repainted ? "PASS" : "FAIL");
    DestroyWindow(child);
    return ok && repainted;
}

static bool two_target_retention()
{
    Scene s;
    if (!s.init() || !paint_application_background(s,RGB(0,160,0))) return false;
    IDCompositionTarget *bottom = NULL;
    IDCompositionVisual *visual = NULL;
    IDXGISwapChain1 *swapchain = create_comp_swapchain(s.dev,64,64);
    ID3D11Texture2D *buffer = NULL;
    bool ok = swapchain && SUCCEEDED(s.comp->CreateTargetForHwnd(s.hwnd,FALSE,&bottom))
            && SUCCEEDED(s.comp->CreateVisual(&visual)) && SUCCEEDED(visual->SetContent(swapchain))
            && SUCCEEDED(bottom->SetRoot(visual)) && SUCCEEDED(s.visual->SetOffsetX(16.0f))
            && SUCCEEDED(swapchain->GetBuffer(0,__uuidof(ID3D11Texture2D),(void **)&buffer));
    if (ok)
    {
        // Bottom: red then blue at x32; top: same pattern shifted right16.
        s.ctx->CopyResource(buffer,s.pattern); s.ctx->Flush();
        HRESULT committed = s.comp->Commit();
        printf("HWND_TWO_TARGETS commit=%08lx\n",(unsigned long)committed);
        ok = SUCCEEDED(committed) && SUCCEEDED(swapchain->Present(0,0)) && s.present();
        bool top_last = ok && s.pixel(8,8,RGB(255,0,0)) && s.pixel(40,8,RGB(255,0,0))
                && s.pixel(72,8,RGB(0,0,255)) && s.pixel(88,8,RGB(0,160,0));
        printf("HWND_TWO_TARGETS top_last=%s\n",top_last ? "PASS" : "FAIL");
        bool bottom_last = ok && SUCCEEDED(swapchain->Present(0,0))
                && s.pixel(8,8,RGB(255,0,0)) && s.pixel(40,8,RGB(255,0,0))
                && s.pixel(72,8,RGB(0,0,255)) && s.pixel(88,8,RGB(0,160,0));
        printf("HWND_TWO_TARGETS bottom_last=%s\n",bottom_last ? "PASS" : "FAIL");
        ok = ok && top_last && bottom_last;
    }
    if (bottom) bottom->SetRoot(NULL);
    s.comp->Commit();
    release(buffer); release(visual); release(swapchain); release(bottom);
    return ok;
}

int main()
{
    SetProcessDPIAware();
    bool top = child_clip_order(true);
    printf("hwnd_top_child_clip=%s\n",top ? "PASS" : "FAIL");
    bool bottom = child_clip_order(false);
    printf("hwnd_bottom_child_clip=%s\n",bottom ? "PASS" : "FAIL");
    bool retained = two_target_retention();
    printf("hwnd_two_target_retention=%s\n",retained ? "PASS" : "FAIL");
    return top && bottom && retained ? 0 : 1;
}
