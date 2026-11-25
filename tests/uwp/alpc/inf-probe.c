/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe (Windows 11 and Wine): a completion arriving while the thread is blocked in MsgWaitForMultipleObjectsEx with an INFINITE
 * timeout (as CoreMessagingXP's input thread does) versus a finite timeout: what the wait returns, what the window procedure sees inside
 * the wait, and what PeekMessage then returns and calls.
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

typedef ULONG_PTR (WINAPI *init2_t)(HWND, ULONG *, ULONG_PTR, ULONG_PTR);
typedef LONG (WINAPI *create_t)(HANDLE *, ACCESS_MASK, OBJECT_ATTRIBUTES *);
typedef LONG (WINAPI *assoc_t)(HANDLE, HANDLE, HANDLE, PVOID, PVOID, LONG, ULONG_PTR, BOOLEAN *);
typedef LONG (WINAPI *setio_t)(HANDLE, ULONG_PTR, ULONG_PTR, LONG, ULONG_PTR);

static int count;
static ULONG_PTR wps[8], lps[8];
static const char *where[8];
static const char *cur = "?";
static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == 0x60 && count < 8) { wps[count] = w; lps[count] = l; where[count] = cur; count++; }
    return DefWindowProcW(h, m, w, l);
}

static HANDLE trigger;
static DWORD delay;
static DWORD WINAPI signaler(void *arg) { Sleep(delay); SetEvent(trigger); return 0; }

static void run(const char *title, DWORD timeout, int use_packet, int use_dup_as_wait_handle)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll"), win32u = LoadLibraryA("win32u.dll");
    init2_t pInit2 = (void *)GetProcAddress(win32u, "NtUserInitThreadCoreMessagingIocp2");
    setio_t pSet = (void *)GetProcAddress(ntdll, "NtSetIoCompletion");
    create_t pCreate = (void *)GetProcAddress(ntdll, "NtCreateWaitCompletionPacket");
    assoc_t pAssoc = (void *)GetProcAddress(ntdll, "NtAssociateWaitCompletionPacket");
    WNDCLASSW wc = {0};
    HWND hwnd;
    ULONG out;
    HANDLE port, packet, ev, zev = CreateEventW(NULL, FALSE, FALSE, NULL), handles[1], dup = NULL;
    OBJECT_ATTRIBUTES oa;
    MSG msg;
    DWORD st;
    int i;
    BOOL r;

    InitializeObjectAttributes(&oa, NULL, 0, NULL, NULL);
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = L"InfProbe";
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(0, L"InfProbe", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    port = (HANDLE)pInit2(hwnd, &out, 0, 0);
    ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    DuplicateHandle(GetCurrentProcess(), ev, GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS);
    trigger = ev; delay = 200;
    if (use_packet) { pCreate(&packet, 1, &oa); pAssoc(packet, port, ev, (PVOID)0x1c9a6160520ull, (PVOID)0, 0, 0, NULL); }
    handles[0] = use_dup_as_wait_handle ? dup : zev;
    CreateThread(NULL, 0, signaler, NULL, 0, NULL);
    SetTimer(NULL, 0, 2500, NULL);   /* watchdog: a timer message ends an INFINITE wait that nothing else ends */
    count = 0; cur = "MsgWait";
    st = MsgWaitForMultipleObjectsEx(1, handles, timeout, 0x1cff, 0);
    printf("%-58s wait returned %#lx", title, st);
    for (i = 0; i < count; i++) printf(" | in %s: (wp %#Ix lp %#Ix)", where[i], wps[i], lps[i]);
    count = 0; cur = "PeekMessage";
    r = PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE);
    printf(" | Peek returned %d%s", r, r ? "" : "");
    if (r) printf(" (msg %#x wp %#Ix lp %#Ix)", msg.message, msg.wParam, msg.lParam);
    for (i = 0; i < count; i++) printf(" | in %s: (wp %#Ix lp %#Ix)", where[i], wps[i], lps[i]);
    printf("\n");
    (void)pSet;
    DestroyWindow(hwnd);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    LoadLibraryA("user32.dll");
    run("finite 600 ms, packet target = event, wait handle other", 600, 1, 0);
    run("INFINITE, packet target = event, wait handle other", INFINITE, 1, 0);
    run("INFINITE, packet target = event, wait handle = dup of target", INFINITE, 1, 1);
    run("finite 600 ms, packet target = event, wait handle = dup", 600, 1, 1);
    return 0;
}
