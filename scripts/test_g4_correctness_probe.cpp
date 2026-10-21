// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
// Independent correctness checks. Legacy HRESULT-only probes remain historical,
// not substitutes for these target-window pixels and imported-fence observers.
#define main historical_g4_main
#include "test_g4_composition_probe.cpp"
#undef main
#include <d3d11_4.h>
#include <math.h>

static bool native_visual_control;

struct Scene
{
    HWND hwnd = NULL;
    ID3D11Device *dev = NULL;
    ID3D11DeviceContext *ctx = NULL;
    IDCompositionDevice *comp = NULL;
    IDCompositionTarget *target = NULL;
    IDCompositionVisual *visual = NULL;
    IDXGISwapChain1 *sc = NULL;
    ID3D11Texture2D *bb = NULL, *pattern = NULL;

    bool init(DXGI_ALPHA_MODE alpha = DXGI_ALPHA_MODE_PREMULTIPLIED,
            DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM)
    {
        hwnd = create_window("G4 independent pixels", 256, 256, NULL);
        if (native_visual_control && hwnd)
        {
            ShowWindow(hwnd,SW_SHOWNOACTIVATE);
            SetWindowPos(hwnd,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
        }
        if (!hwnd || !create_d3d_and_dcomp(hwnd, &dev, &ctx, &comp, &target)) return false;
        sc = create_comp_swapchain(dev, 64, 64, format, alpha);
        if (!sc || FAILED(comp->CreateVisual(&visual))) return false;
        if (FAILED(visual->SetContent(sc)) || FAILED(target->SetRoot(visual))) return false;
        if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb))) return false;
        DWORD pixels[64 * 64];
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                pixels[y * 64 + x] = (x < 32) != (format == DXGI_FORMAT_R8G8B8A8_UNORM) ? 0xffff0000 : 0xff0000ff;
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = desc.Height = 64;
        desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = format;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        D3D11_SUBRESOURCE_DATA data = {pixels, 64 * 4, 0};
        if (FAILED(dev->CreateTexture2D(&desc, &data, &pattern))) return false;
        flush_messages();
        return black();
    }
    bool black()
    {
        RECT rc;
        if (!GetClientRect(hwnd, &rc)) return false;
        // Establish an actual application underlay, not just black screen
        // pixels that silently disagree with the window's GDI backing image.
        HDC dc = GetDC(hwnd);
        if (!dc) return false;
        bool painted = FillRect(dc,&rc,(HBRUSH)GetStockObject(BLACK_BRUSH)) != 0;
        ReleaseDC(hwnd,dc);
        GdiFlush();
        return painted;
    }
    bool present()
    {
        ctx->CopyResource(bb, pattern);
        ctx->Flush();
        return sc->Present(0, 0) == S_OK;
    }
    bool black_new_area(const RECT &previous)
    {
        HDC dc = GetDC(hwnd);
        RECT client;
        if (!dc) return false;
        int saved = SaveDC(dc);
        bool painted = saved && GetClientRect(hwnd,&client)
                && ExcludeClipRect(dc,previous.left,previous.top,previous.right,previous.bottom) != ERROR
                && FillRect(dc,&client,(HBRUSH)GetStockObject(BLACK_BRUSH));
        if (saved) RestoreDC(dc,saved);
        ReleaseDC(hwnd,dc);
        GdiFlush();
        return painted;
    }
    bool pixel(int x, int y, COLORREF expected)
    {
        COLORREF actual = CLR_INVALID;
        for (int i = 0; i < 100; ++i)
        {
            POINT point = {x, y};
            ClientToScreen(hwnd, &point);
            if (!IsWindowVisible(hwnd) || IsIconic(hwnd)
                    || GetAncestor(WindowFromPoint(point),GA_ROOT) != GetAncestor(hwnd,GA_ROOT))
            {
                printf("PIXEL_NOT_VISIBLE x=%d y=%d visible=%d iconic=%d hwnd=%p at_point=%p\n",
                        x,y,IsWindowVisible(hwnd),IsIconic(hwnd),(void *)hwnd,(void *)WindowFromPoint(point));
                return false;
            }
            HDC dc = GetDC(NULL);
            actual = GetPixel(dc, point.x, point.y);
            ReleaseDC(NULL, dc);
            if (actual == expected) return true;
            Sleep(10); // bounded test observation, not production pacing
        }
        printf("PIXEL_FAIL x=%d y=%d expected=%06lx actual=%06lx\n",
               x, y, (unsigned long)expected, (unsigned long)actual);
        HDC backing = GetDC(hwnd);
        printf("PIXEL_BACKING x=%d y=%d color=%06lx\n",x,y,
                (unsigned long)(backing ? GetPixel(backing,x,y) : CLR_INVALID));
        if (backing) ReleaseDC(hwnd,backing);
        HDC whole = GetWindowDC(hwnd);
        POINT origin = {0,0}; RECT window;
        ClientToScreen(hwnd,&origin); GetWindowRect(hwnd,&window);
        printf("PIXEL_SURFACE color=%06lx registered=%d\n",
                (unsigned long)(whole ? GetPixel(whole,x+origin.x-window.left,y+origin.y-window.top) : CLR_INVALID),
                GetPropW(hwnd,L"__wine_dcomp_underlay_top") != NULL);
        if (whole) ReleaseDC(hwnd,whole);
        return false;
    }
    ~Scene()
    {
        if (target) target->SetRoot(NULL);
        if (comp) comp->Commit();
        release(pattern); release(bb); release(visual); release(sc);
        release(target); release(comp); release(ctx); release(dev);
        if (hwnd) DestroyWindow(hwnd);
    }
};

static bool paint_application_background(Scene &s, COLORREF color)
{
    RECT client;
    if (!GetClientRect(s.hwnd,&client)) return false;
    HDC dc = GetDC(s.hwnd);
    HBRUSH brush = CreateSolidBrush(color);
    if (!dc || !brush) { if (dc) ReleaseDC(s.hwnd,dc); if (brush) DeleteObject(brush); return false; }
    bool painted = FillRect(dc,&client,brush) != 0;
    DeleteObject(brush);
    ReleaseDC(s.hwnd,dc);
    GdiFlush();
    return painted;
}

static bool application_background_pixels()
{
    const DXGI_ALPHA_MODE modes[] = {DXGI_ALPHA_MODE_IGNORE,DXGI_ALPHA_MODE_PREMULTIPLIED};
    for (DXGI_ALPHA_MODE mode : modes)
    {
        Scene s;
        if (!s.init(mode) || !paint_application_background(s,RGB(0,160,0))
                || FAILED(s.comp->Commit()) || !s.present()) return false;
        HDC dc = GetDC(s.hwnd);
        COLORREF underlying = dc ? GetPixel(dc,8,8) : CLR_INVALID;
        COLORREF uncovered = dc ? GetPixel(dc,80,80) : CLR_INVALID;
        if (dc) ReleaseDC(s.hwnd,dc);
        printf("APP_BACKGROUND mode=%u gdi_under_layer=%06lx gdi_uncovered=%06lx\n",
                (unsigned int)mode,(unsigned long)underlying,(unsigned long)uncovered);
        if (underlying != RGB(0,160,0) || uncovered != RGB(0,160,0)
                || !s.pixel(8,8,RGB(255,0,0)) || !s.pixel(80,80,RGB(0,160,0))) return false;
        if (FAILED(s.visual->SetOffsetX(80)) || FAILED(s.comp->Commit())
                || !s.pixel(8,8,RGB(0,160,0)) || !s.pixel(88,8,RGB(255,0,0))) return false;
        IDCompositionEffectGroup *effect = NULL;
        if (FAILED(s.comp->CreateEffectGroup(&effect))) return false;
        bool configured = SUCCEEDED(effect->SetOpacity(0.5f)) && SUCCEEDED(s.visual->SetEffect(effect));
        release(effect);
        if (!configured || FAILED(s.comp->Commit()) || !s.pixel(88,8,RGB(128,80,0))) return false;
        // Repeated retained compositions must not accumulate their own output.
        for (UINT i = 0; i < 5; ++i)
            if (FAILED(s.comp->Commit()) || !s.pixel(88,8,RGB(128,80,0))) return false;
        D2D_RECT_F clip = {0,0,16,64};
        if (FAILED(s.visual->SetClip(clip)) || FAILED(s.comp->Commit())
                || !s.pixel(88,8,RGB(128,80,0)) || !s.pixel(104,8,RGB(0,160,0))) return false;
        if (!paint_application_background(s,RGB(0,0,160)) || !s.present()
                || !s.pixel(88,8,RGB(128,0,80)) || !s.pixel(104,8,RGB(0,0,160))) return false;
        // Resizing must not turn the old composed drawable into GDI input.
        if (!SetWindowPos(s.hwnd,NULL,0,0,360,300,SWP_NOMOVE|SWP_NOZORDER)) return false;
        flush_messages();
        if (!s.present() || !s.pixel(88,8,RGB(128,0,80))
                || !s.pixel(104,8,RGB(0,0,160))) return false;
        dc = GetDC(s.hwnd);
        underlying = dc ? GetPixel(dc,88,8) : CLR_INVALID;
        if (dc) ReleaseDC(s.hwnd,dc);
        if (underlying != RGB(0,0,160)) return false;
        if (FAILED(s.visual->SetContent(NULL)) || FAILED(s.comp->Commit())
                || !s.pixel(88,8,RGB(0,0,160))) return false;
    }
    return true;
}

static bool application_background_detach()
{
    Scene s;
    if (!s.init() || !paint_application_background(s,RGB(0,160,0))
            || FAILED(s.comp->Commit()) || !s.present() || !s.pixel(8,8,RGB(255,0,0))) return false;
    IDCompositionTarget *duplicate = NULL, *lower = NULL;
    HRESULT duplicate_hr = s.comp->CreateTargetForHwnd(s.hwnd,TRUE,&duplicate);
    release(duplicate);
    if (duplicate_hr != (HRESULT)0x88980800) return false; // WINDOW_ALREADY_COMPOSED
    if (FAILED(s.comp->CreateTargetForHwnd(s.hwnd,FALSE,&lower))) return false;
    release(lower); // An unused second layer must not remove the live target's backing.
    if (!paint_application_background(s,RGB(0,160,0)) || !s.present()
            || !s.pixel(8,8,RGB(255,0,0))) return false;
    if (FAILED(s.visual->SetContent(NULL)) || FAILED(s.comp->Commit())
            || !s.pixel(8,8,RGB(0,160,0)) || !s.pixel(80,80,RGB(0,160,0))) return false;
    if (FAILED(s.visual->SetContent(s.sc)) || FAILED(s.comp->Commit()) || !s.present()
            || !s.pixel(8,8,RGB(255,0,0))) return false;
    if (FAILED(s.target->SetRoot(NULL)) || FAILED(s.comp->Commit())
            || !s.pixel(8,8,RGB(0,160,0))) return false;
    if (FAILED(s.target->SetRoot(s.visual)) || FAILED(s.comp->Commit()) || !s.present()
            || !s.pixel(8,8,RGB(255,0,0))) return false;
    release(s.target); // Destroying the target must also expose the underlay.
    if (!s.pixel(8,8,RGB(0,160,0))) return false;
    if (FAILED(s.comp->CreateTargetForHwnd(s.hwnd,TRUE,&s.target))
            || FAILED(s.target->SetRoot(s.visual)) || FAILED(s.comp->Commit())
            || !s.present() || !s.pixel(8,8,RGB(255,0,0))) return false;
    release(s.target);
    return s.pixel(8,8,RGB(0,160,0));
}

