/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2026 WineExplorer contributors */
/*
 * ALPC impersonation, one role per process, for suites that start the two processes with different tokens (a limited and an
 * elevated token of the same user, a low and a medium integrity process ...).
 *
 *   alpc-pair server NAME [WAIT_SECONDS]   creates \RPC Control\NAME with ALPC_PORTFLG_ALLOWIMPERSONATION, waits for a client,
 *                                          impersonates its request and prints "level=N" (the impersonation level of the token of the
 *                                          server thread, 0 = anonymous .. 3), "notoken" or "impersonate=STATUS"
 *   alpc-pair client NAME LEVEL            connects and sends one request with a security attribute of the impersonation level LEVEL
 *                                          (1 identification, 2 impersonation), then stays 3 seconds; prints "sent"
 *
 * Expected (measured on Windows 11, Wine follows): server and client with the same token: level=LEVEL; a server that has no
 * SeImpersonatePrivilege and is less trusted than the client (lower integrity, or limited while the client is elevated) gets
 * level=1 whatever LEVEL is. Exit status 0 when the role ran through.
 * Build: x86_64-w64-mingw32-gcc -O2 -o alpc-pair.exe alpc-pair.c -ladvapi32
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
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
#pragma pack(pop)

static NTSTATUS (WINAPI *pCreatePort)(HANDLE *, OBJATTR *, APATTR *);
static NTSTATUS (WINAPI *pConnectPort)(HANDLE *, USTR *, OBJATTR *, APATTR *, ULONG, void *, APMSG *, SIZE_T *, AMATTR *, AMATTR *, LARGE_INTEGER *);
static NTSTATUS (WINAPI *pAcceptConnectPort)(HANDLE *, HANDLE, ULONG, OBJATTR *, APATTR *, void *, APMSG *, AMATTR *, BOOLEAN);
static NTSTATUS (WINAPI *pSendWaitReceive)(HANDLE, ULONG, APMSG *, AMATTR *, APMSG *, SIZE_T *, AMATTR *, LARGE_INTEGER *);
static NTSTATUS (WINAPI *pImpersonate)(HANDLE, APMSG *, void *);
static NTSTATUS (WINAPI *pInitAttr)(ULONG, AMATTR *, SIZE_T, SIZE_T *);
static NTSTATUS (WINAPI *pCreateCtx)(HANDLE, ULONG, void *);
static void (WINAPI *pInitUstr)(USTR *, const WCHAR *);

static void init(void)
{
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");

#define GET(n, f) *(FARPROC *)&f = GetProcAddress(nt, n)
    GET("NtAlpcCreatePort", pCreatePort); GET("NtAlpcConnectPort", pConnectPort);
    GET("NtAlpcAcceptConnectPort", pAcceptConnectPort); GET("NtAlpcSendWaitReceivePort", pSendWaitReceive);
    GET("NtAlpcImpersonateClientOfPort", pImpersonate); GET("AlpcInitializeMessageAttribute", pInitAttr);
    GET("NtAlpcCreateSecurityContext", pCreateCtx); GET("RtlInitUnicodeString", pInitUstr);
}

static void make_name(USTR *us, WCHAR *buf, const char *name)
{
    swprintf(buf, 128, L"\\RPC Control\\%hs", name);
    pInitUstr(us, buf);
}

static int server(const char *name, int wait_seconds)
{
    WCHAR wname[128];
    USTR us;
    OBJATTR oa = { sizeof(oa) };
    APATTR pa = { 0 };
    HANDLE port = NULL, comm = NULL, token = NULL;
    char buf[0x200];
    APMSG *m = (APMSG *)buf;
    SIZE_T size;
    LARGE_INTEGER to;
    NTSTATUS st;
    DWORD level = 0xff, len;

    make_name(&us, wname, name);
    oa.ObjectName = &us;
    pa.Flags = 0x10000;  /* ALPC_PORTFLG_ALLOWIMPERSONATION */
    pa.MaxMessageLength = 0x1000;
    pa.SecurityQos.Length = 12;
    pa.SecurityQos.ImpersonationLevel = 2;
    pa.SecurityQos.EffectiveOnly = 1;
    if ((st = pCreatePort(&port, &oa, &pa))) { printf("create=%08lx\n", (ULONG)st); return 1; }

    to.QuadPart = -10000000LL * (wait_seconds > 0 ? wait_seconds : 20);
    memset(buf, 0, sizeof(buf));
    size = sizeof(buf);
    if ((st = pSendWaitReceive(port, 0, NULL, NULL, m, &size, NULL, &to))) { printf("receive=%08lx\n", (ULONG)st); return 1; }
    {
        char rb[sizeof(APMSG)];
        APATTR ap = { 0 };

        memcpy(rb, m, sizeof(APMSG));
        ((APMSG *)rb)->DataLength = 0;
        ((APMSG *)rb)->TotalLength = sizeof(APMSG);
        ap.MaxMessageLength = 0x1000;
        pAcceptConnectPort(&comm, port, 0, NULL, &ap, NULL, (APMSG *)rb, NULL, TRUE);
    }
    memset(buf, 0, sizeof(buf));
    size = sizeof(buf);
    if ((st = pSendWaitReceive(port, 0, NULL, NULL, m, &size, NULL, &to))) { printf("receive2=%08lx\n", (ULONG)st); return 1; }

    st = pImpersonate(port, m, NULL);
    if (st) { printf("impersonate=%08lx\n", (ULONG)st); return 1; }
    /* OpenAsSelf: at identification level the impersonated token may not open objects, its own token included
     * (ERROR_BAD_IMPERSONATION_LEVEL on Windows and Wine) */
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token))
        printf("notoken (error %lu)\n", GetLastError());
    else
    {
        GetTokenInformation(token, TokenImpersonationLevel, &level, sizeof(level), &len);
        printf("level=%lu\n", level);
        CloseHandle(token);
    }
    RevertToSelf();
    return 0;
}

