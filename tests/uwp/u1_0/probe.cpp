/*
 * WineExplorer U1.0 packaged identity probe
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <windows.h>
#include <appmodel.h>
#include <roapi.h>
#include <activation.h>
#include <winstring.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>
#include <cstdlib>

using namespace winrt;
static int failures;
static void check(char const *name, bool passed)
{
    std::printf("%s %s\n", passed ? "PASS" : "FAIL", name);
    if (!passed) ++failures;
}

template<typename F> static std::wstring query(F f)
{
    UINT32 length = 0;
    if (f(&length, nullptr) != ERROR_INSUFFICIENT_BUFFER || !length) return {};
    std::vector<wchar_t> buffer(length);
    if (f(&length, buffer.data()) != ERROR_SUCCESS) return {};
    return buffer.data();
}

int wmain(int argc, wchar_t **argv)
{
    if (argc != 4 || !_wfreopen(argv[2], L"w", stdout)) return 2;
    bool negative = wcscmp(argv[1], L"positive") != 0;
    HRESULT init = RoInitialize(RO_INIT_MULTITHREADED);
    if (negative)
    {
        HSTRING n = nullptr;
        IActivationFactory *f = nullptr;
        HRESULT hr = init;
        constexpr wchar_t negative_name[] = L"WineExplorer.U1Oracle.Component";
        if (FAILED(hr)) return 2;
        hr = WindowsCreateString(negative_name, ARRAYSIZE(negative_name) - 1, &n);
        if (FAILED(hr)) return 2;
        UINT32 size = 0;
        LONG identity = GetCurrentPackageFullName(&size, nullptr);
        bool correct_context = !wcscmp(argv[1], L"unpackaged") ? identity == APPMODEL_ERROR_NO_PACKAGE :
            query([](UINT32 *n, PWSTR p) { return GetCurrentPackageFullName(n, p); }) == argv[3];
        hr = RoGetActivationFactory(n, IID_IActivationFactory, reinterpret_cast<void **>(&f));
        std::printf("HRESULT %s 0x%08lX\n", wcscmp(argv[1], L"missing_class") == 0 ? "missing_class" :
                    wcscmp(argv[1], L"missing_dll") == 0 ? "missing_dll" : "unpackaged", static_cast<unsigned long>(hr));
        check("negative_activation", correct_context && FAILED(hr));
        if (f) f->Release();
        WindowsDeleteString(n);
        if (SUCCEEDED(init)) RoUninitialize();
        return failures ? 1 : 0;
    }
    check("winrt_init", SUCCEEDED(init));
    if (FAILED(init)) return 1;

    auto full = query([](UINT32 *n, PWSTR p) { return GetCurrentPackageFullName(n, p); });
    auto family = query([](UINT32 *n, PWSTR p) { return GetCurrentPackageFamilyName(n, p); });
    auto path = query([](UINT32 *n, PWSTR p) { return GetCurrentPackagePath(n, p); });
    auto process = query([](UINT32 *n, PWSTR p) { return GetPackageFullName(GetCurrentProcess(), n, p); });
    HANDLE token = nullptr;
    std::wstring token_full, token_family;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        token_full = query([&](UINT32 *n, PWSTR p) { return GetPackageFullNameFromToken(token, n, p); });
        token_family = query([&](UINT32 *n, PWSTR p) { return GetPackageFamilyNameFromToken(token, n, p); });
        CloseHandle(token);
    }
    check("package_full_name", !full.empty() && full == process && full == argv[3]);
    check("token_identity", !full.empty() && full == token_full && !family.empty() && family == token_family);
    check("package_path", !path.empty());

    HSTRING name = nullptr;
    IActivationFactory *factory = nullptr;
    IInspectable *instance = nullptr;
    constexpr wchar_t class_name[] = L"WineExplorer.U1Oracle.Component";
    HRESULT hr = WindowsCreateString(class_name, ARRAYSIZE(class_name) - 1, &name);
    if (SUCCEEDED(hr)) hr = RoGetActivationFactory(name, IID_IActivationFactory, reinterpret_cast<void **>(&factory));
    bool module_ok = false;
    if (SUCCEEDED(hr) && factory && !path.empty())
    {
        HMODULE module = nullptr;
        auto method = (*reinterpret_cast<void ***>(factory))[0];
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(method), &module))
        {
            wchar_t loaded[32768];
            DWORD n = GetModuleFileNameW(module, loaded, ARRAYSIZE(loaded));
            std::wstring expected = path + L"\\component.dll";
            module_ok = n && n < ARRAYSIZE(loaded) && !_wcsicmp(loaded, expected.c_str());
        }
    }
    check("factory_module", module_ok);
    if (SUCCEEDED(hr)) hr = factory ? factory->ActivateInstance(&instance) : E_UNEXPECTED;
    HSTRING returned = nullptr;
    if (SUCCEEDED(hr)) hr = instance ? instance->GetRuntimeClassName(&returned) : E_UNEXPECTED;
    UINT32 length = 0;
    wchar_t const *actual = WindowsGetStringRawBuffer(returned, &length);
    check("package_activation", SUCCEEDED(hr) && length == ARRAYSIZE(class_name) - 1 &&
          !wmemcmp(actual, class_name, length));
    WindowsDeleteString(returned);
    if (instance) instance->Release();
    if (factory) factory->Release();
    WindowsDeleteString(name);

    bool async_ok = false;
    try
    {
        if (!path.empty())
        {
            auto operation = winrt::Windows::Storage::StorageFolder::GetFolderFromPathAsync(path);
            auto folder = operation.get();
            async_ok = folder && operation.Status() == winrt::Windows::Foundation::AsyncStatus::Completed;
        }
    }
    catch (winrt::hresult_error const &) { }
    check("async_folder", async_ok);
    RoUninitialize();
    return failures ? 1 : 0;
}