static bool supported_pixels_and_transactions()
{
    Scene s;
    if (!s.init()) return false;
    if (FAILED(s.comp->Commit())) return false;
    s.black();
    if (!s.present() || !s.pixel(8, 8, RGB(255,0,0)) || !s.pixel(48,8,RGB(0,0,255))) return false;
    if (FAILED(s.visual->SetOffsetX(32))) return false;
    // Pending placement must not affect Present before Commit.
    s.black();
    if (!s.present() || !s.pixel(8,8,RGB(255,0,0))) return false;
    if (FAILED(s.comp->Commit()) || FAILED(s.comp->WaitForCommitCompletion())) return false;
    if (!s.present() || !s.pixel(8,8,RGB(0,0,0)) || !s.pixel(40,8,RGB(255,0,0))) return false;
    D2D_RECT_F clip = {0,0,32,64};
    if (FAILED(s.visual->SetClip(clip)) || FAILED(s.comp->Commit())) return false;
    // Cropping must preserve the left red half, not squeeze red+blue into it.
    if (!s.present() || !s.pixel(56,8,RGB(255,0,0)) || !s.pixel(80,8,RGB(0,0,0))) return false;
    clip.right = 31.25f;
    if (FAILED(s.visual->SetClip(clip)) || FAILED(s.comp->Commit())) return false;
    if (!s.present() || !s.pixel(62,8,RGB(255,0,0)) || !s.pixel(63,8,RGB(0,0,0))) return false;
    clip = {0,0,0,0};
    if (FAILED(s.visual->SetClip(clip)) || FAILED(s.comp->Commit())) return false;
    if (!s.present() || !s.pixel(40,8,RGB(0,0,0))) return false;
    if (FAILED(s.visual->SetClip((IDCompositionClip *)NULL))) return false;
    if (FAILED(s.comp->Commit()) || !s.present() || !s.pixel(40,8,RGB(255,0,0))) return false;
    if (FAILED(s.visual->SetContent(NULL)) || FAILED(s.comp->Commit())) return false;
    return s.present() && s.pixel(40,8,RGB(0,0,0));
}

static bool retained_changing_frame_pixels()
{
    Scene s;
    if (!s.init() || FAILED(s.comp->Commit())) return false;
    DWORD pixels[64 * 64];
    for (int frame = 0; frame < 60; ++frame)
    {
        DWORD value = frame % 2 ? 0xff00ff00 : 0xffff0000;
        COLORREF expected = frame % 2 ? RGB(0,255,0) : RGB(255,0,0);
        for (unsigned int i = 0; i < ARRAYSIZE(pixels); ++i) pixels[i] = value;
        s.ctx->UpdateSubresource(s.pattern, 0, NULL, pixels, 64 * 4, 0);
        if (!s.present() || !s.pixel(8,8,expected) || !s.pixel(48,8,expected)) return false;
    }
    return true;
}

static bool unsupported_properties_fail_honestly()
{
    Scene s;
    if (!s.init() || FAILED(s.comp->Commit())) return false;
    IDCompositionEffectGroup *effect = NULL;
    IDCompositionRectangleClip *clip = NULL;
    IDCompositionVisual *second = NULL;
    bool ok = false;
    D2D_MATRIX_3X2_F invalid = {1,0,0,1,NAN,0};
    if (FAILED(s.comp->CreateEffectGroup(&effect)) || FAILED(s.comp->CreateRectangleClip(&clip))) goto done;

    // Truly unsupported or invalid properties must fail honestly
    if (effect->SetOpacity(NAN) != E_INVALIDARG) goto done;
    if (effect->SetOpacity(-0.5f) != E_INVALIDARG) goto done;
    if (effect->SetOpacity(1.5f) != E_INVALIDARG) goto done;
    if (effect->SetOpacity((IDCompositionAnimation *)NULL) != E_NOTIMPL) goto done;

    if (clip->SetTopLeftRadiusX(-1.0f) != E_INVALIDARG) goto done;
    if (clip->SetTopLeftRadiusX(NAN) != E_INVALIDARG) goto done;
    if (clip->SetRight(NAN) != E_INVALIDARG) goto done;
    if (clip->SetTopLeftRadiusX((IDCompositionAnimation *)NULL) != E_NOTIMPL) goto done;

    if (s.visual->SetTransform(invalid) != E_INVALIDARG) goto done;
    if (s.visual->SetCompositeMode(DCOMPOSITION_COMPOSITE_MODE_DESTINATION_INVERT) != E_NOTIMPL) goto done;
    if (s.visual->SetOffsetX((IDCompositionAnimation *)NULL) != E_NOTIMPL) goto done;
    if (s.visual->SetTransformParent(s.visual) != E_NOTIMPL) goto done;

    // Device-owned effect/clip objects cannot be borrowed from another device:
    // its setters and Commit would otherwise use different synchronization.
    {
        ID3D11Device *other_dev = NULL;
        ID3D11DeviceContext *other_ctx = NULL;
        IDCompositionDevice *other_comp = NULL;
        IDCompositionEffectGroup *other_effect = NULL;
        IDCompositionRectangleClip *other_clip = NULL;
        bool rejected = create_d3d_and_dcomp(NULL,&other_dev,&other_ctx,&other_comp,NULL)
                && SUCCEEDED(other_comp->CreateEffectGroup(&other_effect))
                && SUCCEEDED(other_comp->CreateRectangleClip(&other_clip))
                && s.visual->SetEffect(other_effect) == E_INVALIDARG
                && s.visual->SetClip(other_clip) == E_INVALIDARG;
        release(other_clip); release(other_effect); release(other_comp); release(other_ctx); release(other_dev);
        if (!rejected) goto done;
    }

    // Cycle detection in visual hierarchy: adding parent as child of child must fail
    if (FAILED(s.comp->CreateVisual(&second))) goto done;
    if (second->AddVisual(second, TRUE, NULL) != E_INVALIDARG) goto done;
    if (s.visual->AddVisual(second, TRUE, NULL) != S_OK) goto done;
    if (second->AddVisual(s.visual, TRUE, NULL) != E_INVALIDARG) goto done;

    // A rejected transaction must not alter the active leaf
    s.black();
    ok = s.present() && s.pixel(8,8,RGB(255,0,0)) && s.pixel(48,8,RGB(0,0,255));
    s.visual->RemoveVisual(second);

done:
    release(second); release(clip); release(effect);
    return ok;
}

static bool retained_multilayer_and_transform_pixels()
{
    // Test 1: Multilayer Z-order and independent presentation
    // Leaf 1 (Red) and Leaf 2 (Blue) overlapping: Leaf 2 is above Leaf 1.
    // SC1 Present must not erase Leaf 2. SC2 Present must not erase Leaf 1.
    Scene s1;
    if (!s1.init()) return false;

    IDXGISwapChain1 *sc2 = create_comp_swapchain(s1.dev, 64, 64);
    if (!sc2) return false;
    ID3D11Texture2D *bb2 = NULL, *pattern2 = NULL;
    if (FAILED(sc2->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb2)))
    {
        release(sc2);
        return false;
    }

    DWORD blue_pixels[64 * 64];
    for (unsigned int i = 0; i < ARRAYSIZE(blue_pixels); ++i) blue_pixels[i] = 0xff0000ff;
    D3D11_TEXTURE2D_DESC desc2 = {};
    desc2.Width = desc2.Height = 64;
    desc2.MipLevels = desc2.ArraySize = desc2.SampleDesc.Count = 1;
    desc2.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc2.Usage = D3D11_USAGE_DEFAULT;
    desc2.BindFlags = D3D11_BIND_RENDER_TARGET;
    D3D11_SUBRESOURCE_DATA data2 = {blue_pixels, 64 * 4, 0};
    if (FAILED(s1.dev->CreateTexture2D(&desc2, &data2, &pattern2)))
    {
        release(bb2); release(sc2);
        return false;
    }

    auto present2 = [&]() {
        s1.ctx->CopyResource(bb2, pattern2);
        s1.ctx->Flush();
        return sc2->Present(0, 0) == S_OK;
    };

    IDCompositionVisual *v2 = NULL;
    if (FAILED(s1.comp->CreateVisual(&v2))) goto fail;
    if (FAILED(v2->SetContent(sc2))) goto fail;
    if (FAILED(v2->SetOffsetX(16)) || FAILED(v2->SetOffsetY(16))) goto fail;

    // Add v2 above s1.visual
    if (FAILED(s1.visual->AddVisual(v2, TRUE, NULL))) goto fail;
    if (FAILED(s1.comp->Commit()) || FAILED(s1.comp->WaitForCommitCompletion())) goto fail;

    s1.black();
    if (!s1.present() || !present2()) goto fail;

    // At (8, 8): only s1.visual (Red) is present
    if (!s1.pixel(8, 8, RGB(255, 0, 0))) goto fail;
    // At (24, 24): v2 (Blue) is above s1.visual (Red)
    if (!s1.pixel(24, 24, RGB(0, 0, 255))) goto fail;

    // A producer may render its NEXT frame without presenting it. Re-presenting
    // another leaf must continue to show this leaf's last PRESENTED frame.
    for (unsigned int i = 0; i < ARRAYSIZE(blue_pixels); ++i) blue_pixels[i] = 0xff00ff00;
    s1.ctx->UpdateSubresource(pattern2, 0, NULL, blue_pixels, 64 * 4, 0);
    s1.ctx->CopyResource(bb2, pattern2);
    s1.ctx->Flush();
    if (!s1.present() || !s1.pixel(24,24,RGB(0,0,255))) goto fail;
    if (!present2() || !s1.pixel(24,24,RGB(0,255,0))) goto fail;
    for (unsigned int i = 0; i < ARRAYSIZE(blue_pixels); ++i) blue_pixels[i] = 0xff0000ff;
    s1.ctx->UpdateSubresource(pattern2, 0, NULL, blue_pixels, 64 * 4, 0);
    if (!present2() || !s1.pixel(24,24,RGB(0,0,255))) goto fail;

    // Gate R1 Test 1: Re-present lower leaf (s1). It must NOT erase upper leaf (v2)!
    if (!s1.present()) goto fail;
    if (!s1.pixel(24, 24, RGB(0, 0, 255))) goto fail;
    if (!s1.pixel(8, 8, RGB(255, 0, 0))) goto fail;

    // Re-present upper leaf (v2). It must NOT erase lower leaf (s1)!
    if (!present2()) goto fail;
    if (!s1.pixel(8, 8, RGB(255, 0, 0))) goto fail;
    if (!s1.pixel(24, 24, RGB(0, 0, 255))) goto fail;

    // Gate R1 Test 2: Layer restoration on removal
    // Remove v2. Underneath was s1.visual (Red). Red must be restored at (24, 24)!
    if (FAILED(s1.visual->RemoveVisual(v2))) goto fail;
    if (FAILED(s1.comp->Commit()) || FAILED(s1.comp->WaitForCommitCompletion())) goto fail;
    // Commit itself restores the retained background without another Present.
    if (!s1.pixel(24, 24, RGB(255, 0, 0))) goto fail;

    // LAYER applies opacity after isolation; MULTIPLY blends each surface.
    {
        IDCompositionEffectGroup *group = NULL;
        IDCompositionVisual2 *root2 = NULL;
        if (FAILED(s1.comp->CreateEffectGroup(&group))) goto fail;
        if (FAILED(group->SetOpacity(0.5f)) || FAILED(s1.visual->SetEffect(group)))
        { release(group); goto fail; }
        release(group);
        if (FAILED(s1.visual->AddVisual(v2, TRUE, NULL))) goto fail;
        if (FAILED(s1.comp->Commit())) goto fail;
        if (!s1.pixel(24,24,RGB(0,0,128)) || !s1.pixel(8,8,RGB(128,0,0))) goto fail;
        if (FAILED(s1.visual->QueryInterface(__uuidof(IDCompositionVisual2), (void **)&root2))) goto fail;
        HRESULT mode_hr = root2->SetOpacityMode(DCOMPOSITION_OPACITY_MODE_MULTIPLY);
        release(root2);
        if (FAILED(mode_hr) || FAILED(s1.comp->Commit())) goto fail;
        if (!s1.pixel(24,24,RGB(64,0,128)) || !s1.pixel(8,8,RGB(128,0,0))) goto fail;
        if (FAILED(s1.visual->SetEffect(NULL)) || FAILED(s1.visual->RemoveVisual(v2))) goto fail;
        if (FAILED(s1.comp->Commit())) goto fail;
    }

    // Gate R1 Test 3: Opacity Blending
    // Add v2 back with an EffectGroup having 0.5f opacity.
    // Opaque Red (255, 0, 0) + 50% opacity Blue (0, 0, 255) = Purple (128, 0, 128)
    IDCompositionEffectGroup *effect;
    if (FAILED(s1.comp->CreateEffectGroup(&effect))) goto fail;
    if (FAILED(effect->SetOpacity(0.5f))) { release(effect); goto fail; }
    if (FAILED(v2->SetEffect(effect))) { release(effect); goto fail; }
    release(effect);
    if (FAILED(s1.visual->AddVisual(v2, TRUE, NULL))) goto fail;
    if (FAILED(s1.comp->Commit()) || FAILED(s1.comp->WaitForCommitCompletion())) goto fail;
    if (!s1.present() || !present2()) goto fail;
    if (!s1.pixel(24, 24, RGB(128, 0, 128))) goto fail;

    // Gate R1 Test 4: Rounded corner clip
    // Set opacity back to 1.0f on v2
    if (FAILED(s1.comp->CreateEffectGroup(&effect))) goto fail;
    if (FAILED(effect->SetOpacity(1.0f)) || FAILED(v2->SetEffect(effect))) { release(effect); goto fail; }
    release(effect);
    IDCompositionRectangleClip *rclip;
    if (FAILED(s1.comp->CreateRectangleClip(&rclip))) goto fail;
    if (FAILED(rclip->SetLeft(0.0f)) || FAILED(rclip->SetTop(0.0f)) ||
        FAILED(rclip->SetRight(64.0f)) || FAILED(rclip->SetBottom(64.0f))) { release(rclip); goto fail; }
    if (FAILED(rclip->SetTopLeftRadiusX(16.0f)) || FAILED(rclip->SetTopLeftRadiusY(16.0f))) { release(rclip); goto fail; }
    if (FAILED(v2->SetClip(rclip))) { release(rclip); goto fail; }
    release(rclip);
    if (FAILED(s1.comp->Commit()) || FAILED(s1.comp->WaitForCommitCompletion())) goto fail;
    if (!present2()) goto fail;
    // Outside corner radius -> clipped -> underlying Red visible!
    if (!s1.pixel(18, 18, RGB(255, 0, 0))) goto fail;
    // Inside clip -> Blue visible!
    if (!s1.pixel(32, 32, RGB(0, 0, 255))) goto fail;

    // A parent clip masks all descendants, and changes are transactional.
    {
        D2D_RECT_F ancestor_clip = {0,0,8,8};
        if (FAILED(s1.visual->SetClip(ancestor_clip))) goto fail;
        if (!present2() || !s1.pixel(32,32,RGB(0,0,255))) goto fail;
        if (FAILED(s1.comp->Commit())) goto fail;
        if (!s1.pixel(32,32,RGB(0,0,0)) || !s1.pixel(4,4,RGB(255,0,0))) goto fail;
        if (FAILED(s1.visual->SetClip((IDCompositionClip *)NULL))) goto fail;
        if (FAILED(s1.comp->Commit()) || !s1.pixel(32,32,RGB(0,0,255))) goto fail;
    }

    // Gate R1 Test 5: Affine Transform (reflection / negative scale)
    if (FAILED(s1.visual->RemoveVisual(v2))) goto fail;
    {
        D2D_MATRIX_3X2_F reflect = {-1.0f, 0.0f, 0.0f, 1.0f, 64.0f, 0.0f};
        if (FAILED(s1.visual->SetTransform(reflect))) goto fail;
        if (FAILED(s1.comp->Commit()) || FAILED(s1.comp->WaitForCommitCompletion())) goto fail;
        s1.black();
        if (!s1.present()) goto fail;
        if (!s1.pixel(8, 8, RGB(0, 0, 255))) goto fail;
        if (!s1.pixel(56, 8, RGB(255, 0, 0))) goto fail;
        D2D_MATRIX_3X2_F shear = {1,0.5f,0,1,0,0};
        if (FAILED(s1.visual->SetTransform(shear)) || FAILED(s1.comp->Commit())) goto fail;
        if (!s1.pixel(8,12,RGB(255,0,0)) || !s1.pixel(8,2,RGB(0,0,0))) goto fail;
        D2D_MATRIX_3X2_F rotate = {0,1,-1,0,64,0};
        if (FAILED(s1.visual->SetTransform(rotate)) || FAILED(s1.comp->Commit())) goto fail;
        if (!s1.pixel(8,8,RGB(255,0,0)) || !s1.pixel(8,48,RGB(0,0,255))) goto fail;
        if (FAILED(s1.visual->SetTransform((IDCompositionTransform *)NULL)) || FAILED(s1.comp->Commit())) goto fail;
        if (!s1.pixel(8,8,RGB(255,0,0)) || !s1.pixel(48,8,RGB(0,0,255))) goto fail;
    }

    release(v2); release(pattern2); release(bb2); release(sc2);
    return true;

