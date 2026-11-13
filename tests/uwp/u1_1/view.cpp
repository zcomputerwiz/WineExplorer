/*
 * WineExplorer U1.1 packaged view lifecycle probe
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/*
 * A native UWP application: it calls CoreApplication.Run with an IFrameworkViewSource of its own, records what
 * happens (TRACE lines with the time and the thread, INFO lines) and checks what was observed on Windows 11
 * (PASS and FAIL lines, compared, in sorted order, with expected-MODE.txt).
 *
 * A UWP launch passes no command line. The application id that was activated selects the mode, the output goes to
 * %LOCALAPPDATA%\view-MODE.txt (the package's own folder inside an AppContainer):
 *
 *   View        positive: multithreaded apartment; the view ends with CoreApplication.Exit
 *   ViewReturn  return:   multithreaded apartment; the view returns from Run
 *   ViewSta     sta:      single-threaded apartment
 *
 * CoreApplication.Run does not return to the calling thread in the first two modes: the process ends once the view
 * is finished, so every check of them is made by a callback.
 */

#include <windows.h>
#include <appmodel.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.Devices.Input.h>
#include <winrt/Windows.UI.Input.h>
#include <d3d11.h>
#include <dxgi1_2.h>

using namespace winrt;
using namespace Windows::ApplicationModel::Activation;
using namespace Windows::ApplicationModel::Core;
using namespace Windows::UI::Core;

static int failures;
static std::mutex lock;
static std::vector<std::string> events;     /* the lifecycle events, in order */
static std::vector<std::string> order;      /* the dispatcher batch, in order */
static DWORD main_thread, view_thread, start_tick;
static std::wstring package_full_name, application_id;
static std::string mode;

static void put(std::string const &line)
{
    std::fputs((line + "\n").c_str(), stdout);   /* one write per line: a terminated process keeps whole lines */
}

static void check(char const *name, bool passed)
{
    put(std::string(passed ? "PASS " : "FAIL ") + name);
    if (!passed) ++failures;
}

