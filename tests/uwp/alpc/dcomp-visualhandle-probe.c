/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe: dcomp.dll export 1040, DCompositionCreateSharedVisualHandle (found with dcomp-exports-probe.c), imported by
 * ordinal by dcompi.dll of the Windows App Runtime. Its signature is not documented: this tries the shape of the documented
 * DCompositionCreateSurfaceHandle (access, security attributes, out handle) and prints the result, the handle and what the handle
 * is (type name through NtQueryObject), to see whether the guess holds.
 * Build: x86_64-w64-mingw32-gcc -O2 -o dcomp-visualhandle-probe.exe dcomp-visualhandle-probe.c
 */
#include <stdio.h>
#include <windows.h>

typedef LONG (WINAPI *NtQueryObject_t)(HANDLE, int, void *, ULONG, ULONG *);
typedef HRESULT (WINAPI *create_t)(DWORD access, SECURITY_ATTRIBUTES *sa, HANDLE *out);
typedef HRESULT (WINAPI *surface_t)(DWORD access, SECURITY_ATTRIBUTES *sa, HANDLE *out);

static void describe(const char *what, HANDLE h)
{
    NtQueryObject_t q = (NtQueryObject_t)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryObject");
    char buf[512];
    ULONG len = 0;
    LONG st = q(h, 2 /* ObjectTypeInformation */, buf, sizeof(buf), &len);
    if (!st)
    {
        USHORT n = *(USHORT *)buf;
        WCHAR *name = *(WCHAR **)(buf + 8);
        printf("%s: handle %p type %.*ls\n", what, h, n / 2, name);
    }
    else printf("%s: handle %p, NtQueryObject %08lx\n", what, h, st);
}

int main(void)
{
    HMODULE dcomp = LoadLibraryA("dcomp.dll");
    create_t shared = (create_t)GetProcAddress(dcomp, (LPCSTR)1040);
    surface_t surface = (surface_t)GetProcAddress(dcomp, "DCompositionCreateSurfaceHandle");
    HANDLE h = (HANDLE)0x1234;
    HRESULT hr;

    printf("ordinal 1040 %p, surface %p\n", shared, surface);
    hr = surface(0x3 /* COMPOSITIONOBJECT_READ | WRITE */, NULL, &h);
    printf("DCompositionCreateSurfaceHandle(all, NULL, &h): hr %08lx\n", hr);
    if (!hr) describe("surface", h);

    h = (HANDLE)0x1234;
    hr = shared(0x1f0003 /* all access-like */, NULL, &h);
    printf("shared(0x1f0003, NULL, &h): hr %08lx handle %p\n", hr, h);
    if (!hr && h != (HANDLE)0x1234) describe("shared", h);

    h = (HANDLE)0x1234;
    hr = shared(0, NULL, &h);
    printf("shared(0, NULL, &h): hr %08lx handle %p\n", hr, h);
    if (!hr && h != (HANDLE)0x1234) describe("shared", h);

    h = (HANDLE)0x1234;
    hr = shared(0x1f0003, NULL, NULL);
    printf("shared(0x1f0003, NULL, NULL): hr %08lx\n", hr);
    return 0;
}
