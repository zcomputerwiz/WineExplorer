/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 */
#include <windows.h>
#include <shobjidl.h>
#include <cstdio>
#include <string>

int wmain(int argc, wchar_t **argv)
{
    if (argc != 5) return 2;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return 2;
    IApplicationActivationManager *manager = nullptr;
    hr = CoCreateInstance(CLSID_ApplicationActivationManager, nullptr, CLSCTX_LOCAL_SERVER,
                          IID_PPV_ARGS(&manager));
    DWORD pid = 0;
    std::wstring args = std::wstring(argv[2]) + L" \"" + argv[3] + L"\" \"" + argv[4] + L"\"";
    if (SUCCEEDED(hr)) hr = manager->ActivateApplication(argv[1], args.c_str(), AO_NONE, &pid);
    if (manager) manager->Release();
    if (FAILED(hr)) { std::fwprintf(stderr, L"ActivateApplication: 0x%08lX\n", static_cast<unsigned long>(hr)); CoUninitialize(); return 2; }
    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    DWORD code = 2;
    if (process)
    {
        if (WaitForSingleObject(process, 60000) == WAIT_OBJECT_0) GetExitCodeProcess(process, &code);
        CloseHandle(process);
    }
    CoUninitialize();
    return static_cast<int>(code);
}
