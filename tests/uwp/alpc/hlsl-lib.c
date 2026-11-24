/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Compile an HLSL file of our own with the d3dcompiler_47 of Windows for a target (a library target such as lib_4_0_level_9_3 or
 * lib_5_0) and print the bytecode as a C byte array, for the tests of Wine's shader linker (Wine's compiler has no library targets).
 *   hlsl-lib.exe FILE.hlsl TARGET > array.txt
 * Build (mingw): x86_64-w64-mingw32-gcc -O2 -o hlsl-lib.exe hlsl-lib.c
 */
#include <windows.h>
#include <stdio.h>

typedef HRESULT (WINAPI *compile_t)(const void *, SIZE_T, const char *, const void *, void *, const char *, const char *, UINT, UINT,
                                    void **, void **);

int main(int argc, char **argv)
{
    HMODULE d = LoadLibraryA("d3dcompiler_47.dll");
    compile_t compile = d ? (compile_t)GetProcAddress(d, "D3DCompile") : NULL;
    static char src[65536];
    void *blob = NULL, *errors = NULL;
    const BYTE *p;
    size_t n, size, i;
    FILE *f;
    HRESULT hr;

    if (argc < 3 || !compile || !(f = fopen(argv[1], "rb"))) return 2;
    n = fread(src, 1, sizeof(src) - 1, f);
    fclose(f);
    /* ID3DBlob: GetBufferPointer is slot 3, GetBufferSize slot 4 */
    hr = compile(src, n, "lib.hlsl", NULL, NULL, NULL, argv[2], 0, 0, &blob, &errors);
    if (errors) fprintf(stderr, "%s\n", (const char *)((void *(STDMETHODCALLTYPE *)(void *))(*(void ***)errors)[3])(errors));
    if (FAILED(hr)) { fprintf(stderr, "hr %08lx\n", hr); return 1; }
    p = ((const BYTE *(STDMETHODCALLTYPE *)(void *))(*(void ***)blob)[3])(blob);
    size = ((SIZE_T (STDMETHODCALLTYPE *)(void *))(*(void ***)blob)[4])(blob);
    printf("    /* %Iu bytes */\n", size);
    for (i = 0; i < size; ++i)
        printf("%s0x%02x,%s", i % 16 ? " " : "    ", p[i], i % 16 == 15 || i == size - 1 ? "\n" : "");
    return 0;
}
