/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Based on ReactOS acppage, Copyright 2015-2019 Mark Jansen
 *
 * The "Compatibility" tab of a program's Properties: a shell property sheet
 * extension (IShellExtInit + IShellPropSheetExt) that stores the user's choice
 * as compatibility layers through apphelp's SdbGetPermLayerKeys /
 * SdbSetPermLayerKeys.
 */
#define COBJMACROS
#include <stdlib.h>
#include <windows.h>
#include <ole2.h>
#include <shobjidl.h>
#include <prsht.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <rpcproxy.h>
#include "resource.h"

#define MAX_LAYER_LENGTH 256
#define LAYER_BUF_SIZE   1024

/* SdbGetPermLayerKeys flags */
#define GPLK_USER    1
#define GPLK_MACHINE 2

/* bit flags for the check-box layers */
#define LAYER_256COLOR      0x1
#define LAYER_640X480       0x2
#define LAYER_DISABLETHEMES 0x4
#define LAYER_RUNASADMIN    0x8

/* Windows' own CLSID for this property page */
static const CLSID clsid_acppage =
    {0x513d916f, 0x2a8e, 0x4f51, {0xae, 0xab, 0x0c, 0xbc, 0x76, 0xfb, 0x1a, 0xf8}};

static HINSTANCE instance;
static LONG objects, locks, factory_refs;

/* apphelp entry points, looked up at run time (no import library) */
typedef BOOL (WINAPI *SdbGetPermLayerKeys_fn)(PCWSTR, PWSTR, PDWORD, DWORD);
typedef BOOL (WINAPI *SdbSetPermLayerKeys_fn)(PCWSTR, PCWSTR, BOOL);
static SdbGetPermLayerKeys_fn pSdbGetPermLayerKeys;
static SdbSetPermLayerKeys_fn pSdbSetPermLayerKeys;
static BOOL apphelp_tried;

static const WCHAR *allowed_extensions[] =
{
    L".exe", L".msi", L".pif", L".bat", L".cmd", NULL
};

/* the fixed compatibility-mode list: layer name -> display string */
static const struct
{
    const WCHAR *name;
    UINT string_id;
} compat_modes[] =
{
    { L"WIN95",    IDS_OS_FIRST + 0 },
    { L"WIN98",    IDS_OS_FIRST + 1 },
    { L"NT4SP5",   IDS_OS_FIRST + 2 },
    { L"WIN2000",  IDS_OS_FIRST + 3 },
    { L"WINXPSP2", IDS_OS_FIRST + 4 },
    { L"WINXPSP3", IDS_OS_FIRST + 5 },
    { L"VISTARTM", IDS_OS_FIRST + 6 },
    { L"VISTASP2", IDS_OS_FIRST + 7 },
    { L"WIN7RTM",  IDS_OS_FIRST + 8 },
    { L"WIN8RTM",  IDS_OS_FIRST + 9 },
};

#define DEFAULT_OS_INDEX 5  /* "Windows XP (Service Pack 3)" */

struct acppage
{
    IShellExtInit IShellExtInit_iface;
    IShellPropSheetExt IShellPropSheetExt_iface;
    LONG refs;
    WCHAR path[MAX_PATH * 2];
    WCHAR unknown_layers[MAX_LAYER_LENGTH];  /* layers we do not recognise, kept as-is */
    DWORD os_mode;        /* index+1 into compat_modes, 0 = none */
    DWORD enabled_layers; /* LAYER_* bits */
};

/* the page currently being shown, so the dialog procedure can reach its object */
static struct acppage *active_page;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        instance = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

static struct acppage *extinit_obj(IShellExtInit *iface)
{
    return CONTAINING_RECORD(iface, struct acppage, IShellExtInit_iface);
}
static struct acppage *proppage_obj(IShellPropSheetExt *iface)
{
    return CONTAINING_RECORD(iface, struct acppage, IShellPropSheetExt_iface);
}

static HRESULT acppage_query_impl(struct acppage *obj, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IShellExtInit))
        *out = &obj->IShellExtInit_iface;
    else if (IsEqualGUID(iid, &IID_IShellPropSheetExt))
        *out = &obj->IShellPropSheetExt_iface;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&obj->refs);
    return S_OK;
}

