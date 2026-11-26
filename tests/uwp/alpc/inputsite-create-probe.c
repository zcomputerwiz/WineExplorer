/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe: what the system's input-site factories hand out and what they ask of their caller.
 * The call shapes come from traces of an application at the interface boundary (wintrace, _O):
 *   InputSite factory, interface {73f80911-...}, slot 6: (this, an object of the caller, out: the site)
 *   ActivationConfigurationInputObject factory, interface {4c8421ca-...}, slot 6: (this, the site, out: the object)
 *   on that object: slots 7 and 9, each with a second argument of 0
 * This program makes the same calls with an object of its own in place of the caller's. That object answers
 * QueryInterface as the first argument says and logs every interface id asked for and every method called on it:
 *   none   only IUnknown, IInspectable, IAgileObject
 *   all    every interface id (each gets its own logging stub; methods return E_NOTIMPL)
 *   IID... the listed interface ids (braces included), besides the three above
 *   sink0, sinkbad   as sink, but the handle handed out is NULL or an event
 *   sink   as all, and the two questions the system was seen asking get an answer: slot 9 of {92179cc0-...} gives a
 *          composition input sink that this program makes for a window of its own (NtCreateCompositionInputSink, block as in
 *          sink-probe2.c), slot 9 of {01dc794b-...} gives the host object itself
 * The second argument chooses the thread: sta (default), mta, or dq (a single-threaded apartment with a dispatcher queue made
 * by CreateDispatcherQueueController, and a message loop of two seconds after the calls).
 * For every object obtained: GetIids, GetRuntimeClassName, GetTrustLevel, QueryInterface for the ids seen so far.
 * A third argument names a library whose DllGetActivationFactory is used in place of the system's factories
 * (inputhost-logstub.dll: the stand-in for a Wine prefix, tried here with the calls of the application).
 * 64-bit only (the logging stubs do not know how many arguments a method takes).
 *
 * Build (mingw): x86_64-w64-mingw32-gcc -O2 -municode -o inputsite-create-probe.exe inputsite-create-probe.c -lole32 -lruntimeobject -luuid
 */
#define COBJMACROS
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <activation.h>
#include <inspectable.h>
#include <stdio.h>

