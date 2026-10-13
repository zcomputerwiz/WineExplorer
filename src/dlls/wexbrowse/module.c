/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 */
#include "module.h"
static HINSTANCE instance;
HINSTANCE wexbrowse_module(void) { return instance; }
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        instance = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
