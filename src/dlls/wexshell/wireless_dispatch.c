/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 */
#include <windows.h>
#include "wexwlan.h"

void wlan_dialog_show(HWND parent)
{
    HMODULE module = LoadLibraryW(L"wzcdlg.dll");
    void (__cdecl *show)(HWND);
    show = module ? (void *)GetProcAddress(module, "wlan_dialog_show") : NULL;
    if (show) show(parent);
    if (module) FreeLibrary(module);
}