fail:
    printf("retained_multilayer_failure\n");
    release(v2); release(pattern2); release(bb2); release(sc2);
    return false;
}

static bool retained_nested_group_pixels()
{
    Scene s;
    IDCompositionVisual *root = NULL, *middle = NULL;
    IDCompositionEffectGroup *effect = NULL;
    IDCompositionRectangleClip *clip = NULL;
    bool ok = false;
    D2D_MATRIX_3X2_F rotate = {0,1,-1,0,128,16};
    D2D_RECT_F root_clip = {16,8,80,72};
    if (!s.init()) return false;
    if (FAILED(s.comp->CreateVisual(&root)) || FAILED(s.comp->CreateVisual(&middle))
            || FAILED(s.comp->CreateEffectGroup(&effect)) || FAILED(s.comp->CreateRectangleClip(&clip))) goto done;
    if (FAILED(root->SetTransform(rotate)) || FAILED(root->SetClip(root_clip))
            || FAILED(middle->SetOffsetX(16.0f)) || FAILED(middle->SetOffsetY(8.0f))
            || FAILED(middle->SetClip(clip)) || FAILED(root->SetEffect(effect))
            || FAILED(middle->AddVisual(s.visual,TRUE,NULL)) || FAILED(root->AddVisual(middle,TRUE,NULL))
            || FAILED(s.target->SetRoot(root)) || FAILED(s.comp->Commit()) || !s.present()) goto done;
    // A newly created clip is unbounded, not an empty rectangle. Contentless
    // rotated ancestors must preserve the colored leaf at its transformed location.
    if (!s.pixel(112,40,RGB(255,0,0)) || !s.pixel(112,88,RGB(0,0,255))) goto done;
    if (FAILED(clip->SetLeft(0.0f)) || FAILED(clip->SetTop(0.0f))
            || FAILED(clip->SetRight(64.0f)) || FAILED(clip->SetBottom(64.0f))
            || FAILED(clip->SetTopLeftRadiusX(16.0f)) || FAILED(clip->SetTopLeftRadiusY(16.0f))
            || FAILED(clip->SetTopRightRadiusX(8.0f)) || FAILED(clip->SetTopRightRadiusY(12.0f))
            || FAILED(clip->SetBottomLeftRadiusX(12.0f)) || FAILED(clip->SetBottomLeftRadiusY(8.0f))
            || FAILED(clip->SetBottomRightRadiusX(4.0f)) || FAILED(clip->SetBottomRightRadiusY(4.0f))) goto done;
    // Live clip-object mutations are pending too, even if a producer presents.
    if (!s.present() || !s.pixel(118,34,RGB(255,0,0))) goto done;
    if (FAILED(s.comp->Commit())) goto done;
    if (!s.pixel(118,34,RGB(0,0,0)) || !s.pixel(112,40,RGB(255,0,0))
            || !s.pixel(118,94,RGB(0,0,0)) || !s.pixel(112,88,RGB(0,0,255))
            || !s.pixel(58,34,RGB(0,0,0)) || !s.pixel(64,40,RGB(255,0,0))
            || !s.pixel(57,95,RGB(0,0,0)) || !s.pixel(64,88,RGB(0,0,255))) goto done;
    // Contract the root clip independently of the child's rounded clip. Both
    // masks must survive transformation; expanding restores the retained image.
    root_clip.right = 48;
    if (FAILED(root->SetClip(root_clip)) || !s.present() || !s.pixel(112,88,RGB(0,0,255))) goto done;
    if (FAILED(s.comp->Commit()) || !s.pixel(112,88,RGB(0,0,0)) || !s.pixel(112,40,RGB(255,0,0))) goto done;
    root_clip.right = 80;
    if (FAILED(root->SetClip(root_clip)) || FAILED(s.comp->Commit()) || !s.pixel(112,88,RGB(0,0,255))) goto done;
    if (FAILED(effect->SetOpacity(0.5f)) || !s.present() || !s.pixel(112,40,RGB(255,0,0))) goto done;
    if (FAILED(s.comp->Commit()) || !s.pixel(112,40,RGB(128,0,0)) || !s.pixel(112,88,RGB(0,0,128))) goto done;
    if (FAILED(effect->SetOpacity(1.0f)) || !s.present() || !s.pixel(112,40,RGB(128,0,0))) goto done;
    if (FAILED(s.comp->Commit()) || !s.pixel(112,40,RGB(255,0,0))) goto done;
    ok = true;
done:
    s.target->SetRoot(NULL);
    s.comp->Commit();
    release(clip); release(effect); release(middle); release(root);
    return ok;
}

