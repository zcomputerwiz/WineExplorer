/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe (Windows 11 and Wine): does a call of NtUserDrainThreadCoreMessagingCompletions2 (with NULL arguments, or with output
 * buffers like CoreMessagingXP's) before a wait packet completion change how the completion reaches the window: in the wait as (3,0),
 * or by the wait returning and PeekMessage delivering (1,0)?
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

typedef ULONG_PTR (WINAPI *init2_t)(HWND, ULONG *, ULONG_PTR, ULONG_PTR);
typedef ULONG (WINAPI *drain2_t)(HWND, void *, void *, void *);
typedef LONG (WINAPI *create_t)(HANDLE *, ACCESS_MASK, OBJECT_ATTRIBUTES *);
typedef LONG (WINAPI *assoc_t)(HANDLE, HANDLE, HANDLE, PVOID, PVOID, LONG, ULONG_PTR, BOOLEAN *);

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
static DWORD WINAPI signaler(void *arg) { Sleep(250); SetEvent(trigger); return 0; }

static void run(const char *title, int mode)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll"), win32u = LoadLibraryA("win32u.dll");
    init2_t pInit2 = (void *)GetProcAddress(win32u, "NtUserInitThreadCoreMessagingIocp2");
    drain2_t pDrain2 = (void *)GetProcAddress(win32u, "NtUserDrainThreadCoreMessagingCompletions2");
    create_t pCreate = (void *)GetProcAddress(ntdll, "NtCreateWaitCompletionPacket");
    assoc_t pAssoc = (void *)GetProcAddress(ntdll, "NtAssociateWaitCompletionPacket");
    WNDCLASSW wc = {0};
    HWND hwnd;
    ULONG out;
    HANDLE port, packet, ev, zev = CreateEventW(NULL, FALSE, FALSE, NULL), handles[1];
    OBJECT_ATTRIBUTES oa;
    MSG msg;
    DWORD st;
    int i;
    BOOL r;
    void *buf1 = calloc(1, 256), *buf2 = calloc(1, 256), *ctx = calloc(1, 256);
    ULONG dr = 99;

    InitializeObjectAttributes(&oa, NULL, 0, NULL, NULL);
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = L"DrainProbe";
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(0, L"DrainProbe", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    port = (HANDLE)pInit2(hwnd, &out, 0, 0);
    ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    trigger = ev;
    pCreate(&packet, 1, &oa);
    pAssoc(packet, port, ev, (PVOID)0x1c9a6160520ull, (PVOID)0, 0, 0, NULL);
    if (mode == 1) dr = pDrain2(hwnd, NULL, NULL, NULL);
    if (mode == 2) dr = pDrain2(hwnd, NULL, buf1, buf2);
    if (mode == 3) dr = pDrain2(hwnd, ctx, buf1, buf2);
    handles[0] = zev;
    CreateThread(NULL, 0, signaler, NULL, 0, NULL);
    SetTimer(NULL, 0, 1500, NULL);
    count = 0; cur = "MsgWait";
    st = MsgWaitForMultipleObjectsEx(1, handles, INFINITE, 0x1cff, 0);
    printf("%-56s (Drain2 -> %lu) wait returned %#lx", title, dr, st);
    for (i = 0; i < count; i++) printf(" | in %s: (wp %#Ix lp %#Ix)", where[i], wps[i], lps[i]);
    count = 0; cur = "PeekMessage";
    r = PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE);
    printf(" | Peek returned %d", r);
    if (r) printf(" (msg %#x wp %#Ix)", msg.message, msg.wParam);
    for (i = 0; i < count; i++) printf(" | in %s: (wp %#Ix lp %#Ix)", where[i], wps[i], lps[i]);
    printf("\n");
    DestroyWindow(hwnd);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    LoadLibraryA("user32.dll");
    run("no Drain2 call", 0);
    run("Drain2(hwnd, NULL, NULL, NULL) first", 1);
    run("Drain2(hwnd, NULL, buf, buf) first", 2);
    run("Drain2(hwnd, ctx, buf, buf) first", 3);
    return 0;
}
