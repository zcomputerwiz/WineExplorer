/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Run as startup child of an isolated refactor desktop. Exercises the Themes
 * page across the DLL/process boundary, without changing a user's prefix.
 */
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include "wextheme.h"

static HWND combo;
static BOOL CALLBACK find_combo(HWND hwnd, LPARAM unused)
{
    if (GetDlgCtrlID(hwnd) == 2100) combo = hwnd;
    return TRUE;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, char *command, int show)
{
    HMODULE module = LoadLibraryW(L"themeui.dll");
    void (__cdecl *snapshot)(THEME_CONFIG *) = module ?
        (void *)GetProcAddress(module, "theme_get_current") : NULL;
    BOOL (__cdecl *apply_file)(const WCHAR *) = module ?
        (void *)GetProcAddress(module, "theme_apply_file") : NULL;
    THEME_CONFIG first, second;
    WCHAR path[MAX_PATH], folder[MAX_PATH], current[MAX_PATH], name[80];
    STARTUPINFOW startup = {sizeof(startup)};
    PROCESS_INFORMATION process = {0};
    WCHAR launch[] = L"rundll32.exe shell32.dll,Control_RunDLL desk.cpl";
    FILE *report = fopen("Z:\\tmp\\theme-module-probe.txt", "w");
    HWND sheet = NULL;
    unsigned int attempt;
    DWORD bytes;
    int choice, failures = 0;
    if (!report) return 2;
#define CHECK(condition, label) do { BOOL passed = !!(condition); \
    fprintf(report, "%s %s\n", passed ? "PASS" : "FAIL", label); \
    fflush(report); if (!passed) failures++; } while (0)
    CHECK(snapshot && apply_file, "shared theme API available");
    if (!snapshot || !apply_file) goto done;
    snapshot(&first);
    first.start_menu_style = 1234;
    snapshot(&second);
    CHECK(second.start_menu_style != 1234, "snapshot cannot mutate DLL state");
    CHECK(!apply_file(L"C:\\missing-refactor-theme.theme"), "missing theme rejected");
    GetEnvironmentVariableW(L"APPDATA", folder, MAX_PATH);
    lstrcatW(folder, L"\\Microsoft"); CreateDirectoryW(folder, NULL);
    lstrcatW(folder, L"\\Windows"); CreateDirectoryW(folder, NULL);
    lstrcatW(folder, L"\\Themes"); CreateDirectoryW(folder, NULL);
    swprintf(path, MAX_PATH, L"%ls\\Refactor.theme", folder);
    CHECK(WritePrivateProfileStringW(L"Theme", L"DisplayName", L"Refactor fixture", path) &&
          WritePrivateProfileStringW(L"Control Panel\\Colors", L"Background", L"12 34 56", path),
          "theme fixture created");
    CHECK(CreateProcessW(NULL, launch, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process),
          "Display Properties launched");
    if (!process.hProcess) goto done;
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    for (attempt = 0; attempt < 200; attempt++)
    {
        sheet = FindWindowW(L"#32770", L"Display Properties");
        if (sheet) EnumChildWindows(sheet, find_combo, 0);
        if (combo) break;
        Sleep(50);
    }
    CHECK(combo != NULL, "Themes page visible");
    if (!combo) goto done;
    choice = SendMessageW(combo, CB_FINDSTRINGEXACT, -1, (LPARAM)L"Refactor fixture");
    CHECK(choice != CB_ERR, "installed theme discovered dynamically");
    if (choice == CB_ERR) goto done;
    SendMessageW(combo, CB_SETCURSEL, choice, 0);
    SendMessageW(GetParent(combo), WM_COMMAND, MAKEWPARAM(2100, CBN_SELCHANGE), (LPARAM)combo);
    SendMessageW(sheet, PSM_APPLY, 0, 0);
    for (attempt = 0; attempt < 100; attempt++)
    {
        bytes = sizeof(current); current[0] = 0;
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes",
                     L"CurrentTheme", RRF_RT_REG_SZ, NULL, current, &bytes);
        if (!lstrcmpiW(current, path)) break;
        Sleep(50);
    }
    CHECK(!lstrcmpiW(current, path), "Themes Apply updates active theme");
    bytes = sizeof(current);
    CHECK(!RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Colors", L"Background",
                       RRF_RT_REG_SZ, NULL, current, &bytes) && !lstrcmpW(current, L"12 34 56"),
          "applied theme color written to registry");
    CHECK(FindWindowW(L"Shell_TrayWnd", NULL) != NULL, "shell survives theme application");
    WritePrivateProfileStringW(L"Theme", L"DisplayName", L"Refactor renamed", path);
    SendMessageW(sheet, PSM_SETCURSEL, 1, 0);
    SendMessageW(sheet, PSM_SETCURSEL, 0, 0);
    choice = SendMessageW(combo, CB_FINDSTRINGEXACT, -1, (LPARAM)L"Refactor renamed");
    CHECK(choice != CB_ERR, "theme list refreshes after file changes");
    if (choice != CB_ERR) SendMessageW(combo, CB_GETLBTEXT, choice, (LPARAM)name);
done:
    if (sheet) PostMessageW(sheet, WM_CLOSE, 0, 0);
    fprintf(report, "COMPLETE: %d failures\n", failures);
    fclose(report);
    if (module) FreeLibrary(module);
    return failures != 0;
}