static bool multi_target_transactions()
{
    Scene s, other;
    bool ok = false;
    if (!s.init()) return false;
    other.hwnd = create_window("G4 second target",256,256,NULL);
    if (!other.hwnd || !SetWindowPos(other.hwnd,NULL,350,10,256,256,SWP_NOZORDER)) return false;
    other.dev = s.dev; other.dev->AddRef();
    other.ctx = s.ctx; other.ctx->AddRef();
    other.comp = s.comp; other.comp->AddRef();
    other.pattern = s.pattern; other.pattern->AddRef();
    other.sc = create_comp_swapchain(other.dev,64,64);
    if (!other.sc || FAILED(s.comp->CreateTargetForHwnd(other.hwnd,TRUE,&other.target))
            || FAILED(s.comp->CreateVisual(&other.visual))
            || FAILED(other.visual->SetContent(other.sc)) || FAILED(other.target->SetRoot(other.visual))
            || FAILED(other.sc->GetBuffer(0,__uuidof(ID3D11Texture2D),(void **)&other.bb))) return false;
    flush_messages();
    if (!other.black()) return false;
    if (FAILED(s.comp->Commit()) || !s.present() || !other.present()
            || !s.pixel(4,8,RGB(255,0,0)) || !other.pixel(4,8,RGB(255,0,0))) goto done;
    if (FAILED(s.visual->SetOffsetX(8.0f)) || FAILED(other.visual->SetOffsetX(16.0f))) goto done;
    if (!s.present() || !other.present()
            || !s.pixel(4,8,RGB(255,0,0)) || !other.pixel(4,8,RGB(255,0,0))) goto done;
    if (FAILED(s.comp->Commit()) || FAILED(s.comp->WaitForCommitCompletion())
            || !s.pixel(4,8,RGB(0,0,0)) || !other.pixel(4,8,RGB(0,0,0))
            || !s.pixel(12,8,RGB(255,0,0)) || !other.pixel(20,8,RGB(255,0,0))) goto done;
    // Allocation failure on target two must not publish target one's edits.
    if (FAILED(s.visual->SetOffsetX(32.0f)) || FAILED(other.visual->SetOffsetX(40.0f))) goto done;
    if (!SetWindowPos(other.hwnd,NULL,0,0,8300,300,SWP_NOMOVE|SWP_NOZORDER|SWP_NOSENDCHANGING)) goto done;
    if (s.comp->Commit() != E_NOTIMPL || !s.pixel(12,8,RGB(255,0,0))) goto done;
    if (!SetWindowPos(other.hwnd,NULL,0,0,256,256,SWP_NOMOVE|SWP_NOZORDER)) goto done;
    flush_messages();
    if (FAILED(s.comp->Commit()) || !s.pixel(12,8,RGB(0,0,0)) || !s.pixel(40,8,RGB(255,0,0))
            || !other.pixel(20,8,RGB(0,0,0)) || !other.pixel(48,8,RGB(255,0,0))) goto done;
    // Shared content placements within a target are supported, but the current
    // one-HWND-per-producer backend must reject ambiguous cross-target routing.
    if (FAILED(other.visual->SetContent(s.sc)) || FAILED(s.visual->SetOffsetX(0.0f))) goto done;
    if (s.comp->Commit() != E_NOTIMPL || !s.pixel(12,8,RGB(0,0,0))
            || !other.pixel(48,8,RGB(255,0,0))) goto done;
    if (FAILED(other.visual->SetContent(other.sc)) || FAILED(s.visual->SetOffsetX(32.0f))
            || FAILED(other.target->SetRoot(NULL)) || FAILED(s.comp->Commit())) goto done;
    ok = s.pixel(40,8,RGB(255,0,0)) && other.pixel(48,8,RGB(0,0,0));
done:
    return ok;
}

static bool reparented_tree_lifetime()
{
    Scene s;
    IDCompositionVisual *a = NULL, *b = NULL, *marker = NULL, *leaf = NULL;
    IDCompositionVisual **visuals[] = {&a,&b,&marker,&leaf};
    bool ok = false;
    if (!s.init()) return false;
    if (FAILED(s.comp->CreateVisual(&a)) || FAILED(s.comp->CreateVisual(&b))
            || FAILED(s.comp->CreateVisual(&marker)) || FAILED(s.comp->CreateVisual(&leaf))) goto done;
    if (FAILED(marker->SetContent(s.sc)) || FAILED(leaf->SetContent(s.sc))
            || FAILED(marker->SetOffsetX(128.0f)) || FAILED(b->SetOffsetX(64.0f))
            || FAILED(a->AddVisual(marker,TRUE,NULL)) || FAILED(b->AddVisual(leaf,TRUE,NULL))
            || FAILED(s.visual->AddVisual(a,TRUE,NULL)) || FAILED(s.visual->AddVisual(b,FALSE,NULL))
            || FAILED(s.comp->Commit()) || !s.present()
            || !s.pixel(72,8,RGB(255,0,0)) || !s.pixel(136,8,RGB(255,0,0))) goto done;
    // The new parent's traversal precedes the old parent's traversal. Reusing
    // active sibling links too early disconnects or leaks the marker sibling.
    if (FAILED(leaf->SetOffsetX(16.0f)) || FAILED(a->AddVisual(leaf,TRUE,NULL))
            || FAILED(s.comp->Commit()) || !s.pixel(72,8,RGB(0,0,255))
            || !s.pixel(88,8,RGB(0,0,0)) || !s.pixel(136,8,RGB(255,0,0))) goto done;
    if (FAILED(b->AddVisual(leaf,TRUE,NULL)) || FAILED(s.comp->Commit())
            || !s.pixel(88,8,RGB(255,0,0)) || !s.pixel(136,8,RGB(255,0,0))) goto done;
    if (FAILED(a->AddVisual(leaf,TRUE,NULL)) || FAILED(s.comp->Commit())
            || !s.pixel(88,8,RGB(0,0,0)) || !s.pixel(136,8,RGB(255,0,0))) goto done;
    if (FAILED(a->RemoveAllVisuals()) || FAILED(b->RemoveAllVisuals())
            || FAILED(s.visual->RemoveAllVisuals()) || FAILED(s.target->SetRoot(NULL))
            || FAILED(s.comp->Commit()) || FAILED(s.comp->WaitForCommitCompletion())) goto done;
    ok = true;
done:
    // Wine's explicit active/pending ownership must release every edge, not
    // just display the right flat scene while orphaning an active COM reference.
    for (IDCompositionVisual **visual : visuals)
    {
        if (*visual)
        {
            ULONG refs = (*visual)->Release();
            *visual = NULL;
            if (ok && refs) { printf("REPARENT_LEAK refs=%lu\n",(unsigned long)refs); ok = false; }
        }
    }
    return ok;
}

struct ConcurrentSceneUpdate
{
    IDCompositionDevice *device;
    IDCompositionVisual *visual;
    IDCompositionEffectGroup *effect;
    IDCompositionRectangleClip *clip;
    HANDLE start;
    bool passed;
};

static DWORD WINAPI concurrent_scene_update(void *argument)
{
    ConcurrentSceneUpdate *update = (ConcurrentSceneUpdate *)argument;
    update->passed = false;
    if (WaitForSingleObject(update->start,10000) != WAIT_OBJECT_0) return 1;
    for (int i = 0; i < 64; ++i)
    {
        if (FAILED(update->visual->SetOffsetX(float(i % 8)))
                || FAILED(update->effect->SetOpacity(i % 2 ? 0.5f : 1.0f))
                || FAILED(update->clip->SetRight(i % 2 ? 32.0f : 64.0f))
                || FAILED(update->device->Commit())) return 2;
    }
    update->passed = true;
    return 0;
}

static bool concurrent_commit_and_device_lifetime()
{
    Scene s;
    IDCompositionEffectGroup *effect = NULL;
    IDCompositionRectangleClip *clip = NULL;
    HANDLE thread = NULL, start = NULL;
    ConcurrentSceneUpdate update = {};
    bool ok = false;
    DWORD code = 1;
    if (!s.init()) return false;
    if (FAILED(s.comp->CreateEffectGroup(&effect)) || FAILED(s.comp->CreateRectangleClip(&clip))) goto done;
    if (FAILED(clip->SetLeft(0.0f)) || FAILED(clip->SetTop(0.0f))
            || FAILED(clip->SetRight(64.0f)) || FAILED(clip->SetBottom(64.0f))
            || FAILED(s.visual->SetEffect(effect)) || FAILED(s.visual->SetClip(clip))
            || FAILED(s.comp->Commit()) || !s.present()) goto done;
    if (!(start = CreateEventA(NULL,TRUE,FALSE,NULL))) goto done;
    update = {s.comp,s.visual,effect,clip,start,false};
    if (!(thread = CreateThread(NULL,0,concurrent_scene_update,&update,0,NULL))) goto done;
    if (!SetEvent(start)) goto done;
    for (int i = 0; i < 64; ++i)
    {
        flush_messages();
        if (FAILED(s.comp->Commit()) || !s.present()) goto done;
    }
    if (WaitForSingleObject(thread,30000) != WAIT_OBJECT_0
            || !GetExitCodeThread(thread,&code) || code || !update.passed) goto done;
    printf("CONCURRENT_STAGE final_commit\n");
    flush_messages();
    if (FAILED(s.visual->SetOffsetX(0.0f)) || FAILED(effect->SetOpacity(1.0f))
            || FAILED(clip->SetRight(64.0f)) || FAILED(s.comp->Commit())
            || !s.pixel(8,8,RGB(255,0,0)) || !s.pixel(48,8,RGB(0,0,255))) goto done;
    // Children own their creating device. Drop the caller's last device ref
    // before mutating/releasing targets, visuals, effects and clips in turn.
    release(s.comp);
    if (FAILED(s.visual->SetOffsetX(4.0f)) || FAILED(effect->SetOpacity(0.5f))
            || FAILED(clip->SetRight(32.0f)) || FAILED(s.target->SetRoot(NULL))) goto done;
    release(s.target);
    release(s.visual);
    if (FAILED(effect->SetOpacity(1.0f)) || FAILED(clip->SetRight(64.0f))) goto done;
    release(effect);
    if (FAILED(clip->SetRight(32.0f))) goto done;
    ok = true;
done:
    if (thread)
    {
        // Never kill a thread while it can hold a graphics/device lock. Fail the
        // owned probe process on a stuck worker; the external runner reaps it.
        if (WaitForSingleObject(thread,30000) != WAIT_OBJECT_0) ExitProcess(124);
        CloseHandle(thread);
    }
    if (start) CloseHandle(start);
    release(clip); release(effect);
    return ok;
}

static bool retained_formats_resize_and_lifetime()
{
    Scene s;
    RECT previous_client;
    if (!s.init() || FAILED(s.comp->Commit()) || !s.present()) return false;
    if (!s.pixel(24,24,RGB(255,0,0))) return false;
    for (int format = 0; format < 2; ++format)
    for (int ignore = 0; ignore < 2; ++ignore)
    {
        IDXGISwapChain1 *sc = create_comp_swapchain(s.dev,64,64,
                format ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM,
                ignore ? DXGI_ALPHA_MODE_IGNORE : DXGI_ALPHA_MODE_PREMULTIPLIED);
        IDCompositionVisual *visual = NULL;
        ID3D11Texture2D *buffer = NULL;
        ID3D11RenderTargetView *view = NULL;
        bool ok = false;
        const float color[] = {0,0,ignore ? 1.0f : 128.0f/255.0f,ignore ? 0.0f : 128.0f/255.0f};
        if (!sc) return false;
        if (FAILED(s.comp->CreateVisual(&visual)) || FAILED(visual->SetContent(sc))) goto cleanup;
        if (FAILED(visual->SetOffsetX(16)) || FAILED(visual->SetOffsetY(16))) goto cleanup;
        if (FAILED(sc->GetBuffer(0,__uuidof(ID3D11Texture2D),(void **)&buffer))) goto cleanup;
        if (FAILED(s.dev->CreateRenderTargetView(buffer,NULL,&view))) goto cleanup;
        if (FAILED(s.visual->AddVisual(visual,TRUE,NULL)) || FAILED(s.comp->Commit())) goto cleanup;
        s.ctx->ClearRenderTargetView(view,color);
        if (FAILED(sc->Present(0,0))) goto cleanup;
        printf("FORMAT_STAGE format=%d ignore=%d initial\n",format,ignore);
        if (!s.pixel(24,24,ignore ? RGB(0,0,255) : RGB(127,0,128))) goto cleanup;

        // Reallocate the target surface after a real HWND resize without Commit.
        if (!GetClientRect(s.hwnd,&previous_client)
                || !SetWindowPos(s.hwnd,NULL,0,0,360,300,SWP_NOMOVE|SWP_NOZORDER)) goto cleanup;
        flush_messages();
        // Paint only the newly exposed application area. Never repaint old
        // pixels: that would hide underlay feedback or preservation failures.
        if (!s.black_new_area(previous_client)) goto cleanup;
        printf("FORMAT_STAGE format=%d ignore=%d resized\n",format,ignore);
        if (!s.present() || !s.pixel(24,24,ignore ? RGB(0,0,255) : RGB(127,0,128))) goto cleanup;
        if (!s.pixel(300,240,RGB(0,0,0))) goto cleanup;

        // Drop caller references before detaching: retained data must not use
        // raw producer pointers after the visual releases its last reference.
        release(view); release(buffer); release(sc);
        if (FAILED(s.visual->RemoveVisual(visual)) || FAILED(s.comp->Commit())) goto cleanup;
        release(visual);
        ok = s.present() && s.pixel(24,24,RGB(255,0,0));
cleanup:
        if (visual) s.visual->RemoveVisual(visual);
        s.comp->Commit();
        release(view); release(buffer); release(sc); release(visual);
        if (!ok) { printf("FORMAT_FAIL format=%d ignore=%d\n",format,ignore); return false; }
    }
    // One producer can appear twice in a target. Prepare/publish it once, while
    // retaining both placements and restoring the underlying content on removal.
    printf("FORMAT_STAGE shared_content\n");
    {
        IDCompositionVisual *copy = NULL;
        if (FAILED(s.comp->CreateVisual(&copy))) return false;
        bool ok = SUCCEEDED(copy->SetContent(s.sc)) && SUCCEEDED(copy->SetOffsetX(32))
                && SUCCEEDED(s.visual->AddVisual(copy,TRUE,NULL)) && SUCCEEDED(s.comp->Commit())
                && s.pixel(40,8,RGB(255,0,0));
        if (SUCCEEDED(s.visual->RemoveVisual(copy))) s.comp->Commit();
        release(copy);
        if (!ok || !s.pixel(40,8,RGB(0,0,255))) return false;
    }
    // Deterministic allocation-policy failure: preparation must reject an
    // oversized target BEFORE applying the staged placement change.
    printf("FORMAT_STAGE preparation_failure\n");
    {
        RECT original;
        if (!GetWindowRect(s.hwnd,&original) || FAILED(s.visual->SetOffsetX(32))) return false;
        if (!SetWindowPos(s.hwnd,NULL,0,0,8300,300,SWP_NOMOVE|SWP_NOZORDER|SWP_NOSENDCHANGING)) return false;
        flush_messages();
        RECT oversized;
        if (!GetClientRect(s.hwnd,&oversized)) return false;
        printf("PREPARE_CLIENT %ld x %ld\n",oversized.right,oversized.bottom);
        HRESULT rejected = s.comp->Commit();
        printf("PREPARE_FAILURE_HR %08lx\n",(unsigned long)rejected);
        if (!SetWindowPos(s.hwnd,NULL,0,0,original.right-original.left,original.bottom-original.top,
                SWP_NOMOVE|SWP_NOZORDER)) return false;
        flush_messages();
        if (rejected != E_NOTIMPL || !s.present() || !s.pixel(8,8,RGB(255,0,0))) return false;
        if (FAILED(s.visual->SetOffsetX(0.0f)) || FAILED(s.comp->Commit())) return false;
    }
    // Target destruction revokes the binding even with a live producing chain.
    printf("FORMAT_STAGE target_release\n");
    release(s.target);
    return s.present();
}

