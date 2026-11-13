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

static void pump(CoreDispatcher const &dispatcher, DWORD milliseconds)
{
    DWORD start = GetTickCount();
    do
    {
        dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
        Sleep(10);
    } while (GetTickCount() - start < milliseconds);
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
            Sleep(8000);
            put("TRACE WATCHDOG: the process was still running 8 s after Run");
            ExitProcess(3);
        }).detach();
        try { run_checks(CoreWindow::GetForCurrentThread()); }
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

static std::string hex(HRESULT hr)
{
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08lX", (unsigned long)hr);
    return text;
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
               application_id == L"View" ? "positive" : "";
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