static const GUID iid_unknown = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID iid_inspectable = {0xaf86e2e0, 0xb12d, 0x4c6a, {0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90}};
static const GUID iid_agile = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};
static const GUID iid_marshal = {0x00000003, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID iid_weakref_source = {0x00000038, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID iid_inputsite_factory = {0x73f80911, 0x7a51, 0x5b0f, {0x90, 0x6b, 0xf6, 0x40, 0x2e, 0xa6, 0xc3, 0xcb}};
static const GUID iid_inputsite_old = {0x8ca0442d, 0xae80, 0x47b0, {0xaf, 0x23, 0x81, 0x3f, 0xc1, 0xda, 0x9c, 0xd0}};
static const GUID iid_inputsite_factory2 = {0x24701c1a, 0x88f5, 0x4598, {0xb3, 0xe4, 0x85, 0x50, 0x85, 0xcf, 0x2e, 0xf1}};
static const GUID iid_acio_factory = {0x4c8421ca, 0x3863, 0x5236, {0xb0, 0x9a, 0x75, 0x7c, 0xf6, 0x8c, 0x5f, 0xa5}};
static const GUID iid_site = {0xcfb32fa7, 0x5399, 0x5f3d, {0x82, 0xba, 0x96, 0x41, 0xf1, 0x06, 0xff, 0xcd}};

static const char *step = "start";

static const char *guid_str(const GUID *g)
{
    static char buf[4][40];
    static int idx;
    char *s = buf[idx++ & 3];
    sprintf(s, "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", (unsigned long)g->Data1, g->Data2, g->Data3,
            g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
    return s;
}

/* the caller's object: one stub per interface id that was answered */
#define MAX_STUBS 48
#define SLOTS 64
struct stub { void **vtbl; GUID iid; int index; };
static struct stub stubs[MAX_STUBS];
static int nstubs;
static LONG host_refs = 1;
static int answer_all;
static GUID answer_list[32];
static int nanswer;
static GUID seen[64];
static int nseen;
static void *stub_vtbl[SLOTS];

static void remember(const GUID *iid)
{
    int i;
    for (i = 0; i < nseen; i++) if (IsEqualGUID(&seen[i], iid)) return;
    if (nseen < 64) seen[nseen++] = *iid;
}

static struct stub *stub_for(const GUID *iid)
{
    int i;
    for (i = 0; i < nstubs; i++) if (IsEqualGUID(&stubs[i].iid, iid)) return &stubs[i];
    if (nstubs >= MAX_STUBS) return NULL;
    stubs[nstubs].vtbl = stub_vtbl;
    stubs[nstubs].iid = *iid;
    stubs[nstubs].index = nstubs;
    return &stubs[nstubs++];
}

static HRESULT WINAPI stub_QueryInterface(struct stub *stub, const GUID *iid, void **out)
{
    int i, ok;

    ok = IsEqualGUID(iid, &iid_unknown) || IsEqualGUID(iid, &iid_inspectable) || IsEqualGUID(iid, &iid_agile) || answer_all;
    for (i = 0; i < nanswer && !ok; i++) ok = IsEqualGUID(iid, &answer_list[i]);
    printf("    host: QueryInterface(%s) on stub %d -> %s\n", guid_str(iid), stub->index, ok ? "S_OK" : "E_NOINTERFACE");
    remember(iid);
    *out = NULL;
    if (!ok) return E_NOINTERFACE;
    /* IUnknown is always the first stub: one identity */
    *out = IsEqualGUID(iid, &iid_unknown) ? &stubs[0] : stub_for(iid);
    if (!*out) return E_NOINTERFACE;
    InterlockedIncrement(&host_refs);
    return S_OK;
}

static ULONG WINAPI stub_AddRef(struct stub *stub)
{
    return InterlockedIncrement(&host_refs);
}

static ULONG WINAPI stub_Release(struct stub *stub)
{
    return InterlockedDecrement(&host_refs);
}

static HRESULT WINAPI stub_GetIids(struct stub *stub, ULONG *count, IID **iids)
{
    printf("    host: GetIids on stub %d\n", stub->index);
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI stub_GetRuntimeClassName(struct stub *stub, HSTRING *name)
{
    printf("    host: GetRuntimeClassName on stub %d\n", stub->index);
    *name = NULL;
    return S_OK;
}

static HRESULT WINAPI stub_GetTrustLevel(struct stub *stub, TrustLevel *level)
{
    printf("    host: GetTrustLevel on stub %d\n", stub->index);
    *level = BaseTrust;
    return S_OK;
}

static int give_sink;
static HANDLE sink;
static const GUID iid_host_sink = {0x92179cc0, 0xae1f, 0x48bc, {0xa3, 0x54, 0x8f, 0x7a, 0x88, 0x00, 0xf2, 0xfb}};
static const GUID iid_host_object = {0x01dc794b, 0x4ff5, 0x4491, {0x99, 0x42, 0xb9, 0xe7, 0xb8, 0x89, 0x3b, 0xe4}};

static HRESULT method_called(struct stub *stub, int slot, void *a1, void *a2, void *a3)
{
    if (give_sink && slot == 9 && a1 && IsEqualGUID(&stub->iid, &iid_host_sink))
    {
        printf("    host: slot 9 of %s called -> the input sink, S_OK\n", guid_str(&stub->iid));
        *(HANDLE *)a1 = sink;
        return S_OK;
    }
    if (give_sink && slot == 9 && a1 && IsEqualGUID(&stub->iid, &iid_host_object))
    {
        printf("    host: slot 9 of %s called -> the host object, S_OK\n", guid_str(&stub->iid));
        InterlockedIncrement(&host_refs);
        *(void **)a1 = &stubs[0];
        return S_OK;
    }
    printf("    host: slot %d of %s (stub %d) called, arguments %p %p %p -> E_NOTIMPL\n", slot, guid_str(&stub->iid), stub->index, a1, a2, a3);
    return E_NOTIMPL;
}

#define M(n) static HRESULT WINAPI stub_method_##n(struct stub *stub, void *a1, void *a2, void *a3) { return method_called(stub, n, a1, a2, a3); }
#ifdef __i386__
/* 32-bit: the method must remove exactly its own arguments. Slot 9 is the one the system calls; one argument (an out
 * pointer) is what the 64-bit calls show. The other stubs are not right for any call there. */
static HRESULT WINAPI stub_method_9(struct stub *stub, void *a1) { return method_called(stub, 9, a1, NULL, NULL); }
#define M9
#else
#define M9 M(9)
#endif
M(6) M(7) M(8) M9 M(10) M(11) M(12) M(13) M(14) M(15) M(16) M(17) M(18) M(19) M(20) M(21) M(22) M(23) M(24) M(25) M(26) M(27) M(28) M(29)
M(30) M(31) M(32) M(33) M(34) M(35) M(36) M(37) M(38) M(39) M(40) M(41) M(42) M(43) M(44) M(45) M(46) M(47) M(48) M(49) M(50) M(51) M(52)
M(53) M(54) M(55) M(56) M(57) M(58) M(59) M(60) M(61) M(62) M(63)
#undef M

static void init_stubs(void)
{
    stub_vtbl[0] = stub_QueryInterface;
    stub_vtbl[1] = stub_AddRef;
    stub_vtbl[2] = stub_Release;
    stub_vtbl[3] = stub_GetIids;
    stub_vtbl[4] = stub_GetRuntimeClassName;
    stub_vtbl[5] = stub_GetTrustLevel;
#define M(n) stub_vtbl[n] = stub_method_##n;
    M(6) M(7) M(8) M(9) M(10) M(11) M(12) M(13) M(14) M(15) M(16) M(17) M(18) M(19) M(20) M(21) M(22) M(23) M(24) M(25) M(26) M(27) M(28) M(29)
    M(30) M(31) M(32) M(33) M(34) M(35) M(36) M(37) M(38) M(39) M(40) M(41) M(42) M(43) M(44) M(45) M(46) M(47) M(48) M(49) M(50) M(51) M(52)
    M(53) M(54) M(55) M(56) M(57) M(58) M(59) M(60) M(61) M(62) M(63)
#undef M
    stub_for(&iid_unknown);
}

static void inspect(const char *what, IInspectable *obj)
{
    static const struct { const char *name; const GUID *iid; } fixed[] =
    {
        {"IAgileObject", &iid_agile}, {"IMarshal", &iid_marshal}, {"IWeakReferenceSource", &iid_weakref_source},
        {"{cfb32fa7} (asked of the site by the application)", &iid_site},
    };
    HSTRING name = NULL;
    TrustLevel trust = 0xdead;
    ULONG count = 0, i;
    IID *iids = NULL;
    IUnknown *unk;
    HRESULT hr;

    printf("  %s:\n", what);
    hr = IInspectable_GetRuntimeClassName(obj, &name);
    printf("    GetRuntimeClassName: %08lx \"%ls\"\n", hr, name ? WindowsGetStringRawBuffer(name, NULL) : L"(null)");
    WindowsDeleteString(name);
    hr = IInspectable_GetTrustLevel(obj, &trust);
    printf("    GetTrustLevel: %08lx %d\n", hr, (int)trust);
    hr = IInspectable_GetIids(obj, &count, &iids);
    printf("    GetIids: %08lx count %lu\n", hr, count);
    for (i = 0; SUCCEEDED(hr) && iids && i < count; i++)
    {
        unk = NULL;
        printf("      iid %s, QueryInterface %08lx", guid_str(&iids[i]), IInspectable_QueryInterface(obj, &iids[i], (void **)&unk));
        printf("%s\n", unk == (IUnknown *)obj ? " (this pointer)" : "");
        if (unk) IUnknown_Release(unk);
    }
    CoTaskMemFree(iids);
    for (i = 0; i < sizeof(fixed) / sizeof(fixed[0]); i++)
    {
        unk = NULL;
        printf("    QueryInterface %s: %08lx\n", fixed[i].name, IInspectable_QueryInterface(obj, fixed[i].iid, (void **)&unk));
        if (unk) IUnknown_Release(unk);
    }
    for (i = 0; i < (ULONG)nseen; i++)
    {
        unk = NULL;
        printf("    QueryInterface %s (asked of the host): %08lx\n", guid_str(&seen[i]), IInspectable_QueryInterface(obj, &seen[i], (void **)&unk));
        if (unk) IUnknown_Release(unk);
    }
}

static LONG WINAPI crashed(EXCEPTION_POINTERS *info)
{
    printf("EXCEPTION %08lx during: %s\n", info->ExceptionRecord->ExceptionCode, step);
    fflush(stdout);
    ExitProcess(3);
}

/* the factories come from the system (RoGetActivationFactory) or, with a third argument, from that library's
 * DllGetActivationFactory (to try inputhost-logstub.dll with the calls an application makes) */
static HRESULT (WINAPI *get_factory)(HSTRING, IActivationFactory **);

static HRESULT factory_of(HSTRING class_name, const GUID *iid, void **out)
{
    IActivationFactory *factory = NULL;
    HRESULT hr;

    if (!get_factory) return RoGetActivationFactory(class_name, iid, out);
    if (FAILED(hr = get_factory(class_name, &factory))) return hr;
    hr = IActivationFactory_QueryInterface(factory, iid, out);
    IActivationFactory_Release(factory);
    return hr;
}

/* On 32-bit Windows a method removes its own arguments from the stack. There the calls go through wrappers that say how
 * many bytes were left on the stack (0 when the assumed number of arguments is right) and put the stack right again. */
static int stack_left;

#ifdef __i386__
static HRESULT call3(void *fn, void *a0, void *a1, void *a2)
{
    HRESULT hr;
    int left, d1, d2;

    __asm__ __volatile__("movl %%esp, %%esi\n\tpushl %%edx\n\tpushl %%ecx\n\tpushl %%ebx\n\tcall *%%edi\n\t"
                         "subl %%esp, %%esi\n\taddl %%esi, %%esp"
                         : "=a"(hr), "=S"(left), "=c"(d1), "=d"(d2) : "D"(fn), "b"(a0), "c"(a1), "d"(a2) : "memory", "cc");
    stack_left = left;
    return hr;
}

static HRESULT call2(void *fn, void *a0, UINT_PTR a1)
{
    HRESULT hr;
    int left, d1, d2;

    __asm__ __volatile__("movl %%esp, %%esi\n\tpushl %%ecx\n\tpushl %%ebx\n\tcall *%%edi\n\t"
                         "subl %%esp, %%esi\n\taddl %%esi, %%esp"
                         : "=a"(hr), "=S"(left), "=c"(d1), "=d"(d2) : "D"(fn), "b"(a0), "c"(a1) : "memory", "cc");
    stack_left = left;
    return hr;
}
#else
static HRESULT call3(void *fn, void *a0, void *a1, void *a2)
{
    return ((HRESULT (WINAPI *)(void *, void *, void *))fn)(a0, a1, a2);
}

static HRESULT call2(void *fn, void *a0, UINT_PTR a1)
{
    return ((HRESULT (WINAPI *)(void *, UINT_PTR))fn)(a0, a1);
}
#endif

int wmain(int argc, WCHAR **argv)
{
    static const WCHAR site_class[] = L"Windows.UI.Internal.Input.InputSite";
    static const WCHAR acio_class[] = L"Windows.UI.Internal.Input.ActivationConfigurationInputObject";
    const WCHAR *thread_mode = argc > 2 ? argv[2] : L"sta";
    IInspectable *site_factory = NULL, *acio_factory = NULL, *site = NULL, *site_default = NULL, *acio = NULL;
    IUnknown *controller = NULL, *unk;
    HSTRING hs;
    HRESULT hr;
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    SetUnhandledExceptionFilter(crashed);
    init_stubs();
    if (argc > 1 && !wcscmp(argv[1], L"all")) answer_all = 1;
    else if (argc > 1 && !wcsncmp(argv[1], L"sink", 4))
    {
        LONG (NTAPI *create_sink)(const void *block, HANDLE *out);
        HMODULE win32u = LoadLibraryA("win32u.dll");
        BYTE block[0xe8];
        HWND hwnd;
        LONG status;
        int e;

        answer_all = give_sink = 1;
        hwnd = CreateWindowW(L"static", L"inputsite-create-probe", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, 0, 0, 0, 0);
        memset(block, 0, sizeof(block));
        *(ULONG_PTR *)block = sizeof(block);
        for (e = 0; e < 4; e++)
        {
            *(ULONG_PTR *)(block + 8 + e * 0x38) = 2;
            *(HWND *)(block + 16 + e * 0x38) = hwnd;
        }
        create_sink = win32u ? (void *)GetProcAddress(win32u, "NtCreateCompositionInputSink") : NULL;
        status = create_sink ? create_sink(block, &sink) : -1;
        printf("window %s, NtCreateCompositionInputSink: %s, status %08lx, sink %s\n", hwnd ? "created" : "FAILED",
               create_sink ? "exported" : "not exported by win32u.dll", (ULONG)status, sink ? "made" : "none");
        if (!wcscmp(argv[1], L"sink0"))
        {
            sink = NULL;
            printf("the host will hand out a NULL handle in place of the sink\n");
        }
        else if (!wcscmp(argv[1], L"sinkbad"))
        {
            sink = CreateEventW(NULL, FALSE, FALSE, NULL);
            printf("the host will hand out an event handle in place of the sink\n");
        }
    }
    else if (argc > 1 && wcscmp(argv[1], L"none"))
    {
        WCHAR *tok = _wcsdup(argv[1]), *comma;
        for (; tok && nanswer < 32; tok = comma)
        {
            if ((comma = wcschr(tok, ','))) *comma++ = 0;
            if (SUCCEEDED(IIDFromString(tok, &answer_list[nanswer]))) nanswer++;
        }
    }
    printf("inputsite-create-probe: host answers %ls, thread %ls\n", argc > 1 ? argv[1] : L"none", thread_mode);

    hr = RoInitialize(!wcscmp(thread_mode, L"mta") ? RO_INIT_MULTITHREADED : RO_INIT_SINGLETHREADED);
    printf("RoInitialize: %08lx\n", hr);
    if (!wcscmp(thread_mode, L"dq"))
    {
        /* DispatcherQueueOptions: size, DQTYPE_THREAD_CURRENT (2), DQTAT_COM_NONE (0) */
        struct { DWORD size; int thread_type; int apartment_type; } options = {12, 2, 0};
        HRESULT (WINAPI *create_controller)(void *, IUnknown **);
        HMODULE mod = LoadLibraryA("CoreMessaging.dll");

        create_controller = mod ? (void *)GetProcAddress(mod, "CreateDispatcherQueueController") : NULL;
        hr = create_controller ? create_controller(&options, &controller) : E_NOTIMPL;
        printf("CreateDispatcherQueueController: %08lx\n", hr);
    }

    if (argc > 3)
    {
        HMODULE mod = LoadLibraryW(argv[3]);
        get_factory = mod ? (void *)GetProcAddress(mod, "DllGetActivationFactory") : NULL;
        printf("factories from %ls: %s\n", argv[3], get_factory ? "DllGetActivationFactory found" : "not loaded");
        if (!get_factory) return 1;
    }
    WindowsCreateString(site_class, (UINT32)wcslen(site_class), &hs);
    hr = factory_of(hs, &iid_inputsite_factory, (void **)&site_factory);
    WindowsDeleteString(hs);
    printf("InputSite factory {73f80911}: %08lx\n", hr);
    if (hr == E_NOINTERFACE)
    {
        /* Windows 10 serves {8ca0442d-...} instead. Its slot 6 is called with the same arguments: a guess from the
         * newer interface, not a measured shape; the lines that follow say what that call did */
        WindowsCreateString(site_class, (UINT32)wcslen(site_class), &hs);
        hr = factory_of(hs, &iid_inputsite_old, (void **)&site_factory);
        WindowsDeleteString(hs);
        printf("InputSite factory {8ca0442d}: %08lx (the calls below use this interface)\n", hr);
    }
    if (FAILED(hr) || !site_factory) return 1;

    step = "InputSite factory slot 6 (host, out)";
    hr = call3((*(void ***)site_factory)[6], site_factory, &stubs[0], &site);
    printf("InputSite factory slot 6 (host, out): %08lx, site %s, %d bytes left on the stack\n", hr, site ? "returned" : "NULL", stack_left);
    if (SUCCEEDED(hr) && site)
    {
        step = "inspect site";
        inspect("site", site);
        hr = IInspectable_QueryInterface(site, &iid_unknown, (void **)&unk);
        if (SUCCEEDED(hr))
        {
            hr = IUnknown_QueryInterface(unk, &iid_site, (void **)&site_default);
            printf("  site -> IUnknown -> {cfb32fa7}: %08lx%s\n", hr, site_default == site ? " (the pointer the factory returned)" : "");
            IUnknown_Release(unk);
        }
        if (!site_default)
        {
            /* Windows 10: another default interface; the pointer the factory returned is passed on as it is */
            site_default = site;
            IInspectable_AddRef(site_default);
            printf("  the pointer the factory returned is used as the site\n");
        }

        WindowsCreateString(acio_class, (UINT32)wcslen(acio_class), &hs);
        hr = factory_of(hs, &iid_acio_factory, (void **)&acio_factory);
        WindowsDeleteString(hs);
        printf("ActivationConfigurationInputObject factory {4c8421ca}: %08lx\n", hr);
        if (SUCCEEDED(hr) && acio_factory && site_default)
        {
            step = "ActivationConfigurationInputObject factory slot 6 (site, out)";
            hr = call3((*(void ***)acio_factory)[6], acio_factory, site_default, &acio);
            printf("ActivationConfigurationInputObject factory slot 6 (site, out): %08lx, object %s, %d bytes left on the stack\n",
                   hr, acio ? "returned" : "NULL", stack_left);
        }
        if (acio)
        {
            step = "inspect configuration object";
            inspect("configuration object", acio);
            step = "configuration object slot 7 (0)";
            hr = call2((*(void ***)acio)[7], acio, 0);
            printf("  configuration object slot 7 (0): %08lx, %d bytes left on the stack\n", hr, stack_left);
            step = "configuration object slot 9 (0)";
            hr = call2((*(void ***)acio)[9], acio, 0);
            printf("  configuration object slot 9 (0): %08lx, %d bytes left on the stack\n", hr, stack_left);
            /* a guess, not a measured shape: slots 6 and 8 read as the getters of what 7 and 9 set (one out pointer) */
            for (i = 6; i <= 8; i += 2)
            {
                static char what[64];
                BYTE buf[16];
                int pass;

                for (pass = 0; pass < 3; pass++)
                {
                    snprintf(what, sizeof(what), "configuration object slot %d read as a getter", i);
                    step = what;
                    memset(buf, 0xaa, sizeof(buf));
                    hr = call2((*(void ***)acio)[i], acio, (UINT_PTR)buf);
                    printf("  slot %d as a getter: %08lx, %d bytes left on the stack, buffer %02x %02x %02x %02x %02x %02x %02x %02x\n",
                           i, hr, stack_left, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
                    if (pass == 2) break;
                    snprintf(what, sizeof(what), "configuration object slot %d (%d)", i + 1, !pass);
                    hr = call2((*(void ***)acio)[i + 1], acio, !pass);
                    printf("  slot %d (%d): %08lx\n", i + 1, !pass, hr);
                }
            }
        }
    }
    else if (FAILED(hr))
    {
        /* which interface of the factory is the other one? only its presence is recorded */
        unk = NULL;
        printf("InputSite factory QueryInterface {24701c1a}: %08lx\n", IInspectable_QueryInterface(site_factory, &iid_inputsite_factory2, (void **)&unk));
        if (unk) IUnknown_Release(unk);
    }

    if (!wcscmp(thread_mode, L"dq"))
    {
        DWORD end = GetTickCount() + 2000;
        MSG msg;

        step = "message loop";
        printf("message loop, 2 s\n");
        while (GetTickCount() < end)
        {
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
            MsgWaitForMultipleObjects(0, NULL, FALSE, 50, QS_ALLINPUT);
        }
    }
    step = "release";
    if (acio) printf("configuration object Release: %lu\n", IInspectable_Release(acio));
    if (site_default) IInspectable_Release(site_default);
    if (site) printf("site Release: %lu\n", IInspectable_Release(site));
    if (acio_factory) IInspectable_Release(acio_factory);
    IInspectable_Release(site_factory);
    printf("host references left: %ld, interface ids asked of the host: %d\n", host_refs, nseen);
    for (i = 0; i < nseen; i++) printf("  %s\n", guid_str(&seen[i]));
    printf("done\n");
    return 0;
}
