/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe (Windows 11 and Wine): CoreMessagingXP's dispatcher round re-associates a wait packet on an object that is
 * already signaled and calls NtUserDrainThreadCoreMessagingCompletions2 right away, with no message retrieval in between. What
 * does the drain do with that packet: a nested message 0x60 (which parameters), a return value, the port's depth? And with two
 * such packets, and with a packet that carries an APC context?
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

typedef ULONG_PTR (WINAPI *ord2669_t)(HWND, ULONG *);
typedef ULONG (WINAPI *drain2_t)(HWND, void *, void *, void *);
typedef LONG (WINAPI *create_t)(HANDLE *, ACCESS_MASK, OBJECT_ATTRIBUTES *);
typedef LONG (WINAPI *assoc_t)(HANDLE, HANDLE, HANDLE, PVOID, PVOID, LONG, ULONG_PTR, BOOLEAN *);
typedef LONG (WINAPI *query_t)(HANDLE, int, void *, ULONG, ULONG *);
typedef LONG (WINAPI *remove_t)(HANDLE, ULONG_PTR *, ULONG_PTR *, IO_STATUS_BLOCK *, LARGE_INTEGER *);

static ord2669_t p2669;
static drain2_t pDrain2;
static create_t pCreate;
static assoc_t pAssoc;
static query_t pQuery;
static remove_t pRemove;
static HANDLE g_port;
static const char *where = "?";
static ULONG_PTR keybuf[2][8];
static HWND g_hwnd;
static void step(HWND hwnd, int npackets, void *apc);

static LONG depth(void)
{
    LONG d = -1;
    ULONG len;
    pQuery(g_port, 0, &d, sizeof(d), &len);
    return d;
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_USER + 5) { const char *s = where; where = (const char *)l; step(h, 1, NULL); where = s; return 0; }
    if (m != 0x60) return DefWindowProcW(h, m, w, l);
    printf("    0x60 (%#Ix, %#Ix)%s in %s, depth %ld\n", w, l, InSendMessageEx(NULL) & ISMEX_SEND ? " sent" : "", where, depth());
    return 0;
}

static void pump(const char *name)
{
    MSG msg;
    where = name;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        printf("    peeked %#x (%#Ix, %#Ix)\n", msg.message, msg.wParam, msg.lParam);
        DispatchMessageW(&msg);
    }
}

static void step(HWND hwnd, int npackets, void *apc)
{
    HANDLE ev[2], pkt[2];
    OBJECT_ATTRIBUTES oa;
    ULONG_PTR b[3][4];
    ULONG r;
    int i;

    InitializeObjectAttributes(&oa, NULL, 0, NULL, NULL);
    printf("== %d packet(s) on signaled events, APC context %p, drain called from %s\n", npackets, apc, where);
    { const char *s = where; (void)s; }
    for (i = 0; i < npackets; i++)
    {
        BOOLEAN already = 0xcc;
        LONG st;
        ev[i] = CreateEventW(NULL, TRUE, TRUE, NULL);
        pCreate(&pkt[i], 1, &oa);
        st = pAssoc(pkt[i], g_port, ev[i], keybuf[i], apc, 0, 0, &already);
        printf("  associate %d: %#lx already %d, depth %ld\n", i, st, already, depth());
    }
    { static char buf[64]; snprintf(buf, sizeof(buf), "drain (from %s)", where); where = buf; }
    memset(b, 0xa5, sizeof(b));
    r = pDrain2(hwnd, b[0], b[1], b[2]);
    printf("  drain returns %lu, depth %ld\n", r, depth());
    pump("peek after drain");
    {
        ULONG_PTR key, a;
        IO_STATUS_BLOCK io;
        LARGE_INTEGER zero = {{0}};
        while (!pRemove(g_port, &key, &a, &io, &zero)) printf("  left on the port: key %#Ix apc %#Ix\n", key, a);
    }
    for (i = 0; i < npackets; i++) { CloseHandle(pkt[i]); CloseHandle(ev[i]); }
}

int main(void)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll"), win32u = LoadLibraryA("win32u.dll"), user32 = LoadLibraryA("user32.dll");
    WNDCLASSW wc = {0};
    HWND hwnd;
    ULONG out = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    p2669 = (void *)GetProcAddress(user32, (LPCSTR)2669);
    pDrain2 = (void *)GetProcAddress(win32u, "NtUserDrainThreadCoreMessagingCompletions2");
    pCreate = (void *)GetProcAddress(ntdll, "NtCreateWaitCompletionPacket");
    pAssoc = (void *)GetProcAddress(ntdll, "NtAssociateWaitCompletionPacket");
    pQuery = (void *)GetProcAddress(ntdll, "NtQueryIoCompletion");
    pRemove = (void *)GetProcAddress(ntdll, "NtRemoveIoCompletion");
    if (!p2669 || !pDrain2 || !pCreate || !pAssoc || !pQuery || !pRemove) { printf("missing exports\n"); return 1; }
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = L"Drain5Probe";
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(0, L"Drain5Probe", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    g_port = (HANDLE)p2669(hwnd, &out);
    SetTimer(hwnd, 1, 0x7fffffff, NULL);
    printf("port %p, keys %p %p\n", g_port, keybuf[0], keybuf[1]);
    pump("start");
    step(hwnd, 1, NULL);
    step(hwnd, 2, NULL);
    step(hwnd, 1, (void *)0x5555);
    where = "main";
    printf("-- inside a sent message:\n");
    SendMessageW(hwnd, WM_USER + 5, 0, (LPARAM)"a sent message");
    pump("after sent");
    printf("-- inside a posted message:\n");
    PostMessageW(hwnd, WM_USER + 5, 0, (LPARAM)"a posted message");
    pump("posted");
    pump("after posted");
    return 0;
}
