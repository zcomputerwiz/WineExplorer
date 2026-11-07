/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Disposable Wine-only S10 test broker and standard-user peer. Never install
 * this as the shipping broker. No PAM, launch, profile or password interface.
 */
#define COBJMACROS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <sddl.h>
#include <winternl.h>
/* Byte helpers only; keep Wine SDK headers off the MinGW include search path. */
#include "../wine/include/wine/wex_token.h"
#include "e1s6_protocol.h"

typedef NTSTATUS (WINAPI *create_fn)(ULONG,HANDLE*,HANDLE,ULONGLONG*,const void*,ULONG,HANDLE*,void*,ULONG*);
typedef DWORD (WINAPI *transport_fn)(HANDLE,DWORD,void*,DWORD,UINT64*);
static create_fn create_token;
static unsigned int failures;
#define check(c) do { if (!(c)) { printf("FAIL line %u: %s\n", __LINE__, #c); failures++; } } while (0)

static BOOL info( HANDLE token, TOKEN_INFORMATION_CLASS type, void *out, DWORD size )
{
    DWORD needed;
    return GetTokenInformation( token, type, out, size, &needed );
}

static BOOL same_luid( LUID a, LUID b )
{
    return a.LowPart == b.LowPart && a.HighPart == b.HighPart;
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
            if (primary->PrimaryGroup && EqualSid(g->Sid, primary->PrimaryGroup)) primary_found = TRUE;
            if ((g->Attributes & SE_GROUP_LOGON_ID) == SE_GROUP_LOGON_ID)
            {
                check( !logon );
                logon = *GetSidSubAuthorityCount(g->Sid) == 3 &&
                    *GetSidSubAuthority(g->Sid, 0) == 5 &&
                    *GetSidSubAuthority(g->Sid, 1) == (DWORD)stats.AuthenticationId.HighPart &&
                    *GetSidSubAuthority(g->Sid, 2) == stats.AuthenticationId.LowPart;
            }
        }
    }
    else check(FALSE);
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
    else check(FALSE);
    check( info(token, TokenOwner, owner_buf, sizeof(owner_buf)) && EqualSid(owner->Owner, expected) );
    if (info(token, TokenDefaultDacl, dacl_buf, sizeof(dacl_buf)) && dacl->DefaultDacl)
    {
        BOOL found_user = FALSE, found_system = FALSE;
        check(dacl->DefaultDacl->AceCount == 2);
        for (i = 0; i < dacl->DefaultDacl->AceCount; i++)
        {
            ACCESS_ALLOWED_ACE *ace;
            if (!GetAce(dacl->DefaultDacl, i, (void **)&ace)) { check(FALSE); break; }
            check(ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE && ace->Mask == GENERIC_ALL);
            if (EqualSid((PSID)&ace->SidStart, expected)) found_user = TRUE;
            else if (system && EqualSid((PSID)&ace->SidStart, system)) found_system = TRUE;
            else check(FALSE);
        }
        check(found_user && found_system);
    }
    else check(FALSE);
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

static int denied(void)
{
    HANDLE context = NULL, token = NULL;
    ULONGLONG caps = WEX_TOKEN_CAPS;
    ULONG size = 0;
    NTSTATUS status = create_token(WEX_TOKEN_NEGOTIATE, &context, NULL, &caps, NULL, 0, &token, NULL, &size);
    check(status == (NTSTATUS)0xc0000022 || status == (NTSTATUS)0xc00000bb);
    check(!context && !token && !caps && !size);
    return failures ? 1 : 0;
}

/* The peer remains alive while the broker compares its retained token. It
 * cannot receive token/context handles; one byte is the whole fixture message. */
static int peer( const char *name )
{
    HANDLE pipe;
    DWORD n;
    BYTE marker = 0x5a;
    if (!WaitNamedPipeA(name, 10000)) return 2;
    pipe = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (pipe == INVALID_HANDLE_VALUE) return 2;
    if (!WriteFile(pipe, &marker, 1, &n, NULL) || n != 1 || !ReadFile(pipe, &marker, 1, &n, NULL) || n != 1)
    { CloseHandle(pipe); return 2; }
    CloseHandle(pipe);
    return marker;
}

