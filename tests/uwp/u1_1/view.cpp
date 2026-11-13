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
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdlib>
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
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Foundation.Collections.h>
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
static bool presenting() { return mode == "present" || mode == "suspend"; }

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

/* the index of the first recorded event that starts with prefix, at or after start; -1 when none */
static int find_event(std::string const &prefix, int start = 0)
{
    for (size_t i = start < 0 ? 0 : start; i < events.size(); ++i)
        if (events[i].compare(0, prefix.size(), prefix) == 0) return (int)i;
    return -1;
}

/* a key press as Windows 11 raised it (tests/uwp/u1_1/README.md): KeyDown, then for a character key
 * CharacterReceived, then KeyUp; the checks are on the recorded events, so they hold for a run that was typed by hand
 * and for one with injected input */
static void check_key(char const *name, int vk, int scan, int extended, int character)
{
    char down[96], up[96], chr[48];
    std::snprintf(down, sizeof(down), "KeyDown vk=%d scan=%d extended=%d repeat=1 down=0 released=0", vk, scan, extended);
    std::snprintf(up, sizeof(up), "KeyUp vk=%d scan=%d extended=%d repeat=1 down=1 released=1", vk, scan, extended);
    std::snprintf(chr, sizeof(chr), "CharacterReceived code=%d", character);
    std::lock_guard<std::mutex> guard(lock);
    int d = find_event(down), c = character ? find_event(chr, d) : -1, u = find_event(up, d);
    bool ok = d >= 0 && u > d && (character ? (c > d && c < u) : true);
    if (!character && d >= 0 && u > d)
        for (size_t i = d; i < (size_t)u; ++i)
            if (events[i].compare(0, 17, "CharacterReceived") == 0) ok = false;   /* no character for a function key */
    check(name, ok);
}

static void present_checks()
{
    {
        std::lock_guard<std::mutex> guard(lock);
        int visible = find_event("Visible=1"), activated = find_event("WindowActivated"), input = find_event("InputEnabled=1");
        check("activate_raises_visible_activated_inputenabled_in_order",
              visible >= 0 && activated > visible && input > activated);
        check("no_size_changed_without_resizing", find_event("SizeChanged") < 0);
        int entered = find_event("PointerEntered");
        check("pointer_entered_before_first_move", entered >= 0 && entered < find_event("PointerMoved"));
        int pressed = find_event("PointerPressed id=1");
        int left = -1;
        for (size_t i = 0; i < events.size(); ++i)
            if (events[i].compare(0, 14, "PointerPressed") == 0 && events[i].find("left=1") != std::string::npos) { left = (int)i; break; }
        int released = left < 0 ? -1 : find_event("PointerReleased", left);
        check("left_button_press_and_release", left >= 0 && released > left && events[released].find("left=0") != std::string::npos);
        check("pointer_is_a_mouse_with_id_1", pressed >= 0 && events[pressed].find("device=2") != std::string::npos);
        bool right = false;
        for (auto const &e : events)
            if (e.compare(0, 14, "PointerPressed") == 0 && e.find("left=0") != std::string::npos) right = true;
        check("right_button_press", right);
        bool wheel = false;
        for (auto const &e : events)
            if (e.compare(0, 19, "PointerWheelChanged") == 0 && (e.find("wheel=120") != std::string::npos || e.find("wheel=-120") != std::string::npos)) wheel = true;
        check("wheel_notch_is_120", wheel);
    }
    check_key("key_a", 65, 30, 0, 97);
    check_key("key_shift_b", 66, 48, 0, 66);
    check_key("key_enter", 13, 28, 0, 13);
    check_key("key_left_is_extended_without_character", 37, 75, 1, 0);
    check_key("key_f5_without_character", 116, 63, 0, 0);
}

template <typename F> static std::wstring query(F call)
{
    UINT32 size = 0;
    if (call(&size, nullptr) != ERROR_INSUFFICIENT_BUFFER || !size) return {};
    std::vector<wchar_t> buffer(size);
    if (call(&size, buffer.data()) != ERROR_SUCCESS) return {};
    return buffer.data();
}

/* pkgapi mode: what the package APIs return for this process, recorded as INFO lines (no window, no Run; unattended) */
static std::string wide_to_utf8(wchar_t const *text)
{
    return text ? to_string(hstring(text)) : std::string("(null)");
}

/* pkgapi mode: the checks, from what Windows 11 returned (INFO lines of run_pkgapi, tests/uwp/u1_1/README.md) */
static std::vector<BYTE> package_info_bytes(UINT32 flags, UINT32 &count, LONG &rc_size, LONG &rc)
{
    UINT32 length = 0;
    count = 99;
    rc_size = GetCurrentPackageInfo(flags, &length, nullptr, &count);
    std::vector<BYTE> buffer(length ? length : 1);
    UINT32 got = length;
    rc = rc_size == ERROR_INSUFFICIENT_BUFFER ? GetCurrentPackageInfo(flags, &got, buffer.data(), &count) : rc_size;
    buffer.resize(got);
    return buffer;
}