static ULONG acppage_release_impl(struct acppage *obj)
{
    ULONG refs = InterlockedDecrement(&obj->refs);
    if (!refs)
    {
        free(obj);
        InterlockedDecrement(&objects);
    }
    return refs;
}

static HRESULT WINAPI extinit_query(IShellExtInit *iface, REFIID iid, void **out)
{
    return acppage_query_impl(extinit_obj(iface), iid, out);
}
static ULONG WINAPI extinit_addref(IShellExtInit *iface)
{
    return InterlockedIncrement(&extinit_obj(iface)->refs);
}
static ULONG WINAPI extinit_release(IShellExtInit *iface)
{
    return acppage_release_impl(extinit_obj(iface));
}

static HRESULT WINAPI proppage_query(IShellPropSheetExt *iface, REFIID iid, void **out)
{
    return acppage_query_impl(proppage_obj(iface), iid, out);
}
static ULONG WINAPI proppage_addref(IShellPropSheetExt *iface)
{
    return InterlockedIncrement(&proppage_obj(iface)->refs);
}
static ULONG WINAPI proppage_release(IShellPropSheetExt *iface)
{
    return acppage_release_impl(proppage_obj(iface));
}

static HRESULT WINAPI acppage_initialize(IShellExtInit *iface, PCIDLIST_ABSOLUTE pidl,
                                         IDataObject *data, HKEY key);
static HRESULT WINAPI acppage_addpages(IShellPropSheetExt *iface, LPFNADDPROPSHEETPAGE add_page,
                                       LPARAM lparam);
static HRESULT WINAPI acppage_replacepage(IShellPropSheetExt *iface, UINT page_id,
                                          LPFNADDPROPSHEETPAGE replace_with, LPARAM lparam);

static const IShellExtInitVtbl extinit_vtbl =
{
    extinit_query, extinit_addref, extinit_release, acppage_initialize
};
static const IShellPropSheetExtVtbl proppage_vtbl =
{
    proppage_query, proppage_addref, proppage_release, acppage_addpages, acppage_replacepage
};

static BOOL load_apphelp(void)
{
    HMODULE module;

    if (apphelp_tried)
        return (pSdbGetPermLayerKeys != NULL && pSdbSetPermLayerKeys != NULL);

    module = LoadLibraryW(L"apphelp.dll");
    if (module)
    {
        pSdbGetPermLayerKeys = (SdbGetPermLayerKeys_fn)GetProcAddress(module, "SdbGetPermLayerKeys");
        pSdbSetPermLayerKeys = (SdbSetPermLayerKeys_fn)GetProcAddress(module, "SdbSetPermLayerKeys");
    }
    apphelp_tried = TRUE;
    return (pSdbGetPermLayerKeys != NULL && pSdbSetPermLayerKeys != NULL);
}

static BOOL resolve_link(const WCHAR *lnk, WCHAR *out, int out_len)
{
    IShellLinkW *link;
    IPersistFile *file;
    HRESULT hr;
    BOOL ret = FALSE;

    if (FAILED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IShellLinkW, (void **)&link)))
        return FALSE;
    hr = IShellLinkW_QueryInterface(link, &IID_IPersistFile, (void **)&file);
    if (SUCCEEDED(hr))
    {
        hr = IPersistFile_Load(file, lnk, STGM_READ);
        IPersistFile_Release(file);
    }
    if (SUCCEEDED(hr) && SUCCEEDED(IShellLinkW_GetPath(link, out, out_len, NULL, 0)))
        ret = TRUE;
    IShellLinkW_Release(link);
    return ret;
}

static BOOL is_under_windows_dir(const WCHAR *path)
{
    WCHAR windir[MAX_PATH];
    DWORD len;

    len = GetWindowsDirectoryW(windir, ARRAY_SIZE(windir));
    if (!len || len >= ARRAY_SIZE(windir))
        return FALSE;
    if (windir[len - 1] != L'\\')
    {
        if (len + 1 >= ARRAY_SIZE(windir))
            return FALSE;
        windir[len++] = L'\\';
        windir[len] = 0;
    }
    return wcsnicmp(path, windir, len) == 0;
}

