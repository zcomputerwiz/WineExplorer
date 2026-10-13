/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Registered IOleCommandTarget services, using XP's NEW/SAVE lifecycle.
 */
#define COBJMACROS
#include <stdlib.h>
#include <windows.h>
#include <ole2.h>
#include <docobj.h>
#include <shlguid.h>
#include "wine/debug.h"
#include "shellservices.h"

WINE_DEFAULT_DEBUG_CHANNEL(explorer);

struct shell_service
{
    IOleCommandTarget *target;
    struct shell_service *next;
};
static struct shell_service *services;
static BOOL initialized, com_initialized;

void shellservices_start(void)
{
    static const WCHAR key_name[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\ShellServiceObjectDelayLoad";
    HKEY key;
    DWORD index;
    HRESULT hr;

    if (initialized) return;
    initialized = TRUE;
    hr = CoInitialize(NULL);
    if (FAILED(hr))
    {
        WARN("Cannot initialize shell services: %#lx\n", hr);
        return;
    }
    com_initialized = TRUE;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key_name, 0, KEY_QUERY_VALUE, &key)) return;
    for (index = 0; ; index++)
    {
        WCHAR name[256], value[128];
        DWORD name_len = ARRAY_SIZE(name), size = sizeof(value), type;
        struct shell_service *service;
        IOleCommandTarget *target;
        CLSID clsid;
        LONG ret;

        ret = RegEnumValueW(key, index, name, &name_len, NULL, &type, (BYTE *)value, &size);
        if (ret == ERROR_NO_MORE_ITEMS) break;
        if (ret && ret != ERROR_MORE_DATA) break;
        if (ret || type != REG_SZ || size < sizeof(WCHAR) || size > sizeof(value) ||
            size % sizeof(WCHAR) || value[size / sizeof(WCHAR) - 1]) continue;
        if (FAILED(CLSIDFromString(value, &clsid))) continue;
        hr = CoCreateInstance(&clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IOleCommandTarget, (void **)&target);
        if (FAILED(hr))
        {
            WARN("Skipping shell service %s: %#lx\n", debugstr_w(name), hr);
            continue;
        }
        if (!(service = malloc(sizeof(*service))))
        {
            IOleCommandTarget_Release(target);
            continue;
        }
        hr = IOleCommandTarget_Exec(target, &CGID_ShellServiceObject, OLECMDID_NEW,
                                   OLECMDEXECOPT_DODEFAULT, NULL, NULL);
        if (FAILED(hr))
        {
            IOleCommandTarget_Release(target);
            free(service);
            continue;
        }
        service->target = target;
        service->next = services;
        services = service;
    }
    RegCloseKey(key);
}

void shellservices_stop(void)
{
    while (services)
    {
        struct shell_service *service = services;
        services = service->next;
        IOleCommandTarget_Exec(service->target, &CGID_ShellServiceObject, OLECMDID_SAVE,
                              OLECMDEXECOPT_DODEFAULT, NULL, NULL);
        IOleCommandTarget_Release(service->target);
        free(service);
    }
    if (com_initialized) CoUninitialize();
    com_initialized = initialized = FALSE;
}