static void pkgapi_checks()
{
    auto full = query([](UINT32 *n, PWSTR p) { return GetCurrentPackageFullName(n, p); });
    auto family = query([](UINT32 *n, PWSTR p) { return GetCurrentPackageFamilyName(n, p); });
    auto path = query([](UINT32 *n, PWSTR p) { return GetCurrentPackagePath(n, p); });
    UINT32 count = 0;
    LONG rc_size = 0, rc = 0;

    /* GetCurrentPackageInfo: no filter is the head of the graph; DIRECT alone is the (empty) dependencies */
    auto none = package_info_bytes(0, count, rc_size, rc);
    check("package_info_size_query_is_insufficient_buffer", rc_size == ERROR_INSUFFICIENT_BUFFER);
    check("package_info_unfiltered_is_the_head", rc == ERROR_SUCCESS && count == 1);
    if (rc == ERROR_SUCCESS && count == 1)
    {
        auto *info = reinterpret_cast<PACKAGE_INFO *>(none.data());
        check("package_info_names", full == info->packageFullName && family == info->packageFamilyName && path == info->path);
        auto const &id = info->packageId;
        check("package_info_id", std::wstring(id.name) == L"WineExplorer.U1View" && id.processorArchitecture != 0xffff &&
              id.version.Major == 1 && id.version.Minor == 0 && id.version.Build == 0 && id.version.Revision == 0 &&
              std::wstring(id.publisherId).size() == 13 && family.find(id.publisherId) != std::wstring::npos &&
              id.resourceId == nullptr);
        check("package_info_publisher_is_the_manifest_publisher", id.publisher && std::wstring(id.publisher) == L"CN=WineExplorer U1 Oracle");
        check("package_info_flags", info->flags == 0x10);
    }
    UINT32 head_count = 0, direct_count = 0, both_count = 0;
    LONG rc_a = 0, rc_b = 0;
    auto head = package_info_bytes(PACKAGE_FILTER_HEAD, head_count, rc_a, rc_b);
    check("package_info_head", rc_b == ERROR_SUCCESS && head_count == 1 && head.size() == none.size());
    auto direct = package_info_bytes(PACKAGE_FILTER_DIRECT, direct_count, rc_a, rc_b);
    check("package_info_direct_alone_is_empty_success", rc_a == ERROR_SUCCESS && direct_count == 0 && direct.size() <= 1);
    package_info_bytes(PACKAGE_FILTER_HEAD | PACKAGE_FILTER_DIRECT, both_count, rc_a, rc_b);
    check("package_info_head_and_direct", rc_b == ERROR_SUCCESS && both_count == 1);
    package_info_bytes(0x80000000, count, rc_a, rc_b);
    check("package_info_unknown_flags_are_ignored", rc_a == ERROR_INSUFFICIENT_BUFFER);
    UINT32 length = 0;
    check("package_info_null_length_is_invalid", GetCurrentPackageInfo(PACKAGE_FILTER_HEAD, nullptr, nullptr, &count) == ERROR_INVALID_PARAMETER);
    check("package_info_size_query_without_count", GetCurrentPackageInfo(PACKAGE_FILTER_HEAD, &length, nullptr, nullptr) == ERROR_INSUFFICIENT_BUFFER);
    check("package_name_null_length", GetCurrentPackageFullName(nullptr, nullptr) == ERROR_INVALID_PARAMETER);
    length = 1;
    check("package_name_length_without_buffer_is_invalid", GetCurrentPackageFullName(&length, nullptr) == ERROR_INVALID_PARAMETER);

    /* through a package info reference */
    PACKAGE_INFO_REFERENCE reference = nullptr;
    check("open_package_info", OpenPackageInfoByFullName(full.c_str(), 0, &reference) == ERROR_SUCCESS && reference);
    if (reference)
    {
        length = 0; count = 0;
        LONG r = GetPackageApplicationIds(reference, &length, nullptr, &count);
        std::vector<BYTE> buffer(length ? length : 1);
        if (r == ERROR_INSUFFICIENT_BUFFER) r = GetPackageApplicationIds(reference, &length, buffer.data(), &count);
        bool ids_ok = r == ERROR_SUCCESS && count == 7;
        std::vector<std::wstring> seen;
        for (UINT32 i = 0; ids_ok && i < count; ++i) seen.push_back(reinterpret_cast<PCWSTR *>(buffer.data())[i]);
        for (wchar_t const *app : { L"View", L"ViewSta", L"ViewReturn", L"ViewPresent", L"ViewSuspend", L"ViewPkgapi", L"ViewAppdata" })
            if (std::find(seen.begin(), seen.end(), family + L"!" + app) == seen.end()) ids_ok = false;
        check("application_ids_are_all_applications_of_the_package", ids_ok);
        length = 0; count = 0;
        check("package_info_by_reference_size_query", GetPackageInfo(reference, PACKAGE_FILTER_HEAD, &length, nullptr, &count) == ERROR_INSUFFICIENT_BUFFER && count == 1);
        check("close_package_info", ClosePackageInfo(reference) == ERROR_SUCCESS);
    }
    wchar_t const *unknown_full = L"Nope.Package_1.0.0.0_x64__0123456789abc";
    check("open_unknown_package_is_not_found", OpenPackageInfoByFullName(unknown_full, 0, &reference) == ERROR_NOT_FOUND);
    check("open_garbage_is_invalid", OpenPackageInfoByFullName(L"garbage", 0, &reference) == ERROR_INVALID_PARAMETER);
    check("open_null_is_invalid", OpenPackageInfoByFullName(nullptr, 0, &reference) == ERROR_INVALID_PARAMETER);

    /* identity conversions */
    length = 0;
    PackageIdFromFullName(full.c_str(), PACKAGE_INFORMATION_BASIC, &length, nullptr);
    std::vector<BYTE> id_buffer(length ? length : 1);
    LONG r = PackageIdFromFullName(full.c_str(), PACKAGE_INFORMATION_BASIC, &length, id_buffer.data());
    check("package_id_from_full_name", r == ERROR_SUCCESS);
    if (r == ERROR_SUCCESS)
    {
        auto *id = reinterpret_cast<PACKAGE_ID *>(id_buffer.data());
        auto name_from_id = query([&](UINT32 *n, PWSTR p) { return PackageFullNameFromId(id, n, p); });
        auto family_from_id = query([&](UINT32 *n, PWSTR p) { return PackageFamilyNameFromId(id, n, p); });
        check("full_name_from_id_round_trip", name_from_id == full);
        check("family_name_from_id", family_from_id == family);
        UINT32 name_len = 0, pub_len = 0;
        r = PackageNameAndPublisherIdFromFamilyName(family.c_str(), &name_len, nullptr, &pub_len, nullptr);
        std::vector<wchar_t> pname(name_len ? name_len : 1), ppub(pub_len ? pub_len : 1);
        bool split_ok = r == ERROR_INSUFFICIENT_BUFFER &&
            PackageNameAndPublisherIdFromFamilyName(family.c_str(), &name_len, pname.data(), &pub_len, ppub.data()) == ERROR_SUCCESS &&
            std::wstring(pname.data()) == L"WineExplorer.U1View" && std::wstring(ppub.data()) == id->publisherId;
        check("name_and_publisher_id_from_family_name", split_ok);
    }
    length = 0;
    check("package_id_from_garbage_is_invalid", PackageIdFromFullName(L"garbage", 0, &length, nullptr) == ERROR_INVALID_PARAMETER);
    UINT32 n = 0;
    check("family_name_from_null_id_is_invalid", PackageFamilyNameFromId(nullptr, &n, nullptr) == ERROR_INVALID_PARAMETER);
    check("parse_aumid_garbage_is_invalid", ParseApplicationUserModelId(L"garbage", &n, nullptr, &n, nullptr) == ERROR_INVALID_PARAMETER);
    check("family_split_garbage_is_invalid", PackageNameAndPublisherIdFromFamilyName(L"garbage", &n, nullptr, &n, nullptr) == ERROR_INVALID_PARAMETER);
    UINT32 pn = 0, pp = 0;
    check("family_split_of_an_uninstalled_family", PackageNameAndPublisherIdFromFamilyName(L"Nope.Package_0123456789abc", &pn, nullptr, &pp, nullptr) == ERROR_INSUFFICIENT_BUFFER);

    /* the user model id */
    auto aumid = query([](UINT32 *k, PWSTR p) { return GetCurrentApplicationUserModelId(k, p); });
    UINT32 fl = 0, al = 0;
    r = ParseApplicationUserModelId(aumid.c_str(), &fl, nullptr, &al, nullptr);
    std::vector<wchar_t> pf(fl ? fl : 1), pa(al ? al : 1);
    bool parse_ok = r == ERROR_INSUFFICIENT_BUFFER &&
        ParseApplicationUserModelId(aumid.c_str(), &fl, pf.data(), &al, pa.data()) == ERROR_SUCCESS &&
        std::wstring(pf.data()) == family && std::wstring(pa.data()) == L"ViewPkgapi";
    check("parse_application_user_model_id", parse_ok);
    auto aumid_of_process = query([](UINT32 *k, PWSTR p) { return GetApplicationUserModelId(GetCurrentProcess(), k, p); });
    check("application_user_model_id_of_the_process", aumid_of_process == aumid);

    /* paths and families */
    auto by_name = query([&](UINT32 *k, PWSTR p) { return GetPackagePathByFullName(full.c_str(), k, p); });
    auto staged = query([&](UINT32 *k, PWSTR p) { return GetStagedPackagePathByFullName(full.c_str(), k, p); });
    check("package_path_by_full_name", by_name == path && staged == path);
    length = 0;
    check("package_path_of_unknown_is_not_found", GetPackagePathByFullName(unknown_full, &length, nullptr) == ERROR_NOT_FOUND);
    UINT32 packages = 0, buffer_len = 0;
    r = GetPackagesByPackageFamily(family.c_str(), &packages, nullptr, &buffer_len, nullptr);
    std::vector<PWSTR> names(packages ? packages : 1);
    std::vector<wchar_t> text(buffer_len ? buffer_len : 1);
    bool family_ok = r == ERROR_INSUFFICIENT_BUFFER && packages == 1 &&
        GetPackagesByPackageFamily(family.c_str(), &packages, names.data(), &buffer_len, text.data()) == ERROR_SUCCESS &&
        packages == 1 && std::wstring(names[0]) == full;
    check("packages_by_package_family", family_ok);
    packages = 0; buffer_len = 0;
    r = FindPackagesByPackageFamily(family.c_str(), PACKAGE_FILTER_HEAD | PACKAGE_INFORMATION_BASIC, &packages, nullptr, &buffer_len, nullptr, nullptr);
    check("find_packages_by_package_family", r == ERROR_INSUFFICIENT_BUFFER && packages == 1);
    packages = 0; buffer_len = 0;
    r = GetPackagesByPackageFamily(L"Nope.Package_0123456789abc", &packages, nullptr, &buffer_len, nullptr);
    check("packages_of_an_uninstalled_family_is_empty", r == ERROR_SUCCESS && packages == 0 && buffer_len == 0);
    check("packages_of_garbage_family_is_invalid", GetPackagesByPackageFamily(L"garbage", &packages, nullptr, &buffer_len, nullptr) == ERROR_INVALID_PARAMETER);
}

