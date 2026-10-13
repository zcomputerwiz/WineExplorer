/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * COM lifecycle/resource ownership and optional-service startup checks.
 */
#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <docobj.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>

static const CLSID systray_class =
    {0x35cec8a3,0x2be6,0x11d2,{0x87,0x73,0x92,0xe2,0x20,0x52,0x41,0x53}};
static const GUID service_group =
    {0x000214d2,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static FILE *report;
static int failures;
#define CHECK(condition, label) do { BOOL passed = !!(condition); \
    fprintf(report, "%s %s\n", passed ? "PASS" : "FAIL", label); fflush(report); \
    if (!passed) failures++; } while (0)

static BOOL CALLBACK find_slider(HWND hwnd, LPARAM param)
{
    WCHAR name[64];
    GetClassNameW(hwnd, name, 64);
    if (!lstrcmpW(name, L"msctls_trackbar32")) *(HWND *)param = hwnd;
    return TRUE;
}
static void CALLBACK cancel_menu(HWND hwnd, UINT msg, UINT_PTR id, DWORD time)
{
    KillTimer(hwnd, id);
    EndMenu();
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, char *command, int show)
{
    HMODULE module = NULL;
    HRESULT (WINAPI *can_unload)(void);
    IOleCommandTarget *target = NULL, *second = NULL;
    HRESULT hr;
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_BAR_CLASSES};
    HWND volume, power;
    report = fopen("Z:\\tmp\\tray-service-probe.txt", "w");
    if (!report) return 2;
    InitCommonControlsEx(&controls);
    if (strcmp(command, "com"))
    {
        HWND tray = FindWindowW(L"Shell_TrayWnd", NULL);
        BOOL present = !strcmp(command, "present");
        volume = FindWindowW(L"Volume Tray Window", NULL);
        power = FindWindowW(L"Power Tray Window", NULL);
        CHECK(tray && IsWindowVisible(tray), "shell remains visible");
        CHECK(!!volume == present && !!power == present, "registered versus disabled applets");
        goto done;
    }
    CoInitialize(NULL);
    module = LoadLibraryW(L"stobject.dll");
    can_unload = module ? (void *)GetProcAddress(module, "DllCanUnloadNow") : NULL;
    CHECK(can_unload != NULL, "COM service DLL available");
    hr = CoCreateInstance(&systray_class, NULL, CLSCTX_INPROC_SERVER, &IID_IOleCommandTarget, (void **)&target);
    CHECK(SUCCEEDED(hr) && target, "registered service activates");
    if (!target) goto uninit;
    CHECK(can_unload && can_unload() == S_FALSE, "live interface prevents DLL unload");
    hr = IOleCommandTarget_Exec(target, NULL, OLECMDID_NEW, 0, NULL, NULL);
    CHECK(hr == OLECMDERR_E_UNKNOWNGROUP, "unrelated command group rejected");
    hr = IOleCommandTarget_Exec(target, &service_group, OLECMDID_NEW, 0, NULL, NULL);
    CHECK(SUCCEEDED(hr), "service starts");
    volume = FindWindowW(L"Volume Tray Window", NULL);
    power = FindWindowW(L"Power Tray Window", NULL);
    CHECK(volume && power, "both applet windows exist");
    CHECK(volume && (HMODULE)GetClassLongPtrW(volume, GCLP_HMODULE) == module,
          "applet window class belongs to service DLL");
    hr = IOleCommandTarget_Exec(target, &service_group, OLECMDID_NEW, 0, NULL, NULL);
    CHECK(SUCCEEDED(hr) && FindWindowW(L"Volume Tray Window", NULL) == volume, "start is idempotent");
    if (volume)
    {
        HWND dialog, slider = NULL;
        Sleep(600);
        SendMessageW(volume, WM_APP + 210, 1002, WM_LBUTTONUP);
        dialog = FindWindowW(L"#32770", NULL);
        if (dialog) EnumChildWindows(dialog, find_slider, (LPARAM)&slider);
        CHECK(slider != NULL, "volume popup has slider");
        CHECK(dialog && (HMODULE)GetWindowLongPtrW(dialog, GWLP_HINSTANCE) == module,
              "volume dialog resources belong to service DLL");
        if (dialog) SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
        SetTimer(NULL, 0, 300, cancel_menu);
        SendMessageW(volume, WM_APP + 210, 1002, WM_RBUTTONUP);
        CHECK(!FindWindowW(L"#32768", NULL), "volume menu cancels");
    }
    if (power)
    {
        SetTimer(NULL, 0, 300, cancel_menu);
        SendMessageW(power, WM_APP + 220, 1003, WM_RBUTTONUP);
        CHECK(!FindWindowW(L"#32768", NULL), "power menu cancels");
    }
    hr = CoCreateInstance(&systray_class, NULL, CLSCTX_INPROC_SERVER, &IID_IOleCommandTarget, (void **)&second);
    CHECK(SUCCEEDED(hr) && second, "second interface activates");
    if (second)
    {
        CHECK(SUCCEEDED(IOleCommandTarget_Exec(second, &service_group, OLECMDID_NEW, 0, NULL, NULL)),
              "second user shares applets");
        IOleCommandTarget_Exec(target, &service_group, OLECMDID_SAVE, 0, NULL, NULL);
        CHECK(FindWindowW(L"Volume Tray Window", NULL) == volume, "one user's stop preserves another user");
        IOleCommandTarget_Release(second);
        CHECK(!FindWindowW(L"Volume Tray Window", NULL) && !FindWindowW(L"Power Tray Window", NULL),
              "last user release cleans applets");
    }
    CHECK(SUCCEEDED(IOleCommandTarget_Exec(target, &service_group, OLECMDID_NEW, 0, NULL, NULL)),
          "service restarts after cleanup");
    IOleCommandTarget_Exec(target, &service_group, OLECMDID_SAVE, 0, NULL, NULL);
    CHECK(!FindWindowW(L"Volume Tray Window", NULL) && !FindWindowW(L"Power Tray Window", NULL),
          "SAVE shuts down both applets");
    IOleCommandTarget_Release(target);
    CHECK(can_unload && can_unload() == S_OK, "released service can unload");
uninit:
    CoUninitialize();
done:
    fprintf(report, "COMPLETE: %d failures\n", failures);
    fclose(report);
    if (module) FreeLibrary(module);
    return failures != 0;
}