static void info(char const *fmt, ...)
{
    char line[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    put(std::string("INFO ") + line);
}

static char const *thread_name()
{
    DWORD id = GetCurrentThreadId();
    return id == main_thread ? "main" : id == view_thread ? "view" : "other";
}

static void record(std::string const &name)
{
    std::lock_guard<std::mutex> guard(lock);
    events.push_back(name);
    char stamp[64];
    std::snprintf(stamp, sizeof(stamp), "%5lu ", (unsigned long)(GetTickCount() - start_tick));
    put(std::string("TRACE ") + stamp + name + " thread=" + thread_name());
}

static int position(std::vector<std::string> const &list, std::string const &name)
{
    for (size_t i = 0; i < list.size(); ++i) if (list[i] == name) return (int)i;
    return -1;
}

static int count(std::vector<std::string> const &list, std::string const &name)
{
    int n = 0;
    for (auto const &item : list) if (item == name) ++n;
    return n;
}

static std::string join(std::vector<std::string> const &list)
{
    std::string all;
    for (auto const &item : list) all += item + " ";
    return all;
}

static std::string hex(HRESULT hr)
{
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08lX", (unsigned long)hr);
    return text;
}

static void pump(CoreDispatcher const &dispatcher, DWORD milliseconds)
{
    DWORD start = GetTickCount();
    do
    {
        dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
        Sleep(10);
    } while (GetTickCount() - start < milliseconds);
}

/* present mode: a swap chain for the CoreWindow, a few presented frames, then the window events and the input a
 * runner on the host injects (run-present.ps1 waits for the READY line) are recorded; nothing here is asserted yet,
 * the output is the measurement */
static void run_present(CoreWindow const &window)
{
    CoreDispatcher dispatcher = window.Dispatcher();
    auto bounds = window.Bounds();
    info("before: visible=%d bounds=%.0fx%.0f at %.0f,%.0f", (int)window.Visible(), bounds.Width, bounds.Height,
         bounds.X, bounds.Y);
    window.Activate();
    pump(dispatcher, 500);
    info("after Activate and 500 ms: visible=%d", (int)window.Visible());

    com_ptr<ID3D11Device> device;
    com_ptr<ID3D11DeviceContext> context;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                   nullptr, 0, D3D11_SDK_VERSION, device.put(), nullptr, context.put());
    if (FAILED(hr)) hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                           nullptr, 0, D3D11_SDK_VERSION, device.put(), nullptr, context.put());
    put("HRESULT D3D11CreateDevice " + hex(hr));
    if (FAILED(hr)) return;
    auto dxgi = device.as<IDXGIDevice>();
    com_ptr<IDXGIAdapter> adapter;
    com_ptr<IDXGIFactory2> factory;
    check_hresult(dxgi->GetAdapter(adapter.put()));
    check_hresult(adapter->GetParent(IID_PPV_ARGS(factory.put())));

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_NONE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    com_ptr<IDXGISwapChain1> chain;
    hr = factory->CreateSwapChainForCoreWindow(device.get(), static_cast<IUnknown *>(get_abi(window)), &desc, nullptr,
                                               chain.put());
    put("HRESULT CreateSwapChainForCoreWindow " + hex(hr));
    if (FAILED(hr)) return;
    DXGI_SWAP_CHAIN_DESC1 got = {};
    chain->GetDesc1(&got);
    info("swap chain: %ux%u format %u buffers %u", got.Width, got.Height, (unsigned)got.Format, got.BufferCount);
    info("after create: visible=%d", (int)window.Visible());

    for (int frame = 0; frame < 5; ++frame)
    {
        com_ptr<ID3D11Texture2D> back;
        com_ptr<ID3D11RenderTargetView> view;
        check_hresult(chain->GetBuffer(0, IID_PPV_ARGS(back.put())));
        check_hresult(device->CreateRenderTargetView(back.get(), nullptr, view.put()));
        float color[4] = { 0.1f * frame, 0.3f, 0.6f, 1.f };
        context->ClearRenderTargetView(view.get(), color);
        hr = chain->Present(1, 0);
        char text[64];
        std::snprintf(text, sizeof(text), "Present %d %s", frame, hex(hr).c_str());
        record(text);
        pump(dispatcher, 100);
    }
    info("after presenting: visible=%d", (int)window.Visible());
    pump(dispatcher, 3000);   /* the window is still opening on screen */
    auto now = window.Bounds();
    info("bounds now %.0fx%.0f at %.0f,%.0f", now.Width, now.Height, now.X, now.Y);
    auto position = window.PointerPosition();
    info("pointer position %.0f,%.0f", position.X, position.Y);
    put("READY");
    /* input injected by the host arrives here */
    pump(dispatcher, 12000);
    info("end: visible=%d", (int)window.Visible());
    CoreApplication::Exit();
    dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessUntilQuit);
}

