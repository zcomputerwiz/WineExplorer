/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe (Windows 11 and Wine): the Windows App SDK's CoreMessagingXP.dll, loaded into this process, runs a
 * Microsoft.UI.Dispatching DispatcherQueue on this thread. A zero-interval DispatcherQueueTimer is started from another thread,
 * as Microsoft.ui.xaml does in Notepad. Hooks on this thread record every message 0x60 that reaches CoreMessagingXP's windows
 * (sent or posted, nesting depth), the windows' class names and styles, and when the Tick runs.
 * Only the public WinRT interfaces are used (slots as in the Windows App SDK metadata).
 * Usage: cmxp-timer-probe.exe [path to CoreMessagingXP.dll]
 */
#define COBJMACROS
#include <windows.h>
#include <stdio.h>

typedef HRESULT (WINAPI *create_string_t)(const WCHAR *, UINT32, void **);
typedef HRESULT (WINAPI *ro_init_t)(int);
typedef HRESULT (WINAPI *get_factory_t)(void *, void **);

typedef HRESULT (WINAPI *qi_t)(void *, const GUID *, void **);
typedef ULONG (WINAPI *ref_t)(void *);
typedef HRESULT (WINAPI *out_t)(void *, void **);
typedef HRESULT (WINAPI *i64_t)(void *, INT64);
typedef HRESULT (WINAPI *bool_t)(void *, BOOLEAN);
typedef HRESULT (WINAPI *void_t)(void *);
typedef HRESULT (WINAPI *add_t)(void *, void *, INT64 *);
typedef HRESULT (WINAPI *enqueue_t)(void *, void *, BOOLEAN *);

#define SLOT(obj, n) ((*(void ***)(obj))[n])

static const GUID IID_IUnknown_ = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID IID_IAgileObject_ = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};
/* Microsoft.UI.Dispatching.IDispatcherQueueControllerStatics (seen in Notepad's trace as the controller factory's interface) */
static const GUID IID_IDQControllerStatics = {0xf18d6145, 0x722b, 0x593d, {0xbc, 0xf2, 0xa6, 0x1e, 0x71, 0x3f, 0x00, 0x37}};

static DWORD t0, main_tid;
static int depth;
static int ticks;
static void *g_timer;
static HWND cm_windows[8];
static int ncm;

static DWORD ms(void) { return GetTickCount() - t0; }

static int is_cm_window(HWND h)
{
    int i;
    for (i = 0; i < ncm; i++) if (cm_windows[i] == h) return 1;
    return 0;
}

static LRESULT CALLBACK callwnd(int code, WPARAM w, LPARAM l)
{
    CWPSTRUCT *cwp = (CWPSTRUCT *)l;
    if (code == HC_ACTION && cwp->message == 0x60)
    {
        printf("  [%4lu] %*ssent 0x60 (%#Ix, %#Ix) to %p%s\n", ms(), depth * 2, "", cwp->wParam, cwp->lParam, cwp->hwnd,
               is_cm_window(cwp->hwnd) ? "" : " (other window)");
        depth++;
    }
    return CallNextHookEx(NULL, code, w, l);
}

static LRESULT CALLBACK callwndret(int code, WPARAM w, LPARAM l)
{
    CWPRETSTRUCT *cwp = (CWPRETSTRUCT *)l;
    if (code == HC_ACTION && cwp->message == 0x60 && depth > 0) depth--;
    return CallNextHookEx(NULL, code, w, l);
}

static LRESULT CALLBACK getmsg(int code, WPARAM w, LPARAM l)
{
    MSG *msg = (MSG *)l;
    if (code == HC_ACTION && msg->message == 0x60 && w == PM_REMOVE)
        printf("  [%4lu] %*sposted 0x60 (%#Ix, %#Ix) to %p retrieved\n", ms(), depth * 2, "", msg->wParam, msg->lParam, msg->hwnd);
    return CallNextHookEx(NULL, code, w, l);
}

