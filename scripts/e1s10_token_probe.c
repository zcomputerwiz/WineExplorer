/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Disposable Wine-only S10 test broker and standard-user peer. Never install
 * this as the shipping broker. No PAM, profile or password interface; the
 * only process creation below is a benign identity/copy test driver.
 */
#define COBJMACROS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <windows.h>
#include <sddl.h>
#include <winternl.h>
/* Byte helpers only; keep Wine SDK headers off the MinGW include search path. */
#include "../wine/include/wine/wex_token.h"
#include "e1s6_protocol.h"

typedef NTSTATUS (WINAPI *create_fn)(ULONG,HANDLE*,HANDLE,ULONGLONG*,const void*,ULONG,HANDLE*,void*,ULONG*);
typedef DWORD (WINAPI *transport_fn)(HANDLE,DWORD,void*,DWORD,UINT64*);
static create_fn token_export;
typedef NTSTATUS (__cdecl *server_call_fn)(void *);
static server_call_fn server_call;
static unsigned int failures;
static NTSTATUS last_status;
static const char *last_nt_call = "none";
static const char *step = "startup";

static BOOL checked( BOOL ok, const char *function, unsigned int line, const char *call )
{
    DWORD error = GetLastError();
    if (!ok)
    {
        printf("FAIL step=%s %s:%u call=%s GetLastError=%lu last_NT_call=%s NTSTATUS=%08lx\n",
               step, function, line, call, (unsigned long)error, last_nt_call, (unsigned long)last_status);
        fflush(stdout);
        failures++;
        SetLastError(error);
    }
    return ok;
}

/* Sample LastError immediately after the expression. For logical assertions
 * it is ambient context, not a claimed API error. NT calls record their actual
 * status separately. Function, line and expression distinguish every check. */
#define test(c) checked(!!(c), __func__, __LINE__, #c)
#define check(c) do { test(c); } while (0)
#define require(s,c) do { step = (s); if (!test(c)) goto done; } while (0)

static void progress( const char *stage )
{
    step = stage;
    printf("E1S10 %s\n", stage);
    fflush(stdout);
}

static NTSTATUS create_token( ULONG operation, HANDLE *context, HANDLE caller, ULONGLONG *caps,
                              const void *record, ULONG length, HANDLE *token, void *response, ULONG *size )
{
    last_nt_call = operation == WEX_TOKEN_NEGOTIATE ? "NtCreateWexAccountToken(NEGOTIATE)" :
                   operation == WEX_TOKEN_TEST_INSTALL ? "NtCreateWexAccountToken(TEST_INSTALL)" :
                   "NtCreateWexAccountToken(ISSUE)";
    last_status = token_export(operation, context, caller, caps, record, length, token, response, size);
    return last_status;
}

static BOOL info( HANDLE token, TOKEN_INFORMATION_CLASS type, void *out, DWORD size )
{
    DWORD needed;
    BOOL ok = GetTokenInformation( token, type, out, size, &needed );
    if (!ok)
    {
        DWORD error = GetLastError();
        printf("FAIL step=%s GetTokenInformation(class=%u) GetLastError=%lu\n",
               step, (unsigned int)type, (unsigned long)error);
        fflush(stdout);
        failures++;
        SetLastError(error);
    }
    return ok;
}

#ifdef WEX_E1S13_HKU_PROBE
#include "e1s13_hku_checks.h"
#endif

static BOOL same_luid( LUID a, LUID b )
{
    return a.LowPart == b.LowPart && a.HighPart == b.HighPart;
}

static BOOL logon_sid_matches( PSID sid, LUID auth )
{
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    return IsValidSid(sid) && *GetSidSubAuthorityCount(sid) == 3 &&
           !memcmp(GetSidIdentifierAuthority(sid), &nt, sizeof(nt)) &&
           *GetSidSubAuthority(sid, 0) == 5 &&
           *GetSidSubAuthority(sid, 1) == (DWORD)auth.HighPart &&
           *GetSidSubAuthority(sid, 2) == auth.LowPart;
}

static BOOL unhex( const char *s, BYTE *out )
{
    unsigned int i;
    if (strlen(s) != 32) return FALSE;
    for (i = 0; i < 16; i++)
    {
        unsigned int a, b;
        if (s[2*i] >= '0' && s[2*i] <= '9') a = s[2*i] - '0';
        else if (s[2*i] >= 'a' && s[2*i] <= 'f') a = s[2*i] - 'a' + 10;
        else return FALSE;
        if (s[2*i+1] >= '0' && s[2*i+1] <= '9') b = s[2*i+1] - '0';
        else if (s[2*i+1] >= 'a' && s[2*i+1] <= 'f') b = s[2*i+1] - 'a' + 10;
        else return FALSE;
        out[i] = (a << 4) | b;
    }
    return TRUE;
}