static void run_checks(CoreWindow const &window)
{
    CoreDispatcher dispatcher = window.Dispatcher();
    auto bounds = window.Bounds();
    info("window before Activate: visible=%d bounds=%.0fx%.0f at %.0f,%.0f", (int)window.Visible(), bounds.Width,
         bounds.Height, bounds.X, bounds.Y);

    check("dispatcher_access_on_view_thread", dispatcher.HasThreadAccess());
    check("window_invisible_before_activate", !window.Visible());

    /* from another thread: no access, no window, and ProcessEvents is refused */
    bool other_access = true, other_window_is_null = false;
    int32_t other_hr = 0;
    std::thread([&] {
        other_access = dispatcher.HasThreadAccess();
        other_window_is_null = CoreWindow::GetForCurrentThread() == nullptr;
        try { dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent); }
        catch (hresult_error const &e) { other_hr = e.code(); }
    }).join();
    check("dispatcher_no_access_from_other_thread", !other_access);
    check("no_window_on_other_thread", other_window_is_null);
    check("process_events_refused_on_other_thread", (uint32_t)other_hr == 0x8001010E);

    /* Activate: without a presented frame the window stays invisible and raises nothing */
    window.Activate();
    pump(dispatcher, 1000);
    check("window_stays_invisible_after_activate", !window.Visible());
    {
        std::lock_guard<std::mutex> guard(lock);
        check("activate_raises_no_window_events", count(events, "Visible=1") == 0 && count(events, "WindowActivated") == 0 &&
              count(events, "SizeChanged") == 0);
    }

    /* the application cannot close the window of its view */
    int32_t close_hr = 0;
    try { window.Close(); }
    catch (hresult_error const &e) { close_hr = e.code(); }
    check("close_is_refused", (uint32_t)close_hr == 0x8000000E);
    {
        std::lock_guard<std::mutex> guard(lock);
        check("window_not_closed_by_close", count(events, "Closed") == 0);
    }

    /* priorities: the highest first, in order of queueing within one priority; idle last */
    auto mark = [](char const *name) { return [name] { std::lock_guard<std::mutex> guard(lock); order.push_back(name); }; };
    dispatcher.RunAsync(CoreDispatcherPriority::Low, mark("low"));
    dispatcher.RunAsync(CoreDispatcherPriority::Normal, mark("normal1"));
    dispatcher.RunAsync(CoreDispatcherPriority::High, mark("high"));
    dispatcher.RunAsync(CoreDispatcherPriority::Normal, mark("normal2"));
    dispatcher.RunIdleAsync([](IdleDispatchedHandlerArgs const &args) {
        std::lock_guard<std::mutex> guard(lock);
        order.push_back(args.IsDispatcherIdle() ? "idle(idle)" : "idle(busy)");
    });
    dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
    {
        std::lock_guard<std::mutex> guard(lock);
        info("dispatch order: %s", join(order).c_str());
        check("dispatch_priority_order", order == std::vector<std::string>{"high", "normal1", "normal2", "low", "idle(idle)"});
        order.clear();
    }

    /* ProcessOneIfPresent returns at once with nothing queued; with two items queued it runs both */
    DWORD start = GetTickCount();
    dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessOneIfPresent);
    check("process_one_if_present_does_not_wait", GetTickCount() - start < 1000);
    dispatcher.RunAsync(CoreDispatcherPriority::Normal, mark("a"));
    dispatcher.RunAsync(CoreDispatcherPriority::Normal, mark("b"));
    dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessOneIfPresent);
    {
        std::lock_guard<std::mutex> guard(lock);
        info("after ProcessOneIfPresent with a and b queued: [%s]", join(order).c_str());
        check("process_one_if_present_runs_the_pending_items", order == std::vector<std::string>{"a", "b"});
    }
    dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
    {
        std::lock_guard<std::mutex> guard(lock);
        check("process_all_if_present_after_that_runs_nothing", order.size() == 2);
        order.clear();
    }

    /* RunAsync from another thread runs on the view thread, and its Completed handler too */
    DWORD worker_id = 0, callback_thread = 0;
    bool completed_on_view_thread = false;
    Windows::Foundation::IAsyncAction from_worker{ nullptr };
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread worker([&] {
        worker_id = GetCurrentThreadId();
        from_worker = dispatcher.RunAsync(CoreDispatcherPriority::Normal, [&] { callback_thread = GetCurrentThreadId(); });
        from_worker.Completed([&](Windows::Foundation::IAsyncAction const &, Windows::Foundation::AsyncStatus) {
            completed_on_view_thread = GetCurrentThreadId() == view_thread;
        });
        SetEvent(ready);
    });
    WaitForSingleObject(ready, 10000);
    worker.join();
    CloseHandle(ready);
    dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
    check("run_async_from_other_thread_runs_on_view_thread", callback_thread == view_thread && callback_thread != worker_id);
    check("run_async_action_completes", from_worker && from_worker.Status() == Windows::Foundation::AsyncStatus::Completed);
    check("run_async_completed_handler_on_view_thread", completed_on_view_thread);

    /* ProcessOneAndAllPending with nothing queued: observed, not asserted (it returned at once here, and waited
     * for an event in another run) */
    {
        DWORD begin = GetTickCount();
        dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
        info("ProcessOneAndAllPending with nothing queued: %lu ms", (unsigned long)(GetTickCount() - begin));
    }

    /* items queued by a running item run in the same pass, the highest priority first */
    {
        std::lock_guard<std::mutex> guard(lock);
        order.clear();
    }
    dispatcher.RunAsync(CoreDispatcherPriority::Normal, [&, dispatcher] {
        { std::lock_guard<std::mutex> guard(lock); order.push_back("n1"); }
        dispatcher.RunAsync(CoreDispatcherPriority::High, mark("high-from-n1"));
        dispatcher.RunAsync(CoreDispatcherPriority::Normal, mark("normal-from-n1"));
    });
    dispatcher.RunAsync(CoreDispatcherPriority::Normal, mark("n2"));
    dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
    {
        std::lock_guard<std::mutex> guard(lock);
        info("queued by a running item, one ProcessAllIfPresent: [%s]", join(order).c_str());
        check("items_queued_by_a_running_item_run_by_priority",
              order == std::vector<std::string>{"n1", "high-from-n1", "n2", "normal-from-n1"});
        order.clear();
    }

    /* a canceled action does not run */
    bool canceled_ran = false;
    auto canceled = dispatcher.RunAsync(CoreDispatcherPriority::Normal, [&] { canceled_ran = true; });
    canceled.Cancel();
    dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
    check("canceled_action_does_not_run", !canceled_ran && canceled.Status() == Windows::Foundation::AsyncStatus::Canceled);

    /* the end: positive ends the application with CoreApplication.Exit from a dispatcher callback, return
     * returns from Run; a watchdog (in Run) reports a view that does not end */
    if (mode == "return") return;
    dispatcher.RunAsync(CoreDispatcherPriority::Normal, [] {
        try { CoreApplication::Exit(); record("Exit returned"); }
        catch (hresult_error const &e)
        {
            char text[64];
            std::snprintf(text, sizeof(text), "Exit threw 0x%08X", (unsigned)e.code());
            record(text);
        }
    });
    dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessUntilQuit);
    record("ProcessUntilQuit returned");
}

