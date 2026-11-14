/*
 * XAML experiment: load xamlshim.dll from the published shim directory and ask it for one activation factory.
 *
 * Copyright 2026 WineExplorer contributors
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * usage: shimtest.exe <path to xamlshim.dll> <class name>
 */

#include <windows.h>
#include <stdio.h>
#include <roapi.h>
#include <winstring.h>

typedef HRESULT (WINAPI *get_factory_fn)(HSTRING, void **);

int wmain(int argc, WCHAR **argv)
{
    get_factory_fn get;
    HMODULE module;
    HSTRING name;
    void *factory = NULL;
    HRESULT hr;

    if (argc < 3) return 2;
    RoInitialize(RO_INIT_MULTITHREADED);
    if (!(module = LoadLibraryW(argv[1]))) { printf("load error %lu\n", GetLastError()); return 3; }
    get = (get_factory_fn)GetProcAddress(module, "DllGetActivationFactory");
    WindowsCreateString(argv[2], (UINT32)wcslen(argv[2]), &name);
    hr = get(name, &factory);
    printf("DllGetActivationFactory %ls: %#lx %p\n", argv[2], hr, factory);
    return FAILED(hr);
}
