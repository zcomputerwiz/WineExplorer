/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * A wait completion packet on a waitable timer, as CoreMessagingXP's DispatcherQueueTimer uses one: does the packet reach the port
 * when the timer is already signaled at association, when it fires later, and after it is set again?
 * Build: x86_64-w64-mingw32-gcc -O2 -o waitpkt-timer.exe waitpkt-timer.c -lntdll
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

typedef NTSTATUS (NTAPI *create_pkt_t)(HANDLE *, ACCESS_MASK, OBJECT_ATTRIBUTES *);
typedef NTSTATUS (NTAPI *assoc_t)(HANDLE, HANDLE, HANDLE, void *, void *, NTSTATUS, ULONG_PTR, BOOLEAN *);
typedef NTSTATUS (NTAPI *cancel_pkt_t)(HANDLE, BOOLEAN);
typedef NTSTATUS (NTAPI *create_port_t)(HANDLE *, ACCESS_MASK, OBJECT_ATTRIBUTES *, ULONG);
typedef NTSTATUS (NTAPI *remove_t)(HANDLE, void **, void **, IO_STATUS_BLOCK *, LARGE_INTEGER *);

static create_pkt_t pNtCreateWaitCompletionPacket;
static assoc_t pNtAssociateWaitCompletionPacket;
static cancel_pkt_t pNtCancelWaitCompletionPacket;
static create_port_t pNtCreateIoCompletion;
static remove_t pNtRemoveIoCompletion;

static const char *poll(HANDLE port, DWORD ms)
{
    static char buf[64];
    LARGE_INTEGER timeout;
    IO_STATUS_BLOCK iosb;
    void *key = NULL, *apc = NULL;
    NTSTATUS status;

    timeout.QuadPart = -(LONGLONG)ms * 10000;
    status = pNtRemoveIoCompletion(port, &key, &apc, &iosb, &timeout);
    if (status == STATUS_TIMEOUT) return "no packet";
    snprintf(buf, sizeof(buf), "packet status %#lx key %p", status, key);
    return buf;
}

static void run(const char *name, BOOL set_before, LONGLONG due, BOOL manual)
{
    HANDLE timer, port, pkt;
    BOOLEAN already = 0xcc;
    LARGE_INTEGER when;
    NTSTATUS status;

    timer = CreateWaitableTimerExW(NULL, NULL, manual ? CREATE_WAITABLE_TIMER_MANUAL_RESET : 0, TIMER_ALL_ACCESS);
    pNtCreateIoCompletion(&port, IO_COMPLETION_ALL_ACCESS, NULL, 0);
    pNtCreateWaitCompletionPacket(&pkt, GENERIC_ALL, NULL);
    when.QuadPart = due;

    if (set_before)
    {
        SetWaitableTimer(timer, &when, 0, NULL, NULL, FALSE);
        Sleep(50);
    }
    status = pNtAssociateWaitCompletionPacket(pkt, port, timer, (void *)0x1234, NULL, 0, 0, &already);
    printf("%-28s assoc %#lx already %d; ", name, status, already);
    if (!set_before) SetWaitableTimer(timer, &when, 0, NULL, NULL, FALSE);
    printf("1st: %s; ", poll(port, 200));
    /* the dispatcher re-arms the packet each round; the timer is manual-reset and still signaled unless reset */
    already = 0xcc;
    status = pNtAssociateWaitCompletionPacket(pkt, port, timer, (void *)0x5678, NULL, 0, 0, &already);
    printf("re-arm %#lx already %d, 2nd: %s; ", status, already, poll(port, 100));
    /* Start again (the timer's second Start) */
    already = 0xcc;
    status = pNtAssociateWaitCompletionPacket(pkt, port, timer, (void *)0x9abc, NULL, 0, 0, &already);
    printf("arm %#lx already %d, ", status, already);
    SetWaitableTimer(timer, &when, 0, NULL, NULL, FALSE);
    printf("after set again: %s\n", poll(port, 200));
    pNtCancelWaitCompletionPacket(pkt, TRUE);
    CloseHandle(pkt);
    CloseHandle(port);
    CloseHandle(timer);
}

int main(void)
{
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");

    pNtCreateWaitCompletionPacket = (create_pkt_t)GetProcAddress(ntdll, "NtCreateWaitCompletionPacket");
    pNtAssociateWaitCompletionPacket = (assoc_t)GetProcAddress(ntdll, "NtAssociateWaitCompletionPacket");
    pNtCancelWaitCompletionPacket = (cancel_pkt_t)GetProcAddress(ntdll, "NtCancelWaitCompletionPacket");
    pNtCreateIoCompletion = (create_port_t)GetProcAddress(ntdll, "NtCreateIoCompletion");
    pNtRemoveIoCompletion = (remove_t)GetProcAddress(ntdll, "NtRemoveIoCompletion");
    if (!pNtCreateWaitCompletionPacket || !pNtAssociateWaitCompletionPacket)
    {
        printf("no wait completion packets\n");
        return 1;
    }
    run("manual, set before, due 0", TRUE, 0, TRUE);
    run("manual, set before, -1ms", TRUE, -10000, TRUE);
    run("manual, set after, due 0", FALSE, 0, TRUE);
    run("manual, set after, -10ms", FALSE, -100000, TRUE);
    run("auto, set before, due 0", TRUE, 0, FALSE);
    run("auto, set after, -10ms", FALSE, -100000, FALSE);
    return 0;
}