struct App : implements<App, IFrameworkViewSource, IFrameworkView>
{
    IFrameworkView CreateView()
    {
        record("CreateView");
        return *this;
    }

    void Initialize(CoreApplicationView const &view)
    {
        view_thread = GetCurrentThreadId();
        record("Initialize");
        check("callbacks_not_on_the_calling_thread", view_thread != main_thread);
        view.Activated([](CoreApplicationView const &, IActivatedEventArgs const &args) {
            record("ViewActivated");
            auto launch = args.try_as<ILaunchActivatedEventArgs>();
            check("launch_activation_kind", args.Kind() == ActivationKind::Launch);
            info("previous execution state: %d", (int)args.PreviousExecutionState());   /* 0 on a first launch, not stable on Windows */
            check("launch_arguments_are_the_activation_arguments", launch &&
                  to_string(launch.Arguments()).find(to_string(hstring(package_full_name))) != std::string::npos);
            check("launch_tile_id_is_the_application_id", launch && launch.TileId() == hstring(application_id));
            check("launch_has_a_splash_screen", args.SplashScreen() != nullptr);
            info("activation: arguments=\"%s\" tile=\"%s\"", launch ? to_string(launch.Arguments()).c_str() : "",
                 launch ? to_string(launch.TileId()).c_str() : "");
        });
        CoreApplication::Exiting([](IInspectable const &, IInspectable const &) {
            record("Exiting");
            /* in the return mode the process may end before this runs, so only Exit's Exiting is asserted */
            if (mode == "return") return;
            std::lock_guard<std::mutex> guard(lock);
            check("exiting_raised_on_the_calling_thread", GetCurrentThreadId() == main_thread);
            check("exiting_raised_before_the_window_closes", position(events, "Closed") < 0);
        });
        CoreApplication::Suspending([](IInspectable const &, Windows::ApplicationModel::SuspendingEventArgs const &) { record("Suspending"); });
        CoreApplication::Resuming([](IInspectable const &, IInspectable const &) { record("Resuming"); });
        check("view_is_main", view.IsMain());
        check("view_is_not_hosted", !view.IsHosted());
    }

