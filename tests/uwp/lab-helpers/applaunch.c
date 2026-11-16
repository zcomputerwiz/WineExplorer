/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Lab launcher: activate a packaged application the way the Start menu does (no arguments unless given) and wait for
 * it to exit. usage: applaunch.exe AUMID [ARGUMENTS]
 */
#define COBJMACROS
#include <windows.h>
#include <shobjidl.h>
#include <stdio.h>

int wmain(int argc, wchar_t **argv)
{
    IApplicationActivationManager *manager = NULL;
    HANDLE process;
    DWORD pid = 0, code = 2;
    HRESULT hr;

    if (argc != 2 && argc != 3) return 2;
    if (FAILED(hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))) return 2;
    hr = CoCreateInstance(&CLSID_ApplicationActivationManager, NULL, CLSCTX_LOCAL_SERVER,
                          &IID_IApplicationActivationManager, (void **)&manager);
    if (SUCCEEDED(hr)) hr = IApplicationActivationManager_ActivateApplication(manager, argv[1], argc == 3 ? argv[2] : NULL,
                                                                              AO_NONE, &pid);
    if (manager) IApplicationActivationManager_Release(manager);
    if (FAILED(hr))
    {
        fwprintf(stderr, L"ActivateApplication: 0x%08lx\n", (unsigned long)hr);
        CoUninitialize();
        return 2;
    }
    fwprintf(stderr, L"pid %lu\n", pid);
    if ((process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)))
    {
        if (WaitForSingleObject(process, INFINITE) == WAIT_OBJECT_0) GetExitCodeProcess(process, &code);
        CloseHandle(process);
    }
    CoUninitialize();
    return (int)code;
}