/* appdata mode: Windows.Storage.ApplicationData of this package (unattended; run twice in one installation: the second
 * run sees what the first one stored). The checks are from what Windows 11 did (tests/uwp/u1_1/README.md). */
static std::wstring env_value(wchar_t const *name)
{
    wchar_t buffer[MAX_PATH * 2] = {};
    GetEnvironmentVariableW(name, buffer, ARRAYSIZE(buffer));
    return buffer;
}

static bool exists_path(std::wstring const &path)
{
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static void run_appdata()
{
    using namespace Windows::Storage;
    using namespace Windows::Foundation;
    auto family = query([](UINT32 *n, PWSTR p) { return GetCurrentPackageFamilyName(n, p); });
    auto data = ApplicationData::Current();
    std::wstring local_path(data.LocalFolder().Path()), temp_path(data.TemporaryFolder().Path()),
                 roaming_path(data.RoamingFolder().Path()), cache_path(data.LocalCacheFolder().Path());
    info("LocalFolder %s", to_string(hstring(local_path)).c_str());
    info("TemporaryFolder %s", to_string(hstring(temp_path)).c_str());
    info("RoamingFolder %s", to_string(hstring(roaming_path)).c_str());
    info("LocalCacheFolder %s", to_string(hstring(cache_path)).c_str());
    info("Version %u, RoamingStorageQuota %llu", data.Version(), (unsigned long long)data.RoamingStorageQuota());

    /* the folders are in the package's own directory, named by the family; they exist */
    auto suffix = [&](wchar_t const *leaf) { return std::wstring(L"\\Packages\\") + family + L"\\" + leaf; };
    auto ends_with = [](std::wstring const &s, std::wstring const &e) {
        return s.size() >= e.size() && _wcsicmp(s.c_str() + s.size() - e.size(), e.c_str()) == 0; };
    check("local_folder_is_LocalState_of_the_family", ends_with(local_path, suffix(L"LocalState")));
    check("temporary_folder_is_TempState_of_the_family", ends_with(temp_path, suffix(L"TempState")));
    check("roaming_folder_is_RoamingState_of_the_family", ends_with(roaming_path, suffix(L"RoamingState")));
    check("local_cache_folder_is_LocalCache_of_the_family", ends_with(cache_path, suffix(L"LocalCache")));
    check("the_folders_exist", exists_path(local_path) && exists_path(temp_path) && exists_path(roaming_path) && exists_path(cache_path));
    check("local_folder_name", data.LocalFolder().Name() == L"LocalState");
    check("current_is_a_singleton", ApplicationData::Current() == data);
    check("shared_local_folder_not_available", [&] { try { return data.SharedLocalFolder() == nullptr; } catch (hresult_error const &) { return true; } }());

    /* a file written through the folder is there for Win32 too, and survives into the second run */
    int runs = 0;
    {
        auto counter = local_path + L"\\runs.txt";
        FILE *f = _wfopen(counter.c_str(), L"r");
        if (f) { std::fscanf(f, "%d", &runs); std::fclose(f); }
        f = _wfopen(counter.c_str(), L"w");
        if (f) { std::fprintf(f, "%d", runs + 1); std::fclose(f); }
    }
    info("this is run %d", runs + 1);
    {
        /* files are made with Win32 here: StorageFile and FileIO are other APIs (not covered by this mode) */
        auto hello = local_path + L"\\hello.txt";
        FILE *f = _wfopen(hello.c_str(), L"w");
        if (f) { std::fputs("hello from the package", f); std::fclose(f); }
        check("file_created_in_local_folder_is_visible_to_win32", exists_path(hello));
    }

    /* settings: typed values, containers, persistence */
    auto settings = data.LocalSettings();
    check("settings_locality_is_local", settings.Locality() == ApplicationDataLocality::Local);
    check("roaming_settings_locality_is_roaming", data.RoamingSettings().Locality() == ApplicationDataLocality::Roaming);
    auto values = settings.Values();
    if (runs == 0)
    {
        values.Insert(L"int", Windows::Foundation::PropertyValue::CreateInt32(-7));
        values.Insert(L"text", Windows::Foundation::PropertyValue::CreateString(L"value"));
        values.Insert(L"flag", Windows::Foundation::PropertyValue::CreateBoolean(true));
        values.Insert(L"real", Windows::Foundation::PropertyValue::CreateDouble(2.5));
        auto container = settings.CreateContainer(L"Sub", ApplicationDataCreateDisposition::Always);
        container.Values().Insert(L"inner", Windows::Foundation::PropertyValue::CreateString(L"nested"));
        Windows::Storage::ApplicationDataCompositeValue composite;
        composite.Insert(L"a", Windows::Foundation::PropertyValue::CreateInt32(1));
        composite.Insert(L"b", Windows::Foundation::PropertyValue::CreateString(L"two"));
        values.Insert(L"composite", composite);
    }
    check("settings_int", unbox_value_or<int32_t>(values.Lookup(L"int"), 0) == -7);
    check("settings_string", unbox_value_or<hstring>(values.Lookup(L"text"), L"") == L"value");
    check("settings_bool", unbox_value_or<bool>(values.Lookup(L"flag"), false));
    check("settings_double", unbox_value_or<double>(values.Lookup(L"real"), 0.0) == 2.5);
    check("settings_size_is_five", values.Size() == 5);
    check("settings_container_exists", settings.Containers().HasKey(L"Sub") &&
          unbox_value_or<hstring>(settings.Containers().Lookup(L"Sub").Values().Lookup(L"inner"), L"") == L"nested");
    auto composite = values.Lookup(L"composite").try_as<ApplicationDataCompositeValue>();
    check("settings_composite", composite && unbox_value_or<int32_t>(composite.Lookup(L"a"), 0) == 1 &&
          unbox_value_or<hstring>(composite.Lookup(L"b"), L"") == L"two");
    check("settings_missing_key_is_not_found", !values.HasKey(L"nothing") && values.TryLookup(L"nothing") == nullptr);
    check("settings_persist_between_runs", runs == 0 || values.HasKey(L"int"));
    check("roaming_settings_are_separate", !data.RoamingSettings().Values().HasKey(L"int"));
    check("local_cache_folder_is_separate", exists_path(cache_path) && !exists_path(cache_path + L"\\hello.txt"));
    check("data_changed_can_be_signalled", [&] { try { data.SignalDataChanged(); return true; } catch (hresult_error const &) { return false; } }());

    /* ClearAsync(Local) removes the local files and settings of the package */
    if (runs >= 1)
    {
        data.ClearAsync(ApplicationDataLocality::Local).get();
        check("clear_removes_local_files", !exists_path(local_path + L"\\hello.txt") && exists_path(local_path));
        check("clear_removes_local_settings", data.LocalSettings().Values().Size() == 0);
    }
    put("READY");
    put("Uninitialize");
}

static void run_pkgapi()
{
    auto show = [](char const *name, LONG rc) { info("%s -> %ld", name, (long)rc); };

    UINT32 count = 0, length = 0;
    struct { UINT32 flags; char const *name; } variants[] = {
        { 0, "none" }, { PACKAGE_FILTER_HEAD, "HEAD" }, { PACKAGE_FILTER_DIRECT, "DIRECT" },
        { PACKAGE_FILTER_HEAD | PACKAGE_FILTER_DIRECT, "HEAD|DIRECT" }, { PACKAGE_FILTER_HEAD | PACKAGE_INFORMATION_FULL, "HEAD|FULL" },
        { PACKAGE_FILTER_HEAD | PACKAGE_FILTER_DIRECT | PACKAGE_FILTER_RESOURCE | PACKAGE_FILTER_BUNDLE, "all filters" } };
    for (auto const &v : variants)
    {
        length = 0; count = 99;
        LONG rc = GetCurrentPackageInfo(v.flags, &length, nullptr, &count);
        info("GetCurrentPackageInfo(%s) size query -> %ld, length %u, count %u", v.name, (long)rc, length, count);
        if (rc != ERROR_INSUFFICIENT_BUFFER) continue;
        std::vector<BYTE> buffer(length);
        UINT32 got = length;
        rc = GetCurrentPackageInfo(v.flags, &got, buffer.data(), &count);
        info("GetCurrentPackageInfo(%s) -> %ld, length %u, count %u", v.name, (long)rc, got, count);
        if (rc != ERROR_SUCCESS) continue;
        auto *infos = reinterpret_cast<PACKAGE_INFO *>(buffer.data());
        for (UINT32 i = 0; i < count; ++i)
        {
            info("  [%u] flags %#x path %s", i, infos[i].flags, wide_to_utf8(infos[i].path).c_str());
            info("  [%u] full %s family %s", i, wide_to_utf8(infos[i].packageFullName).c_str(), wide_to_utf8(infos[i].packageFamilyName).c_str());
            auto const &id = infos[i].packageId;
            info("  [%u] id name %s publisher %s publisherId %s arch %u version %u.%u.%u.%u resource %s", i, wide_to_utf8(id.name).c_str(),
                 wide_to_utf8(id.publisher).c_str(), wide_to_utf8(id.publisherId).c_str(), id.processorArchitecture, id.version.Major,
                 id.version.Minor, id.version.Build, id.version.Revision, wide_to_utf8(id.resourceId).c_str());
        }
    }

    /* the same through the package info reference */
    auto full = query([](UINT32 *n, PWSTR p) { return GetCurrentPackageFullName(n, p); });
    PACKAGE_INFO_REFERENCE reference = nullptr;
    LONG rc = OpenPackageInfoByFullName(full.c_str(), 0, &reference);
    show("OpenPackageInfoByFullName", rc);
    if (rc == ERROR_SUCCESS)
    {
        length = 0; count = 99;
        rc = GetPackageInfo(reference, PACKAGE_FILTER_HEAD, &length, nullptr, &count);
        info("GetPackageInfo(HEAD) size query -> %ld, length %u, count %u", (long)rc, length, count);
        length = 0; count = 99;
        rc = GetPackageApplicationIds(reference, &length, nullptr, &count);
        info("GetPackageApplicationIds size query -> %ld, length %u, count %u", (long)rc, length, count);
        if (rc == ERROR_INSUFFICIENT_BUFFER)
        {
            std::vector<BYTE> buffer(length);
            rc = GetPackageApplicationIds(reference, &length, buffer.data(), &count);
            info("GetPackageApplicationIds -> %ld, count %u", (long)rc, count);
            auto *ids = reinterpret_cast<PCWSTR *>(buffer.data());
            for (UINT32 i = 0; rc == ERROR_SUCCESS && i < count; ++i) info("  application id %s", wide_to_utf8(ids[i]).c_str());
        }
        show("ClosePackageInfo", ClosePackageInfo(reference));
    }

    /* identity conversions */
    length = 0;
    rc = PackageIdFromFullName(full.c_str(), PACKAGE_INFORMATION_BASIC, &length, nullptr);
    info("PackageIdFromFullName(basic) size query -> %ld, length %u", (long)rc, length);
    std::vector<BYTE> id_buffer(length);
    rc = PackageIdFromFullName(full.c_str(), PACKAGE_INFORMATION_BASIC, &length, id_buffer.data());
    info("PackageIdFromFullName(basic) -> %ld", (long)rc);
    if (rc == ERROR_SUCCESS)
    {
        auto *id = reinterpret_cast<PACKAGE_ID *>(id_buffer.data());
        UINT32 n = 0;
        rc = PackageFullNameFromId(id, &n, nullptr);
        info("PackageFullNameFromId size query -> %ld, length %u", (long)rc, n);
        std::vector<wchar_t> text(n ? n : 1);
        rc = PackageFullNameFromId(id, &n, text.data());
        info("PackageFullNameFromId -> %ld, %s", (long)rc, wide_to_utf8(text.data()).c_str());
        n = 0;
        rc = PackageFamilyNameFromId(id, &n, nullptr);
        info("PackageFamilyNameFromId size query -> %ld, length %u", (long)rc, n);
        std::vector<wchar_t> family(n ? n : 1);
        rc = PackageFamilyNameFromId(id, &n, family.data());
        info("PackageFamilyNameFromId -> %ld, %s", (long)rc, wide_to_utf8(family.data()).c_str());
        UINT32 name_len = 0, publisher_len = 0;
        rc = PackageNameAndPublisherIdFromFamilyName(family.data(), &name_len, nullptr, &publisher_len, nullptr);
        info("PackageNameAndPublisherIdFromFamilyName size query -> %ld, %u, %u", (long)rc, name_len, publisher_len);
        std::vector<wchar_t> pname(name_len ? name_len : 1), ppub(publisher_len ? publisher_len : 1);
        rc = PackageNameAndPublisherIdFromFamilyName(family.data(), &name_len, pname.data(), &publisher_len, ppub.data());
        info("PackageNameAndPublisherIdFromFamilyName -> %ld, %s / %s", (long)rc, wide_to_utf8(pname.data()).c_str(), wide_to_utf8(ppub.data()).c_str());

        UINT32 packages = 0, buffer_len = 0;
        rc = GetPackagesByPackageFamily(family.data(), &packages, nullptr, &buffer_len, nullptr);
        info("GetPackagesByPackageFamily size query -> %ld, packages %u, buffer %u", (long)rc, packages, buffer_len);
        packages = 0; buffer_len = 0;
        rc = FindPackagesByPackageFamily(family.data(), PACKAGE_FILTER_HEAD | PACKAGE_INFORMATION_BASIC, &packages, nullptr, &buffer_len, nullptr, nullptr);
        info("FindPackagesByPackageFamily size query -> %ld, packages %u, buffer %u", (long)rc, packages, buffer_len);
    }

    /* paths */
    length = 0;
    rc = GetPackagePathByFullName(full.c_str(), &length, nullptr);
    info("GetPackagePathByFullName size query -> %ld, length %u", (long)rc, length);
    if (rc == ERROR_INSUFFICIENT_BUFFER)
    {
        std::vector<wchar_t> path(length);
        rc = GetPackagePathByFullName(full.c_str(), &length, path.data());
        info("GetPackagePathByFullName -> %ld, %s", (long)rc, wide_to_utf8(path.data()).c_str());
    }
    length = 0;
    rc = GetStagedPackagePathByFullName(full.c_str(), &length, nullptr);
    info("GetStagedPackagePathByFullName size query -> %ld, length %u", (long)rc, length);
    length = 0;
    rc = GetCurrentApplicationUserModelId(&length, nullptr);
    info("GetCurrentApplicationUserModelId size query -> %ld, length %u", (long)rc, length);
    if (rc == ERROR_INSUFFICIENT_BUFFER)
    {
        std::vector<wchar_t> aumid(length);
        rc = GetCurrentApplicationUserModelId(&length, aumid.data());
        info("GetCurrentApplicationUserModelId -> %ld, %s", (long)rc, wide_to_utf8(aumid.data()).c_str());
        UINT32 f = 0, a = 0;
        rc = ParseApplicationUserModelId(aumid.data(), &f, nullptr, &a, nullptr);
        info("ParseApplicationUserModelId size query -> %ld, %u, %u", (long)rc, f, a);
    }
    length = 0;
    rc = GetApplicationUserModelId(GetCurrentProcess(), &length, nullptr);
    info("GetApplicationUserModelId(process) size query -> %ld, length %u", (long)rc, length);
    length = 0;
    rc = GetPackageFamilyNameFromToken(nullptr, &length, nullptr);
    info("GetPackageFamilyNameFromToken(null) -> %ld", (long)rc);
    length = 0;
    rc = GetPackageFullName(nullptr, &length, nullptr);
    info("GetPackageFullName(null) -> %ld", (long)rc);
    length = 0;
    rc = GetCurrentPackageFullName(nullptr, nullptr);
    info("GetCurrentPackageFullName(null,null) -> %ld", (long)rc);
    /* names that are not installed, and invalid arguments */
    {
        wchar_t const *unknown_full = L"Nope.Package_1.0.0.0_x64__0123456789abc";
        wchar_t const *unknown_family = L"Nope.Package_0123456789abc";
        PACKAGE_INFO_REFERENCE ref = nullptr;
        info("OpenPackageInfoByFullName(unknown) -> %ld", (long)OpenPackageInfoByFullName(unknown_full, 0, &ref));
        info("OpenPackageInfoByFullName(garbage) -> %ld", (long)OpenPackageInfoByFullName(L"garbage", 0, &ref));
        info("OpenPackageInfoByFullName(null) -> %ld", (long)OpenPackageInfoByFullName(nullptr, 0, &ref));
        length = 0;
        info("GetPackagePathByFullName(unknown) -> %ld", (long)GetPackagePathByFullName(unknown_full, &length, nullptr));
        info("  length %u", length);
        length = 0;
        info("GetStagedPackagePathByFullName(unknown) -> %ld", (long)GetStagedPackagePathByFullName(unknown_full, &length, nullptr));
        UINT32 packages = 7, buffer_len = 7;
        LONG r2 = GetPackagesByPackageFamily(unknown_family, &packages, nullptr, &buffer_len, nullptr);
        info("GetPackagesByPackageFamily(unknown) -> %ld, packages %u, buffer %u", (long)r2, packages, buffer_len);
        packages = 7; buffer_len = 7;
        r2 = FindPackagesByPackageFamily(unknown_family, PACKAGE_FILTER_HEAD, &packages, nullptr, &buffer_len, nullptr, nullptr);
        info("FindPackagesByPackageFamily(unknown) -> %ld, packages %u, buffer %u", (long)r2, packages, buffer_len);
        packages = 7; buffer_len = 7;
        r2 = GetPackagesByPackageFamily(L"garbage", &packages, nullptr, &buffer_len, nullptr);
        info("GetPackagesByPackageFamily(garbage) -> %ld", (long)r2);
        length = 0; count = 7;
        info("GetCurrentPackageInfo(bad flags) -> %ld", (long)GetCurrentPackageInfo(0x80000000, &length, nullptr, &count));
        length = 0;
        info("GetCurrentPackageInfo(null count) -> %ld", (long)GetCurrentPackageInfo(PACKAGE_FILTER_HEAD, &length, nullptr, nullptr));
        info("GetCurrentPackageInfo(null length) -> %ld", (long)GetCurrentPackageInfo(PACKAGE_FILTER_HEAD, nullptr, nullptr, &count));
        length = 4; BYTE tiny[4];
        info("GetCurrentPackageInfo(small buffer) -> %ld, length %u", (long)GetCurrentPackageInfo(PACKAGE_FILTER_HEAD, &length, tiny, &count), length);
        length = 1;
        info("GetCurrentPackageFullName(length 1, null buffer) -> %ld, length %u", (long)GetCurrentPackageFullName(&length, nullptr), length);
        UINT32 id_len = 0;
        info("PackageIdFromFullName(garbage) -> %ld", (long)PackageIdFromFullName(L"garbage", 0, &id_len, nullptr));
        UINT32 n = 0;
        info("PackageFamilyNameFromId(null) -> %ld", (long)PackageFamilyNameFromId(nullptr, &n, nullptr));
        info("ParseApplicationUserModelId(garbage) -> %ld", (long)ParseApplicationUserModelId(L"garbage", &n, nullptr, &n, nullptr));
        info("PackageNameAndPublisherIdFromFamilyName(garbage) -> %ld", (long)PackageNameAndPublisherIdFromFamilyName(L"garbage", &n, nullptr, &n, nullptr));
        UINT32 pn = 0, pp = 0;
        info("PackageNameAndPublisherIdFromFamilyName(unknown) -> %ld", (long)PackageNameAndPublisherIdFromFamilyName(unknown_family, &pn, nullptr, &pp, nullptr));
    }
    pkgapi_checks();
    put("READY");
    put("Uninitialize");
}

/* the 3D part of the present mode: a swap chain for the CoreWindow, five presented frames, a pixel read back */
static void present_frames(CoreWindow const &window, CoreDispatcher const &dispatcher, com_ptr<ID3D11Device> const &device,
                           com_ptr<ID3D11DeviceContext> const &context)
{
    HRESULT hr;
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

    bool pixels_ok = true;
    for (int frame = 0; frame < 5; ++frame)
    {
        com_ptr<ID3D11Texture2D> back;
        com_ptr<ID3D11RenderTargetView> view;
        check_hresult(chain->GetBuffer(0, IID_PPV_ARGS(back.put())));
        check_hresult(device->CreateRenderTargetView(back.get(), nullptr, view.put()));
        float color[4] = { 0.1f * frame, 0.3f, 0.6f, 1.f };
        context->ClearRenderTargetView(view.get(), color);

        /* screenshots do not capture a composed swap chain: read a pixel back before presenting */
        D3D11_TEXTURE2D_DESC td;
        back->GetDesc(&td);
        td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ; td.MiscFlags = 0;
        com_ptr<ID3D11Texture2D> staging;
        if (SUCCEEDED(device->CreateTexture2D(&td, nullptr, staging.put())))
        {
            context->CopyResource(staging.get(), back.get());
            D3D11_MAPPED_SUBRESOURCE map;
            if (SUCCEEDED(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &map)))
            {
                auto pixel = static_cast<unsigned char const *>(map.pData);   /* B G R A */
                int want_r = (int)(color[0] * 255.f + 0.5f);
                if (std::abs(pixel[0] - 153) > 2 || std::abs(pixel[1] - 77) > 2 || std::abs(pixel[2] - want_r) > 2) pixels_ok = false;
                if (frame == 4) info("pixel after the last clear: B=%d G=%d R=%d A=%d", pixel[0], pixel[1], pixel[2], pixel[3]);
                context->Unmap(staging.get(), 0);
            }
            else pixels_ok = false;
        }
        else pixels_ok = false;
        hr = chain->Present(1, 0);
        char text[64];
        std::snprintf(text, sizeof(text), "Present %d %s", frame, hex(hr).c_str());
        record(text);
        pump(dispatcher, 100);
    }
    check("back_buffer_holds_the_cleared_colour", pixels_ok);
}

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
    if (SUCCEEDED(hr)) present_frames(window, dispatcher, device, context);
    else info("no 3D device: the swap chain and the pixel check are skipped");
    info("after presenting: visible=%d", (int)window.Visible());
    pump(dispatcher, 3000);   /* the window is still opening on screen */
    auto now = window.Bounds();
    info("bounds now %.0fx%.0f at %.0f,%.0f", now.Width, now.Height, now.X, now.Y);
    auto position = window.PointerPosition();
    info("pointer position %.0f,%.0f", position.X, position.Y);
    put("READY");
    /* input injected by the host, or typed and clicked by hand (run-present.ps1 -Manual), arrives here */
    pump(dispatcher, mode == "suspend" ? 60000 : 30000);
    info("end: visible=%d", (int)window.Visible());
    if (mode == "present") present_checks();
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

    /* Activate: on an unlocked desktop it shows the window (Visible=1, WindowActivated, InputEnabled, no
     * SizeChanged); with the desktop locked the window stays invisible and nothing is raised. What happens
     * depends on the desktop, so it is observed here and asserted in the present mode only. */
    window.Activate();
    pump(dispatcher, 1000);
    info("after Activate and 1 s: visible=%d", (int)window.Visible());
    {
        std::lock_guard<std::mutex> guard(lock);
        info("window events after Activate: Visible=1 x%d, WindowActivated x%d, SizeChanged x%d", count(events, "Visible=1"),
             count(events, "WindowActivated"), count(events, "SizeChanged"));
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
        CoreApplication::Suspending([](IInspectable const &, Windows::ApplicationModel::SuspendingEventArgs const &args) {
            if (!presenting()) { record("Suspending"); return; }
            /* present mode: what the suspension carries (measured by hand: minimize the window, wait, restore it) */
            auto operation = args.SuspendingOperation();
            auto left = std::chrono::duration_cast<std::chrono::milliseconds>(operation.Deadline() - winrt::clock::now()).count();
            char text[96];
            std::snprintf(text, sizeof(text), "Suspending deadline_in=%lld ms", (long long)left);
            record(text);
            auto deferral = operation.GetDeferral();   /* the application is not suspended until this completes */
            std::thread([deferral] { Sleep(1500); record("Suspending deferral completing"); deferral.Complete(); }).detach();
        });
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
        if (presenting())
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
            Sleep(presenting() ? 100000 : 8000);
            put("TRACE WATCHDOG: the process was still running after Run");
            ExitProcess(3);
        }).detach();
        try
        {
            if (presenting()) run_present(CoreWindow::GetForCurrentThread());
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
               application_id == L"ViewAppdata" ? "appdata" : application_id == L"ViewPkgapi" ? "pkgapi" : application_id == L"ViewPresent" ? "present" : application_id == L"ViewSuspend" ? "suspend" : application_id == L"View" ? "positive" : "";
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

    if (mode == "pkgapi" || mode == "appdata")
    {
        check("package_identity", !package_full_name.empty());
        try { if (mode == "pkgapi") run_pkgapi(); else run_appdata(); }
        catch (hresult_error const &e) { info("threw 0x%08X", (unsigned)e.code()); check("no_exception", false); put("READY"); put("Uninitialize"); }
        return 0;
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