    void SetWindow(CoreWindow const &window)
    {
        record("SetWindow");
        check("set_window_is_window_of_thread", window == CoreWindow::GetForCurrentThread());
        window.Activated([](CoreWindow const &, WindowActivatedEventArgs const &args) {
            record(args.WindowActivationState() == CoreWindowActivationState::CodeActivated ? "WindowActivated" :
                   args.WindowActivationState() == CoreWindowActivationState::PointerActivated ? "WindowActivated(pointer)" : "WindowDeactivated");
        });
        window.VisibilityChanged([](CoreWindow const &, VisibilityChangedEventArgs const &args) {
            record(args.Visible() ? "Visible=1" : "Visible=0");
        });
        window.Closed([](CoreWindow const &, CoreWindowEventArgs const &) {
            record("Closed");   /* after Exit the process may end at any time: observed, not asserted */
        });
        window.SizeChanged([](CoreWindow const &, WindowSizeChangedEventArgs const &args) {
            record("SizeChanged");
            info("SizeChanged: %.0fx%.0f", args.Size().Width, args.Size().Height);
        });
        if (mode == "present")
        {
            auto pointer = [](char const *name) {
                return [name](CoreWindow const &, PointerEventArgs const &args) {
                    auto point = args.CurrentPoint();
                    char text[160];
                    std::snprintf(text, sizeof(text), "%s id=%u pos=%.0f,%.0f device=%d left=%d wheel=%d mods=%u", name,
                                  point.PointerId(), point.Position().X, point.Position().Y,
                                  (int)point.PointerDevice().PointerDeviceType(), (int)point.Properties().IsLeftButtonPressed(),
                                  point.Properties().MouseWheelDelta(), (unsigned)args.KeyModifiers());
                    record(text);
                };
            };
            window.PointerEntered(pointer("PointerEntered"));
            window.PointerExited(pointer("PointerExited"));
            window.PointerMoved(pointer("PointerMoved"));
            window.PointerPressed(pointer("PointerPressed"));
            window.PointerReleased(pointer("PointerReleased"));
            window.PointerWheelChanged(pointer("PointerWheelChanged"));
            window.PointerCaptureLost(pointer("PointerCaptureLost"));
            auto key = [](char const *name) {
                return [name](CoreWindow const &, KeyEventArgs const &args) {
                    auto status = args.KeyStatus();
                    char text[160];
                    std::snprintf(text, sizeof(text), "%s vk=%d scan=%u extended=%d repeat=%u down=%d released=%d", name,
                                  (int)args.VirtualKey(), status.ScanCode, (int)status.IsExtendedKey, status.RepeatCount,
                                  (int)status.WasKeyDown, (int)status.IsKeyReleased);
                    record(text);
                };
            };
            window.KeyDown(key("KeyDown"));
            window.KeyUp(key("KeyUp"));
            window.CharacterReceived([](CoreWindow const &, CharacterReceivedEventArgs const &args) {
                char text[64];
                std::snprintf(text, sizeof(text), "CharacterReceived code=%u", args.KeyCode());
                record(text);
            });
            window.InputEnabled([](CoreWindow const &, InputEnabledEventArgs const &args) {
                record(args.InputEnabled() ? "InputEnabled=1" : "InputEnabled=0");
            });
        }
    }

    void Load(hstring const &entry_point)
    {
        record("Load");
        check("load_gets_the_manifest_entry_point", entry_point == L"view.App");
    }

    void Run()
    {
        record("Run");
        {
            std::lock_guard<std::mutex> guard(lock);
            check("launch_activation_before_run", count(events, "ViewActivated") == 1);
        }
        {
            std::lock_guard<std::mutex> guard(lock);
            int create = position(events, "CreateView"), init = position(events, "Initialize"),
                set = position(events, "SetWindow"), load = position(events, "Load"),
                activated = position(events, "ViewActivated"), run = position(events, "Run");
            check("lifecycle_order", create >= 0 && create < init && init < set && set < load && load < activated &&
                  activated < run);
            check("lifecycle_each_callback_once", count(events, "CreateView") == 1 && count(events, "Initialize") == 1 &&
                  count(events, "SetWindow") == 1 && count(events, "Load") == 1 && count(events, "Run") == 1);
        }
        check("view_current_is_main_view", CoreApplication::GetCurrentView() == CoreApplication::MainView());
        std::thread([] {
            Sleep(mode == "present" ? 40000 : 8000);
            put("TRACE WATCHDOG: the process was still running after Run");
            ExitProcess(3);
        }).detach();
        try
        {
            if (mode == "present") run_present(CoreWindow::GetForCurrentThread());
            else run_checks(CoreWindow::GetForCurrentThread());
        }
        catch (hresult_error const &e)
        {
            char text[64];
            std::snprintf(text, sizeof(text), "run_checks threw 0x%08X", (unsigned)e.code());
            record(text);
        }
        record("Run returning");
    }

