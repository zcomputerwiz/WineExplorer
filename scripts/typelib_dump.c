/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2026 WineExplorer contributors */
/* Dump the interfaces of a type library: typelib_dump.exe <file or file\N> [iid]
 * For every TKIND_INTERFACE / DISPATCH it prints flags, parent, and each function with return and parameter types
 * (following aliases and interface pointers). Used to see what a vendor's proxy/stub typelib needs from Wine's
 * typelib marshaller (see docs/TYPELIB_PROXYSTUB_ANALYSIS.md). Build: x86_64-w64-mingw32-gcc -municode -o typelib_dump.exe
 * typelib_dump.c -loleaut32 -lole32 */
#define COBJMACROS
#include <windows.h>
#include <oleauto.h>
#include <stdio.h>
#include <string.h>

static void print_type(ITypeInfo *info, const TYPEDESC *td, int depth)
{
    if (td->vt == VT_PTR)
    {
        printf("PTR(");
        print_type(info, td->lptdesc, depth + 1);
        printf(")");
    }
    else if (td->vt == VT_SAFEARRAY)
    {
        printf("SAFEARRAY(");
        print_type(info, td->lptdesc, depth + 1);
        printf(")");
    }
    else if (td->vt == VT_USERDEFINED)
    {
        ITypeInfo *ref = NULL;
        TYPEATTR *ta = NULL;
        BSTR name = NULL;
        if (SUCCEEDED(ITypeInfo_GetRefTypeInfo(info, td->hreftype, &ref)))
        {
            ITypeInfo_GetDocumentation(ref, MEMBERID_NIL, &name, NULL, NULL, NULL);
            if (SUCCEEDED(ITypeInfo_GetTypeAttr(ref, &ta)))
            {
                printf("USER(%ls kind=%d", name ? name : L"?", ta->typekind);
                if (ta->typekind == TKIND_ALIAS && depth < 6) { printf(" -> "); print_type(ref, &ta->tdescAlias, depth + 1); }
                printf(")");
                ITypeInfo_ReleaseTypeAttr(ref, ta);
            }
            SysFreeString(name);
            ITypeInfo_Release(ref);
        }
        else printf("USER(?)");
    }
    else printf("vt=%u", td->vt);
}

static void dump_info(ITypeInfo *info)
{
    TYPEATTR *ta;
    BSTR name = NULL;
    WORD i;
    WCHAR guid[64];

    if (FAILED(ITypeInfo_GetTypeAttr(info, &ta))) return;
    ITypeInfo_GetDocumentation(info, MEMBERID_NIL, &name, NULL, NULL, NULL);
    if (ta->typekind == TKIND_INTERFACE || ta->typekind == TKIND_DISPATCH)
    {
        StringFromGUID2(&ta->guid, guid, 64);
        printf("%ls %ls kind=%d flags=%#x funcs=%u impltypes=%u cbSizeVft=%u\n", name, guid, ta->typekind, ta->wTypeFlags,
               ta->cFuncs, ta->cImplTypes, ta->cbSizeVft);
        for (i = 0; i < ta->cFuncs; i++)
        {
            FUNCDESC *fd;
            BSTR fname = NULL;
            int p;
            if (FAILED(ITypeInfo_GetFuncDesc(info, i, &fd))) continue;
            ITypeInfo_GetDocumentation(info, fd->memid, &fname, NULL, NULL, NULL);
            printf("  [%u] %ls memid=%ld invkind=%d callconv=%d oVft=%d flags=%#x ret=", i, fname ? fname : L"?", fd->memid,
                   fd->invkind, fd->callconv, fd->oVft, fd->wFuncFlags);
            print_type(info, &fd->elemdescFunc.tdesc, 0);
            printf("\n");
            for (p = 0; p < fd->cParams; p++)
            {
                printf("      p%d flags=%#x ", p, fd->lprgelemdescParam[p].paramdesc.wParamFlags);
                print_type(info, &fd->lprgelemdescParam[p].tdesc, 0);
                printf("\n");
            }
            SysFreeString(fname);
            ITypeInfo_ReleaseFuncDesc(info, fd);
        }
    }
    else printf("(skip %ls kind=%d)\n", name, ta->typekind);
    SysFreeString(name);
    ITypeInfo_ReleaseTypeAttr(info, ta);
}

int wmain(int argc, wchar_t **argv)
{
    ITypeLib *lib = NULL;
    UINT n, i;
    HRESULT hr;

    if (argc < 2) { fprintf(stderr, "usage: typelib_dump file[\\N]\n"); return 2; }
    CoInitialize(NULL);
    hr = LoadTypeLibEx(argv[1], REGKIND_NONE, &lib);
    if (FAILED(hr)) { printf("LoadTypeLibEx failed: %#lx\n", hr); return 1; }
    n = ITypeLib_GetTypeInfoCount(lib);
    printf("%u type infos\n", n);
    for (i = 0; i < n; i++)
    {
        ITypeInfo *info = NULL;
        if (SUCCEEDED(ITypeLib_GetTypeInfo(lib, i, &info)))
        {
            dump_info(info);
            ITypeInfo_Release(info);
        }
    }
    ITypeLib_Release(lib);
    return 0;
}
