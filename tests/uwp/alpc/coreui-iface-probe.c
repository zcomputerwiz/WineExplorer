/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe, Windows 11: which of the interfaces FrameworkUdk (Windows App Runtime) queries do the objects of CoreMessaging's
 * CoreUI answer? The IIDs are the ones next to {4b2edab9-...} and {37a94c05-...} in the data of Microsoft.Internal.FrameworkUdk.dll.
 * Objects: the CoreUICreate object, its ICoreUISession interface, the item of session slot 8 and the interfaces of the item. For each
 * answer the vtable is printed (module and offset of the first slots), and the answers are compared by pointer.
 * Build (mingw): x86_64-w64-mingw32-gcc -O2 -o coreui-iface-probe.exe coreui-iface-probe.c
 */
#include <windows.h>
#include <stdio.h>
#include <setjmp.h>

static jmp_buf jb;
static ULONG_PTR last_rip, last_addr;
static DWORD last_code;
static LONG CALLBACK veh(EXCEPTION_POINTERS *p)
{
    last_code = p->ExceptionRecord->ExceptionCode;
    last_rip = p->ContextRecord->Rip;
    last_addr = p->ExceptionRecord->NumberParameters > 1 ? p->ExceptionRecord->ExceptionInformation[1] : 0;
    longjmp(jb, 1);
    return EXCEPTION_CONTINUE_SEARCH;
}

static void print_fault(void)
{
    HMODULE mod = NULL;
    char name[MAX_PATH] = "?";

    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (void *)last_rip, &mod))
        GetModuleFileNameA(mod, name, sizeof(name));
    printf("    fault %08lx at %s+0x%llx, address %p\n", last_code, strrchr(name, '\\') ? strrchr(name, '\\') + 1 : name,
           (unsigned long long)(last_rip - (ULONG_PTR)mod), (void *)last_addr);
}

typedef HRESULT (WINAPI *create_func)(void **out);

static const struct { const char *name; GUID iid; } iids[] =
{
    { "5d167ee3", {0x5d167ee3, 0xa737, 0x495a, {0x9c, 0xcc, 0x1a, 0xa7, 0xd0, 0x6e, 0xf6, 0xb1}} },
    { "53456138", {0x53456138, 0x20b5, 0x4f17, {0x8f, 0x6c, 0xf3, 0x79, 0xa1, 0xac, 0x98, 0xd0}} },
    { "4ba72ec1", {0x4ba72ec1, 0x8968, 0x4103, {0x91, 0x11, 0x0d, 0x70, 0x57, 0x5a, 0xdd, 0x0f}} },
    { "2f292ff4", {0x2f292ff4, 0x823c, 0x4a17, {0x9f, 0xd5, 0x18, 0x8c, 0x56, 0xe2, 0x52, 0xf8}} },
    { "1bf477b7", {0x1bf477b7, 0x489c, 0x4960, {0xb5, 0x67, 0x25, 0xa6, 0x14, 0xea, 0xa8, 0x1a}} },
    { "4b2edab9", {0x4b2edab9, 0x129b, 0x4733, {0xbe, 0x15, 0x35, 0x68, 0x53, 0xd5, 0x5a, 0xce}} },
    { "37a94c05", {0x37a94c05, 0x1aa6, 0x4cea, {0x90, 0x49, 0x08, 0x3b, 0xba, 0x05, 0x5c, 0xec}} },
    { "6083f66a", {0x6083f66a, 0x8533, 0x4f7e, {0xa3, 0x7e, 0x6c, 0x22, 0x21, 0x34, 0x28, 0x0b}} },
    { "9cbd9312", {0x9cbd9312, 0x070d, 0x4588, {0x9b, 0xf3, 0xbb, 0xf5, 0x28, 0xcf, 0x3e, 0x84}} },
    { "5680c979", {0x5680c979, 0x14c8, 0x48b5, {0x8b, 0x92, 0x32, 0x47, 0x37, 0x9f, 0xd9, 0x07}} },
    { "fcd9d50d", {0xfcd9d50d, 0x0902, 0x4a2d, {0xac, 0x94, 0xb9, 0x5c, 0x33, 0x16, 0xf6, 0xa0}} },
};

static void describe(void *p, int count)
{
    void **vtbl = *(void ***)p;
    int i;

    for (i = 0; i < count; i++)
    {
        HMODULE mod = NULL;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, vtbl[i], &mod))
            GetModuleFileNameA(mod, name, sizeof(name));
        printf("      slot %2d +%lx %s\n", i, mod ? (unsigned long)((char *)vtbl[i] - (char *)mod) : 0,
               strrchr(name, '\\') ? strrchr(name, '\\') + 1 : name);
    }
}

static void try_all(const char *what, void *obj)
{
    unsigned i;

    if (!obj) return;
    printf("== %s %p\n", what, obj);
    for (i = 0; i < sizeof(iids) / sizeof(iids[0]); i++)
    {
        void *out = NULL;
        HRESULT hr;
        PVOID vh = AddVectoredExceptionHandler(1, veh);

        if (!setjmp(jb)) hr = ((IUnknown *)obj)->lpVtbl->QueryInterface((IUnknown *)obj, &iids[i].iid, &out);
        else hr = 0xdead0000;
        RemoveVectoredExceptionHandler(vh);
        printf("  QI %s: %08lx %p%s\n", iids[i].name, (ULONG)hr, out, out == obj ? " (same)" : "");
        if (!hr && out)
        {
            if (out != obj && !strcmp(iids[i].name, "37a94c05")) describe(out, 12);
            ((IUnknown *)out)->lpVtbl->Release((IUnknown *)out);
        }
    }
}

