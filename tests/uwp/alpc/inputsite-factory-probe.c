/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe: the activation factories the Windows App SDK's input thread asks for
 * (Windows.UI.Internal.Input.InputSite, ...ActivationConfigurationInputObject, Windows.System.DispatcherQueue).
 * For each class, in a multithreaded and a single-threaded apartment:
 *   - the registration (HKLM\SOFTWARE\Microsoft\WindowsRuntime\ActivatableClassId\<class>) and whether metadata names it
 *     (RoGetMetaDataFile);
 *   - RoGetActivationFactory for IActivationFactory and for the interface the SDK requests;
 *   - on every interface obtained: GetIids, GetRuntimeClassName, GetTrustLevel, the module that holds the vtable,
 *     QueryInterface for a list of IIDs;
 *   - IActivationFactory::ActivateInstance and the same report for the instance.
 * Only exported APIs and COM calls; no method beyond IInspectable/IActivationFactory is called.
 *
 * The output has no addresses, so that compare/run-compare.ps1 can set a Wine run against the Windows result.
 *
 * Build (mingw): x86_64-w64-mingw32-gcc -O2 -o inputsite-factory-probe.exe inputsite-factory-probe.c
 * (also builds with i686-w64-mingw32-gcc; the WinRT functions are looked up in combase.dll at run time, so no import
 * library beyond the default ones is needed). Run: inputsite-factory-probe.exe [extra class names]
 */
#define COBJMACROS
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <activation.h>
#include <inspectable.h>
#include <stdio.h>

static HRESULT (WINAPI *pRoInitialize)(RO_INIT_TYPE);
static void (WINAPI *pRoUninitialize)(void);
static HRESULT (WINAPI *pRoGetActivationFactory)(HSTRING, REFIID, void **);
static HRESULT (WINAPI *pWindowsCreateString)(const WCHAR *, UINT32, HSTRING *);
static HRESULT (WINAPI *pWindowsDeleteString)(HSTRING);
static const WCHAR *(WINAPI *pWindowsGetStringRawBuffer)(HSTRING, UINT32 *);
static void (WINAPI *pCoTaskMemFree)(void *);
#define RoInitialize pRoInitialize
#define RoUninitialize pRoUninitialize
#define RoGetActivationFactory pRoGetActivationFactory
#define WindowsCreateString pWindowsCreateString
#define WindowsDeleteString pWindowsDeleteString
#define WindowsGetStringRawBuffer pWindowsGetStringRawBuffer
#define CoTaskMemFree pCoTaskMemFree

static BOOL load_functions(void)
{
    HMODULE combase = LoadLibraryA("combase.dll");

    if (!combase) return FALSE;
    pRoInitialize = (void *)GetProcAddress(combase, "RoInitialize");
    pRoUninitialize = (void *)GetProcAddress(combase, "RoUninitialize");
    pRoGetActivationFactory = (void *)GetProcAddress(combase, "RoGetActivationFactory");
    pWindowsCreateString = (void *)GetProcAddress(combase, "WindowsCreateString");
    pWindowsDeleteString = (void *)GetProcAddress(combase, "WindowsDeleteString");
    pWindowsGetStringRawBuffer = (void *)GetProcAddress(combase, "WindowsGetStringRawBuffer");
    pCoTaskMemFree = (void *)GetProcAddress(combase, "CoTaskMemFree");
    return pRoInitialize && pRoUninitialize && pRoGetActivationFactory && pWindowsCreateString && pWindowsDeleteString
           && pWindowsGetStringRawBuffer && pCoTaskMemFree;
}