static void check_token( HANDLE token, HANDLE caller, PSID expected, const BYTE *response )
{
    BYTE user_buf[256] = {0}, groups_buf[4096] = {0}, label_buf[256] = {0}, dacl_buf[1024] = {0};
    BYTE owner_buf[256] = {0}, primary_buf[256] = {0};
    TOKEN_USER *user = (TOKEN_USER *)user_buf;
    TOKEN_GROUPS *groups = (TOKEN_GROUPS *)groups_buf;
    TOKEN_MANDATORY_LABEL *label = (TOKEN_MANDATORY_LABEL *)label_buf;
    TOKEN_DEFAULT_DACL *dacl = (TOKEN_DEFAULT_DACL *)dacl_buf;
    TOKEN_OWNER *owner = (TOKEN_OWNER *)owner_buf;
    TOKEN_PRIMARY_GROUP *primary = (TOKEN_PRIMARY_GROUP *)primary_buf;
    TOKEN_STATISTICS stats = {0}, caller_stats = {0}, dup_stats = {0};
    BYTE priv_buf[1024] = {0};
    TOKEN_PRIVILEGES *privs = (TOKEN_PRIVILEGES *)priv_buf;
    TOKEN_ELEVATION_TYPE elevation = 0;
    TOKEN_TYPE type = 0;
    DWORD session = ~0u, caller_session = 0, flags, i;
    BOOL admin = FALSE, logon = FALSE, primary_found = FALSE;
    HANDLE duplicate = NULL;
    PSID admins = NULL, system = NULL;
    check( info( token, TokenType, &type, sizeof(type) ) && type == TokenPrimary );
    check( info( token, TokenElevationType, &elevation, sizeof(elevation) ) && elevation == TokenElevationTypeFull );
    check( info( token, TokenUser, user_buf, sizeof(user_buf) ) && EqualSid(user->User.Sid, expected) );
    check( info( token, TokenIntegrityLevel, label_buf, sizeof(label_buf) ) &&
           *GetSidSubAuthority(label->Label.Sid, *GetSidSubAuthorityCount(label->Label.Sid)-1) == SECURITY_MANDATORY_HIGH_RID );
    check( info( token, TokenStatistics, &stats, sizeof(stats) ) );
    check( info( caller, TokenStatistics, &caller_stats, sizeof(caller_stats) ) );
    check( (stats.AuthenticationId.LowPart || stats.AuthenticationId.HighPart) &&
           !same_luid(stats.AuthenticationId, caller_stats.AuthenticationId) );
    check( info(token, TokenSessionId, &session, sizeof(session)) &&
           info(caller, TokenSessionId, &caller_session, sizeof(caller_session)) && session == caller_session );
    check( GetHandleInformation(token, &flags) && !(flags & HANDLE_FLAG_INHERIT) );
    check( DuplicateTokenEx(token, TOKEN_QUERY, NULL, SecurityImpersonation, TokenPrimary, &duplicate) );
    if (duplicate)
    {
        check( info(duplicate, TokenStatistics, &dup_stats, sizeof(dup_stats)) &&
               same_luid(stats.AuthenticationId, dup_stats.AuthenticationId) );
        CloseHandle(duplicate);
    }
    check( ConvertStringSidToSidA("S-1-5-32-544", &admins) );
    check( ConvertStringSidToSidA("S-1-5-18", &system) );
    check( info(token, TokenPrimaryGroup, primary_buf, sizeof(primary_buf)) );
    if (info(token, TokenGroups, groups_buf, sizeof(groups_buf)))
    {
        for (i = 0; i < groups->GroupCount; i++)
        {
            SID_AND_ATTRIBUTES *g = &groups->Groups[i];
            if (admins && EqualSid(g->Sid, admins))
                admin = (g->Attributes & SE_GROUP_ENABLED) && !(g->Attributes & SE_GROUP_USE_FOR_DENY_ONLY);
            if (primary->PrimaryGroup && EqualSid(g->Sid, primary->PrimaryGroup))
                primary_found = (g->Attributes & SE_GROUP_ENABLED) && !(g->Attributes & SE_GROUP_USE_FOR_DENY_ONLY);
            if ((g->Attributes & SE_GROUP_LOGON_ID) == SE_GROUP_LOGON_ID)
            {
                check( !logon );
                logon = logon_sid_matches(g->Sid, stats.AuthenticationId) && (g->Attributes & SE_GROUP_ENABLED) &&
                        !(g->Attributes & SE_GROUP_USE_FOR_DENY_ONLY);
                check(logon);
            }
        }
    }
    else check(groups->GroupCount != 0); /* info() printed the failing class. */
    check(admin && logon && primary_found);
    if (info(token, TokenPrivileges, priv_buf, sizeof(priv_buf)))
    {
        BOOL tcb = FALSE, notify = FALSE;
        LUID tcb_id = {0}, notify_id = {0};
        check(LookupPrivilegeValueA(NULL, SE_TCB_NAME, &tcb_id));
        check(LookupPrivilegeValueA(NULL, SE_CHANGE_NOTIFY_NAME, &notify_id));
        check(privs->PrivilegeCount == 21);
        for (i = 0; i < privs->PrivilegeCount; i++)
        {
            if (same_luid(privs->Privileges[i].Luid, tcb_id))
                tcb = !(privs->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED);
            if (same_luid(privs->Privileges[i].Luid, notify_id))
                notify = (privs->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED) != 0;
        }
        check(tcb && notify);
    }
    else check(privs->PrivilegeCount != 0);
    check( info(token, TokenOwner, owner_buf, sizeof(owner_buf)) && EqualSid(owner->Owner, expected) );
    if (info(token, TokenDefaultDacl, dacl_buf, sizeof(dacl_buf)) && dacl->DefaultDacl)
    {
        BOOL found_user = FALSE, found_system = FALSE;
        check(dacl->DefaultDacl->AceCount == 2);
        for (i = 0; i < dacl->DefaultDacl->AceCount; i++)
        {
            ACCESS_ALLOWED_ACE *ace;
            if (!test(GetAce(dacl->DefaultDacl, i, (void **)&ace))) break;
            check(ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE && ace->Mask == GENERIC_ALL);
            if (EqualSid((PSID)&ace->SidStart, expected)) found_user = TRUE;
            else if (system && EqualSid((PSID)&ace->SidStart, system)) found_system = TRUE;
            else check(EqualSid((PSID)&ace->SidStart, expected) || (system && EqualSid((PSID)&ace->SidStart, system)));
        }
        check(found_user && found_system);
    }
    else check(dacl->DefaultDacl != NULL);
    check(wex_token_u32(response + 72) == GetLengthSid(expected) && EqualSid((PSID)(response + 76), expected));
    check(wex_token_u64(response + 104) == ((ULONGLONG)(DWORD)stats.AuthenticationId.HighPart << 32 | stats.AuthenticationId.LowPart));
    check(wex_token_u64(response + 112) == ((ULONGLONG)(DWORD)stats.TokenId.HighPart << 32 | stats.TokenId.LowPart));
    check(wex_token_u32(response + 120) == session && wex_token_u32(response + 124) == TokenPrimary &&
          wex_token_u32(response + 128) == TokenElevationTypeFull && wex_token_u32(response + 132) == SECURITY_MANDATORY_HIGH_RID);
    LocalFree(admins); LocalFree(system);
}

