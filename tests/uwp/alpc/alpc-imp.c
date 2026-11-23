/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2026 WineExplorer contributors */
/*
 * ALPC impersonation probe: what NtAlpcImpersonateClientOfPort and NtAlpcConnectPort return for different port flags,
 * quality of service levels, forged message ids and port security descriptors, and which impersonation level the token of the
 * thread has afterwards (the client is a child process: inside one process the call does not give the thread a token).
 * usage: alpc-imp FLAGS_HEX QOS_LEVEL|-1 [forge] [deny] [inproc]
 *   FLAGS_HEX    flags (hex) of the client port attributes (10000 = ALPC_PORTFLG_ALLOWIMPERSONATION)
 *   QOS_LEVEL    impersonation level of the quality of service (0 anonymous .. 3 delegation), -1: no port attributes
 *   forge        the server impersonates a message id that was never received
 *   deny         the port is created with a DACL that denies everybody the connect right
 *   inproc       the client is a thread of the same process
 * environment SRVFLAGS: flags (hex) of the attributes of the connection port
 * Build (mingw): x86_64-w64-mingw32-gcc -O2 -o alpc-imp.exe alpc-imp.c -ladvapi32
 */
#include <windows.h>
#include <sddl.h>
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
static void (WINAPI *pInitUstr)(USTR *, const WCHAR *);
static NTSTATUS (WINAPI *pInitAttr)(ULONG, AMATTR *, SIZE_T, SIZE_T *);
static NTSTATUS (WINAPI *pCreateCtx)(HANDLE, ULONG, void *);

static const WCHAR port_name[] = L"\\RPC Control\\WexAlpcImp";
static HANDLE conn_port;
static int forge, secattr, srv_restricted;

static void report_token(const char *what)
{
    HANDLE token = NULL;
    DWORD level = 0xff, size;

    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, FALSE, &token))
    {
        printf("%s: no thread token, error %lu\n", what, GetLastError());
        return;
    }
    if (GetTokenInformation(token, TokenImpersonationLevel, &level, sizeof(level), &size))
        printf("%s: token impersonation level %lu\n", what, level);
    else
        printf("%s: level query error %lu\n", what, GetLastError());
    CloseHandle(token);
}

static void try_impersonate(const char *what, const APMSG *m, int change_id)
{
    APMSG copy = *m;
    NTSTATUS st;

    if (forge) copy.MessageId += 1000;
    if (change_id)
    {
        copy.ClientProcess += 4;
        copy.ClientThread += 4;
    }
    st = pImpersonate(conn_port, &copy, NULL);
    printf("%s: %08lx\n", what, (ULONG)st);
    if (!st)
    {
        report_token("  after");
        RevertToSelf();
    }
}

static DWORD WINAPI server(void *arg)
{
    char buf[0x200];
    APMSG *m = (APMSG *)buf;
    SIZE_T size = sizeof(buf);
    LARGE_INTEGER to;
    NTSTATUS st;
    HANDLE comm = NULL;

    memset(buf, 0, sizeof(buf));
    to.QuadPart = -50000000;
    st = pSendWaitReceive(conn_port, 0, NULL, NULL, m, &size, NULL, &to);
    printf("server recv: %08lx type %04x\n", (ULONG)st, m->Type);
    if (st) return 0;

    try_impersonate("impersonate connection request", m, 0);
    try_impersonate("impersonate connection request, changed client id", m, 1);
    {
        char rb[sizeof(APMSG) + 2];
        APATTR pa = { 0 };

        memcpy(rb, m, sizeof(APMSG));
        memcpy(rb + sizeof(APMSG), "OK", 2);
        ((APMSG *)rb)->DataLength = 2;
        ((APMSG *)rb)->TotalLength = sizeof(APMSG) + 2;
        pa.MaxMessageLength = 0x1000;
        st = pAcceptConnectPort(&comm, conn_port, 0, NULL, &pa, NULL, (APMSG *)rb, NULL, TRUE);
        printf("accept: %08lx\n", (ULONG)st);
    }

    /* now a request: the message that can be impersonated */
    memset(buf, 0, sizeof(buf));
    size = sizeof(buf);
    to.QuadPart = -50000000;
    st = pSendWaitReceive(conn_port, 0, NULL, NULL, m, &size, NULL, &to);
    printf("server recv 2: %08lx type %04x\n", (ULONG)st, m->Type);
    if (!st)
    {
        try_impersonate("impersonate request", m, 0);
        try_impersonate("impersonate request, changed client id", m, 1);
    }
    if (comm) CloseHandle(comm);
    return 0;
}