struct CrossCommit { Scene *scene; HRESULT hr; };
static DWORD WINAPI commit_worker(void *arg)
{
    CrossCommit *work = (CrossCommit *)arg;
    work->hr = work->scene->visual->SetOffsetX(32);
    if (SUCCEEDED(work->hr)) work->hr = work->scene->comp->Commit();
    if (SUCCEEDED(work->hr)) work->hr = work->scene->comp->WaitForCommitCompletion();
    return 0;
}
static bool cross_thread_completion_pixels()
{
    Scene s;
    if (!s.init() || FAILED(s.comp->Commit())) return false;
    if (!s.present() || !s.pixel(8,8,RGB(255,0,0))) return false;
    CrossCommit work = {&s, E_FAIL};
    HANDLE thread = CreateThread(NULL, 0, commit_worker, &work, 0, NULL);
    if (!thread) return false;
    // The swapchain-owner thread deliberately does not pump window messages.
    DWORD wait = WaitForSingleObject(thread, 3000);
    if (wait != WAIT_OBJECT_0)
    {
        // Keep stack/COM objects alive until worker ends; runner bounds deadlock.
        WaitForSingleObject(thread, INFINITE);
    }
    CloseHandle(thread);
    if (wait != WAIT_OBJECT_0 || FAILED(work.hr)) return false;
    return s.present() && s.pixel(8,8,RGB(0,0,0)) && s.pixel(40,8,RGB(255,0,0));
}

static int observer_child(HANDLE mapping, HANDLE ready)
{
    Scene s;
    ID3D11Device5 *device = NULL;
    ID3D11Fence *fence = NULL;
    HANDLE event = NULL;
    int result = 1;
    if (!s.init()) return 2;
    if (FAILED(s.dev->QueryInterface(__uuidof(ID3D11Device5), (void **)&device))) goto done;
    if (FAILED(device->OpenSharedFence(mapping, __uuidof(ID3D11Fence), (void **)&fence))) goto done;
    if (!(event = CreateEventA(NULL, FALSE, FALSE, NULL))) goto done;
    for (UINT64 value = 1; value <= 128; ++value)
    {
        if (FAILED(fence->SetEventOnCompletion(value, event))) goto done;
        if (value == 1) SetEvent(ready);
        if (WaitForSingleObject(event, 3000) != WAIT_OBJECT_0) goto done;
    }
    result = 0;
done:
    if (event) CloseHandle(event);
    CloseHandle(mapping); CloseHandle(ready);
    release(fence); release(device);
    return result;
}

static bool imported_fence_observers()
{
    Scene s;
    if (!s.init()) return false;
    ID3D11Device5 *device = NULL;
    ID3D11DeviceContext4 *context = NULL;
    ID3D11Fence *producer = NULL, *observers[3] = {};
    HANDLE handle = NULL, events[3] = {};
    HANDLE ready = NULL;
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), NULL, TRUE};
    PROCESS_INFORMATION child = {};
    STARTUPINFOA startup = {};
    char executable[MAX_PATH], command[2 * MAX_PATH];
    DWORD exit_code = 1;
    bool ok = false;
    if (FAILED(s.dev->QueryInterface(__uuidof(ID3D11Device5), (void **)&device))
            || FAILED(s.ctx->QueryInterface(__uuidof(ID3D11DeviceContext4), (void **)&context))) goto done;
    if (FAILED(device->CreateFence(0, D3D11_FENCE_FLAG_SHARED, __uuidof(ID3D11Fence), (void **)&producer))) goto done;
    if (FAILED(producer->CreateSharedHandle(NULL, GENERIC_ALL, NULL, &handle))) goto done;
    if (!SetHandleInformation(handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) goto done;
    if (!(ready = CreateEventA(&attributes, FALSE, FALSE, NULL))) goto done;
    if (!GetModuleFileNameA(NULL, executable, ARRAYSIZE(executable))) goto done;
    snprintf(command, sizeof(command), "\"%s\" --observer-child %llx %llx", executable,
            (unsigned long long)(UINT_PTR)handle, (unsigned long long)(UINT_PTR)ready);
    startup.cb = sizeof(startup);
    if (!CreateProcessA(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &child)) goto done;
    if (WaitForSingleObject(ready, 10000) != WAIT_OBJECT_0) goto done;
    for (int i = 0; i < 3; ++i)
    {
        if (FAILED(device->OpenSharedFence(handle, __uuidof(ID3D11Fence), (void **)&observers[i]))) goto done;
        if (!(events[i] = CreateEventA(NULL, FALSE, FALSE, NULL))) goto done;
    }
    for (UINT64 value = 1; value <= 128; ++value)
    {
        if (FAILED(observers[0]->SetEventOnCompletion(value, events[0]))) goto done;
        if (FAILED(context->Signal(producer, value))) goto done;
        // Deliberately register some observers concurrently with/already after Signal.
        for (int i = 1; i < 3; ++i)
            if (FAILED(observers[i]->SetEventOnCompletion(value, events[i]))) goto done;
        if (WaitForMultipleObjects(3, events, TRUE, 3000) != WAIT_OBJECT_0) goto done;
        for (int i = 0; i < 3; ++i)
            if (observers[i]->GetCompletedValue() < value) goto done;
    }
    if (WaitForSingleObject(child.hProcess, 10000) != WAIT_OBJECT_0) goto done;
    if (!GetExitCodeProcess(child.hProcess, &exit_code) || exit_code != 0) goto done;
    ok = true;
done:
    if (child.hProcess)
    {
        if (WaitForSingleObject(child.hProcess, 0) == WAIT_TIMEOUT)
        {
            TerminateProcess(child.hProcess, 125); // only our owned diagnostic child
            WaitForSingleObject(child.hProcess, 3000);
        }
        CloseHandle(child.hThread); CloseHandle(child.hProcess);
    }
    if (ready) CloseHandle(ready);
    for (int i = 0; i < 3; ++i) { release(observers[i]); if (events[i]) CloseHandle(events[i]); }
    if (handle) CloseHandle(handle);
    release(producer); release(context); release(device);
    return ok;
}

struct DCompPathStats
{
    UINT64 gpu_captures, cpu_readbacks, gpu_compositions, cpu_compositions;
};

static bool dcomp_path_stats(IDXGISwapChain1 *sc, DCompPathStats *stats)
{
    // Reserved Wine diagnostic GUID; these are operation counters, not public
    // DXGI frame/scanout statistics or a claim of physical GPU acceleration.
    static const GUID guid = {0xa1042da3,0x2ea1,0x4d9a,{0xa4,0x03,0x01,0x1e,0xd3,0x61,0x83,0xfb}};
    UINT size = sizeof(*stats);
    if (sc->GetPrivateData(guid,&size,stats) != S_OK || size != sizeof(*stats)) return false;
    printf("DCOMP_PATH gpu_captures=%llu cpu_readbacks=%llu gpu_compositions=%llu cpu_compositions=%llu\n",
            (unsigned long long)stats->gpu_captures,(unsigned long long)stats->cpu_readbacks,
            (unsigned long long)stats->gpu_compositions,(unsigned long long)stats->cpu_compositions);
    return true;
}