static void failure_record( const BYTE *out, ULONG size, NTSTATUS status )
{
    unsigned int i;
    check(size == WEX_TOKEN_FAILURE_SIZE && wex_token_u32(out) == 0x00010005 &&
          wex_token_u32(out + 4) == WEX_TOKEN_FAILURE_SIZE && wex_token_u32(out + 32) == (ULONG)status);
    for (i = 8; i < WEX_TOKEN_RESPONSE_SIZE; i++)
        if (i < 32 || i >= 36) check(out[i] == 0);
}

/* Private protocol 977, request 324. Mirror only the transport carrier, not
 * Wine's Windows SDK headers. The union is the protocol's fixed 88 bytes;
 * pointer/iovec layout follows wine/server.h and ntdll's WoW64 carrier. */
struct raw_call
{
    union { BYTE bytes[88]; ULONGLONG align; } u;
    unsigned int count;
    void *reply;
    struct { const void *ptr; unsigned int size; } data[5];
};
typedef char raw_count_offset[(offsetof(struct raw_call, count) == 88) ? 1 : -1];
typedef char raw_reply_offset[(offsetof(struct raw_call, reply) == (sizeof(void *) == 8 ? 96 : 92)) ? 1 : -1];
typedef char raw_data_offset[(offsetof(struct raw_call, data) == (sizeof(void *) == 8 ? 104 : 96)) ? 1 : -1];

static NTSTATUS raw_request( ULONG operation, HANDLE context, ULONGLONG caps,
                             const BYTE *input, ULONG length, BYTE *output )
{
    struct raw_call r;
    NTSTATUS status;
    memset(&r, 0, sizeof(r));
    memset(output, 0xcc, WEX_TOKEN_RESPONSE_SIZE);
    wex_token_put32(r.u.bytes, 324);
    wex_token_put32(r.u.bytes + 4, length);
    wex_token_put32(r.u.bytes + 8, WEX_TOKEN_RESPONSE_SIZE);
    wex_token_put32(r.u.bytes + 12, operation);
    wex_token_put32(r.u.bytes + 16, (ULONG)(ULONG_PTR)context);
    wex_token_put64(r.u.bytes + 24, caps);
    r.count = length != 0; r.reply = output;
    r.data[0].ptr = input; r.data[0].size = length;
    status = server_call(&r);
    last_nt_call = "wine_server_call(create_e1s_account_token/ISSUE)";
    last_status = status;
    check(!wex_token_u32(r.u.bytes + 8) && !wex_token_u32(r.u.bytes + 12));
    /* Raw replies do not receive the PE wrapper's canonicalization. These
     * cases reach e1s_issue, which must supply its own cleared failure. */
    if (wex_token_u32(r.u.bytes + 4))
    {
        check(wex_token_u32(r.u.bytes + 4) == WEX_TOKEN_FAILURE_SIZE);
        /* The raw server transfers only 108 bytes, not the wrapper's tail. */
        memset(output + WEX_TOKEN_FAILURE_SIZE, 0, WEX_TOKEN_RESPONSE_SIZE - WEX_TOKEN_FAILURE_SIZE);
        failure_record(output, WEX_TOKEN_FAILURE_SIZE, status);
    }
    return status;
}

static int denied( TOKEN_ELEVATION_TYPE expected, BOOL normal, BOOL system_check )
{
    HANDLE context = NULL, token = NULL;
    ULONGLONG caps = WEX_TOKEN_CAPS;
    ULONG size = 0;
    BYTE input[WEX_TOKEN_FIXTURE_SIZE] = {0}, out[WEX_TOKEN_RESPONSE_SIZE];
    HANDLE own = NULL;
    TOKEN_ELEVATION_TYPE elevation = 0;
    NTSTATUS status;
    check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &own));
    if (own)
    {
        check(info(own, TokenElevationType, &elevation, sizeof(elevation)) && elevation == expected);
        if (system_check)
        {
            BYTE user[256]; PSID system = NULL;
            check(ConvertStringSidToSidA("S-1-5-18", &system));
            check(info(own, TokenUser, user, sizeof(user)) && system && EqualSid(((TOKEN_USER *)user)->User.Sid, system));
            LocalFree(system);
        }
        CloseHandle(own);
    }
    status = create_token(WEX_TOKEN_NEGOTIATE, &context, NULL, &caps, NULL, 0, &token, NULL, &size);
    check(status == (normal ? (NTSTATUS)0xc00000bb : (NTSTATUS)0xc0000022));
    check(!context && !token && !caps && !size);
    if (context) CloseHandle(context);
    if (token) { CloseHandle(token); token = NULL; }
    context = (HANDLE)(ULONG_PTR)0x1234; caps = WEX_TOKEN_CAPS; size = sizeof(out);
    status = create_token(WEX_TOKEN_ISSUE, &context, NULL, &caps, input, WEX_TOKEN_INPUT_SIZE, &token, out, &size);
    check(status == (normal ? (NTSTATUS)0xc00000bb : (NTSTATUS)0xc0000022) && !token);
    failure_record(out, size, status);
    size = 0;
    status = create_token(WEX_TOKEN_TEST_INSTALL, &context, GetCurrentProcess(), &caps,
                          input, sizeof(input), &token, NULL, &size);
    check(status == (normal ? (NTSTATUS)0xc00000bb : (NTSTATUS)0xc0000022) && !token && !size);
    if (!failures) progress("OK");
    return failures ? 1 : 0;
}