int main(void)
{
    HMODULE m;
    create_func create;
    void *obj = NULL, *session = NULL, *item = NULL, *item_unk = NULL, *item_if = NULL;
    GUID iid_unknown = {0, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
    typedef HRESULT (WINAPI *slot8_func)(void *self, void *arg, void **out);

    setvbuf(stdout, NULL, _IONBF, 0);
    m = LoadLibraryW(L"CoreMessaging.dll");
    create = m ? (create_func)GetProcAddress(m, "CoreUICreate") : NULL;
    if (!create || create(&obj)) return 1;
    try_all("CoreUICreate object", obj);
    ((IUnknown *)obj)->lpVtbl->QueryInterface((IUnknown *)obj, &iids[5].iid, &session);
    try_all("session interface", session);
    if (session)
    {
        slot8_func slot8 = (*(slot8_func **)session)[8];
        HRESULT hr = slot8(session, NULL, &item);
        printf("slot 8: %08lx %p\n", (ULONG)hr, item);
        try_all("item", item);
        if (item)
        {
            ((IUnknown *)item)->lpVtbl->QueryInterface((IUnknown *)item, &iid_unknown, &item_unk);
            try_all("item IUnknown", item_unk);
            ((IUnknown *)item)->lpVtbl->QueryInterface((IUnknown *)item, &iids[10].iid, &item_if);
            try_all("item fcd9d50d", item_if);
        }
    }
    /* session slot 3 (out, b, c): the Windows App Runtime passes the address of an interface pointer, a pointer to a stack value and a
     * pointer to an object, and asks the result for {37a94c05-...}. Try zeroed and null arguments; print what the result offers. */
    if (session)
    {
        typedef HRESULT (WINAPI *slot3_func)(void *self, void **out, void *b, void *c);
        slot3_func slot3 = (*(slot3_func **)session)[3];
        char zero[256] = {0};
        void *bs[3] = { NULL, zero, zero };
        void *cs[3] = { NULL, NULL, zero };
        int k;

        for (k = 0; k < 3; k++)
        {
            void *out = NULL;
            HRESULT hr;
            PVOID vh = AddVectoredExceptionHandler(1, veh);

            if (!setjmp(jb)) hr = slot3(session, &out, bs[k], cs[k]);
            else hr = 0xdead0000;
            RemoveVectoredExceptionHandler(vh);
            printf("slot 3 (b %s, c %s): %08lx %p\n", bs[k] ? "zero" : "NULL", cs[k] ? "zero" : "NULL", (ULONG)hr, out);
            if (out)
            {
                void *unk = NULL;
                describe(out, 8);
                ((IUnknown *)out)->lpVtbl->QueryInterface((IUnknown *)out, &iid_unknown, &unk);
                try_all("slot 3 result", out);
                try_all("slot 3 result IUnknown", unk);
            }
        }
    }
    /* the object of slot 3 answers {37a94c05}; FrameworkUdk calls its slot 5 with (out pointer, 0, 2, the object). What does it make? */
    if (session)
    {
        typedef HRESULT (WINAPI *slot3_func)(void *self, void **out, void *b, void *c);
        typedef HRESULT (WINAPI *slot5_func)(void *self, void **out, void *b, void *c, void *d);
        static const GUID IID_visual_priv = {0x2c4eef28, 0x1bc0, 0x4736, {0xb7, 0xdd, 0xb6, 0x26, 0x92, 0xf9, 0xbd, 0x67}};
        void *o = NULL, *made = NULL, *unk = NULL, *priv = NULL;
        HRESULT hr;
        PVOID vh;
        int variant;

        ((slot3_func)(*(void ***)session)[3])(session, &o, NULL, NULL);
        if (!o) return 0;
        for (variant = 0; variant < 4; variant++)
        {
            static char zbuf[512];
            void *args[4][4] = { {NULL, (void *)0, (void *)2, o}, {NULL, (void *)0, (void *)0, o}, {NULL, (void *)0, (void *)1, o}, {NULL, (void *)1, (void *)2, o} };
            made = NULL;
            memset(zbuf, 0, sizeof(zbuf));
            vh = AddVectoredExceptionHandler(1, veh);
            /* (this, a = a zeroed buffer, b, int): the buffer may be an in or an out parameter */
            if (!setjmp(jb)) hr = ((slot5_func)(*(void ***)o)[5])(o, (void **)zbuf, args[variant][1], args[variant][2], args[variant][3]);
            else hr = 0xdead0000;
            RemoveVectoredExceptionHandler(vh);
            if (hr == 0xdead0000) print_fault();
            made = *(void **)zbuf;
            printf("slot 5 (buf, %p, %p) variant %d: %08lx buf[0..3] %p %p %p %p\n", args[variant][1], args[variant][2], variant, (ULONG)hr,
                   ((void **)zbuf)[0], ((void **)zbuf)[1], ((void **)zbuf)[2], ((void **)zbuf)[3]);
            if (made)
            {
                describe(made, 8);
                ((IUnknown *)made)->lpVtbl->QueryInterface((IUnknown *)made, &iid_unknown, &unk);
                printf("  IUnknown %p\n", unk);
                ((IUnknown *)made)->lpVtbl->QueryInterface((IUnknown *)made, &IID_visual_priv, &priv);
                printf("  QI 2c4eef28 (visual private): %p\n", priv);
                try_all("slot 5 result", made);
            }
        }
    }
    return 0;
}
