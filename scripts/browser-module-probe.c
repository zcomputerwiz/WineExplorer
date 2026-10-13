/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 */
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>
#include "wexshell.h"
#include "wexbrowse.h"
static FILE *report;
static int failures;
static HMODULE module;
static BOOL toolbar_found, status_found, pane_found;
#define CHECK(condition,label) do { BOOL ok = !!(condition); \
    fprintf(report,"%s %s\n",ok ? "PASS" : "FAIL",label); fflush(report); \
    if (!ok) failures++; } while (0)
static BOOL CALLBACK inspect(HWND hwnd, LPARAM unused)
{
    WCHAR name[128];
    GetClassNameW(hwnd,name,128);
    if (!wcscmp(name,L"ToolbarWindow32") && IsWindowVisible(hwnd)) toolbar_found = TRUE;
    if (!wcscmp(name,L"msctls_statusbar32") && IsWindowVisible(hwnd)) status_found = TRUE;
    if (!wcscmp(name,L"BrowserTaskPane") && IsWindowVisible(hwnd)) pane_found = TRUE;
    return TRUE;
}
static DWORD WINAPI control_browser(void *unused)
{
    HWND window = NULL, pane;
    unsigned int attempt;
    RECT rect;
    for (attempt=0; attempt<300; attempt++)
    {
        window = FindWindowW(L"CabinetWClass",NULL);
        if (window && IsWindowVisible(window)) break;
        Sleep(50);
    }
    CHECK(window && IsWindowVisible(window),"browser window visible");
    if (!window) return 0;
    CHECK((HMODULE)GetWindowLongPtrW(window,GWLP_HINSTANCE) == module,"browser class owned by feature DLL");
    GetWindowRect(window,&rect);
    CHECK(rect.left >= 0 && rect.top >= 0 && rect.right <= 800 && rect.bottom <= 600,"browser fits display");
    toolbar_found=status_found=pane_found=FALSE;
    EnumChildWindows(window,inspect,0);
    CHECK(toolbar_found && status_found && pane_found,"toolbar, status bar and folder task pane visible");
    CHECK(SendMessageW(window,WM_GETICON,ICON_SMALL,0) != 0,"folder window supplies a taskband icon");
    SendMessageW(window,WM_COMMAND,103,0); /* Search button */
    pane=FindWindowExW(window,NULL,L"SearchCompanionPane",NULL);
    CHECK(pane && IsWindowVisible(pane),"browser opens optional Search Companion");
    if (pane)
    {
        CHECK((HMODULE)GetWindowLongPtrW(pane,GWLP_HINSTANCE) == GetModuleHandleW(L"wexsearch.dll"),
              "browser uses search DLL without sharing browser internals");
        SendMessageW(window,WM_COMMAND,103,0);
        CHECK(!IsWindowVisible(pane),"browser toggles search pane closed");
    }
    PostMessageW(window,WM_CLOSE,0,0);
    return 0;
}
int WINAPI WinMain(HINSTANCE instance,HINSTANCE previous,char *command,int show)
{
    BOOL (WINAPI *open)(const WCHAR *,const WCHAR *,BOOL,int);
    WCHAR path[MAX_PATH];
    HANDLE worker;
    unsigned int pass;
    report=fopen("Z:\\tmp\\browser-module-probe.txt","w");
    if (!report) return 2;
    if (!strcmp(command,"present") || !strcmp(command,"absent"))
    {
        BOOL expected = !strcmp(command,"present");
        WEX_SHELL_COMMAND dispatch;
        CHECK(IsWindowVisible(FindWindowW(L"Shell_TrayWnd",NULL)),"desktop survives optional browser availability");
        module=LoadLibraryW(L"wexbrowse.dll");
        CHECK(!!module == expected,"expected optional browser DLL availability");
        if (!expected)
        {
            WCHAR args[]=L"/e,C:\\";
            HMODULE shell=LoadLibraryW(L"wexshell.dll");
            dispatch=shell ? (void *)GetProcAddress(shell,"WexShellCommand") : NULL;
            CHECK(dispatch && !dispatch(WEX_SHELL_VERSION,args,SW_SHOW),
                  "unavailable browser returns command to stock host");
            CHECK(!FindWindowW(L"CabinetWClass",NULL),"unavailable feature leaves no empty XP browser");
        }
        goto done;
    }
    module=LoadLibraryW(L"wexbrowse.dll");
    open=module ? (void *)GetProcAddress(module,"WexBrowseOpen") : NULL;
    CHECK(open != NULL,"standalone browser export available");
    if (!open) goto done;
    GetTempPathW(MAX_PATH,path);
    lstrcatW(path,L"WexBrowseFixture");
    CreateDirectoryW(path,NULL);
    for (pass=0; pass<3; pass++)
    {
        const WCHAR *root=pass == 0 ? path : pass == 1 ?
            L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}" : L"::{450D8FBA-AD25-11D0-98A8-0800361B1103}";
        worker=CreateThread(NULL,0,control_browser,NULL,0,NULL);
        CHECK(open(root,NULL,TRUE,SW_SHOW),"folder/browser call returns after close");
        CHECK(WaitForSingleObject(worker,15000) == WAIT_OBJECT_0,"browser controller completes");
        CloseHandle(worker);
        CHECK(!FindWindowW(L"CabinetWClass",NULL),"closing browser destroys its window");
    }
    RemoveDirectoryW(path);
    CHECK(GetModuleHandleW(L"wexshell.dll") == NULL,"standalone browser has no core shell dependency");
done:
    fprintf(report,"COMPLETE: %d failures\n",failures);
    fclose(report);
    return failures != 0;
}
