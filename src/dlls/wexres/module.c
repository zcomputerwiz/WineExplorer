/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 */
#include <initguid.h>
#include <windows.h>
#include <shlobj.h>
#include "wexres.h"

HINSTANCE resources_instance;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        resources_instance = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

HMODULE WINAPI WexResModule(void)
{
    return resources_instance;
}