struct copied_context { HANDLE context; BYTE input[WEX_TOKEN_INPUT_SIZE]; };

static DWORD WINAPI copied_thread( void *arg )
{
    struct copied_context *copy = arg;
    HANDLE token = NULL, context = copy->context;
    BYTE out[WEX_TOKEN_RESPONSE_SIZE];
    ULONG size = sizeof(out);
    ULONGLONG caps = WEX_TOKEN_CAPS;
    NTSTATUS status = create_token(WEX_TOKEN_ISSUE, &context, NULL, &caps, copy->input,
                                   sizeof(copy->input), &token, out, &size);
    check(status == (NTSTATUS)0xc0000022 && !token);
    failure_record(out, size, status);
    if (token) CloseHandle(token);
    return failures ? 1 : 0;
}

static BOOL wait_child( PROCESS_INFORMATION *pi )
{
    DWORD code = ~0u, result = WaitForSingleObject(pi->hProcess, 5000);
    BOOL done = result == WAIT_OBJECT_0;
    check(result == WAIT_OBJECT_0); /* WAIT_TIMEOUT is not a GetLastError result. */
    if (!done)
    {
        printf("FAIL step=%s call=WaitForSingleObject(child) wait_result=%lu\n", step, (unsigned long)result);
        fflush(stdout);
        if (test(TerminateProcess(pi->hProcess, 1))) check(WaitForSingleObject(pi->hProcess, 5000) == WAIT_OBJECT_0);
    }
    check(GetExitCodeProcess(pi->hProcess, &code));
    if (code)
    {
        printf("FAIL step=%s call=GetExitCodeProcess(child) exit_code=%lu\n", step, (unsigned long)code);
        fflush(stdout);
        failures++;
    }
    CloseHandle(pi->hThread); CloseHandle(pi->hProcess);
    return done && !code;
}

static void copy_context( HANDLE context, const BYTE *input, BOOL process )
{
    struct copied_context local, *copy = &local;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE mapping = NULL, thread;
    char exe[MAX_PATH], command[2 * MAX_PATH];
    STARTUPINFOA si = {0}; PROCESS_INFORMATION pi = {0};
    if (process)
    {
        mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, sizeof(local), NULL);
        check(mapping != NULL);
        if (!mapping) return;
        copy = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(local));
        check(copy != NULL);
        if (!copy) { CloseHandle(mapping); return; }
    }
    copy->context = NULL;
    check(DuplicateHandle(GetCurrentProcess(), context, GetCurrentProcess(), &copy->context,
                          0, process, DUPLICATE_SAME_ACCESS));
    memcpy(copy->input, input, sizeof(copy->input));
    if (copy->context)
    {
        if (process)
        {
            check(GetModuleFileNameA(NULL, exe, sizeof(exe)) != 0);
            snprintf(command, sizeof(command), "\"%s\" copied %llu", exe, (unsigned long long)(ULONG_PTR)mapping);
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
            si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
            si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
            check(CreateProcessA(exe, command, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi));
            if (pi.hProcess) wait_child(&pi);
        }
        else
        {
            thread = CreateThread(NULL, 0, copied_thread, copy, 0, NULL);
            check(thread != NULL);
            if (thread) { check(WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0); CloseHandle(thread); }
        }
        CloseHandle(copy->context);
    }
    if (process) { UnmapViewOfFile(copy); CloseHandle(mapping); }
}

/* A benign child and grandchild query their own primary, without a profile or
 * environment handoff. This is a lab driver, not broker launch integration. */
static void spawn_identity( HANDLE token, const char *sid, LUID auth, DWORD session, unsigned int depth )
{
    char exe[MAX_PATH], command[2 * MAX_PATH + 256];
    STARTUPINFOA si = {0}; PROCESS_INFORMATION pi = {0};
    check(GetModuleFileNameA(NULL, exe, sizeof(exe)) != 0);
    snprintf(command, sizeof(command), "\"%s\" identity %s %lu %lu %lu %u", exe, sid,
             (unsigned long)(DWORD)auth.HighPart, (unsigned long)auth.LowPart, (unsigned long)session, depth);
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    if (token) check(CreateProcessAsUserA(token, exe, command, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi));
    else check(CreateProcessA(exe, command, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi));
    if (pi.hProcess) wait_child(&pi);
}

static int identity( char **argv )
{
    BYTE user[256], groups_buf[4096];
    TOKEN_GROUPS *groups = (TOKEN_GROUPS *)groups_buf;
    TOKEN_STATISTICS stats = {0};
    LUID auth = {strtoul(argv[4], NULL, 10), (LONG)strtoul(argv[3], NULL, 10)};
    DWORD session = strtoul(argv[5], NULL, 10), actual = ~0u, i;
    TOKEN_ELEVATION_TYPE elevation = 0;
    HANDLE token = NULL; PSID expected = NULL;
    BOOL logon = FALSE;
    check(ConvertStringSidToSidA(argv[2], &expected));
    check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token));
    if (token)
    {
        check(info(token, TokenUser, user, sizeof(user)) && expected && EqualSid(((TOKEN_USER *)user)->User.Sid, expected));
        check(info(token, TokenStatistics, &stats, sizeof(stats)) && stats.TokenType == TokenPrimary && same_luid(stats.AuthenticationId, auth));
        check(info(token, TokenSessionId, &actual, sizeof(actual)) && actual == session);
        check(info(token, TokenElevationType, &elevation, sizeof(elevation)) && elevation == TokenElevationTypeFull);
        if (info(token, TokenGroups, groups_buf, sizeof(groups_buf)))
            for (i = 0; i < groups->GroupCount; i++)
                if ((groups->Groups[i].Attributes & SE_GROUP_LOGON_ID) == SE_GROUP_LOGON_ID)
                {
                    PSID s = groups->Groups[i].Sid;
                    check(!logon && logon_sid_matches(s, auth) && (groups->Groups[i].Attributes & SE_GROUP_ENABLED));
                    logon = TRUE;
                }
        check(logon);
        CloseHandle(token);
    }
    if (!failures && !strcmp(argv[6], "1")) spawn_identity(NULL, argv[2], auth, session, 0);