static int client(const char *name, int level)
{
    WCHAR wname[128];
    USTR us;
    APATTR pa = { 0 };
    HANDLE port = NULL;
    char cb[0x200], ab[128];
    APMSG *m = (APMSG *)cb;
    AMATTR *a = (AMATTR *)ab;
    struct { ULONG Flags; void *QoS; HANDLE Ctx; } *sa;
    SECURITY_QUALITY_OF_SERVICE sqos = { sizeof(sqos), level, 0, 1 };
    SIZE_T csize = sizeof(cb), req;
    LARGE_INTEGER to;
    NTSTATUS st;

    make_name(&us, wname, name);
    memset(cb, 0, sizeof(cb));
    memcpy(m + 1, "hi", 2);
    m->DataLength = 2;
    m->TotalLength = sizeof(APMSG) + 2;
    pa.MaxMessageLength = 0x1000;
    pa.SecurityQos.Length = 12;
    pa.SecurityQos.ImpersonationLevel = 2;
    pa.SecurityQos.EffectiveOnly = 1;
    to.QuadPart = -100000000LL;
    if ((st = pConnectPort(&port, &us, NULL, &pa, 0x20000, NULL, m, &csize, NULL, NULL, &to))) { printf("connect=%08lx\n", (ULONG)st); return 1; }

    memset(cb, 0, sizeof(cb));
    memcpy(m + 1, "req", 3);
    m->DataLength = 3;
    m->TotalLength = sizeof(APMSG) + 3;
    pInitAttr(0x80000000, a, sizeof(ab), &req);
    sa = (void *)(a + 1);
    sa->Flags = 0;
    sa->QoS = &sqos;
    sa->Ctx = NULL;
    if (pCreateCtx && (st = pCreateCtx(port, 0, sa))) { printf("securitycontext=%08lx\n", (ULONG)st); return 1; }
    a->ValidAttributes = 0x80000000;
    if ((st = pSendWaitReceive(port, 0, m, a, NULL, NULL, NULL, NULL))) { printf("send=%08lx\n", (ULONG)st); return 1; }
    printf("sent\n");
    fflush(stdout);
    Sleep(3000);
    return 0;
}

int main(int argc, char **argv)
{
    init();
    if (argc >= 3 && !strcmp(argv[1], "server")) return server(argv[2], argc > 3 ? atoi(argv[3]) : 20);
    if (argc >= 4 && !strcmp(argv[1], "client")) return client(argv[2], atoi(argv[3]));
    fprintf(stderr, "usage: alpc-pair server NAME [WAIT_SECONDS] | client NAME LEVEL\n");
    return 2;
}