static bool opaque_retained_gpu_delivery()
{
    RECT previous_client;
    const char *renderer = getenv("WINE_D3D_CONFIG");
    bool gl = renderer && strstr(renderer,"renderer=gl");
    if (!gl && (!renderer || !strstr(renderer,"renderer=vulkan"))) return false;
    for (int format_index = 0; format_index < 2; ++format_index)
    {
        Scene s;
        ID3D11RenderTargetView *view = NULL;
        DXGI_FORMAT format = format_index ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
        DCompPathStats before = {}, after = {};
        DWORD pixels[64 * 64];
        D2D_RECT_F clip = {0,0,16,64};
        const float blue[] = {0,0,1,0}, red[] = {1,0,0,0};
        bool ok = false;
        if (!s.init(DXGI_ALPHA_MODE_IGNORE,format)) return false;
        if (FAILED(s.comp->Commit()) || !s.present()
                || !s.pixel(8,8,RGB(255,0,0)) || !s.pixel(48,8,RGB(0,0,255))) goto done;
        for (int frame = 0; frame < 60; ++frame)
        {
            DWORD color = frame % 2 ? 0xff00ff00 : format_index ? 0xff0000ff : 0xffff0000;
            for (DWORD &pixel : pixels) pixel = color;
            s.ctx->UpdateSubresource(s.pattern,0,NULL,pixels,64 * 4,0);
            if (!s.present() || !s.pixel(8,8,frame % 2 ? RGB(0,255,0) : RGB(255,0,0))) goto done;
        }
        if (!dcomp_path_stats(s.sc,&before)) goto done;
        if (gl ? before.gpu_captures != 61 || before.cpu_readbacks || before.cpu_compositions
                        || before.gpu_compositions < 61
               : before.gpu_captures || before.gpu_compositions || before.cpu_readbacks < 61
                        || before.cpu_compositions < 61) goto done;

        // Render the next frame, but don't Present it. Commit must move the
        // retained GPU snapshot (green), not consume that unpublished blue.
        if (FAILED(s.dev->CreateRenderTargetView(s.bb,NULL,&view))) goto done;
        s.ctx->ClearRenderTargetView(view,blue);
        if (FAILED(s.visual->SetOffsetX(32.0f)) || FAILED(s.comp->Commit())
                || !s.pixel(8,8,RGB(0,0,0)) || !s.pixel(40,8,RGB(0,255,0))) goto done;
        if (!dcomp_path_stats(s.sc,&after) || after.cpu_readbacks != before.cpu_readbacks) goto done;

        // Switching to the reference path reads the LAST PRESENTED GPU image
        // once, not the mutable back buffer. Switching back reuses that image.
        if (FAILED(s.visual->SetClip(clip)) || FAILED(s.comp->Commit())
                || !s.pixel(40,8,RGB(0,255,0)) || !s.pixel(56,8,RGB(0,0,0))) goto done;
        if (!dcomp_path_stats(s.sc,&after) || (gl && after.cpu_readbacks != before.cpu_readbacks + 1)) goto done;
        before = after;
        if (FAILED(s.visual->SetClip((IDCompositionClip *)NULL)) || FAILED(s.comp->Commit())
                || !s.pixel(56,8,RGB(0,255,0)) || !dcomp_path_stats(s.sc,&after)
                || after.cpu_readbacks != before.cpu_readbacks) goto done;
        if (FAILED(s.sc->Present(0,0)) || !s.pixel(40,8,RGB(0,0,255))) goto done;
        // A frame actually Presented while the CPU fallback is active must
        // supersede the older GPU image when the scene becomes GPU-eligible.
        if (FAILED(s.visual->SetClip(clip)) || FAILED(s.comp->Commit())) goto done;
        s.ctx->ClearRenderTargetView(view,red);
        if (FAILED(s.sc->Present(0,0)) || !s.pixel(40,8,RGB(255,0,0))) goto done;
        if (FAILED(s.visual->SetClip((IDCompositionClip *)NULL)) || FAILED(s.comp->Commit())
                || !s.pixel(56,8,RGB(255,0,0))) goto done;
        s.ctx->ClearRenderTargetView(view,blue);
        if (FAILED(s.sc->Present(0,0)) || !s.pixel(40,8,RGB(0,0,255))) goto done;
        if (FAILED(s.visual->SetOffsetX(-16.0f)) || FAILED(s.comp->Commit())
                || !s.pixel(0,8,RGB(0,0,255)) || !s.pixel(47,8,RGB(0,0,255))
                || !s.pixel(48,8,RGB(0,0,0))) goto done;

        release(view); release(s.bb);
        if (FAILED(s.sc->ResizeBuffers(2,96,32,format,0))
                || FAILED(s.sc->GetBuffer(0,__uuidof(ID3D11Texture2D),(void **)&s.bb))
                || FAILED(s.dev->CreateRenderTargetView(s.bb,NULL,&view))) goto done;
        s.ctx->ClearRenderTargetView(view,red);
        if (FAILED(s.sc->Present(0,0)) || !s.pixel(79,8,RGB(255,0,0))
                || !s.pixel(80,8,RGB(0,0,0)) || !s.pixel(8,40,RGB(0,0,0))) goto done;
        if (!GetClientRect(s.hwnd,&previous_client)
                || !SetWindowPos(s.hwnd,NULL,0,0,360,300,SWP_NOMOVE|SWP_NOZORDER)) goto done;
        flush_messages();
        if (!s.black_new_area(previous_client)) goto done;
        s.ctx->ClearRenderTargetView(view,red);
        if (FAILED(s.sc->Present(0,0)) || !s.pixel(79,8,RGB(255,0,0))
                || !s.pixel(300,240,RGB(0,0,0))) goto done;
        if (FAILED(s.visual->SetContent(NULL)) || FAILED(s.comp->Commit())
                || !s.pixel(8,8,RGB(0,0,0))) goto done;
        ok = true;
done:
        release(view);
        if (!ok) { printf("OPAQUE_GPU_FAIL format=%d gl=%d\n",format_index,gl); return false; }
    }
    return true;
}

static bool opaque_multilayer_gpu_delivery()
{
    const char *renderer = getenv("WINE_D3D_CONFIG");
    bool gl = renderer && strstr(renderer,"renderer=gl");
    if (!gl && (!renderer || !strstr(renderer,"renderer=vulkan"))) return false;
    for (int format_index = 0; format_index < 2; ++format_index)
    {
        Scene s;
        IDXGISwapChain1 *upper_sc = NULL;
        IDCompositionVisual *upper = NULL;
        ID3D11Texture2D *buffer = NULL;
        ID3D11RenderTargetView *view = NULL;
        DXGI_FORMAT format = format_index ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
        const float blue[] = {0,0,1,0}, red[] = {1,0,0,0};
        DCompPathStats lower_stats = {}, upper_stats = {};
        DWORD green[64 * 64];
        bool ok = false;
        if (!s.init(DXGI_ALPHA_MODE_IGNORE,format)) return false;
        upper_sc = create_comp_swapchain(s.dev,64,64,format,DXGI_ALPHA_MODE_IGNORE);
        if (!upper_sc || FAILED(s.comp->CreateVisual(&upper)) || FAILED(upper->SetContent(upper_sc))
                || FAILED(upper->SetOffsetX(16.0f)) || FAILED(upper->SetOffsetY(16.0f))
                || FAILED(upper_sc->GetBuffer(0,__uuidof(ID3D11Texture2D),(void **)&buffer))
                || FAILED(s.dev->CreateRenderTargetView(buffer,NULL,&view))
                || FAILED(s.visual->AddVisual(upper,TRUE,NULL)) || FAILED(s.comp->Commit())) goto done;
        s.ctx->ClearRenderTargetView(view,blue);
        if (!s.present() || FAILED(upper_sc->Present(0,0))
                || !s.pixel(8,8,RGB(255,0,0)) || !s.pixel(24,24,RGB(0,0,255))) goto done;
        s.ctx->ClearRenderTargetView(view,red); // not yet published
        if (!s.present() || !s.pixel(24,24,RGB(0,0,255))) goto done;
        if (FAILED(upper_sc->Present(0,0)) || !s.pixel(24,24,RGB(255,0,0))) goto done;
        for (DWORD &pixel : green) pixel = 0xff00ff00;
        s.ctx->UpdateSubresource(s.pattern,0,NULL,green,64 * 4,0);
        if (!s.present() || !s.pixel(8,8,RGB(0,255,0)) || !s.pixel(24,24,RGB(255,0,0))) goto done;
        if (!dcomp_path_stats(s.sc,&lower_stats) || !dcomp_path_stats(upper_sc,&upper_stats)) goto done;
        if (gl && (lower_stats.gpu_captures != 3 || upper_stats.gpu_captures != 2
                || lower_stats.cpu_readbacks || upper_stats.cpu_readbacks
                || lower_stats.cpu_compositions || upper_stats.cpu_compositions)) goto done;
        if (!gl && (lower_stats.gpu_captures || upper_stats.gpu_captures
                || lower_stats.gpu_compositions || upper_stats.gpu_compositions)) goto done;
        // The target must stop retaining the removed producer's GPU snapshot,
        // even when its caller references are released before this Commit.
        release(view); release(buffer); release(upper_sc);
        if (FAILED(s.visual->RemoveVisual(upper)) || FAILED(s.comp->Commit())
                || !s.pixel(24,24,RGB(0,255,0))) goto done;
        ok = true;
done:
        if (upper) s.visual->RemoveVisual(upper);
        s.comp->Commit();
        release(view); release(buffer); release(upper_sc); release(upper);
        if (!ok) { printf("OPAQUE_MULTILAYER_FAIL format=%d gl=%d\n",format_index,gl); return false; }
    }
    return true;
}

static bool fence_capacity_and_reclaim()
{
    Scene s;
    ID3D11Device5 *device = NULL;
    ID3D11DeviceContext4 *context = NULL;
    ID3D11Fence *producer = NULL, *observers[65] = {};
    HANDLE shared = NULL, duplicate = NULL, events[65] = {}, live_events[64] = {};
    bool ok = false;
    unsigned int live = 0;
    if (!s.init()) return false;
    if (FAILED(s.dev->QueryInterface(__uuidof(ID3D11Device5),(void **)&device))
            || FAILED(s.ctx->QueryInterface(__uuidof(ID3D11DeviceContext4),(void **)&context))
            || FAILED(device->CreateFence(0,D3D11_FENCE_FLAG_SHARED,__uuidof(ID3D11Fence),(void **)&producer))
            || FAILED(producer->CreateSharedHandle(NULL,GENERIC_ALL,NULL,&shared))) goto done;
    if (!DuplicateHandle(GetCurrentProcess(),shared,GetCurrentProcess(),&duplicate,0,FALSE,DUPLICATE_SAME_ACCESS)) goto done;
    CloseHandle(shared); shared = NULL;
    for (unsigned int i = 0; i < ARRAYSIZE(observers); ++i)
    {
        if (FAILED(device->OpenSharedFence(duplicate,__uuidof(ID3D11Fence),(void **)&observers[i]))
                || !(events[i] = CreateEventA(NULL,FALSE,FALSE,NULL))) goto done;
        HRESULT result = observers[i]->SetEventOnCompletion(i + 1,events[i]);
        if (i < 64 ? result != S_OK : result != HRESULT_FROM_WIN32(ERROR_TOO_MANY_OPEN_FILES)) goto done;
    }
    // Teardown must synchronously revoke the observer slot, even with a pending
    // completion callback. The previously rejected observer can then retry.
    release(observers[3]);
    CloseHandle(events[3]); events[3] = NULL;
    if (FAILED(observers[64]->SetEventOnCompletion(65,events[64]))) goto done;
    for (unsigned int i = 0; i < ARRAYSIZE(events); ++i) if (events[i]) live_events[live++] = events[i];
    if (live != 64 || FAILED(context->Signal(producer,128))
            || WaitForMultipleObjects(live,live_events,TRUE,10000) != WAIT_OBJECT_0) goto done;
    for (unsigned int i = 0; i < ARRAYSIZE(observers); ++i)
        if (observers[i] && observers[i]->GetCompletedValue() < 128) goto done;
    // Already-completed thresholds must signal immediately, including repeats.
    for (unsigned int i = 0; i < ARRAYSIZE(observers); ++i)
        if (observers[i] && (FAILED(observers[i]->SetEventOnCompletion(128,events[i]))
                || WaitForSingleObject(events[i],0) != WAIT_OBJECT_0)) goto done;
    ok = true;
done:
    for (unsigned int i = 0; i < ARRAYSIZE(observers); ++i)
    {
        release(observers[i]);
        if (events[i]) CloseHandle(events[i]);
    }
    if (duplicate) CloseHandle(duplicate);
    if (shared) CloseHandle(shared);
    release(producer); release(context); release(device);
    return ok;
}

static bool process_cpu_ticks(HANDLE process, ULONGLONG *ticks)
{
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(process,&created,&exited,&kernel,&user)) return false;
    *ticks = ((ULONGLONG)kernel.dwHighDateTime << 32) + kernel.dwLowDateTime
            + ((ULONGLONG)user.dwHighDateTime << 32) + user.dwLowDateTime;
    return true;
}

static int dormant_observer_child(HANDLE mapping, HANDLE ready)
{
    ID3D11Device *base = NULL;
    ID3D11DeviceContext *context = NULL;
    ID3D11Device5 *device = NULL;
    ID3D11Fence *observers[64] = {};
    HANDLE events[64] = {};
    int result = 2;
    if (FAILED(D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,0,NULL,0,
            D3D11_SDK_VERSION,&base,NULL,&context))
            || FAILED(base->QueryInterface(__uuidof(ID3D11Device5),(void **)&device))) goto done;
    for (UINT i = 0; i < ARRAYSIZE(observers); ++i)
        if (FAILED(device->OpenSharedFence(mapping,__uuidof(ID3D11Fence),(void **)&observers[i]))
                || !(events[i] = CreateEventA(NULL,FALSE,FALSE,NULL))
                || FAILED(observers[i]->SetEventOnCompletion(1,events[i]))) goto done;
    if (!SetEvent(ready)) goto done;
    // Parent intentionally kills only this owned child, leaving stale registry
    // slots. A bounded sleep prevents an orphan if the parent itself crashes.
    Sleep(30000);
    result = 3;
