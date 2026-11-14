/*
 * XAML experiment step 2a: a native process hosts CoreCLR through hostfxr and asks Uno (managed Shim.Start in the
 * published UnoSpike) to show a piece of XAML text, the way a Windows.UI.Xaml shim inside a UWP process would.
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
 * usage: xamlhost.exe   (placed in the published UnoSpike directory; logs to %TEMP%\uno-spike.txt)
 */

#include <windows.h>
#include <ole2.h>
#include <stdio.h>

typedef void *hostfxr_handle;
typedef int (__cdecl *init_cmdline_fn)(int argc, const WCHAR **argv, const void *params, hostfxr_handle *handle);
typedef int (__cdecl *get_delegate_fn)(hostfxr_handle handle, int type, void **delegate);
typedef int (__cdecl *close_fn)(hostfxr_handle handle);
typedef int (__stdcall *get_function_pointer_fn)(const WCHAR *type, const WCHAR *method, const WCHAR *delegate_type,
                                                 void *load_context, void *reserved, void **delegate);
typedef int (__stdcall *start_fn)(const WCHAR *xaml);

#define HDT_GET_FUNCTION_POINTER 6
#define UNMANAGEDCALLERSONLY_METHOD ((const WCHAR *)-1)

static const WCHAR xaml[] =
    L"<StackPanel xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" "
    L"xmlns:x=\"http://schemas.microsoft.com/winfx/2006/xaml\" Padding=\"24\" Spacing=\"12\">"
    L"<TextBlock x:Name=\"Title\" Text=\"Hello from a native host\" FontSize=\"20\"/>"
    L"<Button x:Name=\"Go\" Content=\"Press me\"/>"
    L"</StackPanel>";

static void log_line(const char *line)
{
    char path[MAX_PATH];
    FILE *f;

    GetTempPathA(MAX_PATH, path);
    strcat(path, "uno-spike.txt");
    if ((f = fopen(path, "a"))) { fprintf(f, "%s\n", line); fclose(f); }
}

int wmain(void)
{
    WCHAR dir[MAX_PATH], fxr[MAX_PATH], app[MAX_PATH], *slash;
    const WCHAR *argv[1];
    init_cmdline_fn init;
    get_delegate_fn get_delegate;
    close_fn close_ctx;
    get_function_pointer_fn get_fp = NULL;
    start_fn start = NULL;
    hostfxr_handle handle = NULL;
    HMODULE module;
    char line[256];
    int rc;

    GetModuleFileNameW(NULL, dir, MAX_PATH);
    if ((slash = wcsrchr(dir, '\\'))) *slash = 0;
    swprintf(fxr, MAX_PATH, L"%s\\hostfxr.dll", dir);
    swprintf(app, MAX_PATH, L"%s\\UnoSpike.dll", dir);

    sprintf(line, "host start pid %lu ptr %u", GetCurrentProcessId(), (unsigned)sizeof(void *) * 8);
    log_line(line);
    if (!(module = LoadLibraryW(fxr)))
    {
        sprintf(line, "host hostfxr load error %lu", GetLastError());
        log_line(line);
        return 2;
    }
    init = (init_cmdline_fn)GetProcAddress(module, "hostfxr_initialize_for_dotnet_command_line");
    get_delegate = (get_delegate_fn)GetProcAddress(module, "hostfxr_get_runtime_delegate");
    close_ctx = (close_fn)GetProcAddress(module, "hostfxr_close");
    if (!init || !get_delegate || !close_ctx) { log_line("host hostfxr exports missing"); return 3; }

    argv[0] = app;
    rc = init(1, argv, NULL, &handle);
    sprintf(line, "host init %#x", rc);
    log_line(line);
    if (rc < 0 || !handle) return 4;
    rc = get_delegate(handle, HDT_GET_FUNCTION_POINTER, (void **)&get_fp);
    sprintf(line, "host delegate %#x", rc);
    log_line(line);
    if (rc < 0 || !get_fp) return 5;
    rc = get_fp(L"UnoSpike.Shim, UnoSpike", L"Start", UNMANAGEDCALLERSONLY_METHOD, NULL, NULL, (void **)&start);
    sprintf(line, "host function %#x", rc);
    log_line(line);
    if (rc < 0 || !start) return 6;

    OleInitialize(NULL);
    rc = start(xaml);
    sprintf(line, "host shim returned %d", rc);
    log_line(line);
    OleUninitialize();
    close_ctx(handle);
    return rc;
}