#ifdef WEX_E1S13_HKU_PROBE
    e1s13_identity(argv[2]);
#endif
    LocalFree(expected);
    if (!failures) progress("IDENTITY OK");
    return failures ? 1 : 0;
}

/* The peer remains alive while the broker compares its retained token. It
 * cannot receive token/context handles; one byte is the whole fixture message. */
static int peer( const char *name, BOOL death )
{
    HANDLE pipe = INVALID_HANDLE_VALUE;
    DWORD n = 0;
    BYTE marker = 0x5a;
    printf("E1S10 PEER START pipe=%s death=%u\n", name, (unsigned int)death); fflush(stdout);
#ifdef WEX_E1S13_HKU_PROBE
    e1s13_peer_state(FALSE);
    progress("PEER S13 BEFORE");
#endif
    progress("PEER WAIT PIPE");
    require("peer-wait-pipe", WaitNamedPipeA(name, 10000));
    progress("PEER PIPE AVAILABLE");
    pipe = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    require("peer-open-pipe/CreateFileA", pipe != INVALID_HANDLE_VALUE);
    progress("PEER CONNECTED");
    require("peer-send-marker", WriteFile(pipe, &marker, 1, &n, NULL));
    require("peer-send-marker-length", n == 1);
    progress("PEER MARKER SENT");
    require("peer-read-result", ReadFile(pipe, &marker, 1, &n, NULL));
    require("peer-read-result-length", n == 1);
    printf("E1S10 PEER RESULT marker=%u\n", (unsigned int)marker); fflush(stdout);
    require("peer-result", marker == (death ? 0x7e : 0));
#ifdef WEX_E1S13_HKU_PROBE
    e1s13_peer_state(TRUE);
    progress("PEER S13 AFTER");
#endif
done:
    if (pipe != INVALID_HANDLE_VALUE) check(CloseHandle(pipe));
    if (!failures) progress("PEER OK");
    return failures ? 1 : 0;
}

static BOOL transport_step( transport_fn transport, HANDLE pipe, DWORD operation,
                            void *buffer, DWORD size, UINT64 *channel, const char *name )
{
    DWORD error = transport(pipe, operation, buffer, size, channel);
    if (error)
    {
        printf("FAIL step=%s call=WexE1s6Transport(%s) Win32_return=%lu GetLastError=%lu\n",
               step, name, (unsigned long)error, (unsigned long)GetLastError());
        fflush(stdout);
        failures++;
    }
    return !error;
}

static BOOL transport_control( transport_fn transport, HANDLE pipe, BYTE *reg, UINT64 *channel )
{
    BYTE packet[E1S6_REG_SIZE + E1S6_ANSWER_SIZE] = {0}, original[E1S6_ANSWER_SIZE];
    BYTE *answer = packet + E1S6_REG_SIZE;
    memcpy(packet, reg, E1S6_REG_SIZE);
    wex_token_put32(answer, 0x00010002); wex_token_put32(answer + 4, E1S6_ANSWER_SIZE);
    wex_token_put64(answer + 16, 1); wex_token_put32(answer + 24, 1);
    wex_token_put32(answer + 28, 1); wex_token_put32(answer + 36, 2);
    memset(answer + 40, 0x33, 96); memcpy(answer + 136, reg + 88, 16);
    memcpy(original, answer, sizeof(original));
    require("s6-child-start", transport_step(transport, pipe, E1S6_CHILD_START, packet, sizeof(packet), channel, "CHILD_START"));
    progress("S6 CHILD STARTED");
    require("s6-exchange", transport_step(transport, pipe, E1S6_EXCHANGE, packet, sizeof(packet), channel, "EXCHANGE"));
    require("s6-byte-identical-echo/memcmp", !memcmp(answer, original, sizeof(original)));
    progress("TRANSPORT OK");
    return TRUE;
done:
    return FALSE;
}

