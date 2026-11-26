/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Which font family names Windows applications ask DirectWrite for are found in the system collection, what the system font
 * fallback maps a few of them to, and whether GDI gets a face for them. Windows 10 and 11 find all of the common Office and
 * Windows UI families; a system that lacks one makes controls such as Notepad's RichEdit lay out nothing.
 * Output is the comparison runner's: one line per name, no font file paths.
 */
#define COBJMACROS
#include <windows.h>
#include <dwrite.h>
#include <stdio.h>

static const WCHAR *names[] = {
    L"Consolas", L"Calibri", L"Cambria", L"Segoe UI", L"Segoe UI Variable", L"Segoe UI Symbol", L"Segoe UI Emoji", L"Segoe MDL2 Assets",
    L"Segoe Fluent Icons", L"Arial", L"Times New Roman", L"Courier New", L"Tahoma", L"Verdana", L"Georgia", L"Lucida Console",
    L"Cascadia Mono", L"Cascadia Code", L"Microsoft Sans Serif", L"MS Shell Dlg", L"Marlett", L"Webdings", L"Wingdings", L"Symbol",
    L"Malgun Gothic", L"Microsoft YaHei", L"Yu Gothic UI", L"SimSun", L"Nirmala UI", L"Gadugi", NULL
};

static const GUID iid_dwrite_factory = {0xb859ee5a, 0xd838, 0x4b5b, {0xa2, 0xe8, 0x1a, 0xdc, 0x7d, 0x93, 0xdb, 0x48}};

typedef HRESULT (WINAPI *create_t)(DWRITE_FACTORY_TYPE, REFIID, IUnknown **);

int main(void)
{
    HMODULE dw = LoadLibraryW(L"dwrite.dll");
    create_t create = dw ? (create_t)GetProcAddress(dw, "DWriteCreateFactory") : NULL;
    IDWriteFactory *factory = NULL;
    IDWriteFontCollection *coll = NULL;
    IDWriteGdiInterop *gdi = NULL;
    HRESULT hr;
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (!create) { printf("no dwrite\n"); return 1; }
    hr = create(DWRITE_FACTORY_TYPE_SHARED, &iid_dwrite_factory, (IUnknown **)&factory);
    printf("DWriteCreateFactory: %s\n", SUCCEEDED(hr) ? "ok" : "failed");
    if (FAILED(hr)) return 1;
    hr = IDWriteFactory_GetSystemFontCollection(factory, &coll, FALSE);
    printf("GetSystemFontCollection: %s\n", SUCCEEDED(hr) ? "ok" : "failed");
    IDWriteFactory_GetGdiInterop(factory, &gdi);
    for (i = 0; names[i]; i++)
    {
        UINT32 idx = 0; BOOL exists = FALSE;
        LOGFONTW lf = {0};
        IDWriteFont *font = NULL;
        BOOL gdi_ok = FALSE;
        if (coll) IDWriteFontCollection_FindFamilyName(coll, names[i], &idx, &exists);
        lf.lfHeight = -16; lf.lfCharSet = DEFAULT_CHARSET; wcscpy(lf.lfFaceName, names[i]);
        if (gdi && SUCCEEDED(IDWriteGdiInterop_CreateFontFromLOGFONT(gdi, &lf, &font))) { gdi_ok = TRUE; IDWriteFont_Release(font); }
        printf("%-22ls collection %s, GDI interop %s\n", names[i], exists ? "yes" : "NO", gdi_ok ? "yes" : "no");
    }
    return 0;
}
