/*
 * WineExplorer U1.3 package dependency probe
 *
 * Copyright 2026 WineExplorer contributors
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
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/*
 * A packaged full-trust program that depends on a framework package: it loads the framework's library by name, calls
 * it, and asks for its package graph. The activation passes "mode OUTPUT FULLNAME" as arguments, like the U1.0 probe.
 */

#include <windows.h>
#include <appmodel.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

static int failures;

static void check(char const *name, bool passed)
{
    std::printf("%s %s\n", passed ? "PASS" : "FAIL", name);
    if (!passed) ++failures;
}

static std::wstring last_component(std::wstring const &path)
{
    auto at = path.rfind(L'\\');
    return at == std::wstring::npos ? path : path.substr(at + 1);
}

int wmain(int argc, wchar_t **argv)
{
    if (argc != 4 || !_wfreopen(argv[2], L"w", stdout)) return 2;
    setvbuf(stdout, nullptr, _IONBF, 0);

    UINT32 size = 0;
    LONG rc = GetCurrentPackageFullName(&size, nullptr);
    check("package_identity", rc == ERROR_INSUFFICIENT_BUFFER);
    std::vector<wchar_t> full(size ? size : 1);
    GetCurrentPackageFullName(&size, full.data());
    check("package_full_name_is_the_activated_one", std::wstring(full.data()) == argv[3]);

    UINT32 length = 0, count = 0;
    rc = GetCurrentPackageInfo(PACKAGE_FILTER_DIRECT, &length, nullptr, &count);
    std::vector<BYTE> direct(length ? length : 1);
    if (rc == ERROR_INSUFFICIENT_BUFFER) rc = GetCurrentPackageInfo(PACKAGE_FILTER_DIRECT, &length, direct.data(), &count);
    check("direct_dependencies_are_the_framework", rc == ERROR_SUCCESS && count == 1);
    std::wstring framework_name, framework_path;
    if (rc == ERROR_SUCCESS && count == 1)
    {
        auto *info = reinterpret_cast<PACKAGE_INFO *>(direct.data());
        framework_name = info->packageId.name;
        framework_path = info->path;
        std::wprintf(L"INFO framework %ls at %ls\n", info->packageFullName, info->path);
    }
    check("framework_package_name", framework_name == L"WineExplorer.U1Framework");

    UINT32 both = 0;
    length = 0;
    rc = GetCurrentPackageInfo(PACKAGE_FILTER_HEAD | PACKAGE_FILTER_DIRECT, &length, nullptr, &both);
    check("head_and_direct_are_two_packages", rc == ERROR_INSUFFICIENT_BUFFER && both == 2);

    HMODULE module = LoadLibraryW(L"u1fw.dll");
    check("framework_library_loads_by_name", module != nullptr);
    if (module)
    {
        auto answer = reinterpret_cast<int (*)()>(GetProcAddress(module, "U1FrameworkAnswer"));
        check("framework_library_works", answer && answer() == 42);
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(module, path, MAX_PATH);
        std::wprintf(L"INFO library %ls\n", path);
        check("framework_library_is_in_the_framework_package", !framework_path.empty() &&
              _wcsnicmp(path, framework_path.c_str(), framework_path.size()) == 0 && last_component(path) == L"u1fw.dll");
    }
    return failures ? 1 : 0;
}
