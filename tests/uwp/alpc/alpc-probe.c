/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2026 WineExplorer contributors */
/*
 * ALPC black-box probe: one process, a server thread and a client (the way CoreMessaging self-hosts its registrar). Prints what
 * each call returns and every field of every received message header, so that the output of Windows and Wine can be compared.
 * Build (mingw): x86_64-w64-mingw32-gcc -O2 -o alpc-probe.exe alpc-probe.c
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

typedef LONG NTSTATUS;
typedef struct { USHORT Length, MaximumLength; PWSTR Buffer; } USTR;
typedef struct { ULONG Length; HANDLE RootDirectory; USTR *ObjectName; ULONG Attributes; PVOID SecurityDescriptor; PVOID SecurityQualityOfService; } OBJATTR;
typedef struct { ULONG Length; ULONG ImpersonationLevel; BOOLEAN ContextTrackingMode; BOOLEAN EffectiveOnly; } AQOS;

#pragma pack(push, 8)
typedef struct
{
    USHORT DataLength, TotalLength;
    USHORT Type, DataInfoOffset;
    ULONG_PTR ClientProcess, ClientThread;   /* CLIENT_ID */
    ULONG MessageId;
    ULONG_PTR ClientViewSize;                /* or CallbackId */
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

#define STATUS_TIMEOUT_ ((NTSTATUS)0x00000102)

static NTSTATUS (WINAPI *pCreatePort)(HANDLE *, OBJATTR *, APATTR *);
static NTSTATUS (WINAPI *pConnectPort)(HANDLE *, USTR *, OBJATTR *, APATTR *, ULONG, void *, APMSG *, SIZE_T *, AMATTR *, AMATTR *, LARGE_INTEGER *);
static NTSTATUS (WINAPI *pAcceptConnectPort)(HANDLE *, HANDLE, ULONG, OBJATTR *, APATTR *, void *, APMSG *, AMATTR *, BOOLEAN);
static NTSTATUS (WINAPI *pSendWaitReceive)(HANDLE, ULONG, APMSG *, AMATTR *, APMSG *, SIZE_T *, AMATTR *, LARGE_INTEGER *);
static NTSTATUS (WINAPI *pDisconnectPort)(HANDLE, ULONG);
static NTSTATUS (WINAPI *pInitAttr)(ULONG, AMATTR *, SIZE_T, SIZE_T *);
static void *(WINAPI *pGetAttr)(AMATTR *, ULONG);
static NTSTATUS (WINAPI *pClose)(HANDLE);
static void (WINAPI *pInitUstr)(USTR *, const WCHAR *);

static HANDLE conn_port;
static CRITICAL_SECTION cs;
static char log_buf[1 << 16];
static int log_len;

static void out(const char *fmt, ...)
{
    char line[512];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    EnterCriticalSection(&cs);
    printf("%s\n", line);
    fflush(stdout);
    (void)n;
    LeaveCriticalSection(&cs);
}

static void dump_msg(const char *who, NTSTATUS st, APMSG *m, AMATTR *a)
{
    unsigned char *d = (unsigned char *)(m + 1);
    char hex[80] = "";
    unsigned i, n = m->DataLength > 16 ? 16 : m->DataLength;

    for (i = 0; i < n; i++) sprintf(hex + i * 2, "%02x", d[i]);
    out("%s: status %08lx datalen %u totallen %u type %04x infooff %u mid %lu cbid %llu data %s", who, (ULONG)st, m->DataLength,
        m->TotalLength, m->Type, m->DataInfoOffset, (ULONG)m->MessageId, (unsigned long long)m->ClientViewSize, hex);
    if (a) out("%s: attrs valid %08lx", who, a->ValidAttributes);
    if (a && (a->ValidAttributes & 0x20000000))
    {
        CTXATTR *c = pGetAttr(a, 0x20000000);
        out("%s: context port=%s msg=%s seq %lu mid %lu cb %lu", who, c->PortContext ? "set" : "null",
            c->MessageContext ? "set" : "null", c->Sequence, c->MessageId, c->CallbackId);
    }
}

#define BUFSZ 0x200

