/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Lab tool, not a product module: a stand-in for the system library that serves Windows.UI.Internal.Input.InputSite and
 * Windows.UI.Internal.Input.ActivationConfigurationInputObject, for a Wine prefix that has no such library. It implements
 * nothing. It answers every activation and every QueryInterface with a logging object and records each call made on it
 * (interface id, slot, arguments, the module of the vtable of every object passed in), so that the calls an application
 * makes on these classes can be read from our side of the interface. What a call returns is set from outside, step by step:
 *
 *   INPUTHOST_STUB_LOG     log file (default C:\inputhost-stub.log); every line also goes to stderr
 *   INPUTHOST_STUB_REFUSE  interface ids (first 8 hex digits, comma separated) that QueryInterface refuses
 *   INPUTHOST_STUB_PLAN    semicolon separated rules IID8:SLOT=ACTION for methods (slots from 6); without a rule a method
 *                          returns E_NOTIMPL. Actions:
 *                            ok        return S_OK
 *                            obj@N     store a new logging object through the pointer in argument N (1 = rdx), return S_OK
 *                            zero@N    store 0 (8 bytes) through the pointer in argument N, return S_OK
 *                            ask@N     argument N is an object of the caller: QueryInterface it for the interface ids of
 *                                      INPUTHOST_STUB_ASK (full ids in braces, comma separated), log the results, return S_OK
 *                          Actions can be joined with '+', for example 8ca0442d:6=ask@1+obj@2
 *
 * Use in the lab prefix: copy the DLL to system32 as inputhost.dll, add under
 * HKLM\Software\Microsoft\WindowsRuntime\ActivatableClassId\<class> the values DllPath (the full path) and ActivationType 0,
 * Threading 0, TrustLevel 0 for both classes, and run with WINEDLLOVERRIDES=inputhost=n.
 * 64-bit only: a logging method does not know how many arguments it was given.
 *
 * Build (mingw): x86_64-w64-mingw32-gcc -O2 -shared -o inputhost-logstub.dll inputhost-logstub.c inputhost-logstub.def -lole32 -lruntimeobject
 * (inputhost-logstub.def exports DllGetActivationFactory and DllCanUnloadNow)
 */
#define COBJMACROS
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <activation.h>
#include <inspectable.h>
#include <stdio.h>
#include <stdarg.h>

#ifndef _WIN64
#error 64-bit only
#endif

#define SLOTS 96
#define MAX_IFACES 32
#define MAX_OBJECTS 256

static const GUID iid_unknown = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID iid_inspectable = {0xaf86e2e0, 0xb12d, 0x4c6a, {0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90}};
static const GUID iid_agile = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};
static const GUID iid_activation_factory = {0x00000035, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

struct object;
struct iface { void **vtbl; struct object *obj; GUID iid; };
struct object { int id; char origin[96]; LONG refs; struct iface ifaces[MAX_IFACES]; int nifaces; };

static struct object objects[MAX_OBJECTS];
static int nobjects;
static void *stub_vtbl[SLOTS];
static CRITICAL_SECTION cs;
static FILE *log_file;

static void stub_log(const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    int n;

    n = snprintf(line, sizeof(line), "inputhost-stub: [%5lu] ", GetCurrentThreadId());
    va_start(ap, fmt);
    vsnprintf(line + n, sizeof(line) - n, fmt, ap);
    va_end(ap);
    EnterCriticalSection(&cs);
    if (!log_file)
    {
        char path[MAX_PATH];
        if (!GetEnvironmentVariableA("INPUTHOST_STUB_LOG", path, sizeof(path))) strcpy(path, "C:\\inputhost-stub.log");
        log_file = fopen(path, "a");
    }
    if (log_file) { fprintf(log_file, "%s\n", line); fflush(log_file); }
    fprintf(stderr, "%s\n", line);
    fflush(stderr);
    LeaveCriticalSection(&cs);
}

static const char *guid_str(const GUID *g)
{
    static __thread char buf[4][40];
    static __thread int idx;
    char *s = buf[idx++ & 3];
    sprintf(s, "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", (unsigned long)g->Data1, g->Data2, g->Data3,
            g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
    return s;
}

static struct iface *iface_for(struct object *obj, const GUID *iid)
{
    struct iface *ret = NULL;
    int i;

    EnterCriticalSection(&cs);
    for (i = 0; i < obj->nifaces && !ret; i++) if (IsEqualGUID(&obj->ifaces[i].iid, iid)) ret = &obj->ifaces[i];
    if (!ret && obj->nifaces < MAX_IFACES)
    {
        ret = &obj->ifaces[obj->nifaces++];
        ret->vtbl = stub_vtbl;
        ret->obj = obj;
        ret->iid = *iid;
    }
    LeaveCriticalSection(&cs);
    return ret;
}

static struct object *new_object(const char *origin)
{
    struct object *obj = NULL;

    EnterCriticalSection(&cs);
    if (nobjects < MAX_OBJECTS)
    {
        obj = &objects[nobjects];
        obj->id = nobjects++;
        snprintf(obj->origin, sizeof(obj->origin), "%s", origin);
        obj->refs = 1;
        obj->nifaces = 0;
    }
    LeaveCriticalSection(&cs);
    if (obj)
    {
        iface_for(obj, &iid_unknown);
        stub_log("object %d created: %s", obj->id, obj->origin);
    }
    return obj;
}

static int in_env_list8(const char *var, const GUID *iid)
{
    char list[1024], id8[12];

    if (!GetEnvironmentVariableA(var, list, sizeof(list))) return 0;
    snprintf(id8, sizeof(id8), "%08lx", (unsigned long)iid->Data1);
    _strlwr(list);
    return strstr(list, id8) != NULL;
}

/* what an argument is, as far as can be told without knowing the method */
static void describe_arg(int n, ULONG_PTR value)
{
    MEMORY_BASIC_INFORMATION mbi;
    ULONG_PTR first, vt0;
    HMODULE mod = NULL;
    char path[MAX_PATH], *base;

    if (value < 0x10000 || !VirtualQuery((void *)value, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT
            || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD | PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
        return;   /* not a pointer, or a pointer to code (a leftover register): nothing is read */
    first = *(ULONG_PTR *)value;
    /* an object: its first field points into a module's data, and the first slot there points to code of a module */
    if (first > 0x10000 && VirtualQuery((void *)first, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT && mbi.Type == MEM_IMAGE
            && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD | PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)))
    {
        vt0 = *(ULONG_PTR *)first;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)vt0, &mod) && mod
                && GetModuleFileNameA(mod, path, sizeof(path)))
        {
            base = strrchr(path, '\\');
            stub_log("    argument %d is an object, vtable in %s", n, base ? base + 1 : path);
            return;
        }
    }
    stub_log("    argument %d points to %#llx", n, (unsigned long long)first);
}

