/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2026 WineExplorer contributors */
/* black-box probe (Windows host): what the system-side FrameworkUdk module answers to GetFunctionTableById and GetVersionInformation */
#include <windows.h>
#include <stdio.h>

typedef void *(WINAPI *get_table_t)(unsigned int id, unsigned int *size, unsigned int *version);
typedef int (WINAPI *get_version_t)(void *buf);

static void where(void *p, char *out, size_t n)
{
    HMODULE m = NULL;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, p, &m) && m)
    {
        GetModuleFileNameA(m, name, sizeof(name));
        const char *b = strrchr(name, '\\');
        snprintf(out, n, "%s+%lx", b ? b + 1 : name, (unsigned long)((char *)p - (char *)m));
    }
    else snprintf(out, n, "%p", p);
}

int main(void)
{
    HMODULE m = LoadLibraryExW(L"Microsoft.Internal.FrameworkUdk.System.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    printf("load: %p err %lu\n", m, GetLastError());
    if (!m) return 1;
    get_table_t gt = (get_table_t)GetProcAddress(m, "GetFunctionTableById");
    get_version_t gv = (get_version_t)GetProcAddress(m, "GetVersionInformation");
    printf("GetFunctionTableById %p GetVersionInformation %p\n", gt, gv);
    if (gv)
    {
        unsigned char buf[256] = {0};
        int r = gv(buf);
        printf("GetVersionInformation: %08x\n", r);
        for (int i = 0; i < 64; i += 16)
        {
            printf("  %02x:", i);
            for (int j = 0; j < 16; j++) printf(" %02x", buf[i + j]);
            printf("\n");
        }
    }
    for (unsigned int id = 0; gt && id < 12; id++)
    {
        unsigned int size = 0xdead, ver = 0xdead;
        void **t = gt(id, &size, &ver);
        printf("table %u: %p size %u version %u\n", id, (void *)t, size, ver);
        if (!t || size == 0xdead || size > 0x400) continue;
        for (unsigned int i = 0; i < size / 8; i++)
        {
            char w[300];
            where(t[i], w, sizeof(w));
            printf("   slot %2u %s\n", i, w);
        }
    }
    {
        /* the vtable the batch-query object of the system module has (data only: module offsets of its first slots) */
        void **vt = (void **)((char *)m + 0x99618);
        for (int i = 0; i < 20; i++)
        {
            char w[300];
            where(vt[i], w, sizeof(w));
            printf("   batch-query vtable slot %2d %s\n", i, w);
        }
    }
    return 0;
}