static int broker( int argc, char **argv )
{
    BYTE reg[E1S6_OPEN_SIZE] = {0}, in[WEX_TOKEN_FIXTURE_SIZE] = {0}, out[WEX_TOKEN_RESPONSE_SIZE];
    HANDLE pipe = INVALID_HANDLE_VALUE, process = NULL, caller = NULL, token = NULL, context = NULL;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, FALSE};
    PSECURITY_DESCRIPTOR sd = NULL;
    PSID expected = NULL;
    DWORD n, pid;
    ULONG size = 0, session, input_size = WEX_TOKEN_INPUT_SIZE;
    UINT64 channel = 0;
    ULONGLONG caps = WEX_TOKEN_CAPS, generation;
    TOKEN_STATISTICS before, after;
    NTSTATUS status;
    BYTE marker;
    BOOL negative, transported = FALSE, greeted = FALSE;
    HMODULE host = NULL;
    transport_fn transport = NULL;
    const char *mode = "invalid";
    require("broker-arguments/argc", argc == 8 || argc == 9);
    mode = argv[7];
    printf("E1S10 BROKER START pipe=%s mode=%s abi=%u\n", argv[2], mode, (unsigned int)(8 * sizeof(void *)));
    fflush(stdout);
    step = "broker-load-wexhost/LoadLibraryW";
    host = LoadLibraryW(L"wexhost.dll");
    require("broker-load-wexhost/LoadLibraryW", host != NULL);
    transport = (transport_fn)(void *)GetProcAddress(host, "WexE1s6Transport");
    require("broker-transport-export/GetProcAddress(WexE1s6Transport)", transport != NULL);
    negative = strcmp(mode, "accept") && strcmp(mode, "replay") &&
               strcmp(mode, "thread") && strcmp(mode, "process");
    require("broker-mode/strcmp", !(strcmp(mode,"accept") && strcmp(mode,"unknown") && strcmp(mode,"tombstone") &&
        strcmp(mode,"stale") && strcmp(mode,"no-cap") && strcmp(mode,"digest") &&
        strcmp(mode,"evidence") && strcmp(mode,"expired") && strcmp(mode,"replay") &&
        strcmp(mode,"thread") && strcmp(mode,"process") && strcmp(mode,"caller-death") &&
        strcmp(mode,"group-removed") && strcmp(mode,"missing-group") && strcmp(mode,"absent-install") &&
        strcmp(mode,"raw-short") && strcmp(mode,"raw-trailing") && strcmp(mode,"raw-oversized") &&
        strcmp(mode,"raw-malformed")));
    generation = _strtoui64(argv[5], NULL, 10);
    require("broker-map-generation/_strtoui64", generation != 0);
    require("broker-account-key/unhex", unhex(argv[4], in + 124));
    require("broker-account-name/strlen", strlen(argv[3]) > 0 && strlen(argv[3]) <= 63);
    require("broker-account-sid", ConvertStringSidToSidA(argv[6], &expected));
    require("broker-pipe-sd", ConvertStringSecurityDescriptorToSecurityDescriptorA("D:(A;;GA;;;SY)(A;;GRGW;;;WD)",
          SDDL_REVISION_1, &sd, NULL));
    sa.lpSecurityDescriptor = sd;
    pipe = CreateNamedPipeA(argv[2], PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 1, 1, 10000, &sa);
    require("broker-create-pipe/CreateNamedPipeA", pipe != INVALID_HANDLE_VALUE);
    progress("READY");
    require("broker-connect-pipe", ConnectNamedPipe(pipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED);
    progress("BROKER CONNECTED");
    require("broker-read-marker", ReadFile(pipe, &marker, 1, &n, NULL));
    require("broker-read-marker-length", n == 1);
    require("broker-read-marker-value", marker == 0x5a);
    greeted = TRUE;
    require("broker-peer-pid", GetNamedPipeClientProcessId(pipe, &pid));
    /* open_token requires PROCESS_QUERY_INFORMATION when elevation is enforced
     * (server/token.c). LIMITED_INFORMATION alone cannot open even TOKEN_QUERY.
     * process_map_access also grants LIMITED_INFORMATION, needed by TEST_INSTALL. */
    process = OpenProcess(PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, pid);
    require("broker-open-peer/OpenProcess(PROCESS_QUERY_INFORMATION|SYNCHRONIZE)", process != NULL);
    require("broker-open-peer-token", OpenProcessToken(process, TOKEN_QUERY, &caller));
    require("broker-peer-statistics", info(caller, TokenStatistics, &before, sizeof(before)));
    require("broker-peer-session", info(caller, TokenSessionId, &session, sizeof(session)));
    progress("BROKER PEER QUERIED");
    require("s6-open", transport_step(transport, pipe, E1S6_OPEN, reg, sizeof(reg), &channel, "OPEN"));
    transported = TRUE;
    progress("S6 OPENED");
    require("s6-positive-control/transport_control", transport_control(transport, pipe, reg, &channel));
    if (!strcmp(mode,"no-cap")) caps &= ~0x80u;
    step = "broker-first-negotiate";
    status = create_token(WEX_TOKEN_NEGOTIATE, &context, NULL, &caps, NULL, 0, &token, NULL, &size);
    if (!strcmp(mode,"no-cap"))
    {
        check(status == (NTSTATUS)0xc00000bb && !context && !token && !caps && !size);
        progress("NO CAP DENIED");
        goto complete;
    }
    require("broker-first-negotiate/NtCreateWexAccountToken", !status && context && !token && !size);
    progress("NEGOTIATED");
    wex_token_put32(in, 0x00010004); wex_token_put32(in + 4, WEX_TOKEN_REQUEST_SIZE);
    wex_token_put64(in + 8, caps); wex_token_put64(in + 16, 1);
    /* Synthetic metadata, never authentication evidence. Real S6 role and real
     * standard peer objects are used; only S18 may supply production bindings. */
    memset(in + 24, 0x11, 96); wex_token_put32(in + 120, 1);
    memcpy(in + 140, reg + 40, 8); memcpy(in + 148, reg + 48, 8); memcpy(in + 156, reg + 56, 8);
    wex_token_put64(in + 164, !strcmp(mode,"stale") ? generation + 1 : generation);
    wex_token_put64(in + 172, 1); wex_token_put32(in + 180, session);
    memset(in + 188, 0x22, 16); memcpy(in + 204, argv[3], strlen(argv[3]));
    if (strcmp(mode,"absent-install"))
    {
        step = "broker-first-install";
        status = create_token(WEX_TOKEN_TEST_INSTALL, &context, process, &caps, in, sizeof(in), &token, NULL, &size);
        require("broker-first-install/NtCreateWexAccountToken", !status && !token && !size);
        progress("INSTALLED");
    }
    if (!strcmp(mode,"thread") || !strcmp(mode,"process"))
    {
        step = "broker-copy-context";
        copy_context(context, in, !strcmp(mode,"process"));
        require("broker-copy-context/result", !failures);
        progress("COPY DENIED");
    }
    if (!strcmp(mode,"group-removed") || !strcmp(mode,"missing-group"))
    {
        DWORD start = GetTickCount();
        require("broker-membership-gate/argc", argc == 9);
        while (GetFileAttributesA(argv[8]) == INVALID_FILE_ATTRIBUTES && GetTickCount() - start < 3000) Sleep(10);
        require("broker-membership-gate", GetFileAttributesA(argv[8]) != INVALID_FILE_ATTRIBUTES);
        progress("MEMBERSHIP GATE RELEASED");
    }
    if (!strcmp(mode,"caller-death"))
    {
        marker = 0x7e;
        require("broker-caller-death-send", WriteFile(pipe, &marker, 1, &n, NULL));
        require("broker-caller-death-send-length", n == 1);
        require("broker-caller-death-wait/WaitForSingleObject", WaitForSingleObject(process, 3000) == WAIT_OBJECT_0);
        progress("CALLER EXITED");
    }
    if (!strcmp(mode,"digest")) in[24] ^= 1;
    if (!strcmp(mode,"evidence")) in[188] ^= 1;
    if (!strcmp(mode,"expired")) Sleep(5100);
    size = sizeof(out); memset(out, 0xcc, sizeof(out));
    step = "broker-first-issue";
    if (!strncmp(mode,"raw-",4))
    {
        if (!strcmp(mode,"raw-short")) input_size--;
        if (!strcmp(mode,"raw-trailing")) input_size++;
        if (!strcmp(mode,"raw-oversized")) input_size = WEX_TOKEN_FIXTURE_SIZE + 1;
        if (!strcmp(mode,"raw-malformed")) in[184] = 1;
        /* Oversize is larger than even TEST_INSTALL, but remains bounded. */
        if (input_size > sizeof(in))
        {
            BYTE oversized[WEX_TOKEN_FIXTURE_SIZE + 1];
            memcpy(oversized, in, sizeof(in)); oversized[sizeof(in)] = 0x55;
            status = raw_request(WEX_TOKEN_ISSUE, context, caps, oversized, sizeof(oversized), out);
        }
        else status = raw_request(WEX_TOKEN_ISSUE, context, caps, in, input_size, out);
        check(status == (NTSTATUS)0xc0000022);
        size = WEX_TOKEN_FAILURE_SIZE;
        if (!strcmp(mode,"raw-malformed")) in[184] = 0;
    }
    else status = create_token(WEX_TOKEN_ISSUE, &context, NULL, &caps, in, WEX_TOKEN_INPUT_SIZE, &token, out, &size);
    if (negative)
    {
        check(status == (NTSTATUS)0xc0000022 && !token); failure_record(out, size, status);
        progress("ISSUE DENIED");
    }
    else
    {
        BYTE first_auth[8];
        require("broker-first-issue/NtCreateWexAccountToken", !status && token && size == sizeof(out));
        progress("ISSUED FIRST");
        if (token) check_token(token, caller, expected, out);
#ifdef WEX_E1S13_HKU_PROBE
        if (token) e1s13_load_checks(context, token, expected, argv[6], in, argc == 9 ? argv[8] : NULL);
#endif
        memcpy(first_auth, out + 104, sizeof(first_auth));
        if (token) { CloseHandle(token); token = NULL; }
        /* Check the first successful transaction as well as the second. */
        size = sizeof(out);
        step = "broker-first-replay";
        status = create_token(WEX_TOKEN_ISSUE, &context, NULL, &caps, in, WEX_TOKEN_INPUT_SIZE, &token, out, &size);
        check(status == (NTSTATUS)0xc0000022 && !token); failure_record(out, size, status);
        if (token) { CloseHandle(token); token = NULL; }
        CloseHandle(context); context = NULL; size = 0;
        step = "broker-second-negotiate";
        status = create_token(WEX_TOKEN_NEGOTIATE, &context, NULL, &caps, NULL, 0, &token, NULL, &size);
        require("broker-second-negotiate/NtCreateWexAccountToken", !status && context && !token && !size);
        wex_token_put64(in + 16, 2); in[88] ^= 1; in[188] ^= 1;
        step = "broker-second-install";
        status = create_token(WEX_TOKEN_TEST_INSTALL, &context, process, &caps, in, sizeof(in), &token, NULL, &size);
        require("broker-second-install/NtCreateWexAccountToken", !status && !token && !size);
        size = sizeof(out);
        step = "broker-second-issue";
        status = create_token(WEX_TOKEN_ISSUE, &context, NULL, &caps, in, WEX_TOKEN_INPUT_SIZE, &token, out, &size);
        require("broker-second-issue/NtCreateWexAccountToken", !status && token && size == sizeof(out));
        progress("ISSUED SECOND");
        if (token) check_token(token, caller, expected, out);
        check(memcmp(first_auth, out + 104, sizeof(first_auth)) != 0);
#ifdef WEX_E1S13_HKU_PROBE
        if (token) e1s13_load_checks(context, token, expected, argv[6], in, argc == 9 ? argv[8] : NULL);
#endif
    }
    /* Every issued or failed transaction is consumed, including digest/account
     * failures. Retrying the original bytes must never produce another token. */
    if (!strcmp(mode,"digest")) in[24] ^= 1;
    if (!strcmp(mode,"evidence")) in[188] ^= 1;
    {
        HANDLE replay = NULL;
        size = sizeof(out);
        step = "broker-final-replay";
        status = create_token(WEX_TOKEN_ISSUE, &context, NULL, &caps, in, WEX_TOKEN_INPUT_SIZE, &replay, out, &size);
        check(status == (NTSTATUS)0xc0000022 && !replay); failure_record(out, size, status);
        if (replay) CloseHandle(replay);
    }
    progress("REPLAY CHECKED");
    step = "broker-caller-unchanged";
    check(info(caller, TokenStatistics, &after, sizeof(after)) && same_luid(before.TokenId, after.TokenId) &&
          same_luid(before.ModifiedId, after.ModifiedId) && same_luid(before.AuthenticationId, after.AuthenticationId));
    if (!negative && token && !failures)
    {
#ifdef WEX_E1S13_HKU_PROBE
        if (e1s13_loaded)
#endif
        {
        TOKEN_STATISTICS stats = {0};
        step = "broker-child-and-grandchild";
        check(info(token, TokenStatistics, &stats, sizeof(stats)));
        /* End the context before any process API can enable a broker privilege
         * and change ModifiedId. Replay above must use the original authority. */
        CloseHandle(context); context = NULL;
        spawn_identity(token, argv[6], stats.AuthenticationId, session, 1);
        check(info(caller, TokenStatistics, &after, sizeof(after)) && same_luid(before.TokenId, after.TokenId) &&
              same_luid(before.ModifiedId, after.ModifiedId) && same_luid(before.AuthenticationId, after.AuthenticationId));
        }
    }
complete:
done:
    marker = failures ? 1 : 0;
    step = "broker-peer-result/WriteFile";
    if (greeted && strcmp(mode,"caller-death"))
    {
        if (test(WriteFile(pipe, &marker, 1, &n, NULL)))
        {
            step = "broker-peer-result-length/WriteFile";
            check(n == 1);
        }
    }
    step = "broker-close-transport";
    if (transported) check(transport_step(transport, pipe, E1S6_CLOSE, NULL, 0, &channel, "CLOSE"));
    if (context) CloseHandle(context);
    if (token) CloseHandle(token);
    if (caller) CloseHandle(caller);
    if (process) CloseHandle(process);
    if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
    LocalFree(sd); LocalFree(expected); if (host) FreeLibrary(host);
    if (!failures)
    {
        printf("E1S10 CASE %s OK\n", mode); fflush(stdout);
        progress("OK");
    }
    return failures ? 1 : 0;
}