static int client(ULONG pflags, int qos)
{
    USTR us;
    APATTR pa = { 0 };
    HANDLE client = NULL;
    LARGE_INTEGER to;
    char cb[0x200];
    SIZE_T csize = sizeof(cb);
    APMSG *m = (APMSG *)cb;
    NTSTATUS st;

    pInitUstr(&us, port_name);
    memset(cb, 0, sizeof(cb));
    memcpy(m + 1, "hi", 2);
    m->DataLength = 2;
    m->TotalLength = sizeof(APMSG) + 2;
    pa.Flags = pflags;
    pa.MaxMessageLength = 0x1000;
    pa.SecurityQos.Length = 12;
    pa.SecurityQos.ImpersonationLevel = qos < 0 ? 0 : qos;
    pa.SecurityQos.EffectiveOnly = 1;
    to.QuadPart = -50000000;
    st = pConnectPort(&client, &us, NULL, qos < 0 ? NULL : &pa, 0x20000, NULL, m, &csize, NULL, NULL, &to);
    printf("connect: %08lx\n", (ULONG)st);
    if (client)
    {
        memset(cb, 0, sizeof(cb));
        memcpy(m + 1, "req", 3);
        m->DataLength = 3;
        m->TotalLength = sizeof(APMSG) + 3;
        if (secattr)
        {
            char ab[128];
            AMATTR *a = (AMATTR *)ab;
            SIZE_T req;
            SECURITY_QUALITY_OF_SERVICE sqos = { sizeof(sqos), qos < 0 ? 0 : qos, 0, 1 };
            struct { ULONG Flags; void *QoS; HANDLE Ctx; } *sa;

            pInitAttr(0x80000000, a, sizeof(ab), &req);
            sa = (void *)(a + 1);
            sa->Flags = 0;
            sa->QoS = &sqos;
            sa->Ctx = NULL;
            if (pCreateCtx)
            {
                NTSTATUS cst = pCreateCtx(client, 0, sa);

                printf("create security context: %08lx, handle %s\n", (ULONG)cst, sa->Ctx ? "set" : "null");
            }
            a->ValidAttributes = 0x80000000;
            st = pSendWaitReceive(client, 0, m, a, NULL, NULL, NULL, NULL);
        }
        else
            st = pSendWaitReceive(client, 0, m, NULL, NULL, NULL, NULL, NULL);
        printf("send request: %08lx\n", (ULONG)st);
        Sleep(1500);
        CloseHandle(client);
    }
    return 0;
}

