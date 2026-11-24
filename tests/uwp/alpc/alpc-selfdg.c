/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2026 WineExplorer contributors */
/*
 * ALPC black-box probe: the datagram a server sends to its own connection port (flag 0x10000 and message id 0, which CoreMessaging
 * uses to wake its receive loop), then received on that port with the context attribute: what do ValidAttributes and PortContext
 * hold? (Wine returned the port handle there, and CoreMessagingXP of the Windows App Runtime read it as a pointer and crashed.)
 * Build (mingw): x86_64-w64-mingw32-gcc -O2 -o alpc-selfdg.exe alpc-selfdg.c
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef LONG NTSTATUS;
typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef struct { ULONG Length; HANDLE RootDirectory; USTR *ObjectName; ULONG Attributes; PVOID SecurityDescriptor; PVOID SecurityQualityOfService; } OBJATTR;
typedef struct { ULONG Length; ULONG ImpersonationLevel; BOOLEAN ContextTrackingMode; BOOLEAN EffectiveOnly; } AQOS;

#pragma pack(push, 8)
typedef struct
{
    USHORT DataLength, TotalLength;
    USHORT Type, DataInfoOffset;
    ULONG_PTR ClientProcess, ClientThread;
    ULONG MessageId;
    ULONG_PTR ClientViewSize;
} APMSG;
typedef struct
{
    ULONG Flags;
    AQOS SecurityQos;
    SIZE_T MaxMessageLength, MemoryBandwidth, MaxPoolUsage, MaxSectionSize, MaxViewSize, MaxTotalSectionSize;
    ULONG DupObjectTypes;
    ULONG Reserved;
} APATTR;
typedef struct { ULONG AllocatedAttributes, ValidAttributes; } AMATTR;
typedef struct { void *PortContext, *MessageContext; ULONG Sequence, MessageId, CallbackId; } CTXATTR;
#pragma pack(pop)

static NTSTATUS (WINAPI *pCreatePort)(HANDLE *, OBJATTR *, APATTR *);
static NTSTATUS (WINAPI *pSendWaitReceive)(HANDLE, ULONG, APMSG *, AMATTR *, APMSG *, SIZE_T *, AMATTR *, LARGE_INTEGER *);
static NTSTATUS (WINAPI *pInitAttr)(ULONG, AMATTR *, SIZE_T, SIZE_T *);
static void *(WINAPI *pGetAttr)(AMATTR *, ULONG);
static void (WINAPI *pInitUstr)(USTR *, const WCHAR *);

int main(void)
{
    static const WCHAR name[] = L"\\RPC Control\\WexAlpcSelfDg";
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    USTR us;
    OBJATTR oa = { sizeof(oa) };
    APATTR pa = { 0 };
    HANDLE port = NULL;
    NTSTATUS st;
    LARGE_INTEGER to;
    char sb[sizeof(APMSG) + 8], rb[0x200], abuf[256];
    SIZE_T size, req;
    APMSG *m = (APMSG *)sb, *r = (APMSG *)rb;
    AMATTR *a = (AMATTR *)abuf;
    int i;

#define GET(n, f) *(FARPROC *)&f = GetProcAddress(nt, n)
    GET("NtAlpcCreatePort", pCreatePort); GET("NtAlpcSendWaitReceivePort", pSendWaitReceive);
    GET("AlpcInitializeMessageAttribute", pInitAttr); GET("AlpcGetMessageAttribute", pGetAttr);
    GET("RtlInitUnicodeString", pInitUstr);

    pInitUstr(&us, name);
    oa.ObjectName = &us;
    pa.MaxMessageLength = 0x1000; pa.SecurityQos.Length = 12; pa.SecurityQos.ImpersonationLevel = 2; pa.SecurityQos.EffectiveOnly = 1;
    st = pCreatePort(&port, &oa, &pa);
    printf("create: status %08lx\n", (ULONG)st);
    if (st) return 1;

    for (i = 0; i < 2; i++)
    {
        memset(sb, 0, sizeof(sb));
        memcpy(m + 1, "wakeup!!", 8);
        m->DataLength = 8;
        m->TotalLength = sizeof(APMSG) + 8;
        st = pSendWaitReceive(port, 0x10000, m, NULL, NULL, NULL, NULL, NULL);
        printf("datagram to the connection port #%d: status %08lx\n", i, (ULONG)st);

        memset(rb, 0, sizeof(rb));
        memset(abuf, 0xa5, sizeof(abuf));   /* a stale buffer: what is left of it? */
        size = sizeof(rb);
        pInitAttr(0xe0000000, a, sizeof(abuf), &req);
        to.QuadPart = -30000000;
        st = pSendWaitReceive(port, 0, NULL, NULL, r, &size, a, &to);
        printf("receive #%d: status %08lx type %04x datalen %u mid %lu size %lu\n", i, (ULONG)st, r->Type, r->DataLength,
               (ULONG)r->MessageId, (ULONG)size);
        printf("  attrs allocated %08lx valid %08lx\n", a->AllocatedAttributes, a->ValidAttributes);
        {
            CTXATTR *c = pGetAttr(a, 0x20000000);
            if (c)
                printf("  context: port %p msg %p seq %lu mid %lu cb %lu%s\n", c->PortContext, c->MessageContext, c->Sequence,
                       c->MessageId, c->CallbackId, (a->ValidAttributes & 0x20000000) ? "" : " (not valid)");
        }
    }
    printf("port handle %p\n", port);
    return 0;
}