/* Best-effort failure snapshot in the same owned prefix/server. Wine pipes are
 * server objects, not entries in /tmp/.wine-UID or Linux FIFOs. */
static int pipe_namespace( void )
{
    WIN32_FIND_DATAA data;
    HANDLE search;
    DWORD error;
    unsigned int count = 0;
    progress("PIPE NAMESPACE START");
    search = FindFirstFileA("\\\\.\\pipe\\*", &data);
    if (search == INVALID_HANDLE_VALUE)
    {
        printf("E1S10 PIPE NAMESPACE unavailable GetLastError=%lu\n", (unsigned long)GetLastError());
        fflush(stdout);
        return 2;
    }
    do
    {
        printf("E1S10 PIPE %s\n", data.cFileName); fflush(stdout);
    } while (++count < 256 && FindNextFileA(search, &data));
    error = GetLastError();
    if (count == 256) progress("PIPE NAMESPACE TRUNCATED");
    else if (error != ERROR_NO_MORE_FILES)
    {
        printf("E1S10 PIPE NAMESPACE FindNextFileA GetLastError=%lu\n", (unsigned long)error);
        fflush(stdout);
    }
    FindClose(search);
    progress("PIPE NAMESPACE END");
    return 0;
}

int main( int argc, char **argv )
{
    printf("E1S10 START mode=%s abi=%u\n", argc > 1 ? argv[1] : "missing", (unsigned int)(8 * sizeof(void *)));
    fflush(stdout);
    if (argc == 2 && !strcmp(argv[1], "pipes")) return pipe_namespace();
#ifdef WEX_E1S13_HKU_PROBE
    if (argc == 2 && !strcmp(argv[1],"hku-hold")) return 0; /* created suspended, never resumed */
    if (argc > 1 && (!strcmp(argv[1],"denied") || !strcmp(argv[1],"normal-denied") || !strcmp(argv[1],"normal")))
        e1s13_denied(strcmp(argv[1],"denied") != 0);
#endif
    step = "startup-token-export/GetProcAddress(NtCreateWexAccountToken)";
    token_export = (create_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateWexAccountToken");
    if (!test(token_export != NULL)) return 2;
    step = "startup-raw-export/GetProcAddress(wine_server_call)";
    server_call = (server_call_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_server_call");
    if (!test(server_call != NULL)) return 2;
    step = argc > 1 ? argv[1] : "missing-mode";
#ifdef WEX_E1S13_HKU_PROBE
    if (argc == 7 && !strcmp(argv[1], "hku-peer"))
    {
        if (!strcmp(argv[6], "1"))
            snprintf(e1s13_bob_denied_path, sizeof(e1s13_bob_denied_path), "C:\\users\\.e1s\\%s\\%s-%s\\user.reg",
                     argv[3], argv[4], argv[5]);
        return peer(argv[2], FALSE);
    }
#endif
    if (argc == 3 && !strcmp(argv[1], "denied"))
        return denied(!strcmp(argv[2], "limited") ? TokenElevationTypeLimited : TokenElevationTypeDefault, FALSE, FALSE);
    if (argc == 3 && !strcmp(argv[1], "normal-denied"))
        return denied(!strcmp(argv[2], "limited") ? TokenElevationTypeLimited : TokenElevationTypeDefault, TRUE, FALSE);
    if (argc == 2 && !strcmp(argv[1], "normal")) return denied(TokenElevationTypeDefault, TRUE, TRUE);
    if (argc == 7 && !strcmp(argv[1], "identity")) return identity(argv);
    if (argc == 3 && !strcmp(argv[1], "copied"))
    {
        HANDLE mapping = (HANDLE)(ULONG_PTR)_strtoui64(argv[2], NULL, 10);
        struct copied_context *copy = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(*copy));
        int rc;
        if (!test(copy != NULL)) return 2;
        rc = copied_thread(copy);
        CloseHandle(copy->context); UnmapViewOfFile(copy); CloseHandle(mapping);
        return rc;
    }
    if (argc == 3 && (!strcmp(argv[1], "peer") || !strcmp(argv[1], "peer-death")))
        return peer(argv[2], !strcmp(argv[1], "peer-death"));
    if (argc > 1 && !strcmp(argv[1], "broker")) return broker(argc, argv);
    puts("usage: probe denied|normal-denied default|limited | normal | pipes | peer[-death] PIPE | broker PIPE NAME KEY GENERATION SID MODE [GATE]");
    fflush(stdout);
    return 2;
}