static HRESULT WINAPI stub_QueryInterface(struct iface *iface, const GUID *iid, void **out)
{
    struct iface *ret;
    const GUID *use = iid;

    if (!out) return E_POINTER;
    *out = NULL;
    if (in_env_list8("INPUTHOST_STUB_REFUSE", iid))
    {
        stub_log("object %d (%s): QueryInterface %s -> E_NOINTERFACE (refused by the list)", iface->obj->id, iface->obj->origin, guid_str(iid));
        return E_NOINTERFACE;
    }
    /* one identity: IUnknown, IInspectable and IAgileObject are the first interface */
    if (IsEqualGUID(iid, &iid_inspectable) || IsEqualGUID(iid, &iid_agile)) use = &iid_unknown;
    if (!(ret = iface_for(iface->obj, use))) return E_NOINTERFACE;
    stub_log("object %d (%s): QueryInterface %s -> S_OK", iface->obj->id, iface->obj->origin, guid_str(iid));
    InterlockedIncrement(&iface->obj->refs);
    *out = ret;
    return S_OK;
}

static ULONG WINAPI stub_AddRef(struct iface *iface)
{
    return InterlockedIncrement(&iface->obj->refs);
}

static ULONG WINAPI stub_Release(struct iface *iface)
{
    LONG refs = InterlockedDecrement(&iface->obj->refs);
    if (!refs) stub_log("object %d (%s): last reference released (the object is kept for the log)", iface->obj->id, iface->obj->origin);
    return refs;
}

