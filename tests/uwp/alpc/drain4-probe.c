/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box probe (Windows 11 and Wine): like drain3-probe, but the wait packet's key context is a pointer to a buffer of
 * sentinels, as CoreMessagingXP's keys are heap pointers. Does the (1,0) wake, NtUserDrainThreadCoreMessagingCompletions2 or
 * anything else write through the key? Is the packet still on the port after the wake and after the drain?
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

typedef ULONG_PTR (WINAPI *ord2669_t)(HWND, ULONG *);
typedef ULONG (WINAPI *drain2_t)(HWND, void *, void *, void *);
typedef LONG (WINAPI *create_t)(HANDLE *, ACCESS_MASK, OBJECT_ATTRIBUTES *);
typedef LONG (WINAPI *assoc_t)(HANDLE, HANDLE, HANDLE, PVOID, PVOID, LONG, ULONG_PTR, BOOLEAN *);
typedef LONG (WINAPI *remove_t)(HANDLE, ULONG_PTR *, ULONG_PTR *, IO_STATUS_BLOCK *, LARGE_INTEGER *);
typedef LONG (WINAPI *query_t)(HANDLE, int, void *, ULONG, ULONG *);

static ord2669_t p2669;
static drain2_t pDrain2;
static create_t pCreate;
static assoc_t pAssoc;
static remove_t pRemove;
static query_t pQuery;

#define SENT 0xa5a5a5a5a5a5a5a5ull
static ULONG_PTR keybuf[32];
static HANDLE g_port, g_packet, g_event;
static DWORD t0;
static int drains;
static int in_drain;
static int use_getmessage;

static LONG depth(void)
{
    LONG d = -1;
    ULONG len;
    pQuery(g_port, 0, &d, sizeof(d), &len);   /* IoCompletionBasicInformation: Depth */
    return d;
}

static void dump_key(const char *when)
{
    int i, any = 0;
    for (i = 0; i < 32; i++)
        if (keybuf[i] != SENT)
        {
            if (!any) printf("  [%4lu ms] key buffer after %s:", GetTickCount() - t0, when);
            printf(" [%d]=%#Ix", i, keybuf[i]);
            any = 1;
            keybuf[i] = SENT;
        }
    if (any) printf("\n");
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m != 0x60) return DefWindowProcW(h, m, w, l);
    printf("  [%4lu ms] 0x60 (%#Ix, %#Ix)%s%s, port depth %ld\n", GetTickCount() - t0, w, l, in_drain ? " NESTED IN DRAIN" : "",
           InSendMessageEx(NULL) & ISMEX_SEND ? " sent" : "", depth());
    dump_key("the message");
    if ((w == 1 || w == 3) && !in_drain)
    {
        ULONG_PTR b[3][4];
        ULONG r;
        int i, j;

        for (i = 0; i < 3; i++) for (j = 0; j < 4; j++) b[i][j] = SENT;
        ResetEvent(g_event);
        in_drain = 1;
        r = pDrain2(h, b[0], b[1], b[2]);
        in_drain = 0;
        drains++;
        printf("  [%4lu ms] drain returns %lu, port depth %ld\n", GetTickCount() - t0, r, depth());
        for (i = 0; i < 3; i++) for (j = 0; j < 4; j++) if (b[i][j] != SENT) printf("    buffer %d[%d] = %#Ix\n", i, j, b[i][j]);
        dump_key("the drain");
        {
            BOOLEAN already = 0xcc;
            LONG st = pAssoc(g_packet, g_port, g_event, keybuf, NULL, 0, 0, &already);
            printf("  [%4lu ms] re-associate %#lx already %d\n", GetTickCount() - t0, st, already);
        }
    }
    return 0;
}

static DWORD WINAPI signaler(void *arg)
{
    Sleep(150); SetEvent(g_event);
    Sleep(300); SetEvent(g_event);
    return 0;
}

int main(void)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll"), win32u = LoadLibraryA("win32u.dll"), user32 = LoadLibraryA("user32.dll");
    WNDCLASSW wc = {0};
    OBJECT_ATTRIBUTES oa;
    HWND hwnd;
    ULONG out = 0;
    MSG msg;
    DWORD end;
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    p2669 = (void *)GetProcAddress(user32, (LPCSTR)2669);
    pDrain2 = (void *)GetProcAddress(win32u, "NtUserDrainThreadCoreMessagingCompletions2");
    pCreate = (void *)GetProcAddress(ntdll, "NtCreateWaitCompletionPacket");
    pAssoc = (void *)GetProcAddress(ntdll, "NtAssociateWaitCompletionPacket");
    pRemove = (void *)GetProcAddress(ntdll, "NtRemoveIoCompletion");
    pQuery = (void *)GetProcAddress(ntdll, "NtQueryIoCompletion");
    if (!p2669 || !pDrain2 || !pCreate || !pAssoc || !pRemove || !pQuery) { printf("missing exports\n"); return 1; }
    for (i = 0; i < 32; i++) keybuf[i] = SENT;

    InitializeObjectAttributes(&oa, NULL, 0, NULL, NULL);
    wc.lpfnWndProc = wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = L"Drain4Probe";
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(0, L"Drain4Probe", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    g_port = (HANDLE)p2669(hwnd, &out);
    SetTimer(hwnd, 1, 0x7fffffff, NULL);
    g_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    pCreate(&g_packet, 1, &oa);
    {
        BOOLEAN already = 0xcc;
        LONG st = pAssoc(g_packet, g_port, g_event, keybuf, NULL, 0, 0, &already);
        printf("port %p, associate %#lx already %d, key %p\n", g_port, st, already, keybuf);
    }
    t0 = GetTickCount();
    CloseHandle(CreateThread(NULL, 0, signaler, NULL, 0, NULL));
    end = t0 + 900;
    use_getmessage = getenv("DRAIN4_GETMESSAGE") != NULL;
    printf("loop: %s\n", use_getmessage ? "GetMessageW" : "MsgWaitForMultipleObjectsEx + PeekMessageW");
    if (use_getmessage) SetTimer(hwnd, 2, 900, NULL);
    while (use_getmessage && GetMessageW(&msg, NULL, 0, 0))
    {
        if (msg.message == WM_TIMER && msg.wParam == 2) break;
        printf("  [%4lu ms] GetMessage returns %#x (%#Ix, %#Ix), port depth %ld\n", GetTickCount() - t0, msg.message, msg.wParam, msg.lParam, depth());
        DispatchMessageW(&msg);
    }
    while (!use_getmessage && GetTickCount() < end)
    {
        DWORD now = GetTickCount(), st;
        st = MsgWaitForMultipleObjectsEx(0, NULL, now < end ? end - now : 0, QS_ALLINPUT, 0);
        printf("  [%4lu ms] wait returns %lu, port depth %ld\n", GetTickCount() - t0, st, depth());
        dump_key("the wait");
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message != 0x60) continue;
            DispatchMessageW(&msg);
        }
    }
    {
        ULONG_PTR key, apc;
        IO_STATUS_BLOCK io;
        LARGE_INTEGER zero = {{0}};
        LONG rs = pRemove(g_port, &key, &apc, &io, &zero);
        printf("end: %d drains; port %s (key %#Ix)\n", drains, rs ? "empty" : "had a packet", rs ? 0 : key);
    }
    dump_key("the end");
    return 0;
}