static int broker( int argc, char **argv )
{
    BYTE reg[E1S6_OPEN_SIZE] = {0}, in[WEX_TOKEN_FIXTURE_SIZE] = {0}, out[WEX_TOKEN_RESPONSE_SIZE];
    HANDLE pipe = INVALID_HANDLE_VALUE, process = NULL, caller = NULL, token = NULL, context = NULL;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, FALSE};
    PSECURITY_DESCRIPTOR sd = NULL;
    PSID expected = NULL;
    DWORD n, pid;
    ULONG size = 0, session;
    UINT64 channel = 0;
    ULONGLONG caps = WEX_TOKEN_CAPS, generation;
    TOKEN_STATISTICS before, after;
    NTSTATUS status;
    BYTE marker;
    BOOL negative, transported = FALSE;
    HMODULE host = LoadLibraryW(L"wexhost.dll");
    transport_fn transport = host ? (transport_fn)(void *)GetProcAddress(host, "WexE1s6Transport") : NULL;
    const char *mode;
    if (argc != 8 || !transport) return 2;
    mode = argv[7];
    negative = strcmp(mode, "accept") != 0;
    if (strcmp(mode,"accept") && strcmp(mode,"unknown") && strcmp(mode,"tombstone") &&
        strcmp(mode,"stale") && strcmp(mode,"no-cap") && strcmp(mode,"digest") &&
        strcmp(mode,"evidence") && strcmp(mode,"expired")) return 2;
    generation = _strtoui64(argv[5], NULL, 10);
    if (!generation || !unhex(argv[4], in + 124) || strlen(argv[3]) > 63 ||
        !ConvertStringSidToSidA(argv[6], &expected)) return 2;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA("D:(A;;GA;;;SY)(A;;GRGW;;;WD)",
          SDDL_REVISION_1, &sd, NULL)) goto done;
    sa.lpSecurityDescriptor = sd;
    pipe = CreateNamedPipeA(argv[2], PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 1, 1, 10000, &sa);
    if (pipe == INVALID_HANDLE_VALUE) goto done;
    puts("E1S10 READY"); fflush(stdout);
    if (!ConnectNamedPipe(pipe, NULL) && GetLastError() != ERROR_PIPE_CONNECTED) goto done;
    if (!ReadFile(pipe, &marker, 1, &n, NULL) || n != 1 || marker != 0x5a ||
        !GetNamedPipeClientProcessId(pipe, &pid)) goto done;
    process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process || !OpenProcessToken(process, TOKEN_QUERY, &caller) ||
        !info(caller, TokenStatistics, &before, sizeof(before)) ||
        !info(caller, TokenSessionId, &session, sizeof(session))) goto done;
    if (transport(pipe, E1S6_OPEN, reg, sizeof(reg), &channel)) goto done;
    transported = TRUE;
    if (!strcmp(mode,"no-cap")) caps &= ~0x80u;
    status = create_token(WEX_TOKEN_NEGOTIATE, &context, NULL, &caps, NULL, 0, &token, NULL, &size);
    if (!strcmp(mode,"no-cap"))
    {
        check(status != 0 && !context && !token && !caps);
        goto complete;
    }
    if (status || !context) goto done;
    wex_token_put32(in, 0x00010004); wex_token_put32(in + 4, WEX_TOKEN_REQUEST_SIZE);
    wex_token_put64(in + 8, caps); wex_token_put64(in + 16, 1);
    /* Synthetic metadata, never authentication evidence. Real S6 role and real
     * standard peer objects are used; only S18 may supply production bindings. */
    memset(in + 24, 0x11, 96); wex_token_put32(in + 120, 1);
    memcpy(in + 140, reg + 40, 8); memcpy(in + 148, reg + 48, 8); memcpy(in + 156, reg + 56, 8);
    wex_token_put64(in + 164, !strcmp(mode,"stale") ? generation + 1 : generation);
    wex_token_put64(in + 172, 1); wex_token_put32(in + 180, session);
    memset(in + 188, 0x22, 16); memcpy(in + 204, argv[3], strlen(argv[3]));
    status = create_token(WEX_TOKEN_TEST_INSTALL, &context, process, &caps, in, sizeof(in), &token, NULL, &size);
    if (status) goto done;
    if (!strcmp(mode,"digest")) in[24] ^= 1;
    if (!strcmp(mode,"evidence")) in[188] ^= 1;
    if (!strcmp(mode,"expired")) Sleep(5100);
    size = sizeof(out); memset(out, 0xcc, sizeof(out));
    status = create_token(WEX_TOKEN_ISSUE, &context, NULL, &caps, in, WEX_TOKEN_INPUT_SIZE, &token, out, &size);
    if (negative)
    {
        check(status != 0 && !token); failure_record(out, size, status);
    }
    else
    {
        BYTE first_auth[8];
        check(!status && token && size == sizeof(out));
        if (token) check_token(token, caller, expected, out);
        memcpy(first_auth, out + 104, sizeof(first_auth));
        if (token) { CloseHandle(token); token = NULL; }
        CloseHandle(context); context = NULL; size = 0;
        status = create_token(WEX_TOKEN_NEGOTIATE, &context, NULL, &caps, NULL, 0, &token, NULL, &size);
        check(!status && context && !token);
        if (status || !context) goto done;
        wex_token_put64(in + 16, 2); in[88] ^= 1; in[188] ^= 1;
        status = create_token(WEX_TOKEN_TEST_INSTALL, &context, process, &caps, in, sizeof(in), &token, NULL, &size);
        check(!status && !token);
        if (status) goto done;
        size = sizeof(out);
        status = create_token(WEX_TOKEN_ISSUE, &context, NULL, &caps, in, WEX_TOKEN_INPUT_SIZE, &token, out, &size);
        check(!status && token && size == sizeof(out));
        if (token) check_token(token, caller, expected, out);
        check(memcmp(first_auth, out + 104, sizeof(first_auth)) != 0);
    }
    if (token) { CloseHandle(token); token = NULL; }
    /* Every issued or failed transaction is consumed, including digest/account
     * failures. Retrying the original bytes must never produce another token. */
    if (!strcmp(mode,"digest")) in[24] ^= 1;
    if (!strcmp(mode,"evidence")) in[188] ^= 1;
    size = sizeof(out);
    status = create_token(WEX_TOKEN_ISSUE, &context, NULL, &caps, in, WEX_TOKEN_INPUT_SIZE, &token, out, &size);
    check(status != 0 && !token); failure_record(out, size, status);
    check(info(caller, TokenStatistics, &after, sizeof(after)) && same_luid(before.TokenId, after.TokenId) &&
          same_luid(before.ModifiedId, after.ModifiedId) && same_luid(before.AuthenticationId, after.AuthenticationId));
complete:
    if (!failures) puts("E1S10 OK");
    marker = failures ? 1 : 0;
    WriteFile(pipe, &marker, 1, &n, NULL);
    if (transported) transport(pipe, E1S6_CLOSE, NULL, 0, &channel);
    if (context) CloseHandle(context);
    if (token) CloseHandle(token);
    if (caller) CloseHandle(caller);
    if (process) CloseHandle(process);
    if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
    LocalFree(sd); LocalFree(expected); if (host) FreeLibrary(host);
    return failures ? 1 : 0;
done:
    check(FALSE);
    goto complete;
}

int main( int argc, char **argv )
{
    create_token = (create_fn)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateWexAccountToken");
    if (!create_token) { puts("FAIL: private export missing"); return 2; }
    if (argc == 2 && !strcmp(argv[1], "denied")) return denied();
    if (argc == 3 && !strcmp(argv[1], "peer")) return peer(argv[2]);
    if (argc > 1 && !strcmp(argv[1], "broker")) return broker(argc, argv);
    puts("usage: probe denied | peer PIPE | broker PIPE NAME KEY GENERATION SID MODE");
    return 2;
}
