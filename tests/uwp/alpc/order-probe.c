/* Black-box probe (Windows 11 and Wine): the order in which a window procedure sees message 0x60 when, on its own thread, a
 * deferred SendMessageCallbackW(5,5), the ordinal-2582 notification (0,0) and PostMessageW(5,4) are issued back to back (as
 * CoreMessagingXP does), and what is already delivered inside each API call. */
#include <windows.h>
#include <stdio.h>

typedef INT (WINAPI *sched_t)(HWND);
typedef ULONG_PTR (WINAPI *init2_t)(HWND, ULONG *, ULONG_PTR, ULONG_PTR);
static const char *where = "?";

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == 0x60) printf("    proc 0x60 wp %#Ix lp %#Ix  [inside: %s] InSendMessageEx %#lx\n", w, l, where, InSendMessageEx(NULL));
    return DefWindowProcW(h, m, w, l);
}

static void run(const char *title, int do_init, int order)
{
    HMODULE user32 = LoadLibraryA("user32.dll"), win32u = LoadLibraryA("win32u.dll");
    sched_t pSched = (void *)GetProcAddress(user32, (LPCSTR)2582);
    init2_t pInit2 = (void *)GetProcAddress(win32u, "NtUserInitThreadCoreMessagingIocp2");
    WNDCLASSW wc = {0};
    HWND hwnd;
    ULONG out;
    MSG msg;
    int n = 0;

    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = L"OrderProbe";
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(0, L"OrderProbe", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    printf("== %s\n", title);
    if (do_init && pInit2) pInit2(hwnd, &out, 0, 0);
    where = "SendMessageCallbackW(5,5)"; SendMessageCallbackW(hwnd, 0x60, 5, 5, NULL, 1);
    if (order == 0) { where = "2582"; pSched(hwnd); }
    where = "PostMessageW(5,4)"; PostMessageW(hwnd, 0x60, 5, 4);
    if (order == 1) { where = "2582 after post"; pSched(hwnd); }
    where = "GetQueueStatus"; printf("  queue status %#lx\n", GetQueueStatus(QS_ALLINPUT));
    where = "pump";
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        printf("  pump: posted msg %#x wp %#Ix lp %#Ix returned by PeekMessage\n", msg.message, msg.wParam, msg.lParam);
        DispatchMessageW(&msg);
        if (++n > 10) break;
    }
    DestroyWindow(hwnd);
    UnregisterClassW(L"OrderProbe", wc.hInstance);
}

int main(void)
{
    run("Init2, callback (5,5), 2582, post (5,4)", 1, 0);
    run("no Init2, callback (5,5), 2582, post (5,4)", 0, 0);
    run("Init2, callback (5,5), post (5,4), 2582", 1, 1);
    return 0;
}