static DWORD WINAPI server(void *arg)
{
    HANDLE comm = NULL;
    int quit = 0, n = 0;

    while (!quit && n++ < 20)
    {
        char buf[BUFSZ];
        char abuf[256];
        SIZE_T size = sizeof(buf), req;
        APMSG *m = (APMSG *)buf;
        AMATTR *a = (AMATTR *)abuf;
        LARGE_INTEGER to;
        NTSTATUS st;

        memset(buf, 0, sizeof(buf));
        pInitAttr(0xe0000000, a, sizeof(abuf), &req);
        to.QuadPart = -50000000; /* 5 s */
        st = pSendWaitReceive(conn_port, 0, NULL, NULL, m, &size, a, &to);
        dump_msg("server recv", st, m, a);
        out("server recv: size now %lu", (ULONG)size);
        if (st) break;

        switch (m->Type & 0xff)
        {
        case 10: /* connection request */
        {
            APATTR pa = { 0 };
            APMSG reply = *m;
            char rb[sizeof(APMSG) + 8];

            memcpy(rb, m, sizeof(APMSG));
            reply = *(APMSG *)rb;
            memcpy(rb + sizeof(APMSG), "ACCEPTED", 8);
            ((APMSG *)rb)->DataLength = 8;
            ((APMSG *)rb)->TotalLength = sizeof(APMSG) + 8;
            pa.Flags = 0;
            pa.MaxMessageLength = 0x1000; pa.SecurityQos.Length = 12; pa.SecurityQos.ImpersonationLevel = 2; pa.SecurityQos.EffectiveOnly = 1;
            st = pAcceptConnectPort(&comm, conn_port, 0, NULL, &pa, (void *)0x1234, (APMSG *)rb, NULL, TRUE);
            out("server accept: status %08lx comm %s", (ULONG)st, comm ? "valid" : "null");
            break;
        }
        case 1: /* request: reply */
        case 3: /* datagram */
            if (m->DataLength >= 4 && !memcmp(m + 1, "quit", 4)) { quit = 1; break; }
            if ((m->Type & 0xff) == 1)
            {
                char rb[sizeof(APMSG) + 5];

                ULONG rflags = getenv("ALPC_FLAGS") ? strtoul(getenv("ALPC_FLAGS"), NULL, 16) : 0x10000;

                memcpy(rb, m, sizeof(APMSG));
                if (getenv("ALPC_ZERO")) memset(rb, 0, sizeof(APMSG));
                memcpy(rb + sizeof(APMSG), "REPLY", 5);
                ((APMSG *)rb)->DataLength = 5;
                ((APMSG *)rb)->TotalLength = sizeof(APMSG) + 5;
                st = pSendWaitReceive(conn_port, rflags, (APMSG *)rb, NULL, NULL, NULL, NULL, NULL);
                out("server reply: status %08lx", (ULONG)st);
            }
            break;
        default:
            break;
        }
    }
    out("server: done");
    if (comm) pClose(comm);
    return 0;
}

int main(void)
{
    static const WCHAR name[] = L"\\RPC Control\\WexAlpcProbe";
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    USTR us;
    OBJATTR oa = { sizeof(oa) };
    APATTR pa = { 0 };
    HANDLE client = NULL, th;
    NTSTATUS st;
    LARGE_INTEGER to;
    char cb[BUFSZ], rb[BUFSZ], abuf[256];
    SIZE_T csize, rsize, req;
    APMSG *m;

    InitializeCriticalSection(&cs);
#define GET(n, f) *(FARPROC *)&f = GetProcAddress(nt, n)
    GET("NtAlpcCreatePort", pCreatePort); GET("NtAlpcConnectPort", pConnectPort);
    GET("NtAlpcAcceptConnectPort", pAcceptConnectPort); GET("NtAlpcSendWaitReceivePort", pSendWaitReceive);
    GET("NtAlpcDisconnectPort", pDisconnectPort); GET("AlpcInitializeMessageAttribute", pInitAttr);
    GET("AlpcGetMessageAttribute", pGetAttr); GET("NtClose", pClose); GET("RtlInitUnicodeString", pInitUstr);

    pInitUstr(&us, name);
    oa.ObjectName = &us;
    pa.MaxMessageLength = 0x1000; pa.SecurityQos.Length = 12; pa.SecurityQos.ImpersonationLevel = 2; pa.SecurityQos.EffectiveOnly = 1;
    st = pCreatePort(&conn_port, &oa, &pa);
    out("create: status %08lx", (ULONG)st);
    if (st) return 1;

    th = CreateThread(NULL, 0, server, NULL, 0, NULL);

    /* synchronous connection with data */
    memset(cb, 0, sizeof(cb));
    m = (APMSG *)cb;
    memcpy(m + 1, "hello", 5);
    m->DataLength = 5;
    m->TotalLength = sizeof(APMSG) + 5;
    csize = sizeof(cb);
    pa.Flags = 0;
    to.QuadPart = -50000000;
    st = pConnectPort(&client, &us, NULL, &pa, 0x20000, NULL, m, &csize, NULL, NULL, &to);
    out("connect: status %08lx handle %s size %lu", (ULONG)st, client ? "valid" : "null", (ULONG)csize);
    dump_msg("connect reply", st, m, NULL);

    if (client)
    {
        /* datagram */
        memset(cb, 0, sizeof(cb));
        m = (APMSG *)cb;
        memcpy(m + 1, "dgram", 5);
        m->DataLength = 5;
        m->TotalLength = sizeof(APMSG) + 5;
        st = pSendWaitReceive(client, 0, m, NULL, NULL, NULL, NULL, NULL);
        out("send datagram: status %08lx", (ULONG)st);

        /* request and wait for the reply */
        memset(cb, 0, sizeof(cb));
        m = (APMSG *)cb;
        memcpy(m + 1, "reque", 5);
        m->DataLength = 5;
        m->TotalLength = sizeof(APMSG) + 5;
        memset(rb, 0, sizeof(rb));
        rsize = sizeof(rb);
        pInitAttr(0xe0000000, (AMATTR *)abuf, sizeof(abuf), &req);
        to.QuadPart = -50000000;
        st = pSendWaitReceive(client, 0x20000, m, NULL, (APMSG *)rb, &rsize, (AMATTR *)abuf, &to);
        dump_msg("request reply", st, (APMSG *)rb, (AMATTR *)abuf);

        /* quit */
        memset(cb, 0, sizeof(cb));
        m = (APMSG *)cb;
        memcpy(m + 1, "quit.", 5);
        m->DataLength = 5;
        m->TotalLength = sizeof(APMSG) + 5;
        st = pSendWaitReceive(client, 0, m, NULL, NULL, NULL, NULL, NULL);
        out("send quit: status %08lx", (ULONG)st);
    }
    WaitForSingleObject(th, 8000);
    if (client) pClose(client);
    pClose(conn_port);
    out("end");
    return 0;
}