static HRESULT init_file(struct acppage *obj, const WCHAR *file)
{
    WCHAR resolved[MAX_PATH * 2];
    const WCHAR *path = file;
    const WCHAR *ext;
    int i;

    ext = PathFindExtensionW(file);
    if (ext && !wcsicmp(ext, L".lnk"))
    {
        if (!resolve_link(file, resolved, ARRAY_SIZE(resolved)))
            return E_FAIL;
        path = resolved;
        ext = PathFindExtensionW(resolved);
    }

    if (!ext)
        return E_FAIL;
    for (i = 0; allowed_extensions[i]; i++)
    {
        if (!wcsicmp(ext, allowed_extensions[i]))
            break;
    }
    if (!allowed_extensions[i])
        return E_FAIL;

    if (is_under_windows_dir(path))
        return E_FAIL;

    lstrcpynW(obj->path, path, ARRAY_SIZE(obj->path));
    return S_OK;
}

static HRESULT WINAPI acppage_initialize(IShellExtInit *iface, PCIDLIST_ABSOLUTE pidl,
                                         IDataObject *data, HKEY key)
{
    struct acppage *obj = extinit_obj(iface);
    FORMATETC etc;
    STGMEDIUM stg;
    HDROP hdrop;
    WCHAR file[MAX_PATH * 2];
    HRESULT hr;

    if (!data)
        return E_INVALIDARG;

    memset(&etc, 0, sizeof(etc));
    etc.cfFormat = CF_HDROP;
    etc.ptd = NULL;
    etc.dwAspect = DVASPECT_CONTENT;
    etc.lindex = -1;
    etc.tymed = TYMED_HGLOBAL;
    memset(&stg, 0, sizeof(stg));

    hr = IDataObject_GetData(data, &etc, &stg);
    if (FAILED(hr))
        return E_INVALIDARG;

    hr = E_FAIL;
    hdrop = (HDROP)GlobalLock(stg.hGlobal);
    if (hdrop)
    {
        if (DragQueryFileW(hdrop, 0xFFFFFFFF, NULL, 0) == 1 &&
            DragQueryFileW(hdrop, 0, file, ARRAY_SIZE(file)))
        {
            hr = init_file(obj, file);
        }
        GlobalUnlock(stg.hGlobal);
    }
    ReleaseStgMedium(&stg);
    return hr;
}

static void add_unknown_layer(struct acppage *obj, const WCHAR *layer)
{
    if (obj->unknown_layers[0])
        lstrcatW(obj->unknown_layers, L" ");
    lstrcatW(obj->unknown_layers, layer);
}

static void parse_layers(struct acppage *obj, const WCHAR *layers)
{
    WCHAR buf[MAX_LAYER_LENGTH];
    WCHAR *p;
    int i;

    lstrcpynW(buf, layers, ARRAY_SIZE(buf));
    p = buf;

    while (*p)
    {
        WCHAR *token;

        while (*p == L' ')
            p++;
        if (!*p)
            break;
        token = p;
        while (*p && *p != L' ')
            p++;
        if (*p)
            *p++ = 0;

        if (!wcsicmp(token, L"256COLOR"))
            obj->enabled_layers |= LAYER_256COLOR;
        else if (!wcsicmp(token, L"640X480"))
            obj->enabled_layers |= LAYER_640X480;
        else if (!wcsicmp(token, L"DISABLETHEMES"))
            obj->enabled_layers |= LAYER_DISABLETHEMES;
        else if (!wcsicmp(token, L"RUNASADMIN"))
            obj->enabled_layers |= LAYER_RUNASADMIN;
        else
        {
            for (i = 0; i < ARRAY_SIZE(compat_modes); i++)
            {
                if (!wcsicmp(token, compat_modes[i].name))
                {
                    obj->os_mode = i + 1;
                    break;
                }
            }
            if (i == ARRAY_SIZE(compat_modes))
                add_unknown_layer(obj, token);
        }
    }
}

