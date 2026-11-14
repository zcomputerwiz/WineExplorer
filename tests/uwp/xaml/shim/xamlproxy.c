/*
 * XAML experiment step 2b: native activation proxy. DllGetActivationFactory for Windows.UI.Xaml classes starts CoreCLR
 * once per process through hostfxr (the component directory holds a self-contained .NET application, XamlShim.dll)
 * and forwards the class name to the managed XamlShim.Activation.GetActivationFactory. This is the piece that later
 * moves into Wine's windows.ui.xaml.dll as a fallback for classes it does not implement itself.
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
 * The component directory: the value Path of HKLM\Software\WineExplorer\XamlComponent, else the directory of this DLL.
 * Log: %TEMP%\xamlshim.txt. The DLL is named xamlproxy.dll: the component directory already holds the managed
 * XamlShim.dll, and file names are case-insensitive.
 */

#include <windows.h>
#include <stdio.h>
#include <roapi.h>
#include <winstring.h>

typedef void *hostfxr_handle;
struct hostfxr_initialize_parameters
{
    size_t size;
    const WCHAR *host_path;     /* the component's own apphost: hostfxr takes the application root from it */
    const WCHAR *dotnet_root;
};
typedef int (__cdecl *init_cmdline_fn)(int argc, const WCHAR **argv, const struct hostfxr_initialize_parameters *params,
                                       hostfxr_handle *handle);
typedef int (__cdecl *get_delegate_fn)(hostfxr_handle handle, int type, void **delegate);
typedef int (__stdcall *get_function_pointer_fn)(const WCHAR *type, const WCHAR *method, const WCHAR *delegate_type,
                                                 void *load_context, void *reserved, void **delegate);
typedef HRESULT (__stdcall *get_factory_fn)(const WCHAR *name, void **factory);

#define HDT_GET_FUNCTION_POINTER 6
#define UNMANAGEDCALLERSONLY_METHOD ((const WCHAR *)-1)

static HMODULE self;
static INIT_ONCE start_once = INIT_ONCE_STATIC_INIT;
static get_factory_fn get_factory;
static HRESULT start_hr = E_FAIL;

static void log_line(const char *format, ...)
{
    char path[MAX_PATH];
    va_list args;
    FILE *f;

    GetTempPathA(MAX_PATH, path);
    strcat(path, "xamlshim.txt");
    if (!(f = fopen(path, "a"))) return;
    fprintf(f, "%lu: ", GetCurrentThreadId());
    va_start(args, format);
    vfprintf(f, format, args);
    va_end(args);
    fputc('\n', f);
    fclose(f);
}

static BOOL component_dir(WCHAR *dir, DWORD size)
{
    DWORD bytes = size * sizeof(WCHAR);
    WCHAR *slash;

    if (!RegGetValueW(HKEY_LOCAL_MACHINE, L"Software\\WineExplorer\\XamlComponent", L"Path", RRF_RT_REG_SZ, NULL,
                      dir, &bytes))
        return TRUE;
    if (!GetModuleFileNameW(self, dir, size)) return FALSE;
    if ((slash = wcsrchr(dir, '\\'))) *slash = 0;
    return TRUE;
}

static BOOL CALLBACK start_runtime(INIT_ONCE *once, void *param, void **context)
{
    WCHAR dir[MAX_PATH], fxr[MAX_PATH], app[MAX_PATH], host[MAX_PATH];
    struct hostfxr_initialize_parameters params;
    get_function_pointer_fn get_fp = NULL;
    hostfxr_handle handle = NULL;
    const WCHAR *argv[1];
    init_cmdline_fn init;
    get_delegate_fn get_delegate;
    HMODULE module;
    DWORD start = GetTickCount();
    int rc;

    if (!component_dir(dir, MAX_PATH)) { log_line("no component directory"); return TRUE; }
    swprintf(fxr, MAX_PATH, L"%s\\hostfxr.dll", dir);
    swprintf(app, MAX_PATH, L"%s\\XamlShim.dll", dir);
    if (!(module = LoadLibraryW(fxr))) { log_line("hostfxr load error %lu", GetLastError()); return TRUE; }
    init = (init_cmdline_fn)GetProcAddress(module, "hostfxr_initialize_for_dotnet_command_line");
    get_delegate = (get_delegate_fn)GetProcAddress(module, "hostfxr_get_runtime_delegate");
    if (!init || !get_delegate) { log_line("hostfxr exports missing"); return TRUE; }
    swprintf(host, MAX_PATH, L"%s\\XamlShim.exe", dir);
    params.size = sizeof(params);
    params.host_path = host;
    params.dotnet_root = dir;
    argv[0] = app;
    if ((rc = init(1, argv, &params, &handle)) < 0 || !handle) { log_line("hostfxr init %#x", rc); return TRUE; }
    if ((rc = get_delegate(handle, HDT_GET_FUNCTION_POINTER, (void **)&get_fp)) < 0 || !get_fp)
    {
        log_line("hostfxr delegate %#x", rc);
        return TRUE;
    }
    rc = get_fp(L"XamlShim.Activation, XamlShim", L"GetActivationFactory", UNMANAGEDCALLERSONLY_METHOD, NULL, NULL,
                (void **)&get_factory);
    log_line("runtime started: function %#x, %lu ms", rc, GetTickCount() - start);
    if (rc >= 0 && get_factory) start_hr = S_OK;
    return TRUE;
}

HRESULT WINAPI DllGetActivationFactory(HSTRING classid, IActivationFactory **factory)
{
    const WCHAR *name = WindowsGetStringRawBuffer(classid, NULL);
    HRESULT hr;

    *factory = NULL;
    InitOnceExecuteOnce(&start_once, start_runtime, NULL, NULL);
    if (FAILED(start_hr)) return CLASS_E_CLASSNOTAVAILABLE;
    hr = get_factory(name, (void **)factory);
    log_line("factory %ls: %#lx", name, hr);
    return hr;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        self = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
