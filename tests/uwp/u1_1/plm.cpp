// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
//
// plm.exe suspend|resume|state PACKAGE_FULL_NAME: asks the process lifetime manager of Windows to suspend or resume
// the processes of a package, or prints its execution state, through IPackageDebugSettings (the per-user interface
// that debuggers use). Used for the unattended suspend and resume measurement of the U1.1 view probe.
#include <windows.h>
#include <shobjidl.h>
#include <cstdio>
#include <cwchar>

int wmain(int argc, wchar_t **argv)
{
    if (argc != 3) { std::fwprintf(stderr, L"usage: plm debug|nodebug|suspend|resume|state PACKAGE_FULL_NAME\n"); return 2; }
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IPackageDebugSettings *settings = nullptr;
    if (SUCCEEDED(hr)) hr = CoCreateInstance(CLSID_PackageDebugSettings, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&settings));
    if (SUCCEEDED(hr))
    {
        /* the process lifetime manager suspends on request only a package in debug mode (no debugger is attached) */
        if (!wcscmp(argv[1], L"debug")) hr = settings->EnableDebugging(argv[2], nullptr, nullptr);
        else if (!wcscmp(argv[1], L"nodebug")) hr = settings->DisableDebugging(argv[2]);
        else if (!wcscmp(argv[1], L"suspend")) hr = settings->Suspend(argv[2]);
        else if (!wcscmp(argv[1], L"resume")) hr = settings->Resume(argv[2]);
        else if (!wcscmp(argv[1], L"state"))
        {
            PACKAGE_EXECUTION_STATE state = PES_UNKNOWN;
            if (SUCCEEDED(hr = settings->GetPackageExecutionState(argv[2], &state))) std::wprintf(L"state %d\n", (int)state);
        }
        else hr = E_INVALIDARG;
        settings->Release();
    }
    std::wprintf(L"%s: 0x%08X\n", argv[1], (unsigned)hr);
    CoUninitialize();
    return SUCCEEDED(hr) ? 0 : 1;
}
