/*
 * WineExplorer U1.0 activation component
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/* Plain WinRT ABI. The object intentionally has no custom interfaces. */
#include <windows.h>
#include <roapi.h>
#include <activation.h>
#include <new>
#include <cwchar>

static constexpr wchar_t class_name[] = L"WineExplorer.U1Oracle.Component";
static volatile LONG live_objects;

class Component final : public IInspectable
{
    volatile LONG refs = 1;
public:
    Component() { InterlockedIncrement(&live_objects); }
    ~Component() { InterlockedDecrement(&live_objects); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IInspectable) return E_NOINTERFACE;
        *out = static_cast<IInspectable *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG n = InterlockedDecrement(&refs);
        if (!n) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG *count, IID **iids) override
    {
        if (!count || !iids) return E_POINTER;
        *count = 0; *iids = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING *name) override
    {
        if (!name) return E_POINTER;
        return WindowsCreateString(class_name, ARRAYSIZE(class_name) - 1, name);
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel *level) override
    {
        if (!level) return E_POINTER;
        *level = BaseTrust;
        return S_OK;
    }
};

class Factory final : public IActivationFactory
{
    volatile LONG refs = 1;
public:
    Factory() { InterlockedIncrement(&live_objects); }
    ~Factory() { InterlockedDecrement(&live_objects); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IInspectable && iid != IID_IActivationFactory)
            return E_NOINTERFACE;
        *out = static_cast<IActivationFactory *>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG n = InterlockedDecrement(&refs);
        if (!n) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG *count, IID **iids) override
    {
        if (!count || !iids) return E_POINTER;
        *count = 1;
        *iids = static_cast<IID *>(CoTaskMemAlloc(sizeof(IID)));
        if (!*iids) { *count = 0; return E_OUTOFMEMORY; }
        **iids = IID_IActivationFactory;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING *name) override
    {
        if (!name) return E_POINTER;
        return WindowsCreateString(class_name, ARRAYSIZE(class_name) - 1, name);
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel *level) override
    {
        if (!level) return E_POINTER;
        *level = BaseTrust;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ActivateInstance(IInspectable **instance) override
    {
        if (!instance) return E_POINTER;
        *instance = new (std::nothrow) Component;
        return *instance ? S_OK : E_OUTOFMEMORY;
    }
};

extern "C" __declspec(dllexport) HRESULT WINAPI DllGetActivationFactory(
    HSTRING name, IActivationFactory **factory)
{
    if (!factory) return E_POINTER;
    *factory = nullptr;
    UINT32 length = 0;
    const wchar_t *value = WindowsGetStringRawBuffer(name, &length);
    if (length != ARRAYSIZE(class_name) - 1 ||
        wmemcmp(value, class_name, length)) return REGDB_E_CLASSNOTREG;
    *factory = new (std::nothrow) Factory;
    return *factory ? S_OK : E_OUTOFMEMORY;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllCanUnloadNow()
{
    return InterlockedCompareExchange(&live_objects, 0, 0) ? S_FALSE : S_OK;
}
