/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 */
#include <initguid.h>
#include <shlobj.h>
#include <commoncontrols.h>
#include "module.h"
static HINSTANCE module_instance;
HINSTANCE wexsearch_module(void) { return module_instance; }
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        module_instance = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