done:
    for (UINT i = 0; i < ARRAYSIZE(observers); ++i)
    { release(observers[i]); if (events[i]) CloseHandle(events[i]); }
    release(device); release(context); release(base);
    CloseHandle(mapping); CloseHandle(ready);
    return result;
}

static bool fence_process_death_and_idle()
{
    Scene s;
    ID3D11Device5 *device = NULL;
    ID3D11DeviceContext4 *context = NULL;
    ID3D11Fence *producer = NULL, *observers[64] = {};
    HANDLE mapping = NULL, ready = NULL, events[64] = {};
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes),NULL,TRUE};
    PROCESS_INFORMATION child = {};
    STARTUPINFOA startup = {};
    char executable[MAX_PATH], command[2 * MAX_PATH];
    ULONGLONG child_before, child_after, parent_before, parent_after;
    bool ok = false;
    if (!s.init()) return false;
    if (FAILED(s.dev->QueryInterface(__uuidof(ID3D11Device5),(void **)&device))
            || FAILED(s.ctx->QueryInterface(__uuidof(ID3D11DeviceContext4),(void **)&context))
            || FAILED(device->CreateFence(0,D3D11_FENCE_FLAG_SHARED,__uuidof(ID3D11Fence),(void **)&producer))
            || FAILED(producer->CreateSharedHandle(NULL,GENERIC_ALL,NULL,&mapping))
            || !SetHandleInformation(mapping,HANDLE_FLAG_INHERIT,HANDLE_FLAG_INHERIT)
            || !(ready = CreateEventA(&attributes,FALSE,FALSE,NULL))
            || !GetModuleFileNameA(NULL,executable,ARRAYSIZE(executable))) goto done;
    snprintf(command,sizeof(command),"\"%s\" --dormant-observer-child %llx %llx",executable,
            (unsigned long long)(UINT_PTR)mapping,(unsigned long long)(UINT_PTR)ready);
    startup.cb = sizeof(startup);
    if (!CreateProcessA(NULL,command,NULL,NULL,TRUE,0,NULL,NULL,&startup,&child)) goto done;
    {
        HANDLE started[] = {ready,child.hProcess};
        if (WaitForMultipleObjects(2,started,FALSE,10000) != WAIT_OBJECT_0) goto done;
    }
    if (FAILED(device->OpenSharedFence(mapping,__uuidof(ID3D11Fence),(void **)&observers[0]))
            || !(events[0] = CreateEventA(NULL,FALSE,FALSE,NULL))
            || observers[0]->SetEventOnCompletion(1,events[0]) != HRESULT_FROM_WIN32(ERROR_TOO_MANY_OPEN_FILES)) goto done;
    Sleep(500); // exclude device/threadpool startup from the idle interval
    if (!process_cpu_ticks(child.hProcess,&child_before)
            || !process_cpu_ticks(GetCurrentProcess(),&parent_before)) goto done;
    Sleep(2000);
    if (!process_cpu_ticks(child.hProcess,&child_after)
            || !process_cpu_ticks(GetCurrentProcess(),&parent_after)) goto done;
    printf("FENCE_IDLE parent_ms=%llu child_ms=%llu interval_ms=2000 limit_ms=100\n",
            (unsigned long long)((parent_after-parent_before)/10000),
            (unsigned long long)((child_after-child_before)/10000));
    // Each process must consume at most 5% of one core in this scoped interval.
    if (parent_after-parent_before > 1000000 || child_after-child_before > 1000000) goto done;
    if (!TerminateProcess(child.hProcess,124) || WaitForSingleObject(child.hProcess,5000) != WAIT_OBJECT_0) goto done;
    if (FAILED(observers[0]->SetEventOnCompletion(1,events[0]))
            || FAILED(context->Signal(producer,1)) || WaitForSingleObject(events[0],3000) != WAIT_OBJECT_0) goto done;
    // All dead-process slots must be reusable, not just the first free slot.
    for (UINT i = 0; i < ARRAYSIZE(observers); ++i)
    {
        if (i && (FAILED(device->OpenSharedFence(mapping,__uuidof(ID3D11Fence),(void **)&observers[i]))
                || !(events[i] = CreateEventA(NULL,FALSE,FALSE,NULL)))) goto done;
        if (FAILED(observers[i]->SetEventOnCompletion(2,events[i]))) goto done;
    }
    if (FAILED(context->Signal(producer,2)) || WaitForMultipleObjects(64,events,TRUE,5000) != WAIT_OBJECT_0) goto done;
    ok = true;
done:
    if (child.hProcess)
    {
        if (WaitForSingleObject(child.hProcess,0) == WAIT_TIMEOUT)
        { TerminateProcess(child.hProcess,125); WaitForSingleObject(child.hProcess,5000); }
        CloseHandle(child.hThread); CloseHandle(child.hProcess);
    }
    for (UINT i = 0; i < ARRAYSIZE(observers); ++i)
    { release(observers[i]); if (events[i]) CloseHandle(events[i]); }
    if (mapping) CloseHandle(mapping);
    if (ready) CloseHandle(ready);
    release(producer); release(context); release(device);
    return ok;
}

static bool present1_dirty_retention()
{
    Scene s;
    if (!s.init(DXGI_ALPHA_MODE_IGNORE) || FAILED(s.comp->Commit())) return false;
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    if (FAILED(s.sc->GetDesc1(&desc)) || desc.BufferCount < 2
            || desc.SwapEffect != DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL) return false;
    DWORD pixels[64 * 64], patch[16 * 16];
    for (DWORD &pixel : pixels) pixel = 0xff0000ff;
    s.ctx->UpdateSubresource(s.pattern,0,NULL,pixels,64 * 4,0);
    if (!s.present() || !s.pixel(48,48,RGB(0,0,255))) return false;
    for (DWORD &pixel : pixels) pixel = 0xffff0000;
    s.ctx->UpdateSubresource(s.pattern,0,NULL,pixels,64 * 4,0);
    if (!s.present() || !s.pixel(48,48,RGB(255,0,0))) return false;
    // Back buffer 0 now contains its older BLUE frame. Modify only the entire
    // dirty rectangle, per Present1's contract. Runtime preservation must carry
    // the last presented RED pixels outside that rectangle into the new frame.
    for (DWORD &pixel : patch) pixel = 0xff00ff00;
    D3D11_BOX box = {0,0,0,16,16,1};
    RECT dirty = {0,0,16,16};
    DXGI_PRESENT_PARAMETERS parameters = {1,&dirty,NULL,NULL};
    s.ctx->UpdateSubresource(s.bb,0,&box,patch,16 * 4,0);
    s.ctx->Flush();
    if (FAILED(s.sc->Present1(0,0,&parameters)) || !s.pixel(8,8,RGB(0,255,0))
            || !s.pixel(48,48,RGB(255,0,0))) return false;
    if (FAILED(s.visual->SetOffsetX(32)) || FAILED(s.comp->Commit())
            || !s.pixel(40,8,RGB(0,255,0)) || !s.pixel(80,48,RGB(255,0,0))) return false;
    for (DWORD &pixel : patch) pixel = 0xff0000ff;
    box.left = dirty.left = 40; box.right = dirty.right = 56;
    s.ctx->UpdateSubresource(s.bb,0,&box,patch,16 * 4,0);
    s.ctx->Flush();
    if (FAILED(s.sc->Present1(0,0,&parameters)) || !s.pixel(40,8,RGB(0,255,0))
            || !s.pixel(80,8,RGB(0,0,255)) || !s.pixel(80,48,RGB(255,0,0))) return false;
    // Invalid metadata and TEST must neither rotate nor replace coherent history.
    RECT invalid = {-1,0,16,16};
    parameters.pDirtyRects = &invalid;
    if (s.sc->Present1(0,0,&parameters) != DXGI_ERROR_INVALID_CALL) return false;
    parameters.pDirtyRects = &dirty;
    if (FAILED(s.sc->Present1(0,DXGI_PRESENT_TEST,&parameters))) return false;
    // Scroll from the previous coherent image, not an overlapping in-place
    // producer copy. Explicitly dirtied pixels override the scrolled region.
    RECT scroll = {8,0,48,16};
    POINT offset = {16,0};
    dirty = {24,0,40,16};
    box.left = 24; box.right = 40;
    for (DWORD &pixel : patch) pixel = 0xffffffff;
    s.ctx->UpdateSubresource(s.bb,0,&box,patch,16 * 4,0);
    parameters.pScrollRect = &scroll;
    parameters.pScrollOffset = &offset;
    if (FAILED(s.sc->Present1(0,0,&parameters)) || !s.pixel(52,8,RGB(0,255,0))
            || !s.pixel(64,8,RGB(255,255,255)) || !s.pixel(84,8,RGB(0,0,255))
            || !s.pixel(76,8,RGB(255,0,0)) || !s.pixel(80,48,RGB(255,0,0))) return false;
    // A huge offset clips away without integer overflow or touching old pixels.
    offset.x = LONG_MIN;
    dirty = {0,0,16,16}; box.left = 0; box.right = 16;
    for (DWORD &pixel : patch) pixel = 0xff0000ff;
    s.ctx->UpdateSubresource(s.bb,0,&box,patch,16 * 4,0);
    if (FAILED(s.sc->Present1(0,0,&parameters)) || !s.pixel(40,8,RGB(0,0,255))
            || !s.pixel(64,8,RGB(255,255,255)) || !s.pixel(84,8,RGB(0,0,255))) return false;
    DWORD union_pixels[24 * 24];
    for (DWORD &pixel : union_pixels) pixel = 0xffff00ff;
    RECT overlapping[] = {{0,0,16,16},{8,0,24,16},{0,8,24,24}};
    box = {0,0,0,24,24,1};
    s.ctx->UpdateSubresource(s.bb,0,&box,union_pixels,24 * 4,0);
    parameters = {ARRAYSIZE(overlapping),overlapping,NULL,NULL};
    if (FAILED(s.sc->Present1(0,0,&parameters)) || !s.pixel(44,12,RGB(255,0,255))
            || !s.pixel(64,8,RGB(255,255,255)) || !s.pixel(84,8,RGB(0,0,255))) return false;
    RECT many[65];
    for (RECT &rect : many) rect = {0,0,16,16};
    for (DWORD &pixel : patch) pixel = 0xffffff00;
    box = {0,0,0,16,16,1};
    s.ctx->UpdateSubresource(s.bb,0,&box,patch,16 * 4,0);
    parameters = {ARRAYSIZE(many),many,NULL,NULL};
    return SUCCEEDED(s.sc->Present1(0,0,&parameters)) && s.pixel(40,8,RGB(255,255,0))
            && s.pixel(64,8,RGB(255,255,255)) && s.pixel(84,8,RGB(0,0,255));
}

