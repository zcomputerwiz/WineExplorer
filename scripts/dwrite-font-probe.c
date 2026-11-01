/*
 * WineExplorer - DirectWrite system font probe
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

/*
 * Shows what Skia (Chromium) sees when it asks DirectWrite for a typeface: the size of the system font collection,
 * the family lookups Skia tries, the default UI fonts, the font fallback result for a space and the registry font
 * list the collection is built from. A runtime without Wine's tahoma.ttf prints "Tahoma" with exists=0 for the default
 * UI fonts and MapCharacters returns a NULL font (hr=0, mapped=1, font=0000...), whatever other fonts the host
 * provides; Chromium's font initialization then recurses until the stack is gone
 * (docs/CHROMIUM_MISSING_WINE_FONTS_20261101.md).
 *
 * Build: x86_64-w64-mingw32-gcc -O2 -Wall -o dwrite-font-probe64.exe dwrite-font-probe.c -ldwrite -luuid
 *        i686-w64-mingw32-gcc   -O2 -Wall -o dwrite-font-probe32.exe dwrite-font-probe.c -ldwrite -luuid
 * Run:   wine dwrite-font-probe64.exe [-v]      (-v lists every family and every registry font value)
 */
#define COBJMACROS
#include <initguid.h>
#include <windows.h>
#include <dwrite.h>
#include <dwrite_2.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

struct source
{
    IDWriteTextAnalysisSource iface;
    const WCHAR *text;
    UINT32 len;
};

static struct source *impl_from_source(IDWriteTextAnalysisSource *iface)
{
    return CONTAINING_RECORD(iface, struct source, iface);
}

static HRESULT WINAPI source_QueryInterface(IDWriteTextAnalysisSource *iface, REFIID riid, void **obj)
{
    if (IsEqualIID(riid, &IID_IDWriteTextAnalysisSource) || IsEqualIID(riid, &IID_IUnknown))
    {
        *obj = iface;
        return S_OK;
    }
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI source_AddRef(IDWriteTextAnalysisSource *iface) { return 2; }
static ULONG WINAPI source_Release(IDWriteTextAnalysisSource *iface) { return 1; }

static HRESULT WINAPI source_GetTextAtPosition(IDWriteTextAnalysisSource *iface, UINT32 pos, const WCHAR **text,
        UINT32 *len)
{
    struct source *s = impl_from_source(iface);

    if (pos >= s->len) { *text = NULL; *len = 0; }
    else { *text = s->text + pos; *len = s->len - pos; }
    return S_OK;
}

static HRESULT WINAPI source_GetTextBeforePosition(IDWriteTextAnalysisSource *iface, UINT32 pos, const WCHAR **text,
        UINT32 *len)
{
    struct source *s = impl_from_source(iface);

    if (!pos || pos > s->len) { *text = NULL; *len = 0; }
    else { *text = s->text; *len = pos; }
    return S_OK;
}

static DWRITE_READING_DIRECTION WINAPI source_GetParagraphReadingDirection(IDWriteTextAnalysisSource *iface)
{
    return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
}

static HRESULT WINAPI source_GetLocaleName(IDWriteTextAnalysisSource *iface, UINT32 pos, UINT32 *len,
        const WCHAR **locale)
{
    struct source *s = impl_from_source(iface);

    *locale = L"en-us";
    *len = pos < s->len ? s->len - pos : 0;
    return S_OK;
}

static HRESULT WINAPI source_GetNumberSubstitution(IDWriteTextAnalysisSource *iface, UINT32 pos, UINT32 *len,
        IDWriteNumberSubstitution **substitution)
{
    struct source *s = impl_from_source(iface);

    *substitution = NULL;
    *len = pos < s->len ? s->len - pos : 0;
    return S_OK;
}

static IDWriteTextAnalysisSourceVtbl source_vtbl =
{
    source_QueryInterface,
    source_AddRef,
    source_Release,
    source_GetTextAtPosition,
    source_GetTextBeforePosition,
    source_GetParagraphReadingDirection,
    source_GetLocaleName,
    source_GetNumberSubstitution,
};

static void print_registry_fonts(int verbose)
{
    static const WCHAR fonts_key[] = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts";
    WCHAR name[512], data[MAX_PATH * 2], path[MAX_PATH * 2], windir[MAX_PATH];
    DWORD index = 0, name_len, data_len, type, total = 0, existing = 0, shown_missing = 0;
    HKEY key;

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, fonts_key, 0, KEY_READ, &key))
    {
        printf("registry: Fonts key missing\n");
        return;
    }
    GetWindowsDirectoryW(windir, ARRAYSIZE(windir));
    for (;;)
    {
        name_len = ARRAYSIZE(name);
        data_len = sizeof(data) - sizeof(WCHAR);
        if (RegEnumValueW(key, index++, name, &name_len, NULL, &type, (BYTE *)data, &data_len)) break;
        if (type != REG_SZ) continue;
        data[data_len / sizeof(WCHAR)] = 0;
        total++;
        if (wcschr(data, ':') || data[0] == '\\') wcscpy(path, data);
        else swprintf(path, ARRAYSIZE(path), L"%ls\\fonts\\%ls", windir, data);
        if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) existing++;
        else if (shown_missing++ < 8 || verbose) printf("registry: MISSING file %ls = %ls\n", name, data);
        if (verbose) printf("registry: %ls = %ls\n", name, data);
    }
    RegCloseKey(key);
    printf("registry: %lu font values, %lu with an existing file, %lu missing\n", total, existing, total - existing);
}

