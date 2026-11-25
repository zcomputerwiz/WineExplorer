/* Black-box probe: user32 ordinal 2582 on a window of this thread: is the 0x60 (wp 0, lp 0) notification delivered inside the
 * call, as a sent message (window proc runs inside PeekMessage, which returns FALSE) or as a posted one (PeekMessage returns it)? */
#include <windows.h>
#include <stdio.h>

typedef INT (WINAPI *sched_t)(HWND);
static int inside_call, got;
static const char *where = "?";

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == 0x60) { got++; printf("    proc: 0x60 wp %#Ix lp %#Ix, during: %s, InSendMessageEx %#lx\n", w, l, where, InSendMessageEx(NULL)); }
    return DefWindowProcW(h, m, w, l);
}

int main(void)
{
    sched_t pSched = (void *)GetProcAddress(LoadLibraryA("user32.dll"), (LPCSTR)2582);
    WNDCLASSW wc = {0};
    HWND hwnd;
    MSG msg;
    BOOL r;
    int round;

    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = L"Sched2Probe";
    RegisterClassW(&wc);
    for (round = 0; round < 2; round++)
    {
        hwnd = CreateWindowExW(0, L"Sched2Probe", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
        got = 0;
        where = "2582 call";
        printf("round %d: 2582 -> %d (proc calls so far: %d)\n", round, pSched(hwnd), got);
        printf("  GetQueueStatus before pump: %#lx\n", GetQueueStatus(QS_ALLINPUT));
        if (round == 1) { printf("  GetQueueStatus high word = %#lx (new msgs)\n", GetQueueStatus(QS_ALLINPUT) >> 16); }
        where = "PeekMessageW(PM_REMOVE)";
        r = PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE);
        printf("  PeekMessageW returned %d%s (msg %#x wp %#Ix lp %#Ix hwnd %p)\n", r, r ? " (a posted message)" : "", r ? msg.message : 0, r ? msg.wParam : 0, r ? msg.lParam : 0, r ? msg.hwnd : 0);
        if (r) { where = "DispatchMessageW"; DispatchMessageW(&msg); }
        r = PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE);
        printf("  second PeekMessageW returned %d\n", r);
        DestroyWindow(hwnd);
    }
    return 0;
}
