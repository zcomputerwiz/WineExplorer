/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Probe (Windows 11 and Wine): PSGetPropertyDescriptionListFromString, which Notepad calls with
 * "prop:System.Security.EncryptionOwners" before IFileSaveDialog::SetCollectedProperties. For a set of strings it prints the result,
 * the count, and each entry's canonical name, type and type flags; also IFileSaveDialog::SetCollectedProperties with the list.
 */
#define COBJMACROS
#include <windows.h>
#include <shobjidl.h>
#include <propsys.h>
#include <stdio.h>

static void show(const WCHAR *s)
{
    IPropertyDescriptionList *list = NULL;
    HRESULT hr;
    UINT count = 0, i;

    hr = PSGetPropertyDescriptionListFromString(s, &IID_IPropertyDescriptionList, (void **)&list);
    printf("%-60ls hr %#lx", s ? s : L"(null)", hr);
    if (FAILED(hr) || !list) { printf("\n"); return; }
    IPropertyDescriptionList_GetCount(list, &count);
    printf(" count %u\n", count);
    for (i = 0; i < count; i++)
    {
        IPropertyDescription *d = NULL;
        WCHAR *name = NULL;
        VARTYPE vt = 0;
        PROPDESC_TYPE_FLAGS flags = 0;
        PROPDESC_VIEW_FLAGS view = 0;

        hr = IPropertyDescriptionList_GetAt(list, i, &IID_IPropertyDescription, (void **)&d);
        if (FAILED(hr)) { printf("    %u: GetAt hr %#lx\n", i, hr); continue; }
        IPropertyDescription_GetCanonicalName(d, &name);
        IPropertyDescription_GetPropertyType(d, &vt);
        IPropertyDescription_GetTypeFlags(d, PDTF_MASK_ALL, &flags);
        IPropertyDescription_GetViewFlags(d, &view);
        printf("    %u: %ls type %#x type flags %#x view flags %#x\n", i, name, vt, flags, view);
        CoTaskMemFree(name);
        IPropertyDescription_Release(d);
    }
    {
        IPropertyDescription *d = (void *)0xdeadbeef;
        hr = IPropertyDescriptionList_GetAt(list, count, &IID_IPropertyDescription, (void **)&d);
        printf("    GetAt(count) hr %#lx out %p\n", hr, d);
    }
    IPropertyDescriptionList_Release(list);
}

int main(void)
{
    IFileSaveDialog *dlg;
    IPropertyDescriptionList *list = NULL;
    IUnknown *unk = NULL;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    show(L"prop:System.Security.EncryptionOwners");
    show(L"prop:System.ItemNameDisplay;System.Size");
    show(L"prop:*System.Size;~System.ItemNameDisplay");
    show(L"prop:System.ItemNameDisplay;Not.A.Property;System.Size");
    show(L"prop:Not.A.Property");
    show(L"prop:");
    show(L"System.ItemNameDisplay");
    show(L"PROP:System.ItemNameDisplay");
    show(L"prop: System.ItemNameDisplay ; System.Size");
    show(L"prop:System.ItemNameDisplay;");
    show(L"");
    show(NULL);
    hr = PSGetPropertyDescriptionListFromString(L"prop:System.Size", &IID_IUnknown, (void **)&unk);
    printf("IID_IUnknown hr %#lx\n", hr);
    if (unk) IUnknown_Release(unk);
    hr = PSGetPropertyDescriptionListFromString(L"prop:System.Size", &IID_IPropertyDescription, (void **)&unk);
    printf("IID_IPropertyDescription hr %#lx\n", hr);

    hr = CoCreateInstance(&CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileSaveDialog, (void **)&dlg);
    printf("FileSaveDialog hr %#lx\n", hr);
    if (FAILED(hr)) return 0;
    PSGetPropertyDescriptionListFromString(L"prop:System.Security.EncryptionOwners", &IID_IPropertyDescriptionList, (void **)&list);
    hr = IFileSaveDialog_SetCollectedProperties(dlg, list, FALSE);
    printf("SetCollectedProperties(list, FALSE) hr %#lx\n", hr);
    hr = IFileSaveDialog_SetCollectedProperties(dlg, list, TRUE);
    printf("SetCollectedProperties(list, TRUE) hr %#lx\n", hr);
    hr = IFileSaveDialog_SetCollectedProperties(dlg, NULL, TRUE);
    printf("SetCollectedProperties(NULL, TRUE) hr %#lx\n", hr);
    {
        IPropertyStore *store = (void *)0xdeadbeef;
        hr = IFileSaveDialog_GetProperties(dlg, &store);
        printf("GetProperties (before Show) hr %#lx out %p\n", hr, store);
    }
    if (list) IPropertyDescriptionList_Release(list);
    IFileSaveDialog_Release(dlg);
    return 0;
}
