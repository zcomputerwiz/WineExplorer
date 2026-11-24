/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe: can a plain desktop process get the activation factory of Windows.UI.Internal.Input.InputSite (InputHost.dll),
 * which the Windows App SDK asks for on its input thread, for the interface it requests ({8ca0442d-...}) and for IActivationFactory?
 * Prints the HRESULTs, and for an object the module+offset of its vtable and how many slots point into the same module.
 * Build (mingw): x86_64-w64-mingw32-gcc -O2 -o inputsite-probe.exe inputsite-probe.c -lole32 -lcombase -luuid
 */
#define COBJMACROS
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <stdio.h>

static const GUID iid_requested = {0x8ca0442d, 0xae80, 0x47b0, {0xaf, 0x23, 0x81, 0x3f, 0xc1, 0xda, 0x9c, 0xd0}};
static const GUID iid_activation_factory = {0x00000035, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

static void show(const char *what, HRESULT hr, void *obj)
{
    char path[MAX_PATH], *s;
    HMODULE m = NULL, m0 = NULL;
    unsigned int n;

    printf("%s: hr %08lx", what, hr);
    if (SUCCEEDED(hr) && obj)
    {
        void **vt = *(void ***)obj;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)vt[0], &m0);
        GetModuleFileNameA(m0, path, sizeof(path));
        s = strrchr(path, '\\');
        for (n = 0; n < 64; n++)
        {
            m = NULL;
            if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    (LPCSTR)vt[n], &m) || m != m0) break;
        }
        printf(", vtable in %s, %u slots in that module", s ? s + 1 : path, n);
    }
    printf("\n");
}

int main(void)
{
    static const WCHAR name[] = L"Windows.UI.Internal.Input.InputSite";
    HSTRING hs;
    void *obj = NULL;
    HRESULT hr;
    int mode;

    setvbuf(stdout, NULL, _IONBF, 0);
    for (mode = 0; mode < 2; mode++)
    {
        hr = RoInitialize(mode ? RO_INIT_SINGLETHREADED : RO_INIT_MULTITHREADED);
        printf("RoInitialize(%s): %08lx\n", mode ? "STA" : "MTA", hr);
        WindowsCreateString(name, (UINT32)wcslen(name), &hs);
        obj = NULL;
        hr = RoGetActivationFactory(hs, &iid_requested, &obj);
        show("factory for {8ca0442d-...}", hr, obj);
        obj = NULL;
        hr = RoGetActivationFactory(hs, &iid_activation_factory, &obj);
        show("factory for IActivationFactory", hr, obj);
        WindowsDeleteString(hs);
        RoUninitialize();
    }
    return 0;
}
