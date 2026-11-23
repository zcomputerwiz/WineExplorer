/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2026 WineExplorer contributors */
/*
 * What CoreUICreate of the CoreMessaging.dll of the system returns. The Windows App Runtime (FrameworkUdk, CoreWindowIntegration)
 * calls it with one argument, the address of an interface pointer, and asks the object for IUnknown.
 * Build (mingw): x86_64-w64-mingw32-gcc -O2 -o coreui-probe.exe coreui-probe.c
 */
#include <windows.h>
#include <stdio.h>
#include <setjmp.h>

static jmp_buf jb;

static LONG CALLBACK veh(EXCEPTION_POINTERS *p)
{
    longjmp(jb, 1);
    return EXCEPTION_CONTINUE_SEARCH;
}

typedef HRESULT (WINAPI *create_func)( void **out );

static void describe(const char *what, void *p, int count)
{
    HMODULE mod = NULL;
    void **vtbl = p ? *(void ***)p : NULL;
    int i;

    printf("%s: %p, vtable %p\n", what, p, (void *)vtbl);
    if (!vtbl) return;
    for (i = 0; i < count; i++)
    {
        char name[MAX_PATH] = "?";

        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, vtbl[i], &mod))
            GetModuleFileNameA(mod, name, sizeof(name));
        printf("  slot %d: +%lx in %s\n", i, mod ? (unsigned long)((char *)vtbl[i] - (char *)mod) : 0, name);
    }
}

int main(void)
{
    HMODULE m;

    setvbuf(stdout, NULL, _IONBF, 0);
    m = LoadLibraryW(L"CoreMessaging.dll");

    create_func create = m ? (create_func)GetProcAddress(m, "CoreUICreate") : NULL;
    void *obj = NULL, *unk = NULL, *again = NULL;
    HRESULT hr;
    GUID iid_unknown = {0, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};

    printf("module %p, CoreUICreate %p\n", (void *)m, (void *)create);
    if (!create) return 1;
    hr = create(&obj);
    printf("CoreUICreate: %08lx\n", (ULONG)hr);
    describe("object", obj, 4);
    if (obj)
    {
        IUnknown *u = obj;

        hr = u->lpVtbl->QueryInterface(u, &iid_unknown, &unk);
        printf("QueryInterface(IUnknown): %08lx, %p (%s the object)\n", (ULONG)hr, unk, unk == obj ? "same as" : "other than");
        printf("AddRef: %lu, Release: %lu\n", u->lpVtbl->AddRef(u), u->lpVtbl->Release(u));
    }
    if (obj)
    {
        IUnknown *u = obj;
        void *other = NULL;
        GUID iid = {0x4b2edab9, 0x129b, 0x4733, {0xbe, 0x15, 0x35, 0x68, 0x53, 0xd5, 0x5a, 0xce}};

        hr = u->lpVtbl->QueryInterface(u, &iid, &other);
        printf("QueryInterface(4b2edab9-...): %08lx, %p\n", (ULONG)hr, other);
        describe("interface", other, 12);
        if (other)
        {
            /* slot 8 of the session interface: what does it give for a null or a zeroed argument, and what does that object offer? */
            typedef HRESULT (WINAPI *slot8_func)(void *self, void *arg, void **out);
            slot8_func slot8 = (*(slot8_func **)other)[8];
            char zero[256] = {0};
            void *args[2] = { NULL, zero };
            GUID iid2 = {0xfcd9d50d, 0x0902, 0x4a2d, {0xac, 0x94, 0xb9, 0x5c, 0x33, 0x16, 0xf6, 0xa0}};
            int k;

            for (k = 0; k < 2; k++)
            {
                void *item = NULL, *item_unk = NULL, *iface2 = NULL;
                HRESULT h;

                PVOID vh = AddVectoredExceptionHandler(1, veh);

                if (!setjmp(jb)) h = slot8(other, args[k], &item);
                else h = 0xdead0000;
                RemoveVectoredExceptionHandler(vh);
                printf("slot 8 (arg %s): %08lx, %p\n", k ? "zeroed buffer" : "NULL", (ULONG)h, item);
                if (!item) continue;
                describe("item", item, 8);
                h = ((IUnknown *)item)->lpVtbl->QueryInterface((IUnknown *)item, &iid_unknown, &item_unk);
                printf("item QueryInterface(IUnknown): %08lx, %p\n", (ULONG)h, item_unk);
                h = ((IUnknown *)item)->lpVtbl->QueryInterface((IUnknown *)item, &iid2, &iface2);
                printf("item QueryInterface(fcd9d50d-...): %08lx, %p\n", (ULONG)h, iface2);
                if (iface2) describe("interface 2", iface2, 8);
            }
            printf("its Release: %lu\n", ((IUnknown *)other)->lpVtbl->Release((IUnknown *)other));
        }
    }
    hr = create(&again);
    printf("second CoreUICreate: %08lx, %p (%s)\n", (ULONG)hr, again, again == obj ? "the same object" : "another object");
    hr = create(NULL);
    printf("CoreUICreate(NULL): %08lx\n", (ULONG)hr);
    return 0;
}
