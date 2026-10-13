/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 */
#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <ole2.h>
#include <stdio.h>
#include <string.h>
#include "wexsearch.h"
static FILE *report;
static int failures;
#define CHECK(condition,label) do { BOOL ok = !!(condition); \
    fprintf(report,"%s %s\n",ok ? "PASS" : "FAIL",label); fflush(report); \
    if (!ok) failures++; } while (0)
static void pump(DWORD duration)
{
    DWORD until = GetTickCount() + duration;
    MSG msg;
    do
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(10);
    } while ((LONG)(until - GetTickCount()) > 0);
}
int WINAPI WinMain(HINSTANCE instance,HINSTANCE previous,char *command,int show)
{
    HMODULE module;
    HWND parent,pane,content,edit,flyout;
    SEARCH_UI *ui;
    RECT rect = {0,0,700,450};
    WCHAR folder[MAX_PATH],path[MAX_PATH],text[64];
    HANDLE file;
    unsigned int attempt;
    SEARCH_UI *(__cdecl *create)(HWND,const WCHAR *,BOOL);
    void (__cdecl *destroy)(SEARCH_UI *);
    BOOL (__cdecl *layout)(SEARCH_UI *,RECT *);
    BOOL (__cdecl *visible)(SEARCH_UI *);
    BOOL (__cdecl *results)(SEARCH_UI *);
    int (__cdecl *count)(SEARCH_UI *,BOOL);
    void (__cdecl *show_pane)(SEARCH_UI *,BOOL);
    void (__cdecl *scope)(SEARCH_UI *,const WCHAR *);
    void (__cdecl *update)(HWND,const WCHAR *);
    BOOL (__cdecl *owns)(HWND);
    BOOL (__cdecl *key)(UINT);
    void (__cdecl *hide)(void);
    report = fopen("Z:\\tmp\\search-module-probe.txt","w");
    if (!report) return 2;
    if (!strcmp(command,"present") || !strcmp(command,"absent"))
    {
        STARTUPINFOW startup = { sizeof(startup) };
        PROCESS_INFORMATION process;
        WCHAR args[] = L"explorer.exe ::{E17D4FC0-5564-11D1-83F2-00A0C90DC849}";
        HWND cabinet = NULL;
        CHECK(IsWindowVisible(FindWindowW(L"Shell_TrayWnd",NULL)),"desktop taskbar survives optional search availability");
        module = LoadLibraryW(L"wexsearch.dll");
        CHECK(!!module == !strcmp(command,"present"),"expected optional search DLL availability");
        if (module) FreeLibrary(module);
        if (CreateProcessW(NULL,args,NULL,NULL,FALSE,0,NULL,NULL,&startup,&process))
        {
            for (attempt=0; attempt<100; attempt++)
            {
                cabinet = FindWindowW(L"CabinetWClass",NULL);
                if ((cabinet && IsWindowVisible(cabinet)) ||
                    WaitForSingleObject(process.hProcess,0) == WAIT_OBJECT_0) break;
                Sleep(50);
            }
            CHECK(!!cabinet == !strcmp(command,"present"),"search request opens UI only when optional DLL is available");
            if (cabinet)
                CHECK(IsWindowVisible(FindWindowExW(cabinet,NULL,L"SearchResultsView",NULL)),
                      "shell search request displays the results view");
            if (cabinet) PostMessageW(cabinet,WM_CLOSE,0,0);
            CHECK(WaitForSingleObject(process.hProcess,10000) == WAIT_OBJECT_0,"search request exits cleanly after close or unavailable feature");
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        }
        else CHECK(FALSE,"launch shell Search Results request");
        goto done;
    }
    CoInitialize(NULL);
    InitCommonControls();
    module = LoadLibraryW(L"wexsearch.dll");
    CHECK(module != NULL,"standalone search DLL loads");
    if (!module) goto done;
#define LOAD(name,type) name = (void *)GetProcAddress(module,type)
    LOAD(create,"search_ui_create"); LOAD(destroy,"search_ui_destroy"); LOAD(layout,"search_ui_layout");
    LOAD(visible,"search_ui_pane_visible"); LOAD(results,"search_ui_results_visible");
    LOAD(count,"search_ui_result_count"); LOAD(show_pane,"search_ui_show_pane"); LOAD(scope,"search_ui_set_scope");
    LOAD(update,"instant_search_update"); LOAD(owns,"instant_search_owns"); LOAD(key,"instant_search_key");
    LOAD(hide,"instant_search_hide");
    CHECK(create && destroy && layout && visible && results && count && show_pane && scope &&
          update && owns && key && hide,"public search interface complete");
    if (!(create && destroy && layout && visible && results && count && show_pane && scope &&
          update && owns && key && hide)) goto done;
    GetTempPathW(MAX_PATH,folder);
    lstrcatW(folder,L"WexSearchFixture");
    CreateDirectoryW(folder,NULL);
    swprintf(path,MAX_PATH,L"%ls\\wex-unique-result.txt",folder);
    file = CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);
    CHECK(file != INVALID_HANDLE_VALUE,"search fixture file created");
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    parent = CreateWindowW(L"STATIC",L"Search module probe",WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                          20,20,720,500,NULL,NULL,instance,NULL);
    ui = create(parent,folder,TRUE);
    CHECK(ui && visible(ui) && results(ui),"standalone pane and results visible");
    if (!ui) goto cleanup;
    layout(ui,&rect);
    CHECK(rect.left == rect.right,"results consume remaining client area");
    pane = FindWindowExW(parent,NULL,L"SearchCompanionPane",NULL);
    content = FindWindowExW(pane,NULL,L"SearchCompanionContent",NULL);
    CHECK(pane && (HMODULE)GetWindowLongPtrW(pane,GWLP_HINSTANCE) == module,"pane class belongs to feature DLL");
    CHECK(FindResourceW(module,MAKEINTRESOURCEW(1036),MAKEINTRESOURCEW(2)) != NULL,"Rover bitmap belongs to feature DLL");
    SetWindowTextW(GetDlgItem(content,203),L"wex-unique-result");
    SendMessageW(pane,WM_COMMAND,201,0);
    for (attempt=0; attempt<300 && count(ui,FALSE) != 1; attempt++) pump(50);
    CHECK(count(ui,FALSE) == 1,"asynchronous file search returns exactly the matching fixture");
    show_pane(ui,FALSE);
    CHECK(!visible(ui) && results(ui),"hiding companion retains results");
    show_pane(ui,TRUE);
    scope(ui,folder);
    CHECK(visible(ui),"pane reopens and changes scope");
    SendMessageW(pane,WM_COMMAND,201,0);
    destroy(ui);
    CHECK(!IsWindow(pane),"destroy during search stops workers and destroys UI");
    edit = CreateWindowW(L"EDIT",NULL,WS_CHILD | WS_VISIBLE,0,440,100,20,parent,NULL,instance,NULL);
    SetWindowTextW(edit,L"wex-unique-result");
    update(edit,L"wex-unique-result");
    pump(200);
    flyout = FindWindowW(L"WineInstantSearch",NULL);
    CHECK(flyout && IsWindowVisible(flyout) && owns(flyout),"instant search creates owned flyout");
    CHECK((HMODULE)GetWindowLongPtrW(flyout,GWLP_HINSTANCE) == module,"flyout class belongs to feature DLL");
    CHECK(key(VK_DOWN) && key(VK_ESCAPE),"instant navigation and Escape handled");
    GetWindowTextW(edit,text,64);
    CHECK(!text[0] && !IsWindowVisible(flyout),"Escape clears edit and closes results");
    update(edit,L"another-query");
    hide();
    pump(100);
    CHECK(!IsWindowVisible(flyout) && !key(VK_DOWN),"hide cancels worker and releases keyboard handling");
    CHECK(GetModuleHandleW(L"wexshell.dll") == NULL,"standalone feature has no core shell dependency");
cleanup:
    DestroyWindow(parent);
    DeleteFileW(path);
    RemoveDirectoryW(folder);
    CoUninitialize();
    /* The flyout class lives for the process lifetime, as in its shell caller. */
done:
    fprintf(report,"COMPLETE: %d failures\n",failures);
    fclose(report);
    return failures != 0;
}
