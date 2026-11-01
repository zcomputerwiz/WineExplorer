/* SPDX-License-Identifier: GPL-2.0-or-later
 * Original functional probe; no vendor SDK/source/binaries are included. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int module_version(HMODULE module)
{
    WCHAR path[MAX_PATH];
    DWORD size, unused;
    VS_FIXEDFILEINFO *info;
    UINT length;
    void *data;
    if (!GetModuleFileNameW(module, path, MAX_PATH)) return 0;
    size = GetFileVersionInfoSizeW(path, &unused);
    if (!size || !(data = malloc(size))) return 0;
    if (!GetFileVersionInfoW(path, 0, size, data) ||
        !VerQueryValueW(data, L"\\", (void **)&info, &length) ||
        length < sizeof(*info) || info->dwSignature != VS_FFI_SIGNATURE) {
        free(data);
        return 0;
    }
    printf("DLL version %u.%u.%u.%u\n", HIWORD(info->dwFileVersionMS),
           LOWORD(info->dwFileVersionMS), HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
    unused = HIWORD(info->dwFileVersionMS) == 14 && LOWORD(info->dwFileVersionMS) == 44 &&
             HIWORD(info->dwFileVersionLS) == 35211 && LOWORD(info->dwFileVersionLS) == 0;
    free(data);
    return unused != 0;
}

int main(void)
{
    typedef void *(__cdecl *copy_fn)(void *, const void *, size_t);
    HMODULE runtime = LoadLibraryW(L"vcruntime140.dll");
    HMODULE cpp = LoadLibraryW(L"msvcp140.dll");
    copy_fn copy;
    char destination[64] = {0};
    const char source[] = "original WineExplorer VC runtime probe";
    printf("PE architecture: %u-bit\n", (unsigned)(sizeof(void *) * 8));
    if (!runtime || !cpp) {
        printf("FAIL: native runtime modules unavailable (%lu)\n", GetLastError());
        return 1;
    }
    if (!module_version(runtime) || !module_version(cpp)) {
        puts("FAIL: loaded version is not the approved runtime");
        return 2;
    }
    copy = (copy_fn)(void *)GetProcAddress(runtime, "memcpy");
    if (!copy || copy(destination, source, sizeof(source)) != destination || strcmp(destination, source)) {
        puts("FAIL: actual runtime memcpy invocation");
        return 3;
    }
    FreeLibrary(cpp);
    FreeLibrary(runtime);
    puts("PASS: approved runtime loaded and native function executed");
    return 0;
}
