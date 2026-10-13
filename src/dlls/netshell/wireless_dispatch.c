/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 */
#include <windows.h>
#include "wlan_dialog.h"

void wlan_dialog_show(HWND parent)
{
    HMODULE module = LoadLibraryW(L"wzcdlg.dll");
    void (__cdecl *show)(HWND);
    /* The shell export preserves the original implementation during the next
     * module's extraction, including the owner and the calling process. */
    if (!module) module = LoadLibraryW(L"wexshell.dll");
    show = module ? (void *)GetProcAddress(module, "wlan_dialog_show") : NULL;
    if (show) show(parent);
    if (module) FreeLibrary(module);
}
