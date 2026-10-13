/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 */
#include <initguid.h>
#include <shlobj.h>
#include <commoncontrols.h>
#include "explorer_private.h"
#include "module.h"

HINSTANCE explorer_hInstance;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        explorer_hInstance = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