int main(int argc, char **argv)
{
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    ULONG pflags = argc > 1 ? strtoul(argv[1], NULL, 16) : 0;
    int qos = argc > 2 ? atoi(argv[2]) : 2, deny = 0, in_process = 0, i;
    USTR us;
    OBJATTR oa = { sizeof(oa) };
    APATTR pa = { 0 };
    HANDLE th;
    NTSTATUS st;
    PSECURITY_DESCRIPTOR sd = NULL;

    for (i = 3; i < argc; i++)
    {
        if (!strcmp(argv[i], "forge")) forge = 1;
        if (!strcmp(argv[i], "deny")) deny = 1;
        if (!strcmp(argv[i], "inproc")) in_process = 1;
        if (!strcmp(argv[i], "secattr")) secattr = 1;
        if (!strcmp(argv[i], "srvrestricted")) srv_restricted = 1;
    }
#define GET(n, f) *(FARPROC *)&f = GetProcAddress(nt, n)
    GET("NtAlpcCreatePort", pCreatePort); GET("NtAlpcConnectPort", pConnectPort);
    GET("NtAlpcAcceptConnectPort", pAcceptConnectPort); GET("NtAlpcSendWaitReceivePort", pSendWaitReceive);
    GET("NtAlpcImpersonateClientOfPort", pImpersonate); GET("RtlInitUnicodeString", pInitUstr); GET("AlpcInitializeMessageAttribute", pInitAttr); GET("NtAlpcCreateSecurityContext", pCreateCtx);

    if (argc > 1 && !strcmp(argv[1], "serverchild")) in_process = 2;
    if (argc > 4 && !strcmp(argv[1], "client")) secattr = !strcmp(argv[4], "secattr");
    if (argc > 3 && !strcmp(argv[1], "client")) return client(strtoul(argv[2], NULL, 16), atoi(argv[3]));

    if (srv_restricted && in_process != 2)
    {
        /* the server is a child with a restricted token (no privileges); this process, with its full token, is the client */
        char cmd[MAX_PATH + 64];
        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        HANDLE self = NULL, restricted = NULL;

        sprintf(cmd, "\"%s\" serverchild 0 0", argv[0]);
        OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY, &self);
        CreateRestrictedToken(self, DISABLE_MAX_PRIVILEGE, 0, NULL, 0, NULL, 0, NULL, &restricted);
        if (getenv("SRVLOW"))
        {
            /* the server runs at low integrity: the client (medium) is more trusted than it */
            TOKEN_MANDATORY_LABEL label = { { NULL, SE_GROUP_INTEGRITY } };
            HANDLE dup = NULL;

            DuplicateTokenEx(restricted, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenPrimary, &dup);
            if (dup) { CloseHandle(restricted); restricted = dup; }

            ConvertStringSidToSidA("S-1-16-4096", &label.Label.Sid);
            if (!SetTokenInformation(restricted, TokenIntegrityLevel, &label, sizeof(label) + GetLengthSid(label.Label.Sid)))
                printf("SetTokenInformation error %lu\n", GetLastError());
        }
        if (!CreateProcessAsUserA(restricted, NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
            printf("CreateProcessAsUser error %lu\n", GetLastError());
        Sleep(1500);
        pInitUstr(&us, port_name);
        client(pflags, qos);
        WaitForSingleObject(pi.hProcess, 8000);
        return 0;
    }

    pInitUstr(&us, port_name);
    oa.ObjectName = &us;
    if (deny)
    {
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:(D;;GA;;;WD)(D;;GA;;;BA)(D;;GA;;;AU)", SDDL_REVISION_1, &sd, NULL))
            printf("sddl error %lu\n", GetLastError());
        oa.SecurityDescriptor = sd;
    }
    pa.Flags = getenv("SRVFLAGS") ? strtoul(getenv("SRVFLAGS"), NULL, 16) : 0;
    pa.MaxMessageLength = 0x1000;
    pa.SecurityQos.Length = 12; pa.SecurityQos.ImpersonationLevel = 2; pa.SecurityQos.EffectiveOnly = 1;
    st = pCreatePort(&conn_port, &oa, &pa);
    printf("create: %08lx\n", (ULONG)st);
    if (st) return 1;

    th = CreateThread(NULL, 0, server, NULL, 0, NULL);
    if (in_process == 2)
        ;  /* the server child: the parent connects */
    else if (in_process == 1)
        client(pflags, qos);
    else
    {
        char cmd[MAX_PATH + 64];
        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi;

        HANDLE self = NULL, restricted = NULL;
        BOOL ok;

        sprintf(cmd, "\"%s\" client %lx %d %s", argv[0], pflags, qos, secattr ? "secattr" : "plain");
        /* the client runs with a restricted copy of the token: a different token than the server's */
        OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY, &self);
        if (!CreateRestrictedToken(self, DISABLE_MAX_PRIVILEGE, 0, NULL, 0, NULL, 0, NULL, &restricted))
            printf("CreateRestrictedToken error %lu\n", GetLastError());
        ok = restricted ? CreateProcessAsUserA(restricted, NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)
                        : CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
        if (!ok)
            printf("CreateProcess error %lu\n", GetLastError());
        else
        {
            WaitForSingleObject(pi.hProcess, 8000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }
    WaitForSingleObject(th, 5000);
    return 0;
}