static bool bitmap_interpolation_transactions()
{
    Scene s;
    if (!s.init()) return false;
    D2D_MATRIX_3X2_F scale = {2,0,0,1,0,0};
    if (FAILED(s.visual->SetTransform(scale)) || FAILED(s.comp->Commit()) || !s.present()
            || !s.pixel(63,8,RGB(255,0,0)) || !s.pixel(64,8,RGB(0,0,255))) return false;
    if (FAILED(s.visual->SetBitmapInterpolationMode(DCOMPOSITION_BITMAP_INTERPOLATION_MODE_LINEAR))
            || !s.present() || !s.pixel(63,8,RGB(255,0,0))) return false;
    // Pixel centers straddle the red/blue boundary by 1/4 and 3/4 texel.
    // A property-only Commit must resample the retained image without Present.
    if (FAILED(s.comp->Commit()) || !s.pixel(63,8,RGB(191,0,64))
            || !s.pixel(64,8,RGB(64,0,191)) || !s.pixel(0,8,RGB(255,0,0))) return false;
    IDCompositionVisual *group = NULL;
    if (FAILED(s.comp->CreateVisual(&group))) return false;
    bool ok = SUCCEEDED(group->AddVisual(s.visual,FALSE,NULL))
            && SUCCEEDED(s.target->SetRoot(group))
            && SUCCEEDED(group->SetBitmapInterpolationMode(DCOMPOSITION_BITMAP_INTERPOLATION_MODE_LINEAR))
            && SUCCEEDED(s.visual->SetBitmapInterpolationMode(DCOMPOSITION_BITMAP_INTERPOLATION_MODE_INHERIT))
            && SUCCEEDED(s.comp->Commit()) && s.pixel(63,8,RGB(191,0,64));
    ok = ok && SUCCEEDED(s.visual->SetBitmapInterpolationMode(DCOMPOSITION_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR))
            && SUCCEEDED(s.comp->Commit()) && s.pixel(63,8,RGB(255,0,0));
    ok = ok && SUCCEEDED(s.visual->SetBitmapInterpolationMode(DCOMPOSITION_BITMAP_INTERPOLATION_MODE_INHERIT))
            && SUCCEEDED(s.comp->Commit()) && s.pixel(63,8,RGB(191,0,64));
    ok = ok && s.visual->SetBitmapInterpolationMode((DCOMPOSITION_BITMAP_INTERPOLATION_MODE)2) == E_INVALIDARG
            && SUCCEEDED(s.comp->Commit()) && s.pixel(63,8,RGB(191,0,64));
    DWORD pixels[64 * 64];
    for (UINT y = 0; y < 64; ++y)
        for (UINT x = 0; x < 64; ++x) pixels[y * 64 + x] = x < 32 ? 0x80800000 : 0x80000080;
    s.ctx->UpdateSubresource(s.pattern,0,NULL,pixels,64 * 4,0);
    ok = ok && s.present() && s.pixel(63,8,RGB(96,0,32)) && s.pixel(64,8,RGB(32,0,96));
    ok = ok && SUCCEEDED(group->SetBitmapInterpolationMode(DCOMPOSITION_BITMAP_INTERPOLATION_MODE_INHERIT))
            && SUCCEEDED(s.comp->Commit()) && s.pixel(63,8,RGB(128,0,0));
    for (UINT y = 0; y < 64; ++y)
        for (UINT x = 0; x < 64; ++x)
            pixels[y * 64 + x] = y < 32 ? (x < 32 ? 0xffff0000 : 0xff0000ff)
                    : (x < 32 ? 0xff00ff00 : 0xffffffff);
    scale._22 = 2;
    s.ctx->UpdateSubresource(s.pattern,0,NULL,pixels,64 * 4,0);
    ok = ok && SUCCEEDED(group->SetBitmapInterpolationMode(DCOMPOSITION_BITMAP_INTERPOLATION_MODE_LINEAR))
            && SUCCEEDED(s.visual->SetTransform(scale)) && SUCCEEDED(s.comp->Commit()) && s.present()
            && s.pixel(63,63,RGB(159,64,64)) && s.pixel(64,64,RGB(159,191,191))
            && s.pixel(0,0,RGB(255,0,0));
    release(group);
    return ok;
}

static bool multisample_format_capabilities()
{
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    if (FAILED(D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,0,NULL,0,
            D3D11_SDK_VERSION,&device,NULL,&context))) return false;
    bool ok = true;
    const DXGI_FORMAT formats[] = {DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM};
    for (DXGI_FORMAT format : formats)
    {
        UINT flags = 0;
        bool supported = false;
        if (FAILED(device->CheckFormatSupport(format,&flags))) { ok = false; continue; }
        for (UINT samples = 2; samples <= 32; samples *= 2)
        {
            UINT quality = 0;
            HRESULT hr = device->CheckMultisampleQualityLevels(format,samples,&quality);
            printf("MSAA_CAP format=%u samples=%u hr=%08lx quality=%u flags=%x\n",
                    (unsigned int)format,samples,(unsigned long)hr,quality,flags);
            if (FAILED(hr) || !quality) continue;
            supported = true;
            ID3D11Texture2D *multi = NULL, *single = NULL, *staging = NULL;
            ID3D11RenderTargetView *view = NULL;
            D3D11_TEXTURE2D_DESC desc = {};
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            desc.Width = desc.Height = 8;
            desc.MipLevels = desc.ArraySize = 1;
            desc.Format = format;
            desc.SampleDesc.Count = samples;
            desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            bool sample_ok = SUCCEEDED(device->CreateTexture2D(&desc,NULL,&multi))
                    && SUCCEEDED(device->CreateRenderTargetView(multi,NULL,&view));
            desc.SampleDesc.Count = 1;
            sample_ok = sample_ok && SUCCEEDED(device->CreateTexture2D(&desc,NULL,&single));
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            sample_ok = sample_ok && SUCCEEDED(device->CreateTexture2D(&desc,NULL,&staging));
            if (sample_ok)
            {
                const float red[] = {1,0,0,1};
                context->ClearRenderTargetView(view,red);
                context->ResolveSubresource(single,0,multi,0,format);
                context->CopyResource(staging,single);
                sample_ok = SUCCEEDED(context->Map(staging,0,D3D11_MAP_READ,0,&mapped));
                if (sample_ok)
                {
                    DWORD expected = format == DXGI_FORMAT_R8G8B8A8_UNORM ? 0xff0000ff : 0xffff0000;
                    for (UINT y = 0; y < 8; ++y)
                        for (UINT x = 0; x < 8; ++x)
                            if (((DWORD *)((BYTE *)mapped.pData + y * mapped.RowPitch))[x] != expected) sample_ok = false;
                    context->Unmap(staging,0);
                }
            }
            printf("MSAA_PIXELS format=%u samples=%u result=%s\n",(unsigned int)format,samples,sample_ok ? "PASS" : "FAIL");
            ok = sample_ok && ok;
            release(view); release(staging); release(single); release(multi);
        }
        // A missing 8x mode must not erase a genuinely usable 2x/4x mode.
        if (supported != !!(flags & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET)) ok = false;
    }
    release(context); release(device);
    return ok;
}

static bool video_capability_consistency()
{
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    ID3D11VideoDevice *video = NULL;
    ID3D11VideoDevice1 *video1 = NULL, *via_video = NULL;
    ID3D11VideoContext1 *context1 = NULL;
    if (FAILED(D3D11CreateDevice(NULL,D3D_DRIVER_TYPE_HARDWARE,NULL,0,NULL,0,
            D3D11_SDK_VERSION,&device,NULL,&context))) return false;
    HRESULT base_hr = device->QueryInterface(__uuidof(ID3D11VideoDevice),(void **)&video);
    HRESULT device_hr = device->QueryInterface(__uuidof(ID3D11VideoDevice1),(void **)&video1);
    HRESULT context_hr = context->QueryInterface(__uuidof(ID3D11VideoContext1),(void **)&context1);
    HRESULT nested_hr = video ? video->QueryInterface(__uuidof(ID3D11VideoDevice1),(void **)&via_video)
                              : E_NOINTERFACE;
    printf("VIDEO_CAPS base=%08lx device1=%08lx context1=%08lx via_base=%08lx\n",
            (unsigned long)base_hr,(unsigned long)device_hr,(unsigned long)context_hr,(unsigned long)nested_hr);
    // Existing base-video support must survive. Newer video-device exposure
    // implies the paired context; failing QI must clear outputs consistently.
    bool ok = base_hr == S_OK && video && device_hr == nested_hr
            && (device_hr == S_OK ? video1 && via_video && context_hr == S_OK && context1
                                 : device_hr == E_NOINTERFACE && !video1 && !via_video);
    release(context1); release(via_video); release(video1); release(video);
    release(context); release(device);
    return ok;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--native-visual-control"))
    {
        native_visual_control = true;
        SetProcessDPIAware();
        // Public DComp semantics only. No Wine-only HRESULT expectations,
        // private fence interfaces, registry changes or runtime installation.
        bool layered = retained_multilayer_and_transform_pixels();
        printf("native_retained_multilayer=%s\n", layered ? "PASS" : "FAIL");
        bool nested = retained_nested_group_pixels();
        printf("native_nested_groups=%s\n", nested ? "PASS" : "FAIL");
        bool lifetime = concurrent_commit_and_device_lifetime();
        printf("native_concurrent_lifetime=%s\n", lifetime ? "PASS" : "FAIL");
        return layered && nested && lifetime ? 0 : 1;
    }
    if (argc == 4 && !strcmp(argv[1], "--observer-child"))
    {
        unsigned long long mapping, ready;
        if (sscanf(argv[2], "%llx", &mapping) != 1 || sscanf(argv[3], "%llx", &ready) != 1) return 3;
        return observer_child((HANDLE)(UINT_PTR)mapping, (HANDLE)(UINT_PTR)ready);
    }
    if (argc == 4 && !strcmp(argv[1], "--dormant-observer-child"))
    {
        unsigned long long mapping, ready;
        if (sscanf(argv[2],"%llx",&mapping) != 1 || sscanf(argv[3],"%llx",&ready) != 1) return 3;
        return dormant_observer_child((HANDLE)(UINT_PTR)mapping,(HANDLE)(UINT_PTR)ready);
    }
    const struct { const char *name; bool (*run)(); } cases[] = {
        {"supported_pixels_and_transactions",supported_pixels_and_transactions},
        {"unsupported_properties_fail_honestly",unsupported_properties_fail_honestly},
        {"retained_multilayer_and_transform_pixels",retained_multilayer_and_transform_pixels},
        {"cross_thread_completion_pixels",cross_thread_completion_pixels},
        {"imported_fence_observers",imported_fence_observers},
        {"retained_changing_frame_pixels",retained_changing_frame_pixels},
        {"retained_formats_resize_and_lifetime",retained_formats_resize_and_lifetime},
        {"retained_nested_group_pixels",retained_nested_group_pixels},
        {"concurrent_commit_and_device_lifetime",concurrent_commit_and_device_lifetime},
        {"multi_target_transactions",multi_target_transactions},
        {"reparented_tree_lifetime",reparented_tree_lifetime},
        {"fence_capacity_and_reclaim",fence_capacity_and_reclaim},
        {"opaque_retained_gpu_delivery",opaque_retained_gpu_delivery},
        {"opaque_multilayer_gpu_delivery",opaque_multilayer_gpu_delivery},
        {"video_capability_consistency",video_capability_consistency},
        {"multisample_format_capabilities",multisample_format_capabilities},
        {"present1_dirty_retention",present1_dirty_retention},
        {"bitmap_interpolation_transactions",bitmap_interpolation_transactions},
        {"fence_process_death_and_idle",fence_process_death_and_idle},
        {"application_background_pixels",application_background_pixels},
        {"application_background_detach",application_background_detach},
    };
    const char *selected = argc == 3 && !strcmp(argv[1],"--case") ? argv[2] : NULL;
    bool matched = false, passed = true;
    if (argc != 1 && !selected) return 2;
    for (const auto &test : cases)
    {
        if (selected && strcmp(selected,test.name)) continue;
        matched = true;
        bool result = test.run();
        printf("%s=%s\n",test.name,result ? "PASS" : "FAIL");
        passed = result && passed;
    }
    return !matched ? 2 : passed ? 0 : 1;
}
