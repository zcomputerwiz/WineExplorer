/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2026 WineExplorer contributors */
/*
 * GetDistanceOfClosestLanguageInList of bcp47mrm.dll: the distance (0 = same, 1 = nothing in common) between a language tag and the
 * closest one of a list. The signature (language, list, delimiter, double *) was found by calling it with a canary after the result.
 * Build (mingw): x86_64-w64-mingw32-gcc -O2 -o bcp47-probe.exe bcp47-probe.c
 */
#include <windows.h>
#include <stdio.h>

typedef LONG (WINAPI *dist_func)(const WCHAR *lang, const WCHAR *list, WCHAR delimiter, double *distance);

int main(void)
{
    HMODULE m = LoadLibraryW(L"bcp47mrm.dll");
    dist_func f = m ? (dist_func)GetProcAddress(m, "GetDistanceOfClosestLanguageInList") : NULL;
    static const struct { const WCHAR *lang, *list; } cases[] =
    {
        { L"en-US", L"en-US" }, { L"en-US", L"en-GB" }, { L"en-US", L"en" }, { L"en", L"en-US" }, { L"en-US", L"fr-FR" },
        { L"en-US", L"fr-FR;en-GB" }, { L"en-US", L"fr-FR;en-US" }, { L"en-US", L"" }, { L"", L"en-US" },
        { L"de-DE", L"en-US;de-AT" }, { L"zh-Hans-CN", L"zh-CN" }, { L"zh-Hans-CN", L"zh-TW" }, { L"EN-us", L"en-US" },
        { L"en-US", L"EN-GB;fr" }, { L"pt-BR", L"pt-PT" }, { L"pt-BR", L"pt" }, { L"und", L"en-US" }, { L"en-US", L"und" },
        { L"x-private", L"x-private" }, { L"en-US", L"en-US,fr-FR" }, { L"fr", L"fr-CA" }, { L"fr-CA", L"fr-FR" },
    };
    double d;
    LONG r;
    unsigned i;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("function %p\n", (void *)f);
    if (!f) return 1;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        d = -7.0;
        r = f(cases[i].lang, cases[i].list, L';', &d);
        printf("(%ls, %ls) -> %08lx, %g\n", cases[i].lang, cases[i].list, (ULONG)r, d);
    }
    d = -7.0;
    r = f(L"en-US", L"en-US,fr-FR", L',', &d);
    printf("delimiter ',': (en-US, en-US,fr-FR) -> %08lx, %g\n", (ULONG)r, d);
    d = -7.0;
    r = f(NULL, L"en-US", L';', &d);
    printf("NULL language -> %08lx, %g\n", (ULONG)r, d);
    d = -7.0;
    r = f(L"en-US", NULL, L';', &d);
    printf("NULL list -> %08lx, %g\n", (ULONG)r, d);
    r = f(L"en-US", L"en-US", L';', NULL);
    printf("NULL result -> %08lx\n", (ULONG)r);
    return 0;
}
