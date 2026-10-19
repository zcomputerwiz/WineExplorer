// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
// Independent correctness checks. Legacy HRESULT-only probes remain historical,
// not substitutes for these target-window pixels and imported-fence observers.
#define main historical_g4_main
#include "test_g4_composition_probe.cpp"
#undef main
#include <d3d11_4.h>
#include <math.h>

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
    if (s.visual->SetOffsetX((IDCompositionAnimation *)NULL) != E_NOTIMPL) goto done;

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
    if (!s1.present()) goto fail;
    if (!s1.pixel(24, 24, RGB(255, 0, 0))) goto fail;

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
    }

    release(v2); release(pattern2); release(bb2); release(sc2);
    return true;

fail:
    release(v2); release(pattern2); release(bb2); release(sc2);
    return false;
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
    return a && b && c && d && e && f ? 0 : 1;
}