static void init_dialog(HWND hwnd, struct acppage *obj)
{
    HWND combo = GetDlgItem(hwnd, IDC_COMPATIBILITYMODE);
    WCHAR buf[128];
    WCHAR layers[MAX_LAYER_LENGTH];
    DWORD size;
    int i;

    for (i = 0; i < ARRAY_SIZE(compat_modes); i++)
    {
        LoadStringW(instance, compat_modes[i].string_id, buf, ARRAY_SIZE(buf));
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)buf);
    }

    obj->os_mode = 0;
    obj->enabled_layers = 0;
    obj->unknown_layers[0] = 0;

    if (pSdbGetPermLayerKeys)
    {
        size = sizeof(layers);
        if (pSdbGetPermLayerKeys(obj->path, layers, &size, GPLK_USER))
            parse_layers(obj, layers);
    }

    if (obj->os_mode)
    {
        CheckDlgButton(hwnd, IDC_CHKRUNCOMPATIBILITY, BST_CHECKED);
        SendMessageW(combo, CB_SETCURSEL, obj->os_mode - 1, 0);
        EnableWindow(combo, TRUE);
    }
    else
    {
        CheckDlgButton(hwnd, IDC_CHKRUNCOMPATIBILITY, BST_UNCHECKED);
        SendMessageW(combo, CB_SETCURSEL, DEFAULT_OS_INDEX, 0);
        EnableWindow(combo, FALSE);
    }

    CheckDlgButton(hwnd, IDC_CHKRUNIN256COLORS, (obj->enabled_layers & LAYER_256COLOR) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_CHKRUNIN640480RES, (obj->enabled_layers & LAYER_640X480) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_CHKDISABLEVISUALTHEMES, (obj->enabled_layers & LAYER_DISABLETHEMES) ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd, IDC_CHKRUNASADMIN, (obj->enabled_layers & LAYER_RUNASADMIN) ? BST_CHECKED : BST_UNCHECKED);
}

static void append_layer(WCHAR *buf, int *pos, const WCHAR *layer)
{
    if (*pos > 0)
        buf[(*pos)++] = L' ';
    lstrcpynW(buf + *pos, layer, LAYER_BUF_SIZE - *pos);
    *pos += (int)wcslen(buf + *pos);
}

static void apply_layers(HWND hwnd, struct acppage *obj)
{
    WCHAR layers[LAYER_BUF_SIZE];
    int pos = 0;
    int sel;

    if (IsDlgButtonChecked(hwnd, IDC_CHKRUNCOMPATIBILITY) == BST_CHECKED)
    {
        sel = SendMessageW(GetDlgItem(hwnd, IDC_COMPATIBILITYMODE), CB_GETCURSEL, 0, 0);
        if (sel >= 0 && sel < (int)ARRAY_SIZE(compat_modes))
            append_layer(layers, &pos, compat_modes[sel].name);
    }

    if (IsDlgButtonChecked(hwnd, IDC_CHKRUNIN256COLORS) == BST_CHECKED)
        append_layer(layers, &pos, L"256COLOR");
    if (IsDlgButtonChecked(hwnd, IDC_CHKRUNIN640480RES) == BST_CHECKED)
        append_layer(layers, &pos, L"640X480");
    if (IsDlgButtonChecked(hwnd, IDC_CHKDISABLEVISUALTHEMES) == BST_CHECKED)
        append_layer(layers, &pos, L"DISABLETHEMES");
    if (IsDlgButtonChecked(hwnd, IDC_CHKRUNASADMIN) == BST_CHECKED)
        append_layer(layers, &pos, L"RUNASADMIN");

    if (obj->unknown_layers[0])
        append_layer(layers, &pos, obj->unknown_layers);

    layers[pos] = 0;

    if (pSdbSetPermLayerKeys)
        pSdbSetPermLayerKeys(obj->path, pos ? layers : NULL, FALSE);
}

