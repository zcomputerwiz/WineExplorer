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
 *   view.exe positive|unpackaged OUTPUT_FILE EXPECTED_PACKAGE_FULL_NAME
 *
 * "positive" is started by package activation. It calls CoreApplication.Run with an IFrameworkViewSource of its
 * own, drives the view from the callbacks (dispatcher priorities, ProcessEvents options, RunAsync from another
 * thread, cancellation, idle callbacks, window close) and checks what the documented contract promises:
 * PASS and FAIL lines (compared with expected.txt) and INFO lines (observations, not assertions).
 * "unpackaged" calls CoreApplication.Run in a process without a package and reports the result.
 */

#include <windows.h>
#include <appmodel.h>
#include <cstdarg>
#include <cstdio>
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
static DWORD main_thread, ui_thread;
static std::wstring expected_name;
static bool wrong_thread_callback;

static void check(char const *name, bool passed)
{
    std::printf("%s %s\n", passed ? "PASS" : "FAIL", name);
    if (!passed) ++failures;
}

static void info(char const *fmt, ...)
{
    va_list args;
    std::printf("INFO ");
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
    std::printf("\n");
}

static void record(std::string const &name)
{
    std::lock_guard<std::mutex> guard(lock);
    if (ui_thread && GetCurrentThreadId() != ui_thread) wrong_thread_callback = true;
    events.push_back(name);
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

static void run_checks(CoreWindow const &window)
{
    CoreDispatcher dispatcher = window.Dispatcher();
    bool in_order;

    check("dispatcher_access_on_view_thread", dispatcher.HasThreadAccess());

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
    info("ProcessEvents from another thread: 0x%08X", (unsigned)other_hr);

    window.Activate();
    /* what activation queued is raised while the thread processes events, not inside Activate */
    {
        std::lock_guard<std::mutex> guard(lock);
        check("activation_events_are_queued", position(events, "WindowActivated") < 0);
    }
    dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
    {
        std::lock_guard<std::mutex> guard(lock);
        check("launch_activation_raised_by_processing", count(events, "ViewActivated") == 1);
        check("window_activated_raised_by_processing", count(events, "WindowActivated") == 1);
        check("window_visible_raised_by_processing", position(events, "Visible=1") >= 0);
    }
    check("window_visible_after_activate", window.Visible());

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
        in_order = order == std::vector<std::string>{"high", "normal1", "normal2", "low", "idle(idle)"};
        std::string all;
        for (auto const &item : order) all += item + " ";
        info("dispatch order: %s", all.c_str());
    }
    check("dispatch_priority_order", in_order);
    order.clear();

    /* ProcessOneIfPresent with nothing queued returns at once and runs nothing; with two, it runs one */
    DWORD start = GetTickCount();
    dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessOneIfPresent);
    check("process_one_if_present_does_not_wait", GetTickCount() - start < 1000);
    dispatcher.RunAsync(CoreDispatcherPriority::Normal, mark("a"));
    dispatcher.RunAsync(CoreDispatcherPriority::Normal, mark("b"));
    dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessOneIfPresent);
    { std::lock_guard<std::mutex> guard(lock); check("process_one_if_present_runs_one", order.size() == 1 && order[0] == "a"); }
    dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
    { std::lock_guard<std::mutex> guard(lock); check("process_all_if_present_runs_the_rest", order.size() == 2 && order[1] == "b"); }
    order.clear();

    /* RunAsync from another thread runs on the view thread, and its Completed handler too */
    DWORD worker_id = 0, callback_thread = 0;
    bool completed_on_view_thread = false;
    Windows::Foundation::IAsyncAction from_worker{ nullptr };
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread worker([&] {
        worker_id = GetCurrentThreadId();
        from_worker = dispatcher.RunAsync(CoreDispatcherPriority::Normal, [&] { callback_thread = GetCurrentThreadId(); });
        from_worker.Completed([&](Windows::Foundation::IAsyncAction const &, Windows::Foundation::AsyncStatus) {
            completed_on_view_thread = GetCurrentThreadId() == ui_thread;
        });
        SetEvent(ready);
    });
    WaitForSingleObject(ready, 10000);
    worker.join();
    CloseHandle(ready);
    dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
    check("run_async_from_other_thread_runs_on_view_thread", callback_thread == ui_thread && callback_thread != worker_id);
    check("run_async_action_completes", from_worker && from_worker.Status() == Windows::Foundation::AsyncStatus::Completed);
    check("run_async_completed_handler_on_view_thread", completed_on_view_thread);

    /* ProcessOneAndAllPending waits for the next event when nothing is pending */
    bool late_ran = false;
    std::thread late([&] {
        Sleep(300);
        dispatcher.RunAsync(CoreDispatcherPriority::Normal, [&] { late_ran = true; });
    });
    start = GetTickCount();
    dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessOneAndAllPending);
    DWORD waited = GetTickCount() - start;
    late.join();
    check("process_one_and_all_pending_waits_for_an_event", late_ran && waited >= 150);
    info("ProcessOneAndAllPending waited %lu ms", (unsigned long)waited);

    /* a canceled action does not run */
    bool canceled_ran = false;
    auto canceled = dispatcher.RunAsync(CoreDispatcherPriority::Normal, [&] { canceled_ran = true; });
    canceled.Cancel();
    dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
    check("canceled_action_does_not_run", !canceled_ran && canceled.Status() == Windows::Foundation::AsyncStatus::Canceled);

    /* closing the window ends ProcessUntilQuit, after Closed was raised */
    dispatcher.RunAsync(CoreDispatcherPriority::Normal, [window] { window.Close(); });
    dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessUntilQuit);
    {
        std::lock_guard<std::mutex> guard(lock);
        check("closed_raised_before_process_until_quit_returns", count(events, "Closed") == 1);
    }
    check("window_not_visible_after_close", !window.Visible());
    record("RunReturning");
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
        ui_thread = GetCurrentThreadId();
        record("Initialize");
        view.Activated([](CoreApplicationView const &, IActivatedEventArgs const &args) {
            record("ViewActivated");
            auto launch = args.try_as<ILaunchActivatedEventArgs>();
            std::lock_guard<std::mutex> guard(lock);
            events.push_back(args.Kind() == ActivationKind::Launch ? "Kind=Launch" : "Kind=other");
            events.push_back(args.PreviousExecutionState() == ApplicationExecutionState::NotRunning ? "Previous=NotRunning" : "Previous=other");
            events.push_back(launch ? "Arguments=" + to_string(launch.Arguments()) : "Arguments=none");
        });
        check("view_is_main", view.IsMain());
        check("view_is_not_hosted", !view.IsHosted());
    }

    void SetWindow(CoreWindow const &window)
    {
        record("SetWindow");
        check("set_window_is_window_of_thread", window == CoreWindow::GetForCurrentThread());
        window.Activated([](CoreWindow const &, WindowActivatedEventArgs const &args) {
            record(args.WindowActivationState() == CoreWindowActivationState::CodeActivated ? "WindowActivated" : "WindowActivated(other)");
        });
        window.VisibilityChanged([](CoreWindow const &, VisibilityChangedEventArgs const &args) {
            record(args.Visible() ? "Visible=1" : "Visible=0");
        });
        window.Closed([](CoreWindow const &, CoreWindowEventArgs const &) { record("Closed"); });
    }

    void Load(hstring const &entry_point)
    {
        record("Load");
        info("Load entry point: \"%s\"", to_string(entry_point).c_str());
    }

    void Run()
    {
        record("Run");
        check("view_current_is_main_view", CoreApplication::GetCurrentView() == CoreApplication::MainView());
        run_checks(CoreWindow::GetForCurrentThread());
    }

    void Uninitialize()
    {
        record("Uninitialize");
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

static void report_lifecycle(HRESULT run_result)
{
    std::vector<std::string> copy;
    {
        std::lock_guard<std::mutex> guard(lock);
        copy = events;
    }
    std::string all;
    for (auto const &item : copy) all += item + " | ";
    info("events: %s", all.c_str());
    check("run_returned_success", run_result == S_OK);

    int create = position(copy, "CreateView"), init = position(copy, "Initialize"), set = position(copy, "SetWindow"),
        load = position(copy, "Load"), run = position(copy, "Run"), uninit = position(copy, "Uninitialize");
    bool once = count(copy, "CreateView") == 1 && count(copy, "Initialize") == 1 && count(copy, "SetWindow") == 1 &&
                count(copy, "Load") == 1 && count(copy, "Run") == 1 && count(copy, "Uninitialize") == 1;
    check("lifecycle_each_callback_once", once);
    check("lifecycle_order", create >= 0 && create < init && init < set && set < load && load < run && run < uninit);
    check("uninitialize_after_run_returned", position(copy, "RunReturning") >= 0 && position(copy, "RunReturning") < uninit);
    check("callbacks_on_one_thread", !wrong_thread_callback && ui_thread != 0);
    info("view thread is the thread that called Run: %d", (int)(ui_thread == main_thread));

    check("launch_activation_once", count(copy, "ViewActivated") == 1);
    check("launch_activation_kind", count(copy, "Kind=Launch") == 1 && count(copy, "Previous=NotRunning") == 1);
    bool arguments = false;
    for (auto const &item : copy)
        if (item.rfind("Arguments=", 0) == 0 && item.find(to_string(hstring(expected_name))) != std::string::npos) arguments = true;
    check("launch_arguments_are_the_activation_arguments", arguments);
    check("window_closed_once", count(copy, "Closed") == 1);
    check("closed_after_window_activated", position(copy, "WindowActivated") >= 0 &&
          position(copy, "WindowActivated") < position(copy, "Closed"));

    /* the thread has no window any more */
    check("no_window_after_run", CoreWindow::GetForCurrentThread() == nullptr);
}

int wmain(int argc, wchar_t **argv)
{
    if (argc != 4 || !_wfreopen(argv[2], L"w", stdout)) return 2;
    bool packaged = wcscmp(argv[1], L"positive") == 0;
    main_thread = GetCurrentThreadId();
    expected_name = argv[3];

    init_apartment(apartment_type::single_threaded);
    if (packaged)
    {
        check("package_identity", query_full_name() == expected_name);
        HRESULT hr = S_OK;
        try { CoreApplication::Run(make<App>()); }
        catch (hresult_error const &e) { hr = e.code(); }
        info("CoreApplication.Run: 0x%08X", (unsigned)hr);
        report_lifecycle(hr);
    }
    else
    {
        check("no_package_identity", query_full_name().empty());
        HRESULT hr = S_OK;
        try { CoreApplication::Run(make<App>()); }
        catch (hresult_error const &e) { hr = e.code(); }
        std::printf("HRESULT run_unpackaged 0x%08lX\n", static_cast<unsigned long>(hr));
        check("run_unpackaged_fails", FAILED(hr));
        std::lock_guard<std::mutex> guard(lock);
        check("run_unpackaged_calls_nothing", events.empty());
    }
    return failures ? 1 : 0;
}