/* a delegate: IUnknown plus Invoke at slot 3 */
struct delegate { void **vtbl; LONG ref; int kind; };
static HRESULT WINAPI d_qi(struct delegate *d, const GUID *iid, void **out)
{
    /* the parameterized delegate interfaces have generated ids: the first id asked that is not a known COM one is the delegate's */
    static GUID delegate_iid;
    static int have_iid;
    WCHAR s[64];

    StringFromGUID2(iid, s, 64);
    if (IsEqualGUID(iid, &IID_IUnknown_) || IsEqualGUID(iid, &IID_IAgileObject_)
        || (have_iid && IsEqualGUID(iid, &delegate_iid)))
    {
        *out = d;
        d->ref++;
        return S_OK;
    }
    if (!have_iid && iid->Data4[0] != 0xc0)   /* not one of the {0000xxxx-0000-0000-c000-000000000046} COM ids */
    {
        delegate_iid = *iid;
        have_iid = 1;
        printf("  delegate QI %ls: taken as the delegate's id\n", s);
        *out = d;
        d->ref++;
        return S_OK;
    }
    printf("  delegate QI %ls: no\n", s);
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI d_addref(struct delegate *d) { return ++d->ref; }
static ULONG WINAPI d_release(struct delegate *d) { return --d->ref; }
static HRESULT WINAPI d_invoke(struct delegate *d, void *a, void *b)
{
    if (d->kind == 0)
    {
        ticks++;
        printf("  [%4lu] %*sTICK %d on thread %s\n", ms(), depth * 2, "", ticks, GetCurrentThreadId() == main_tid ? "main" : "other");
    }
    else printf("  [%4lu] %*sTryEnqueue work runs on thread %s\n", ms(), depth * 2, "", GetCurrentThreadId() == main_tid ? "main" : "other");
    return S_OK;
}
static void *d_vtbl[] = { d_qi, d_addref, d_release, d_invoke };
static struct delegate tick_handler = { d_vtbl, 1, 0 }, work_handler = { d_vtbl, 1, 1 };

static void *g_queue;

static DWORD WINAPI starter(void *arg)
{
    int i;
    for (i = 0; i < 3; i++)
    {
        HRESULT hr;
        Sleep(400);
        hr = ((void_t)SLOT(g_timer, 11))(g_timer);   /* Start */
        printf("  [%4lu] Start from another thread: %#lx\n", ms(), hr);
    }
    Sleep(300);
    {
        BOOLEAN ok = 0;
        HRESULT hr = ((enqueue_t)SLOT(g_queue, 7))(g_queue, &work_handler, &ok);   /* TryEnqueue */
        printf("  [%4lu] TryEnqueue from another thread: %#lx, %d\n", ms(), hr, ok);
    }
    return 0;
}

static WNDPROC orig_proc;
static int sub_depth;
static LRESULT CALLBACK subclass_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    LRESULT r;
    printf("  [%4lu] %*sWNDPROC %#x (%#Ix, %#Ix) ism %#lx\n", ms(), sub_depth * 2, "", m, w, l, InSendMessageEx(NULL));
    sub_depth++;
    r = CallWindowProcW(orig_proc, h, m, w, l);
    sub_depth--;
    printf("  [%4lu] %*s  -> returns %#Ix\n", ms(), sub_depth * 2, "", r);
    return r;
}

static void list_windows(void)
{
    HWND h = NULL;
    while ((h = FindWindowExW(HWND_MESSAGE, h, NULL, NULL)))
    {
        WCHAR cls[128];
        WNDCLASSEXW wc = {sizeof(wc)};
        if (GetWindowThreadProcessId(h, NULL) != main_tid) continue;
        GetClassNameW(h, cls, 128);
        GetClassInfoExW(GetModuleHandleW(NULL), cls, &wc) || GetClassInfoExW((HINSTANCE)GetClassLongPtrW(h, GCLP_HMODULE), cls, &wc);
        printf("  window %p class %ls, style %#lx exstyle %#lx, class style %#lx, cls extra %d, wnd extra %d, wndproc in %s\n",
               h, cls, (DWORD)GetWindowLongPtrW(h, GWL_STYLE), (DWORD)GetWindowLongPtrW(h, GWL_EXSTYLE),
               (DWORD)GetClassLongPtrW(h, GCL_STYLE), (int)GetClassLongPtrW(h, GCL_CBCLSEXTRA), (int)GetClassLongPtrW(h, GCL_CBWNDEXTRA),
               GetWindowLongPtrW(h, GWLP_WNDPROC) ? "a module" : "?");
        if (ncm < 8) cm_windows[ncm++] = h;
        if (!wcscmp(cls, L"DcppUserAdapterWindowClass") && !orig_proc && getenv("CMXP_SUBCLASS"))
            orig_proc = (WNDPROC)SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)subclass_proc);
    }
}

