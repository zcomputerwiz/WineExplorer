/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe: what IDXGIFactory2::RegisterOcclusionStatusEvent returns (cookies, NULL arguments, whether the event is set), and
 * the D3DKMT_ADAPTERTYPE flags of D3DKMTQueryAdapterInfo(KMTQAITYPE_ADAPTERTYPE) for each adapter. The compositor of the Windows App
 * SDK asks for both before it renders. Only results of documented calls are printed.
 * Build (mingw): x86_64-w64-mingw32-gcc -O2 -o occlusion-kmt-probe.exe occlusion-kmt-probe.c -ldxgi -lgdi32 -luuid
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <dxgi1_2.h>
#include <stdio.h>

/* the documented D3DKMT declarations this probe needs (MinGW has no d3dkmthk.h); the functions are exports of gdi32 */
typedef UINT D3DKMT_HANDLE;
typedef struct { LUID AdapterLuid; D3DKMT_HANDLE hAdapter; } D3DKMT_OPENADAPTERFROMLUID;
typedef struct { D3DKMT_HANDLE hAdapter; UINT Type; void *pPrivateDriverData; UINT PrivateDriverDataSize; } D3DKMT_QUERYADAPTERINFO;
typedef struct { D3DKMT_HANDLE hAdapter; } D3DKMT_CLOSEADAPTER;
typedef struct { UINT Value; } D3DKMT_ADAPTERTYPE;
#define KMTQAITYPE_ADAPTERTYPE 15
static LONG (WINAPI *pD3DKMTOpenAdapterFromLuid)(D3DKMT_OPENADAPTERFROMLUID *);
static LONG (WINAPI *pD3DKMTQueryAdapterInfo)(D3DKMT_QUERYADAPTERINFO *);
static LONG (WINAPI *pD3DKMTCloseAdapter)(D3DKMT_CLOSEADAPTER *);
#define D3DKMTOpenAdapterFromLuid pD3DKMTOpenAdapterFromLuid
#define D3DKMTQueryAdapterInfo pD3DKMTQueryAdapterInfo
#define D3DKMTCloseAdapter pD3DKMTCloseAdapter

int main(void)
{
    IDXGIFactory2 *factory;
    HANDLE event = CreateEventW(NULL, FALSE, FALSE, NULL), event2 = CreateEventW(NULL, FALSE, FALSE, NULL);
    DWORD cookie = 0xdead, cookie2 = 0xdead;
    IDXGIAdapter1 *adapter;
    HRESULT hr;
    UINT i;

    setvbuf(stdout, NULL, _IONBF, 0);
    {
        HMODULE gdi = LoadLibraryW(L"gdi32.dll");
        pD3DKMTOpenAdapterFromLuid = (void *)GetProcAddress(gdi, "D3DKMTOpenAdapterFromLuid");
        pD3DKMTQueryAdapterInfo = (void *)GetProcAddress(gdi, "D3DKMTQueryAdapterInfo");
        pD3DKMTCloseAdapter = (void *)GetProcAddress(gdi, "D3DKMTCloseAdapter");
    }
    hr = CreateDXGIFactory1(&IID_IDXGIFactory2, (void **)&factory);
    printf("CreateDXGIFactory1: %08lx\n", hr);
    if (FAILED(hr)) return 1;

    hr = IDXGIFactory2_RegisterOcclusionStatusEvent(factory, event, &cookie);
    printf("RegisterOcclusionStatusEvent: hr %08lx cookie %#lx, event set after 200 ms: %s\n", hr, cookie,
           WaitForSingleObject(event, 200) == WAIT_OBJECT_0 ? "yes" : "no");
    hr = IDXGIFactory2_RegisterOcclusionStatusEvent(factory, event2, &cookie2);
    printf("second registration: hr %08lx cookie %#lx\n", hr, cookie2);
    hr = IDXGIFactory2_RegisterOcclusionStatusEvent(factory, event, &cookie2);
    printf("the same event again: hr %08lx cookie %#lx\n", hr, cookie2);
    cookie2 = 0xdead;
    hr = IDXGIFactory2_RegisterOcclusionStatusEvent(factory, NULL, &cookie2);
    printf("NULL event: hr %08lx cookie %#lx\n", hr, cookie2);
    hr = IDXGIFactory2_RegisterOcclusionStatusEvent(factory, event, NULL);
    printf("NULL cookie: hr %08lx\n", hr);
    IDXGIFactory2_UnregisterOcclusionStatus(factory, cookie);
    IDXGIFactory2_UnregisterOcclusionStatus(factory, 0x12345);
    printf("Unregister of a known and an unknown cookie returned\n");
    hr = IDXGIFactory2_RegisterOcclusionStatusEvent(factory, event, &cookie2);
    printf("after unregistering: hr %08lx cookie %#lx\n", hr, cookie2);

    for (i = 0; SUCCEEDED(IDXGIFactory2_EnumAdapters1(factory, i, &adapter)); i++)
    {
        DXGI_ADAPTER_DESC1 desc;
        D3DKMT_OPENADAPTERFROMLUID open = {0};
        D3DKMT_QUERYADAPTERINFO query = {0};
        D3DKMT_ADAPTERTYPE type = {0};
        D3DKMT_CLOSEADAPTER close = {0};
        NTSTATUS status;

        IDXGIAdapter1_GetDesc1(adapter, &desc);
        open.AdapterLuid = desc.AdapterLuid;
        status = D3DKMTOpenAdapterFromLuid(&open);
        printf("adapter %u (%ls, flags %#x): open %08lx", i, desc.Description, desc.Flags, status);
        if (!status)
        {
            query.hAdapter = open.hAdapter;
            query.Type = KMTQAITYPE_ADAPTERTYPE;
            query.pPrivateDriverData = &type;
            query.PrivateDriverDataSize = sizeof(type);
            status = D3DKMTQueryAdapterInfo(&query);
            printf(", ADAPTERTYPE %08lx value %#x (size %u)", status, type.Value, (unsigned)sizeof(type));
            query.PrivateDriverDataSize = sizeof(type) - 1;
            status = D3DKMTQueryAdapterInfo(&query);
            printf(", short buffer %08lx", status);
            close.hAdapter = open.hAdapter;
            D3DKMTCloseAdapter(&close);
        }
        printf("\n");
        IDXGIAdapter1_Release(adapter);
    }
    return 0;
}
