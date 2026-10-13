/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Isolated dialog tests. Every path cancels; no power action is requested.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "wexpower.h"

static FILE *report;
static int failures;
static BOOL saw_dialog;
#define CHECK(condition, label) do { BOOL passed = !!(condition); \
    fprintf(report, "%s %s\n", passed ? "PASS" : "FAIL", label); fflush(report); \
    if (!passed) failures++; } while (0)

static HWND wait_window(const WCHAR *name)
{
    unsigned int attempt;
    HWND hwnd;
    for (attempt = 0; attempt < 200; attempt++)
    {
        if ((hwnd = FindWindowW(name, NULL)) && IsWindowVisible(hwnd)) return hwnd;
        Sleep(50);
    }
    return NULL;
}

static DWORD WINAPI cancel_dialog(void *unused)
{
    HWND hwnd = wait_window(L"WineExplorerTurnOff");
    RECT rect;
    saw_dialog = hwnd != NULL;
    CHECK(saw_dialog, "power dialog visible");
    if (hwnd)
    {
        GetWindowRect(hwnd, &rect);
        CHECK(rect.left >= 0 && rect.top >= 0 &&
              rect.right <= GetSystemMetrics(SM_CXSCREEN) &&
              rect.bottom <= GetSystemMetrics(SM_CYSCREEN), "dialog within display");
        PostMessageW(hwnd, WM_KEYDOWN, VK_ESCAPE, 0);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, char *command, int show)
{
    HMODULE module = NULL;
    HWND greeter = NULL;
    report = fopen("Z:\\tmp\\power-module-probe.txt", "w");
    if (!report) return 2;
    if (!strcmp(command, "standalone"))
    {
        WEX_POWER_SHOW_DIALOG functions[2];
        int i;
        module = LoadLibraryW(L"msgina.dll");
        functions[0] = module ? (WEX_POWER_SHOW_DIALOG)GetProcAddress(module, "power_mgmt_show_shutdown_dialog") : NULL;
        functions[1] = module ? (WEX_POWER_SHOW_DIALOG)GetProcAddress(module, "power_mgmt_show_logoff_dialog") : NULL;
        CHECK(functions[0] && functions[1] && GetProcAddress(module, "power_mgmt_execute_action"),
              "shared power API available");
        CHECK(!GetModuleHandleW(L"wexshell.dll"), "standalone consumer does not load shell");
        for (i = 0; i < 2; i++)
        {
            HANDLE worker;
            if (!functions[i]) continue;
            saw_dialog = FALSE;
            worker = CreateThread(NULL, 0, cancel_dialog, NULL, 0, NULL);
            functions[i](NULL);
            WaitForSingleObject(worker, 15000);
            CloseHandle(worker);
            CHECK(saw_dialog && !FindWindowW(L"WineExplorerTurnOff", NULL), "Cancel returns and closes dialog");
        }
    }
    else
    {
        if (!strcmp(command, "greeter"))
        {
            RECT rect;
            greeter = wait_window(L"WexLogonUI");
            CHECK(greeter != NULL, "logonui visible with fixture connection");
            if (!greeter) goto done;
            GetClientRect(greeter, &rect);
            PostMessageW(greeter, WM_LBUTTONUP, 0, MAKELPARAM(40, rect.bottom - 60));
        }
        {
            HWND dialog = wait_window(L"WineExplorerTurnOff");
            DWORD dialog_pid = 0, owner_pid = 0;
            CHECK(dialog != NULL, "consumer opens power dialog");
            if (dialog)
            {
                if (greeter)
                {
                    GetWindowThreadProcessId(dialog, &dialog_pid);
                    GetWindowThreadProcessId(greeter, &owner_pid);
                    CHECK(dialog_pid == owner_pid, "greeter uses DLL in its own process");
                }
                PostMessageW(dialog, WM_KEYDOWN, VK_ESCAPE, 0);
                Sleep(300);
                CHECK(!FindWindowW(L"WineExplorerTurnOff", NULL), "consumer dialog cancels");
            }
        }
        if (greeter) PostMessageW(greeter, WM_CLOSE, 0, 0);
    }
done:
    CHECK(!FindWindowW(L"WineExplorerDimmedScreen", NULL), "dimming overlay removed");
    fprintf(report, "COMPLETE: %d failures\n", failures);
    fclose(report);
    if (module) FreeLibrary(module);
    return failures != 0;
}