static INT_PTR CALLBACK acppage_dlg_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    struct acppage *obj = active_page;

    switch (msg)
    {
    case WM_INITDIALOG:
        if (obj)
            init_dialog(hwnd, obj);
        return TRUE;

    case WM_COMMAND:
        if (!obj)
            return FALSE;
        switch (LOWORD(wParam))
        {
        case IDC_CHKRUNCOMPATIBILITY:
            EnableWindow(GetDlgItem(hwnd, IDC_COMPATIBILITYMODE),
                         IsDlgButtonChecked(hwnd, IDC_CHKRUNCOMPATIBILITY) == BST_CHECKED);
            SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
            return TRUE;
        case IDC_CHKRUNIN256COLORS:
        case IDC_CHKRUNIN640480RES:
        case IDC_CHKDISABLEVISUALTHEMES:
        case IDC_CHKRUNASADMIN:
            SendMessageW(GetParent(hwnd), PSM_CHANGED, (WPARAM)hwnd, 0);
            return TRUE;
        }
        return FALSE;

    case WM_NOTIFY:
        if (obj && ((LPNMHDR)lParam)->code == PSN_APPLY)
        {
            apply_layers(hwnd, obj);
            return PSNRET_NOERROR;
        }
        return FALSE;
    }
    return FALSE;
}

static UINT CALLBACK page_callback(HWND hwnd, UINT msg, LPPROPSHEETPAGEW psp)
{
    switch (msg)
    {
    case PSPCB_CREATE:
        active_page = (struct acppage *)psp->lParam;
        return TRUE;
    case PSPCB_RELEASE:
        active_page = NULL;
        if (psp->lParam)
            IUnknown_Release((IUnknown *)psp->lParam);
        return TRUE;
    }
    return FALSE;
}

static HRESULT WINAPI acppage_addpages(IShellPropSheetExt *iface, LPFNADDPROPSHEETPAGE add_page,
                                       LPARAM lparam)
{
    struct acppage *obj = proppage_obj(iface);
    PROPSHEETPAGEW psp;
    HPROPSHEETPAGE hpage;
    WCHAR title[64];

    if (!load_apphelp())
        return S_OK;  /* no apphelp, no page */

    LoadStringW(instance, IDS_COMPAT_TITLE, title, ARRAY_SIZE(title));

    memset(&psp, 0, sizeof(psp));
    psp.dwSize = sizeof(psp);
    psp.dwFlags = PSP_USETITLE | PSP_USECALLBACK;
    psp.hInstance = instance;
    psp.pszTemplate = MAKEINTRESOURCEW(IDD_ACPPAGESHEET);
    psp.pszTitle = title;
    psp.pfnCallback = page_callback;
    psp.lParam = (LPARAM)obj;
    psp.pfnDlgProc = (DLGPROC)acppage_dlg_proc;

    /* the page holds a reference for its lifetime; the callback releases it */
    IUnknown_AddRef((IUnknown *)&obj->IShellExtInit_iface);

    hpage = CreatePropertySheetPageW(&psp);
    if (!hpage)
    {
        IUnknown_Release((IUnknown *)&obj->IShellExtInit_iface);
        return S_OK;
    }
    if (!add_page(hpage, lparam))
        DestroyPropertySheetPage(hpage);
    return S_OK;
}

static HRESULT WINAPI acppage_replacepage(IShellPropSheetExt *iface, UINT page_id,
                                          LPFNADDPROPSHEETPAGE replace_with, LPARAM lparam)
{
    return E_NOTIMPL;
}

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
    struct acppage *obj;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    if (outer) return CLASS_E_NOAGGREGATION;
    if (!(obj = calloc(1, sizeof(*obj)))) return E_OUTOFMEMORY;
    obj->IShellExtInit_iface.lpVtbl = &extinit_vtbl;
    obj->IShellPropSheetExt_iface.lpVtbl = &proppage_vtbl;
    obj->refs = 1;
    InterlockedIncrement(&objects);
    hr = IUnknown_QueryInterface((IUnknown *)&obj->IShellExtInit_iface, iid, out);
    IUnknown_Release((IUnknown *)&obj->IShellExtInit_iface);
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
    if (!IsEqualGUID(clsid, &clsid_acppage)) return CLASS_E_CLASSNOTAVAILABLE;
    return IClassFactory_QueryInterface(&factory, iid, out);
}
HRESULT WINAPI DllCanUnloadNow(void)
{
    return objects || locks || factory_refs ? S_FALSE : S_OK;
}
HRESULT WINAPI DllRegisterServer(void)
{
    return __wine_register_resources();
}
HRESULT WINAPI DllUnregisterServer(void)
{
    return __wine_unregister_resources();
}