static const WCHAR *probe_names[] =
{
    L"Segoe UI", L"Tahoma", L"Arial", L"Times New Roman", L"Courier New", L"Verdana", L"Calibri",
    L"Microsoft Sans Serif", L"MS Shell Dlg", L"MS Shell Dlg 2", L"Marlett", L"Symbol", L"sans", L"sans-serif",
    L"DejaVu Sans", L"Liberation Sans",
};

int main(int argc, char **argv)
{
    int verbose = argc > 1 && !strcmp(argv[1], "-v");
    IDWriteFontCollection *collection = NULL;
    IDWriteFactory2 *factory2 = NULL;
    IDWriteFontFallback *fallback = NULL;
    IDWriteFactory *factory = NULL;
    NONCLIENTMETRICSW metrics = { sizeof(metrics) };
    UINT32 count, i, index;
    BOOL exists;
    HRESULT hr;

    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory, (IUnknown **)&factory);
    if (FAILED(hr)) { printf("DWriteCreateFactory failed %08lx\n", hr); return 1; }

    hr = IDWriteFactory_GetSystemFontCollection(factory, &collection, FALSE);
    if (FAILED(hr)) { printf("GetSystemFontCollection failed %08lx\n", hr); return 1; }
    count = IDWriteFontCollection_GetFontFamilyCount(collection);
    printf("system collection: %u families\n", count);

    for (i = 0; i < count && (i < 12 || verbose); i++)
    {
        IDWriteFontFamily *family;
        IDWriteLocalizedStrings *names;
        WCHAR buffer[128] = L"?";

        if (FAILED(IDWriteFontCollection_GetFontFamily(collection, i, &family))) continue;
        if (SUCCEEDED(IDWriteFontFamily_GetFamilyNames(family, &names)))
        {
            IDWriteLocalizedStrings_GetString(names, 0, buffer, ARRAYSIZE(buffer));
            IDWriteLocalizedStrings_Release(names);
        }
        printf("  family[%u] = %ls\n", i, buffer);
        IDWriteFontFamily_Release(family);
    }

    for (i = 0; i < ARRAYSIZE(probe_names); i++)
    {
        index = ~0u;
        exists = FALSE;
        hr = IDWriteFontCollection_FindFamilyName(collection, probe_names[i], &index, &exists);
        printf("FindFamilyName(%ls): hr=%08lx exists=%d index=%d\n", probe_names[i], hr, exists, (int)index);
    }

    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
    {
        const LOGFONTW *fonts[] = { &metrics.lfMessageFont, &metrics.lfCaptionFont, &metrics.lfMenuFont,
                                    &metrics.lfStatusFont };
        static const char *labels[] = { "message", "caption", "menu", "status" };

        for (i = 0; i < ARRAYSIZE(fonts); i++)
        {
            index = ~0u;
            exists = FALSE;
            IDWriteFontCollection_FindFamilyName(collection, fonts[i]->lfFaceName, &index, &exists);
            printf("system font %s: \"%ls\" height=%ld -> DirectWrite family exists=%d\n", labels[i],
                    fonts[i]->lfFaceName, fonts[i]->lfHeight, exists);
        }
    }
    else printf("SystemParametersInfo(SPI_GETNONCLIENTMETRICS) failed %lu\n", GetLastError());

    if (SUCCEEDED(IDWriteFactory_QueryInterface(factory, &IID_IDWriteFactory2, (void **)&factory2))
            && SUCCEEDED(IDWriteFactory2_GetSystemFontFallback(factory2, &fallback)))
    {
        static const WCHAR *bases[] = { L"Tahoma", L"Segoe UI", L"sans", L"" };
        struct source src;

        src.iface.lpVtbl = &source_vtbl;
        src.text = L" ";
        src.len = 1;
        for (i = 0; i < ARRAYSIZE(bases); i++)
        {
            IDWriteFont *font = NULL;
            UINT32 mapped = 0;
            FLOAT scale = 0.0f;

            hr = IDWriteFontFallback_MapCharacters(fallback, &src.iface, 0, 1, collection, bases[i],
                    DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, &mapped,
                    &font, &scale);
            printf("MapCharacters(' ', base \"%ls\"): hr=%08lx mapped=%u font=%p\n", bases[i], hr, mapped, font);
            if (font) IDWriteFont_Release(font);
        }
        IDWriteFontFallback_Release(fallback);
    }
    else printf("system font fallback unavailable\n");

    print_registry_fonts(verbose);
    return 0;
}
