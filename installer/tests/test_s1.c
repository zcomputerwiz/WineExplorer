/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2026 WineExplorer contributors */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    BOOL is_wow64 = FALSE;
    IsWow64Process(GetCurrentProcess(), &is_wow64);
    printf("Process running: %s (is_wow64=%d)\n", sizeof(void*) == 8 ? "64-bit" : "32-bit", is_wow64);

    HMODULE h = LoadLibraryA("advapi32.dll");
    if (!h) {
        printf("FAIL: LoadLibrary advapi32.dll failed (%lu)\n", GetLastError());
        return 1;
    }
    printf("SUCCESS: LoadLibrary advapi32.dll succeeded: %p\n", h);

    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeA("kernel32.dll", &handle);
    if (!size) {
        printf("FAIL: GetFileVersionInfoSizeA kernel32.dll failed (%lu)\n", GetLastError());
        return 2;
    }
    void *buf = malloc(size);
    if (!GetFileVersionInfoA("kernel32.dll", handle, size, buf)) {
        printf("FAIL: GetFileVersionInfoA failed (%lu)\n", GetLastError());
        free(buf);
        return 3;
    }
    VS_FIXEDFILEINFO *ffi = NULL;
    UINT ffi_len = 0;
    if (!VerQueryValueA(buf, "\\", (void**)&ffi, &ffi_len) || !ffi) {
        printf("FAIL: VerQueryValueA failed\n");
        free(buf);
        return 4;
    }
    printf("SUCCESS: kernel32.dll FileVersion = %d.%d.%d.%d\n",
           HIWORD(ffi->dwFileVersionMS), LOWORD(ffi->dwFileVersionMS),
           HIWORD(ffi->dwFileVersionLS), LOWORD(ffi->dwFileVersionLS));
    free(buf);
    return 0;
}
