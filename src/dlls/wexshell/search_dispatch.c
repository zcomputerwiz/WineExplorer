/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 */
#include <windows.h>
#include "wexsearch.h"

/* Keep the module loaded: its window procedures and workers can outlive a call.
 * InitOnce also prevents two callers from retaining different module handles. */
static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
static HMODULE module;
static BOOL CALLBACK load_search(INIT_ONCE *init, void *parameter, void **context)
{
    module = LoadLibraryW(L"wexsearch.dll");
    return TRUE;
}
BOOL wexsearch_available(void)
{
    InitOnceExecuteOnce(&once, load_search, NULL, NULL);
    return module != NULL;
}

SEARCH_UI * search_ui_create(HWND parent, const WCHAR *scope, BOOL results)
{
    SEARCH_UI * (__cdecl *entry)(HWND parent, const WCHAR *scope, BOOL results);
    entry = wexsearch_available() ? (void *)GetProcAddress(module, "search_ui_create") : NULL;
    return entry ? entry(parent, scope, results) : NULL;
}

void search_ui_destroy(SEARCH_UI *ui)
{
    void (__cdecl *entry)(SEARCH_UI *ui);
    entry = wexsearch_available() ? (void *)GetProcAddress(module, "search_ui_destroy") : NULL;
    if (entry) entry(ui);
}

BOOL search_ui_layout(SEARCH_UI *ui, RECT *rect)
{
    BOOL (__cdecl *entry)(SEARCH_UI *ui, RECT *rect);
    entry = wexsearch_available() ? (void *)GetProcAddress(module, "search_ui_layout") : NULL;
    return entry ? entry(ui, rect) : FALSE;
}

BOOL search_ui_pane_visible(SEARCH_UI *ui)
{
    BOOL (__cdecl *entry)(SEARCH_UI *ui);
    entry = wexsearch_available() ? (void *)GetProcAddress(module, "search_ui_pane_visible") : NULL;
    return entry ? entry(ui) : FALSE;
}

BOOL search_ui_results_visible(SEARCH_UI *ui)
{
    BOOL (__cdecl *entry)(SEARCH_UI *ui);
    entry = wexsearch_available() ? (void *)GetProcAddress(module, "search_ui_results_visible") : NULL;
    return entry ? entry(ui) : FALSE;
}

int search_ui_result_count(SEARCH_UI *ui, BOOL selected)
{
    int (__cdecl *entry)(SEARCH_UI *ui, BOOL selected);
    entry = wexsearch_available() ? (void *)GetProcAddress(module, "search_ui_result_count") : NULL;
    return entry ? entry(ui, selected) : 0;
}

void search_ui_show_pane(SEARCH_UI *ui, BOOL show)
{
    void (__cdecl *entry)(SEARCH_UI *ui, BOOL show);
    entry = wexsearch_available() ? (void *)GetProcAddress(module, "search_ui_show_pane") : NULL;
    if (entry) entry(ui, show);
}

void search_ui_set_scope(SEARCH_UI *ui, const WCHAR *folder)
{
    void (__cdecl *entry)(SEARCH_UI *ui, const WCHAR *folder);
    entry = wexsearch_available() ? (void *)GetProcAddress(module, "search_ui_set_scope") : NULL;
    if (entry) entry(ui, folder);
}

BOOL search_ui_translate(SEARCH_UI *ui, MSG *msg)
{
    BOOL (__cdecl *entry)(SEARCH_UI *ui, MSG *msg);
    entry = wexsearch_available() ? (void *)GetProcAddress(module, "search_ui_translate") : NULL;
    return entry ? entry(ui, msg) : FALSE;
}

void instant_search_update(HWND edit, const WCHAR *text)
{
    void (__cdecl *entry)(HWND edit, const WCHAR *text);
    entry = wexsearch_available() ? (void *)GetProcAddress(module, "instant_search_update") : NULL;
    if (entry) entry(edit, text);
}

BOOL instant_search_key(UINT vk)
{
    BOOL (__cdecl *entry)(UINT vk);
    entry = wexsearch_available() ? (void *)GetProcAddress(module, "instant_search_key") : NULL;
    return entry ? entry(vk) : FALSE;
}

void instant_search_hide(void)
{
    void (__cdecl *entry)(void);
    entry = wexsearch_available() ? (void *)GetProcAddress(module, "instant_search_hide") : NULL;
    if (entry) entry();
}

BOOL instant_search_owns(HWND hwnd)
{
    BOOL (__cdecl *entry)(HWND hwnd);
    entry = wexsearch_available() ? (void *)GetProcAddress(module, "instant_search_owns") : NULL;
    return entry ? entry(hwnd) : FALSE;
}
