/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Probe (Windows 11 and Wine): slot 3 of the DirectWrite text analyzer's private interface
 * {b7e6163e-7f46-43b4-84b3-e4e6249c365e}, which the Windows App SDK's XAML calls for every text run as
 * (analyzer, source, position, length, &a, &b, buffer, flags) with an IDWriteTextAnalysisSource. The probe passes its own source and
 * pattern-filled outputs, and prints which source methods are called and what is written. Black-box: interface calls only.
 * Usage: dwrite-ta3-probe [text [flags]]
 */
#include <windows.h>
#include <dwrite.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const GUID IID_private = {0xb7e6163e, 0x7f46, 0x43b4, {0x84, 0xb3, 0xe4, 0xe6, 0x24, 0x9c, 0x36, 0x5e}};

struct source : public IDWriteTextAnalysisSource
{
    const WCHAR *text;
    UINT32 len;
    STDMETHOD(QueryInterface)(REFIID iid, void **out)
    {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IDWriteTextAnalysisSource)) { *out = this; return S_OK; }
        printf("  source QI {%08lx-%04x-%04x-...}\n", iid.Data1, iid.Data2, iid.Data3);
        *out = NULL; return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() { return 2; }
    STDMETHOD_(ULONG, Release)() { return 1; }
    STDMETHOD(GetTextAtPosition)(UINT32 pos, const WCHAR **str, UINT32 *n)
    {
        printf("  source GetTextAtPosition(%u)\n", pos);
        if (pos >= len) { *str = NULL; *n = 0; } else { *str = text + pos; *n = len - pos; }
        return S_OK;
    }
    STDMETHOD(GetTextBeforePosition)(UINT32 pos, const WCHAR **str, UINT32 *n)
    {
        printf("  source GetTextBeforePosition(%u)\n", pos);
        if (!pos || pos > len) { *str = NULL; *n = 0; } else { *str = text; *n = pos; }
        return S_OK;
    }
    DWRITE_READING_DIRECTION dir = DWRITE_READING_DIRECTION_LEFT_TO_RIGHT;
    STDMETHOD_(DWRITE_READING_DIRECTION, GetParagraphReadingDirection)() { printf("  source GetParagraphReadingDirection\n"); return dir; }
    STDMETHOD(GetLocaleName)(UINT32 pos, UINT32 *n, const WCHAR **locale)
    {
        printf("  source GetLocaleName(%u)\n", pos);
        *locale = L"en-us"; *n = len - pos; return S_OK;
    }
    STDMETHOD(GetNumberSubstitution)(UINT32 pos, UINT32 *n, IDWriteNumberSubstitution **subst)
    {
        printf("  source GetNumberSubstitution(%u)\n", pos);
        *subst = NULL; *n = len - pos; return S_OK;
    }
};

typedef HRESULT (STDMETHODCALLTYPE *slot3_fn)(void *iface, IDWriteTextAnalysisSource *src, UINT_PTR pos, UINT_PTR len,
                                             void *a, void *b, void *buffer, UINT_PTR flags);

int main(int argc, char **argv)
{
    static unsigned char buffer[8192], pat[8192];
    IDWriteFactory *factory;
    IDWriteTextAnalyzer *analyzer;
    void *priv = NULL;
    WCHAR text[256];
    source src;
    UINT32 ab[4];
    UINT_PTR flags = argc > 2 ? strtoull(argv[2], NULL, 0) : 0;
    HRESULT hr;
    unsigned int i, first = ~0u, last = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1 && !strncmp(argv[1], "u:", 2))
    {
        /* u:0645,0631,...: UTF-16 code units in hex (the command line would mangle the characters themselves) */
        const char *p = argv[1] + 2;
        unsigned int n = 0;

        while (*p && n < 255)
        {
            text[n++] = (WCHAR)strtoul(p, (char **)&p, 16);
            if (*p == ',') p++;
        }
        text[n] = 0;
    }
    else MultiByteToWideChar(CP_UTF8, 0, argc > 1 ? argv[1] : "Hello file", -1, text, 256);
    src.text = text;
    src.len = lstrlenW(text);
    /* argv[3] "rtl": the source's paragraph reading direction is right to left */
    if (argc > 3 && !strcmp(argv[3], "rtl")) src.dir = DWRITE_READING_DIRECTION_RIGHT_TO_LEFT;
    if (FAILED(hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown **)&factory)))
    { printf("factory %#lx\n", hr); return 1; }
    factory->CreateTextAnalyzer(&analyzer);
    hr = analyzer->QueryInterface(IID_private, &priv);
    printf("private interface: hr %#lx, %p (analyzer %p)\n", hr, priv, analyzer);
    if (FAILED(hr)) return 0;
    memset(buffer, 0xcc, sizeof(buffer));
    memset(pat, 0xcc, sizeof(pat));
    ab[0] = ab[1] = ab[2] = ab[3] = 0xdeadbeef;
    printf("slot 3: %u chars:", src.len);
    for (i = 0; i < src.len; i++) printf(" %04x", text[i]);
    printf(", flags %#llx\n", (unsigned long long)flags);
    hr = ((slot3_fn)(*(void ***)priv)[3])(priv, &src, 0, src.len, &ab[0], &ab[1], buffer, flags);
    printf("hr %#lx, a %#x, b %#x (after them: %#x %#x)\n", hr, ab[0], ab[1], ab[2], ab[3]);
    for (i = 0; i < sizeof(buffer); i++) if (buffer[i] != pat[i]) { if (first == ~0u) first = i; last = i; }
    if (first == ~0u) printf("buffer untouched\n");
    else
    {
        printf("buffer bytes %u..%u changed:", first, last);
        for (i = first; i <= last && i < first + 96; i++) printf(" %02x", buffer[i]);
        printf("\n");
    }
    return 0;
}