int main(int argc, char **argv)
{
    static const WCHAR default_path[] = L"C:\\Program Files\\WindowsApps\\Microsoft.WindowsAppRuntime.2_2.7.0.0_x64__8wekyb3d8bbwe\\CoreMessagingXP.dll";
    HMODULE combase = LoadLibraryA("combase.dll"), cmxp;
    create_string_t create_string = (void *)GetProcAddress(combase, "WindowsCreateString");
    ro_init_t ro_init = (void *)GetProcAddress(combase, "RoInitialize");
    get_factory_t get_factory;
    WCHAR path[MAX_PATH];
    void *name, *factory = NULL, *statics = NULL, *controller = NULL, *queue = NULL, *timer = NULL;
    INT64 token = 0;
    HRESULT hr;
    MSG msg;
    DWORD end;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1) MultiByteToWideChar(CP_ACP, 0, argv[1], -1, path, MAX_PATH);
    else lstrcpyW(path, default_path);
    main_tid = GetCurrentThreadId();
    ro_init(0);   /* single-threaded, like a UI thread */
    if (!(cmxp = LoadLibraryExW(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH))) { printf("load %ls failed %lu\n", path, GetLastError()); return 1; }
    get_factory = (void *)GetProcAddress(cmxp, "DllGetActivationFactory");
    create_string(L"Microsoft.UI.Dispatching.DispatcherQueueController", 50, &name);
    hr = get_factory(name, &factory);
    printf("factory %#lx\n", hr);
    if (FAILED(hr)) return 1;
    hr = ((qi_t)SLOT(factory, 0))(factory, &IID_IDQControllerStatics, &statics);
    printf("statics %#lx\n", hr);
    if (FAILED(hr)) return 1;
    hr = ((out_t)SLOT(statics, 7))(statics, &controller);   /* CreateOnCurrentThread */
    printf("CreateOnCurrentThread %#lx\n", hr);
    if (FAILED(hr)) return 1;
    hr = ((out_t)SLOT(controller, 6))(controller, &queue);   /* get_DispatcherQueue */
    printf("get_DispatcherQueue %#lx\n", hr);
    if (FAILED(hr)) return 1;
    g_queue = queue;
    hr = ((out_t)SLOT(queue, 6))(queue, &timer);   /* CreateTimer */
    printf("CreateTimer %#lx\n", hr);
    if (FAILED(hr)) return 1;
    g_timer = timer;
    printf("put_Interval %#lx, ", ((i64_t)SLOT(timer, 7))(timer, 0));
    printf("put_IsRepeating %#lx, ", ((bool_t)SLOT(timer, 10))(timer, FALSE));
    printf("add_Tick %#lx\n", ((add_t)SLOT(timer, 13))(timer, &tick_handler, &token));

    list_windows();
    SetWindowsHookExW(WH_CALLWNDPROC, callwnd, NULL, main_tid);
    SetWindowsHookExW(WH_CALLWNDPROCRET, callwndret, NULL, main_tid);
    SetWindowsHookExW(WH_GETMESSAGE, getmsg, NULL, main_tid);

    t0 = GetTickCount();
    CloseHandle(CreateThread(NULL, 0, starter, NULL, 0, NULL));
    if ((argc > 2 && !strcmp(argv[2], "busy")) || getenv("CMXP_BUSY"))
    {
        printf("  main thread busy (no messages) for 700 ms\n");
        Sleep(700);
        printf("  [%4lu] main thread pumps\n", ms());
    }
    end = t0 + 2500;
    SetTimer(NULL, 0, 2500, NULL);
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        if (msg.message == WM_TIMER && !msg.hwnd) break;
        DispatchMessageW(&msg);
        if (GetTickCount() > end) break;
    }
    printf("end: %d ticks\n", ticks);
    if (getenv("CMXP_HANDLES"))
    {
        /* the type of every handle of this process (what CoreMessagingXP's wait packets point at) */
        typedef LONG (WINAPI *query_object_t)(HANDLE, int, void *, ULONG, ULONG *);
        query_object_t query = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryObject");
        ULONG_PTR h;

        for (h = 4; h < 0x800; h += 4)
        {
            struct { USHORT len, max; WCHAR *buf; WCHAR text[128]; } type;
            if (query((HANDLE)h, 2, &type, sizeof(type), NULL)) continue;
            printf("handle %#Ix: %.*ls\n", h, type.len / 2, type.buf);
        }
    }
    return 0;
}
