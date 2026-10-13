/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 */
#define COBJMACROS
#include <stdlib.h>
#include <initguid.h>
#include <windows.h>
#include <ole2.h>
#include <docobj.h>
#include <shlguid.h>
#include "nettray.h"
static const CLSID clsid_connection_tray =
    {0x7007accf,0x3202,0x11d1,{0xaa,0xd2,0x00,0x80,0x5f,0xc1,0x27,0x0e}};
static LONG objects, locks, users, factory_refs;
static DWORD service_thread;

struct network_service
{
    IOleCommandTarget IOleCommandTarget_iface;
    LONG refs;
    BOOL started;
};

static struct network_service *impl(IOleCommandTarget *iface)
{
    return CONTAINING_RECORD(iface, struct network_service, IOleCommandTarget_iface);
}

static HRESULT stop_service(struct network_service *service)
{
    if (!service->started) return S_OK;
    if (service_thread != GetCurrentThreadId()) return RPC_E_WRONG_THREAD;
    service->started = FALSE;
    if (!--users)
    {
        nettray_shutdown();
        service_thread = 0;
    }
    return S_OK;
}

static HRESULT WINAPI service_query(IOleCommandTarget *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualGUID(iid, &IID_IUnknown) && !IsEqualGUID(iid, &IID_IOleCommandTarget)) return E_NOINTERFACE;
    *out = iface;
    IOleCommandTarget_AddRef(iface);
    return S_OK;
}
static ULONG WINAPI service_addref(IOleCommandTarget *iface)
{
    return InterlockedIncrement(&impl(iface)->refs);
}
static ULONG WINAPI service_release(IOleCommandTarget *iface)
{
    struct network_service *service = impl(iface);
    ULONG refs = InterlockedDecrement(&service->refs);
    if (!refs)
    {
        stop_service(service);
        free(service);
        InterlockedDecrement(&objects);
    }
    return refs;
}
static HRESULT WINAPI service_status(IOleCommandTarget *iface, const GUID *group, ULONG count,
                                     OLECMD *commands, OLECMDTEXT *text)
{
    ULONG i;
    if (!group || !IsEqualGUID(group, &CGID_ShellServiceObject)) return OLECMDERR_E_UNKNOWNGROUP;
    if (count && !commands) return E_POINTER;
    for (i = 0; i < count; i++)
        commands[i].cmdf = commands[i].cmdID == OLECMDID_NEW || commands[i].cmdID == OLECMDID_SAVE ?
                          OLECMDF_SUPPORTED | OLECMDF_ENABLED : 0;
    return S_OK;
}
static HRESULT WINAPI service_exec(IOleCommandTarget *iface, const GUID *group, DWORD command,
                                   DWORD options, VARIANT *in, VARIANT *out)
{
    struct network_service *service = impl(iface);
    if (!group || !IsEqualGUID(group, &CGID_ShellServiceObject)) return OLECMDERR_E_UNKNOWNGROUP;
    if (command == OLECMDID_SAVE) return stop_service(service);
    if (command != OLECMDID_NEW) return OLECMDERR_E_NOTSUPPORTED;
    if (service->started) return S_OK;
    if (users && service_thread != GetCurrentThreadId()) return RPC_E_WRONG_THREAD;
    if (!users)
    {
        service_thread = GetCurrentThreadId();
        if (!nettray_init(NULL))
        {
            service_thread = 0;
            return E_FAIL;
        }
    }
    users++;
    service->started = TRUE;
    return S_OK;
}
static const IOleCommandTargetVtbl service_vtbl =
{
    service_query, service_addref, service_release, service_status, service_exec
};

static HRESULT WINAPI factory_query(IClassFactory *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualGUID(iid, &IID_IUnknown) && !IsEqualGUID(iid, &IID_IClassFactory)) return E_NOINTERFACE;
    *out = iface;
    IClassFactory_AddRef(iface);
    return S_OK;
}
static ULONG WINAPI factory_addref(IClassFactory *iface) { return InterlockedIncrement(&factory_refs); }
static ULONG WINAPI factory_release(IClassFactory *iface) { return InterlockedDecrement(&factory_refs); }
static HRESULT WINAPI factory_create(IClassFactory *iface, IUnknown *outer, REFIID iid, void **out)
{
    struct network_service *service;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    if (outer) return CLASS_E_NOAGGREGATION;
    if (!(service = calloc(1, sizeof(*service)))) return E_OUTOFMEMORY;
    service->IOleCommandTarget_iface.lpVtbl = &service_vtbl;
    service->refs = 1;
    InterlockedIncrement(&objects);
    hr = IOleCommandTarget_QueryInterface(&service->IOleCommandTarget_iface, iid, out);
    IOleCommandTarget_Release(&service->IOleCommandTarget_iface);
    return hr;
}
static HRESULT WINAPI factory_lock(IClassFactory *iface, BOOL lock)
{
    if (lock) InterlockedIncrement(&locks);
    else InterlockedDecrement(&locks);
    return S_OK;
}
static const IClassFactoryVtbl factory_vtbl =
{
    factory_query, factory_addref, factory_release, factory_create, factory_lock
};
static IClassFactory factory = {&factory_vtbl};

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualGUID(clsid, &clsid_connection_tray)) return CLASS_E_CLASSNOTAVAILABLE;
    return IClassFactory_QueryInterface(&factory, iid, out);
}
HRESULT WINAPI DllCanUnloadNow(void)
{
    return objects || locks || factory_refs || users ? S_FALSE : S_OK;
}