static HRESULT WINAPI stub_GetIids(struct iface *iface, ULONG *count, IID **iids)
{
    stub_log("object %d (%s): GetIids", iface->obj->id, iface->obj->origin);
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI stub_GetRuntimeClassName(struct iface *iface, HSTRING *name)
{
    stub_log("object %d (%s): GetRuntimeClassName", iface->obj->id, iface->obj->origin);
    *name = NULL;
    return S_OK;
}

static HRESULT WINAPI stub_GetTrustLevel(struct iface *iface, TrustLevel *level)
{
    stub_log("object %d (%s): GetTrustLevel", iface->obj->id, iface->obj->origin);
    *level = BaseTrust;
    return S_OK;
}

static void ask_object(IUnknown *obj, int n)
{
    char list[1024], *tok, *comma;

    if (!GetEnvironmentVariableA("INPUTHOST_STUB_ASK", list, sizeof(list))) return;
    for (tok = list; tok && *tok; tok = comma)
    {
        WCHAR wide[64];
        IUnknown *res = NULL;
        HRESULT hr;
        GUID iid;

        if ((comma = strchr(tok, ','))) *comma++ = 0;
        MultiByteToWideChar(CP_ACP, 0, tok, -1, wide, 64);
        if (FAILED(IIDFromString(wide, &iid))) continue;
        hr = IUnknown_QueryInterface(obj, &iid, (void **)&res);
        stub_log("    asked argument %d for %s: %08lx", n, guid_str(&iid), hr);
        if (res) IUnknown_Release(res);
    }
}

static HRESULT method_called(struct iface *iface, int slot, ULONG_PTR a[5])
{
    char plan[2048], key[32], origin[96], *rule, *end, *act, *plus;
    HRESULT hr = E_NOTIMPL;
    int i;

    stub_log("object %d (%s): slot %d of %s called, arguments %#llx %#llx %#llx, stack %#llx %#llx", iface->obj->id, iface->obj->origin, slot,
         guid_str(&iface->iid), (unsigned long long)a[0], (unsigned long long)a[1], (unsigned long long)a[2],
         (unsigned long long)a[3], (unsigned long long)a[4]);
    for (i = 0; i < 3; i++) describe_arg(i + 1, a[i]);

    if (!GetEnvironmentVariableA("INPUTHOST_STUB_PLAN", plan, sizeof(plan))) plan[0] = 0;
    _strlwr(plan);
    snprintf(key, sizeof(key), "%08lx:%d=", (unsigned long)iface->iid.Data1, slot);
    for (rule = plan; (rule = strstr(rule, key)); rule++)
        if (rule == plan || rule[-1] == ';') break;
    if (rule)
    {
        rule += strlen(key);
        if ((end = strchr(rule, ';'))) *end = 0;
        hr = S_OK;
        for (act = rule; act && *act; act = plus)
        {
            int n = 0;
            char *at;

            if ((plus = strchr(act, '+'))) *plus++ = 0;
            if ((at = strchr(act, '@'))) { *at++ = 0; n = atoi(at); }
            if (n < 0 || n > 5) n = 0;
            if (!strcmp(act, "ok")) continue;
            if (!n || a[n - 1] < 0x10000 || IsBadReadPtr((void *)a[n - 1], sizeof(void *)))
            {
                stub_log("    rule %s@%d: argument %d is not a pointer, nothing done", act, n, n);
                continue;
            }
            if (!strcmp(act, "obj") && !IsBadWritePtr((void *)a[n - 1], sizeof(void *)))
            {
                struct object *obj;

                snprintf(origin, sizeof(origin), "from slot %d of {%08lx} of object %d", slot, (unsigned long)iface->iid.Data1, iface->obj->id);
                if ((obj = new_object(origin))) *(void **)a[n - 1] = &obj->ifaces[0];
                else hr = E_OUTOFMEMORY;
            }
            else if (!strcmp(act, "zero") && !IsBadWritePtr((void *)a[n - 1], sizeof(void *))) *(ULONG_PTR *)a[n - 1] = 0;
            else if (!strcmp(act, "ask")) ask_object((IUnknown *)a[n - 1], n);
            else stub_log("    rule %s@%d: not understood", act, n);
        }
    }
    stub_log("    -> %08lx%s", hr, rule ? " (by rule)" : "");
    return hr;
}

#define M(n) static HRESULT WINAPI stub_method_##n(struct iface *iface, ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3, ULONG_PTR a4, ULONG_PTR a5) \
    { ULONG_PTR a[5] = {a1, a2, a3, a4, a5}; return method_called(iface, n, a); }
#define M10(t) M(t##0) M(t##1) M(t##2) M(t##3) M(t##4) M(t##5) M(t##6) M(t##7) M(t##8) M(t##9)
M(6) M(7) M(8) M(9) M10(1) M10(2) M10(3) M10(4) M10(5) M10(6) M10(7) M10(8) M(90) M(91) M(92) M(93) M(94) M(95)
#undef M

static void init_vtbl(void)
{
    stub_vtbl[0] = stub_QueryInterface;
    stub_vtbl[1] = stub_AddRef;
    stub_vtbl[2] = stub_Release;
    stub_vtbl[3] = stub_GetIids;
    stub_vtbl[4] = stub_GetRuntimeClassName;
    stub_vtbl[5] = stub_GetTrustLevel;
#define M(n) stub_vtbl[n] = stub_method_##n;
    M(6) M(7) M(8) M(9) M10(1) M10(2) M10(3) M10(4) M10(5) M10(6) M10(7) M10(8) M(90) M(91) M(92) M(93) M(94) M(95)
#undef M
}

HRESULT WINAPI DllGetActivationFactory(HSTRING class_id, IActivationFactory **factory)
{
    char origin[96];
    struct object *obj;
    struct iface *iface;

    snprintf(origin, sizeof(origin), "factory of %ls", class_id ? WindowsGetStringRawBuffer(class_id, NULL) : L"(null)");
    stub_log("DllGetActivationFactory %s", origin + 11);
    *factory = NULL;
    if (!(obj = new_object(origin)) || !(iface = iface_for(obj, &iid_activation_factory))) return E_OUTOFMEMORY;
    *factory = (IActivationFactory *)iface;
    return S_OK;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        InitializeCriticalSection(&cs);
        init_vtbl();
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