    void Uninitialize()
    {
        record("Uninitialize");   /* after Run returns the process may end at any time: observed, not asserted */
    }
};

static std::wstring query_full_name()
{
    UINT32 length = 0;
    if (GetCurrentPackageFullName(&length, nullptr) != ERROR_INSUFFICIENT_BUFFER || !length) return {};
    std::vector<wchar_t> buffer(length);
    if (GetCurrentPackageFullName(&length, buffer.data()) != ERROR_SUCCESS) return {};
    return buffer.data();
}

int wmain(int argc, wchar_t **argv)
{
    std::wstring out;
    package_full_name = query_full_name();
    if (argc == 4 && wcscmp(argv[1], L"unpackaged") == 0)
    {
        mode = "unpackaged";
        out = argv[2];
    }
    else
    {
        UINT32 length = 0;
        if (GetCurrentApplicationUserModelId(&length, nullptr) != ERROR_INSUFFICIENT_BUFFER) return 2;
        std::vector<wchar_t> aumid(length);
        if (GetCurrentApplicationUserModelId(&length, aumid.data()) != ERROR_SUCCESS) return 2;
        std::wstring id = aumid.data();
        application_id = id.substr(id.find(L'!') + 1);
        mode = application_id == L"ViewSta" ? "sta" : application_id == L"ViewReturn" ? "return" :
               application_id == L"ViewPresent" ? "present" : application_id == L"View" ? "positive" : "";
        wchar_t dir[MAX_PATH];
        if (mode.empty() || !GetEnvironmentVariableW(L"LOCALAPPDATA", dir, ARRAYSIZE(dir))) return 2;
        out = std::wstring(dir) + L"\\view-" + std::wstring(mode.begin(), mode.end()) + L".txt";
    }
    if (!_wfreopen(out.c_str(), L"w", stdout)) return 3;
    setvbuf(stdout, nullptr, _IONBF, 0);
    main_thread = GetCurrentThreadId();
    start_tick = GetTickCount();

    init_apartment(mode == "sta" ? apartment_type::single_threaded : apartment_type::multi_threaded);
    if (mode == "unpackaged")
    {
        /* Windows terminates an unpackaged process that calls CoreApplication.Run (fast fail), so this control
         * is only run on Wine */
        check("no_package_identity", package_full_name.empty());
        HRESULT hr = S_OK;
        try { CoreApplication::Run(make<App>()); }
        catch (hresult_error const &e) { hr = e.code(); }
        put("HRESULT run_unpackaged " + hex(hr));
        check("run_unpackaged_fails", FAILED(hr));
        std::lock_guard<std::mutex> guard(lock);
        check("run_unpackaged_calls_nothing", events.empty());
        return failures ? 1 : 0;
    }

    check("package_identity", !package_full_name.empty());
    info("package full name: %s", to_string(hstring(package_full_name)).c_str());
    HRESULT hr = S_OK;
    try { CoreApplication::Run(make<App>()); }
    catch (hresult_error const &e) { hr = e.code(); }
    /* only reached when Run fails: in a single-threaded apartment, or if the view failed */
    info("CoreApplication.Run returned %s on thread %s", hex(hr).c_str(), thread_name());
    if (mode == "sta")
    {
        put("HRESULT run_sta " + hex(hr));
        check("run_in_sta_fails", FAILED(hr));
        std::lock_guard<std::mutex> guard(lock);
        check("run_in_sta_calls_nothing", events.empty());
    }
    else check("run_does_not_return_after_a_view", false);
    info("wmain returning %d", failures ? 1 : 0);
    return failures ? 1 : 0;
}
