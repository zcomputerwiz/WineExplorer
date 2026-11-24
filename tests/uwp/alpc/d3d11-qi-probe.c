/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe: which interfaces does a Direct3D 11 device (hardware, BGRA support, as the compositor creates it) answer
 * for the interface ids that the Windows App SDK compositor asks for under Wine? For each id: the HRESULT of QueryInterface,
 * whether the pointer is the device itself, and the first slots of its vtable (as module+offset, to tell which object answers).
 * Also GetCreationFlags. Nothing of the system is copied; only the results of the calls are printed.
 * Build (mingw): x86_64-w64-mingw32-gcc -O2 -o d3d11-qi-probe.exe d3d11-qi-probe.c -ld3d11 -lole32
 */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <stdio.h>

static const struct { const char *name; GUID iid; } ids[] =
{
    { "f13ebcd1-672c-4f8b-a631-9539ca748d71", {0xf13ebcd1, 0x672c, 0x4f8b, {0xa6, 0x31, 0x95, 0x39, 0xca, 0x74, 0x8d, 0x71}} },
    { "26c5dc23-e49c-4b0a-8f79-e7b1ac804d32", {0x26c5dc23, 0xe49c, 0x4b0a, {0x8f, 0x79, 0xe7, 0xb1, 0xac, 0x80, 0x4d, 0x32}} },
    { "a44472e1 (ID2D1Device2)",              {0xa44472e1, 0x8dfb, 0x4e60, {0x84, 0x92, 0x6e, 0x28, 0x61, 0xc9, 0xca, 0x8b}} },
};

static void where(void *p, char *buf, size_t n)
{
    HMODULE m = NULL;
    char path[MAX_PATH], *s;

    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, p, &m) && m &&
        GetModuleFileNameA(m, path, sizeof(path)))
    {
        s = strrchr(path, '\\');
        snprintf(buf, n, "%s+%#llx", s ? s + 1 : path, (unsigned long long)((char *)p - (char *)m));
    }
    else snprintf(buf, n, "%p", p);
}

int main(void)
{
    ID3D11Device *device;
    D3D_FEATURE_LEVEL level;
    HRESULT hr;
    unsigned int i;

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION,
                           &device, &level, NULL);
    printf("D3D11CreateDevice: hr %08lx level %#x\n", hr, level);
    if (FAILED(hr)) return 1;
    printf("GetCreationFlags: %#x\n", ID3D11Device_GetCreationFlags(device));
    for (i = 0; i < ARRAYSIZE(ids); i++)
    {
        IUnknown *unk = NULL;
        char a[160], b[160], c[160];

        hr = ID3D11Device_QueryInterface(device, &ids[i].iid, (void **)&unk);
        printf("QI {%s}: hr %08lx", ids[i].name, hr);
        if (SUCCEEDED(hr) && unk)
        {
            void **vt = *(void ***)unk;
            where(vt, a, sizeof(a)); where(vt[3], b, sizeof(b)); where(vt[4], c, sizeof(c));
            printf(" %s the device; vtable %s, slot3 %s, slot4 %s", (void *)unk == (void *)device ? "is" : "is not", a, b, c);
            {
                /* the slots of the vtable, while they point into the module of slot 3 (the end of the vtable is where they stop) */
                HMODULE m3 = NULL, m = NULL;
                unsigned int n;

                GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, vt[3], &m3);
                for (n = 0; n < 64; n++)
                {
                    m = NULL;
                    if (IsBadReadPtr(&vt[n], sizeof(void *)) ||
                        !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, vt[n], &m) ||
                        m != m3) break;
                }
                printf("; %u slots", n);
            }
            IUnknown_Release(unk);
        }
        printf("\n");
    }
    ID3D11Device_Release(device);
    return 0;
}
