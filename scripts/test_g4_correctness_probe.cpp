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

    bool init()
    {
        hwnd = create_window("G4 independent pixels", 256, 256, NULL);
        if (native_visual_control && hwnd)
        {
            ShowWindow(hwnd,SW_SHOWNOACTIVATE);
            SetWindowPos(hwnd,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
        }
        if (!hwnd || !create_d3d_and_dcomp(hwnd, &dev, &ctx, &comp, &target)) return false;
        sc = create_comp_swapchain(dev, 64, 64);
        if (!sc || FAILED(comp->CreateVisual(&visual))) return false;
        if (FAILED(visual->SetContent(sc)) || FAILED(target->SetRoot(visual))) return false;
        if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&bb))) return false;
        DWORD pixels[64 * 64];
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                pixels[y * 64 + x] = x < 32 ? 0xffff0000 : 0xff0000ff;
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = desc.Height = 64;
        desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        D3D11_SUBRESOURCE_DATA data = {pixels, 64 * 4, 0};
        if (FAILED(dev->CreateTexture2D(&desc, &data, &pattern))) return false;
        flush_messages();
        return true;
    }
    void black()
    {
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (native_visual_control)
        {
            HDC window_dc = GetDC(hwnd);
            FillRect(window_dc,&rc,(HBRUSH)GetStockObject(BLACK_BRUSH));
            ReleaseDC(hwnd,window_dc);
            GdiFlush();
            return;
        }
        POINT point = {0, 0};
        ClientToScreen(hwnd, &point);
        OffsetRect(&rc, point.x, point.y);
        // Establish the actual visible test background, not Wine's separate
        // GDI backing bitmap underneath its OpenGL drawable. This is placement
        // evidence, not automatic exposed-area/retained-layer repaint proof.
        HDC dc = GetDC(NULL);
        FillRect(dc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        ReleaseDC(NULL, dc);
        GdiFlush();
    }
    bool present()
    {
        ctx->CopyResource(bb, pattern);
        ctx->Flush();
        return sc->Present(0, 0) == S_OK;
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
        if (!SetWindowPos(s.hwnd,NULL,0,0,360,300,SWP_NOMOVE|SWP_NOZORDER)) goto cleanup;
        flush_messages();
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
    bool a = supported_pixels_and_transactions();
    printf("supported_pixels_and_transactions=%s\n", a ? "PASS" : "FAIL");
    bool b = unsupported_properties_fail_honestly();
    printf("unsupported_properties_fail_honestly=%s\n", b ? "PASS" : "FAIL");
    bool c = retained_multilayer_and_transform_pixels();
    printf("retained_multilayer_and_transform_pixels=%s\n", c ? "PASS" : "FAIL");
    bool d = cross_thread_completion_pixels();
    printf("cross_thread_completion_pixels=%s\n", d ? "PASS" : "FAIL");
    bool e = imported_fence_observers();
    printf("imported_fence_observers=%s\n", e ? "PASS" : "FAIL");
    bool f = retained_changing_frame_pixels();
    printf("retained_changing_frame_pixels=%s\n", f ? "PASS" : "FAIL");
    bool g = retained_formats_resize_and_lifetime();
    printf("retained_formats_resize_and_lifetime=%s\n", g ? "PASS" : "FAIL");
    bool h = retained_nested_group_pixels();
    printf("retained_nested_group_pixels=%s\n", h ? "PASS" : "FAIL");
    bool i = concurrent_commit_and_device_lifetime();
    printf("concurrent_commit_and_device_lifetime=%s\n", i ? "PASS" : "FAIL");
    bool j = multi_target_transactions();
    printf("multi_target_transactions=%s\n", j ? "PASS" : "FAIL");
    bool k = reparented_tree_lifetime();
    printf("reparented_tree_lifetime=%s\n", k ? "PASS" : "FAIL");
    return a && b && c && d && e && f && g && h && i && j && k ? 0 : 1;
}