static const GUID iid_activation_factory = {0x00000035, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID iid_inspectable = {0xaf86e2e0, 0xb12d, 0x4c6a, {0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90}};
static const GUID iid_agile = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};
static const GUID iid_marshal = {0x00000003, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID iid_weakref_source = {0x00000038, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID iid_closable = {0x30d5a829, 0x7fa4, 0x4026, {0x83, 0xbb, 0xd7, 0x5b, 0xae, 0x4e, 0xa9, 0x9e}};
static const GUID iid_stringable = {0x96369f54, 0x8eb6, 0x48f0, {0xab, 0xce, 0xc1, 0xb2, 0x11, 0xe6, 0x27, 0xc3}};
/* requested by the system's input component on Windows 11 (RoGetActivationFactory traces) */
static const GUID iid_inputsite_factory = {0x73f80911, 0x7a51, 0x5b0f, {0x90, 0x6b, 0xf6, 0x40, 0x2e, 0xa6, 0xc3, 0xcb}};
static const GUID iid_acio_factory = {0x4c8421ca, 0x3863, 0x5236, {0xb0, 0x9a, 0x75, 0x7c, 0xf6, 0x8c, 0x5f, 0xa5}};
static const GUID iid_dq_statics = {0xa96d83d7, 0x9371, 0x4517, {0x92, 0x45, 0xd0, 0x82, 0x4a, 0xc1, 0x2c, 0x74}};
/* requested by the app's own copy of the component when the system one is absent; refused on Windows 11 */
static const GUID iid_inputsite_old = {0x8ca0442d, 0xae80, 0x47b0, {0xaf, 0x23, 0x81, 0x3f, 0xc1, 0xda, 0x9c, 0xd0}};

static const struct { const char *name; const GUID *iid; } known_iids[] =
{
    {"IActivationFactory", &iid_activation_factory},
    {"IAgileObject", &iid_agile},
    {"IMarshal", &iid_marshal},
    {"IWeakReferenceSource", &iid_weakref_source},
    {"IClosable", &iid_closable},
    {"IStringable", &iid_stringable},
    {"{73f80911} (InputSite factory)", &iid_inputsite_factory},
    {"{4c8421ca} (ActivationConfigurationInputObject factory)", &iid_acio_factory},
    {"{a96d83d7} (IDispatcherQueueStatics)", &iid_dq_statics},
    {"{8ca0442d} (old InputSite request)", &iid_inputsite_old},
};

static const char *guid_str(const GUID *g)
{
    static char buf[4][40];
    static int idx;
    char *s = buf[idx++ & 3];
    sprintf(s, "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", (unsigned long)g->Data1, g->Data2, g->Data3,
            g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
    return s;
}

static const char *iid_name(const GUID *g)
{
    unsigned int i;
    if (IsEqualGUID(g, &iid_inspectable)) return "IInspectable";
    for (i = 0; i < sizeof(known_iids) / sizeof(known_iids[0]); i++)
        if (IsEqualGUID(g, known_iids[i].iid)) return known_iids[i].name;
    return "";
}

static void vtable_info(const char *indent, void *obj)
{
    char path[MAX_PATH] = "?", *s;
    HMODULE m0 = NULL;
    void **vt = *(void ***)obj;

    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)vt[0], &m0);
    if (m0) GetModuleFileNameA(m0, path, sizeof(path));
    s = strrchr(path, '\\');
    s = s ? s + 1 : path;
    _strlwr(s);   /* the name's case differs between systems */
    printf("%svtable in %s\n", indent, s);
}

static void inspect(const char *indent, const char *what, IInspectable *obj, BOOL qi_list)
{
    HSTRING name = NULL;
    TrustLevel trust = 0xdead;
    ULONG count = 0, i;
    IID *iids = NULL;
    HRESULT hr;

    printf("%s%s\n", indent, what);
    vtable_info(indent, obj);
    hr = IInspectable_GetRuntimeClassName(obj, &name);
    printf("%sGetRuntimeClassName: %08lx \"%ls\"\n", indent, hr, name ? WindowsGetStringRawBuffer(name, NULL) : L"(null)");
    WindowsDeleteString(name);
    hr = IInspectable_GetTrustLevel(obj, &trust);
    printf("%sGetTrustLevel: %08lx %d\n", indent, hr, (int)trust);
    hr = IInspectable_GetIids(obj, &count, &iids);
    printf("%sGetIids: %08lx count %lu\n", indent, hr, count);
    for (i = 0; SUCCEEDED(hr) && iids && i < count; i++)
    {
        IUnknown *unk = NULL;
        HRESULT hr2 = IInspectable_QueryInterface(obj, &iids[i], (void **)&unk);
        printf("%s  iid %s %s qi %08lx\n", indent, guid_str(&iids[i]), iid_name(&iids[i]), hr2);
        if (SUCCEEDED(hr2) && unk)
        {
            char ind2[32];
            snprintf(ind2, sizeof(ind2), "%s    ", indent);
            vtable_info(ind2, unk);
            IUnknown_Release(unk);
        }
    }
    CoTaskMemFree(iids);
    if (qi_list)
    {
        for (i = 0; i < sizeof(known_iids) / sizeof(known_iids[0]); i++)
        {
            IUnknown *unk = NULL;
            hr = IInspectable_QueryInterface(obj, known_iids[i].iid, (void **)&unk);
            printf("%s  QueryInterface %s: %08lx%s\n", indent, known_iids[i].name, hr,
                   SUCCEEDED(hr) && unk == (IUnknown *)obj ? " (same pointer)" : "");
            if (SUCCEEDED(hr) && unk) IUnknown_Release(unk);
        }
    }
}

static void registration(const WCHAR *class_name)
{
    WCHAR key_name[512], value[512];
    DWORD type, size, num;
    HKEY key;
    LSTATUS st;

    swprintf(key_name, 512, L"SOFTWARE\\Microsoft\\WindowsRuntime\\ActivatableClassId\\%ls", class_name);
    st = RegOpenKeyExW(HKEY_LOCAL_MACHINE, key_name, 0, KEY_READ, &key);
    if (st)
    {
        printf("  registration: none (error %ld)\n", st);
        return;
    }
    size = sizeof(value);
    if (!RegQueryValueExW(key, L"DllPath", NULL, &type, (BYTE *)value, &size))
    {
        WCHAR *base = wcsrchr(value, '\\');
        base = base ? base + 1 : value;
        _wcslwr(base);
        printf("  registration DllPath: %ls\n", base);
    }
    size = sizeof(num);
    if (!RegQueryValueExW(key, L"ActivationType", NULL, &type, (BYTE *)&num, &size)) printf("  registration ActivationType: %lu\n", num);
    size = sizeof(num);
    if (!RegQueryValueExW(key, L"Threading", NULL, &type, (BYTE *)&num, &size)) printf("  registration Threading: %lu\n", num);
    size = sizeof(num);
    if (!RegQueryValueExW(key, L"TrustLevel", NULL, &type, (BYTE *)&num, &size)) printf("  registration TrustLevel: %lu\n", num);
    RegCloseKey(key);
}

static void metadata(HSTRING hs)
{
    HRESULT (WINAPI *pRoGetMetaDataFile)(HSTRING, void *, HSTRING *, void **, unsigned int *);
    HMODULE mod = LoadLibraryA("wintypes.dll");
    HSTRING path = NULL;
    HRESULT hr;

    pRoGetMetaDataFile = mod ? (void *)GetProcAddress(mod, "RoGetMetaDataFile") : NULL;
    if (!pRoGetMetaDataFile)
    {
        printf("  RoGetMetaDataFile: not available\n");
        return;
    }
    hr = pRoGetMetaDataFile(hs, NULL, &path, NULL, NULL);
    printf("  RoGetMetaDataFile: %08lx%s\n", hr, path ? " (a metadata file names the class)" : "");
    WindowsDeleteString(path);
}

static void probe_class(const WCHAR *class_name, const GUID *requested)
{
    IActivationFactory *factory = NULL;
    IInspectable *insp = NULL, *instance = NULL;
    HSTRING hs;
    HRESULT hr;

    printf("class %ls\n", class_name);
    WindowsCreateString(class_name, (UINT32)wcslen(class_name), &hs);
    registration(class_name);
    metadata(hs);

    hr = RoGetActivationFactory(hs, &iid_activation_factory, (void **)&factory);
    printf("  RoGetActivationFactory(IActivationFactory): %08lx\n", hr);
    if (SUCCEEDED(hr) && factory)
    {
        inspect("    ", "factory", (IInspectable *)factory, TRUE);
        hr = IActivationFactory_ActivateInstance(factory, &instance);
        printf("    ActivateInstance: %08lx %s\n", hr, instance ? "an object" : "NULL");
        if (SUCCEEDED(hr) && instance)
        {
            inspect("      ", "instance", instance, TRUE);
            IInspectable_Release(instance);
        }
        IActivationFactory_Release(factory);
    }
    if (requested)
    {
        hr = RoGetActivationFactory(hs, requested, (void **)&insp);
        printf("  RoGetActivationFactory(%s): %08lx\n", guid_str(requested), hr);
        if (SUCCEEDED(hr) && insp)
        {
            inspect("    ", "requested interface", insp, FALSE);
            IInspectable_Release(insp);
        }
    }
    if (!wcscmp(class_name, L"Windows.UI.Internal.Input.InputSite"))
    {
        insp = NULL;
        hr = RoGetActivationFactory(hs, &iid_inputsite_old, (void **)&insp);
        printf("  RoGetActivationFactory(%s): %08lx\n", guid_str(&iid_inputsite_old), hr);
        if (SUCCEEDED(hr) && insp) IInspectable_Release(insp);
    }
    WindowsDeleteString(hs);
}

int main(int argc, char **argv)
{
    OSVERSIONINFOW ver = {sizeof(ver)};
    LONG (WINAPI *pRtlGetVersion)(OSVERSIONINFOW *) = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlGetVersion");
    int mode, i;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (!load_functions())
    {
        printf("combase.dll: the WinRT functions are missing\n");
        return 1;
    }
    if (pRtlGetVersion) pRtlGetVersion(&ver);
    /* the version goes to stderr: the comparison is of stdout */
    fprintf(stderr, "inputsite-factory-probe: %u-bit, system %lu.%lu.%lu\n", (unsigned int)sizeof(void *) * 8,
            ver.dwMajorVersion, ver.dwMinorVersion, ver.dwBuildNumber);
    for (mode = 0; mode < 2; mode++)
    {
        hr = RoInitialize(mode ? RO_INIT_SINGLETHREADED : RO_INIT_MULTITHREADED);
        printf("== RoInitialize(%s): %08lx\n", mode ? "STA" : "MTA", hr);
        probe_class(L"Windows.UI.Internal.Input.InputSite", &iid_inputsite_factory);
        probe_class(L"Windows.UI.Internal.Input.ActivationConfigurationInputObject", &iid_acio_factory);
        probe_class(L"Windows.System.DispatcherQueue", &iid_dq_statics);
        for (i = 1; i < argc; i++)
        {
            WCHAR name[256];
            MultiByteToWideChar(CP_ACP, 0, argv[i], -1, name, 256);
            probe_class(name, NULL);
        }
        printf("  inputhost.dll loaded: %s, coremessaging.dll loaded: %s\n",
               GetModuleHandleA("InputHost.dll") ? "yes" : "no", GetModuleHandleA("CoreMessaging.dll") ? "yes" : "no");
        RoUninitialize();
    }
    return 0;
}
