/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Read-only backend and cancel-only network UI/COM checks.
 */
#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <docobj.h>
#include <cpl.h>
#include <stdio.h>
#include <string.h>
#include "wexnet.h"

static const CLSID tray_class = {0x7007accf,0x3202,0x11d1,{0xaa,0xd2,0,0x80,0x5f,0xc1,0x27,0x0e}};
static const GUID group = {0x214d2,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static FILE *report;
static int failures;
static HMODULE module;
static BOOL cpl_mode;
#define CHECK(condition, label) do { BOOL passed = !!(condition); \
    fprintf(report, "%s %s\n", passed ? "PASS" : "FAIL", label); fflush(report); \
    if (!passed) failures++; } while (0)

static DWORD WINAPI cancel_dialog(void *unused)
{
    unsigned int attempt;
    HWND dialog = NULL;
    RECT rect;
    for (attempt = 0; attempt < 200; attempt++)
    {
        dialog = FindWindowW(L"#32770", NULL);
        if (dialog && IsWindowVisible(dialog)) break;
        Sleep(50);
    }
    CHECK(dialog && IsWindowVisible(dialog), "network dialog visible");
    if (dialog)
    {
        if (cpl_mode)
        {
            DWORD pid;
            GetWindowThreadProcessId(dialog, &pid);
            CHECK(pid == GetCurrentProcessId(), "ncpa uses shared UI in its own caller process");
        }
        else CHECK((HMODULE)GetWindowLongPtrW(dialog, GWLP_HINSTANCE) == module, "dialog resources belong to network DLL");
        GetWindowRect(dialog, &rect);
        CHECK(rect.left >= 0 && rect.top >= 0 && rect.right <= GetSystemMetrics(SM_CXSCREEN) &&
              rect.bottom <= GetSystemMetrics(SM_CYSCREEN), "network dialog within display");
        PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
    }
    return 0;
}

static void CALLBACK cancel_menu(HWND hwnd, UINT msg, UINT_PTR id, DWORD time)
{
    KillTimer(hwnd, id);
    EndMenu();
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, char *command, int show)
{
    if (!strcmp(command, "cpl"))
    {
        HMODULE cpl = LoadLibraryW(L"ncpa.cpl");
        LONG (CALLBACK *applet)(HWND, UINT, LPARAM, LPARAM) = cpl ?
            (void *)GetProcAddress(cpl, "CPlApplet") : NULL;
        report = fopen("Z:\\tmp\\network-module-probe.txt", "w");
        if (!report) return 2;
        CHECK(applet != NULL, "ncpa applet available");
        if (applet)
        {
            HANDLE worker;
            cpl_mode = TRUE;
            worker = CreateThread(NULL, 0, cancel_dialog, NULL, 0, NULL);
            applet(NULL, CPL_DBLCLK, 0, 0);
            WaitForSingleObject(worker, 15000);
            CloseHandle(worker);
            CHECK(!FindWindowW(L"#32770", NULL), "ncpa dialog cancels");
        }
        if (cpl) FreeLibrary(cpl);
        goto done;
    }
    if (strcmp(command, "com"))
    {
        HWND tray = FindWindowW(L"Shell_TrayWnd", NULL);
        BOOL present = !strcmp(command, "present");
        report = fopen("Z:\\tmp\\network-module-probe.txt", "w");
        if (!report) return 2;
        CHECK(tray && IsWindowVisible(tray), "shell remains visible");
        CHECK(!!FindWindowW(L"Connections Tray", NULL) == present, "network service follows registry/DLL availability");
        goto done;
    }
    report = fopen("Z:\\tmp\\network-module-probe.txt", "w");
    if (!report) return 2;
    CoInitialize(NULL);
    module = LoadLibraryW(L"netshell.dll");
    CHECK(module != NULL, "network DLL available");
    if (module)
    {
        BOOL (__cdecl *status)(NET_STATUS_INFO *) = (void *)GetProcAddress(module, "net_backend_get_status");
        void (__cdecl *dialogs[2])(HWND) = {
            (void *)GetProcAddress(module, "wlan_status_show"), (void *)GetProcAddress(module, "net_props_show")};
        NET_STATUS_INFO info;
        IOleCommandTarget *target = NULL;
        HRESULT hr;
        int i;
        CHECK(status && status(&info), "host network status available through copied struct");
        if (status)
            CHECK(info.has_adapter && info.active_device[0], "host adapter identified");
        hr = CoCreateInstance(&tray_class, NULL, CLSCTX_INPROC_SERVER, &IID_IOleCommandTarget, (void **)&target);
        CHECK(SUCCEEDED(hr) && target, "registered connection service activates");
        if (target)
        {
            CHECK(SUCCEEDED(IOleCommandTarget_Exec(target, &group, OLECMDID_NEW, 0, NULL, NULL)) &&
                  FindWindowW(L"Connections Tray", NULL), "connection tray starts");
            SetTimer(NULL, 0, 300, cancel_menu);
            SendMessageW(FindWindowW(L"Connections Tray", NULL), WM_APP + 200, 1001, WM_RBUTTONUP);
            CHECK(!FindWindowW(L"#32768", NULL), "connection menu cancels");
            IOleCommandTarget_Exec(target, &group, OLECMDID_SAVE, 0, NULL, NULL);
            CHECK(!FindWindowW(L"Connections Tray", NULL), "connection tray stops");
            IOleCommandTarget_Release(target);
        }
        for (i = 0; i < 2; i++)
        {
            HANDLE worker;
            CHECK(dialogs[i] != NULL, "network UI export available");
            if (!dialogs[i]) continue;
            worker = CreateThread(NULL, 0, cancel_dialog, NULL, 0, NULL);
            dialogs[i](NULL);
            WaitForSingleObject(worker, 15000);
            CloseHandle(worker);
            CHECK(!FindWindowW(L"#32770", NULL), "network dialog cancels");
        }
        FreeLibrary(module);
    }
    CoUninitialize();
done:
    fprintf(report, "COMPLETE: %d failures\n", failures);
    fclose(report);
    return failures != 0;
}
