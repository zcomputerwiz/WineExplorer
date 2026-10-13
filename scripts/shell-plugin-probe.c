/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Run as the desktop startup program; argument is present or absent.
 * Build with MinGW and the assembled Wine include directory.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "wexshell.h"

static BOOL clock_found, tasks_found;

static BOOL CALLBACK inspect(HWND hwnd, LPARAM unused)
{
    WCHAR name[128];
    GetClassNameW(hwnd, name, sizeof(name) / sizeof(name[0]));
    if (!wcscmp(name, L"TrayClockWClass")) clock_found = TRUE;
    if (!wcscmp(name, L"MSTaskListWClass")) tasks_found = TRUE;
    return TRUE;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, char *command, int show)
{
    BOOL expected = !strcmp(command, "present");
    HMODULE module;
    HWND tray = FindWindowW(L"Shell_TrayWnd", NULL);
    FILE *report = fopen("Z:\\tmp\\shell-plugin-probe.txt", "w");
    int failures = 0;
    if (!report) return 2;
    if (!strcmp(command, "menu"))
    {
        HWND button = tray ? FindWindowExW(tray, NULL, L"Start", NULL) : NULL;
        unsigned int attempt;
        PostMessageW(button, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
        PostMessageW(button, WM_LBUTTONUP, 0, MAKELPARAM(10, 10));
        for (attempt = 0; attempt < 100; attempt++)
        {
            HWND popup = FindWindowW(L"#32768", NULL);
            HWND panel = FindWindowW(L"WineStartMenuWnd", NULL);
            if ((popup && IsWindowVisible(popup)) || (panel && IsWindowVisible(panel))) break;
            Sleep(50);
        }
        fprintf(report, "COMPLETE: %d failures\n", attempt == 100);
        fclose(report);
        return attempt == 100;
    }
    module = LoadLibraryW(L"wexshell.dll");
#define CHECK(condition, label) do { BOOL passed = !!(condition); \
    fprintf(report, "%s %s\n", passed ? "PASS" : "FAIL", label); \
    if (!passed) failures++; } while (0)
    CHECK(!!module == expected, "plugin availability");
    CHECK(tray && IsWindowVisible(tray), "visible taskbar");
    if (tray) EnumChildWindows(tray, inspect, 0);
    CHECK(clock_found == expected && tasks_found == expected, "plugin controls versus stock tray");
    if (module)
    {
        WEX_SHELL_START start = (WEX_SHELL_START)GetProcAddress(module, "WexShellStart");
        WEX_SHELL_COMMAND dispatch = (WEX_SHELL_COMMAND)GetProcAddress(module, "WexShellCommand");
        CHECK(start && dispatch && GetProcAddress(module, "WexShellStop"), "versioned exports");
        if (start)
        {
            CHECK(!start(WEX_SHELL_VERSION + 1, GetDesktopWindow(), 800, 600, TRUE, FALSE),
                  "incompatible start rejected");
            CHECK(!start(WEX_SHELL_VERSION, NULL, 800, 600, TRUE, FALSE), "null desktop rejected");
        }
        if (dispatch)
        {
            WCHAR argument[] = L"/desktop=Probe,800x600";
            CHECK(!dispatch(WEX_SHELL_VERSION + 1, argument, SW_SHOW), "incompatible command rejected");
            CHECK(!dispatch(WEX_SHELL_VERSION, argument, SW_SHOW), "desktop command left to host");
            CHECK(!dispatch(WEX_SHELL_VERSION, L"/n,/desktop=Probe,800x600", SW_SHOW),
                  "desktop after other switches left to host");
        }
        FreeLibrary(module);
    }
    fprintf(report, "COMPLETE: %d failures\n", failures);
    fclose(report);
    return failures != 0;
}
