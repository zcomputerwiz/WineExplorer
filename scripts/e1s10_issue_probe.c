/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * The security-relevant negatives of E1-S10 token issuance (the lab's create_e1s_account_token), for
 * test_sysrun_server.sh's lab section e1s10_issue. Never install this as the shipping broker.
 *
 *   broker RESULT PIPE NAME KEY GENERATION SID TOMBNAME TOMBKEY
 *       Runs as the S6-endorsed test broker (wexelevate_test.exe, a SYSTEM service in the standard user's
 *       session). Waits on PIPE for the standard-user peer (the caller), opens S6 on that pipe, then runs each case
 *       on a context of its own: one accepted issue (the control: real account NAME, its map KEY and GENERATION,
 *       expected SID), then replay, stale generation, tombstoned account, unknown account, a changed request,
 *       the context used from another thread and from another process, and raw requests of wrong length or with an
 *       out-of-range field. Every refusal must be STATUS_ACCESS_DENIED with the cleared failure record and no
 *       token. Writes NAME=PASS|FAIL lines and a last e1s10=PASS|FAIL to RESULT.
 *   peer PIPE        the caller: a standard user's process that connects and waits for the broker's result
 *   denied           a process that is not the endorsed broker: every operation is refused
 *   copied MAPPING   (internal) the other process of the process case
 *
 * The raw requests need the request's number, which the suite reads from the Wine tree it built and passes as
 * E1S10_ISSUE_REQ; the layout it relies on (header 12 bytes, operation, context, caller, caps at 24) is checked by
 * the suite against include/wine/server_protocol.h before it builds this file.
 */
#include <errno.h>
#include <stdarg.h>
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

#ifndef E1S10_ISSUE_REQ
#error "E1S10_ISSUE_REQ (the number of create_e1s_account_token) is set by the suite"
#endif

#define DENIED ((NTSTATUS)0xc0000022)

typedef NTSTATUS (WINAPI *create_fn)(ULONG,HANDLE*,HANDLE,ULONGLONG*,const void*,ULONG,HANDLE*,void*,ULONG*);
typedef DWORD (WINAPI *transport_fn)(HANDLE,DWORD,void*,DWORD,UINT64*);
typedef NTSTATUS (__cdecl *server_call_fn)(void *);
static create_fn create_export;
static server_call_fn server_call;
static FILE *out;
static unsigned int failures, case_failures;
static const char *current = "setup";

static void report( const char *fmt, ... )
{
    va_list args;
    FILE *f = out ? out : stdout;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fflush(f);
}

static BOOL checked( BOOL ok, unsigned int line, const char *expr )
{
    if (!ok)
    {
        report("FAIL case=%s line=%u %s GetLastError=%lu\n", current, line, expr, (unsigned long)GetLastError());
        failures++; case_failures++;
    }
    return ok;
}
#define test(c) checked(!!(c), __LINE__, #c)
#define check(c) do { test(c); } while (0)

static NTSTATUS create_token( ULONG operation, HANDLE *context, HANDLE caller, ULONGLONG *caps,
                              const void *record, ULONG length, HANDLE *token, void *response, ULONG *size )
{
    return create_export(operation, context, caller, caps, record, length, token, response, size);
}

static BOOL unhex( const char *s, BYTE *dst )
{
    unsigned int i, a, b;
    if (strlen(s) != 32) return FALSE;
    for (i = 0; i < 16; i++)
    {
        char x = s[2*i], y = s[2*i+1];
        if (x >= '0' && x <= '9') a = x - '0'; else if (x >= 'a' && x <= 'f') a = x - 'a' + 10; else return FALSE;
        if (y >= '0' && y <= '9') b = y - '0'; else if (y >= 'a' && y <= 'f') b = y - 'a' + 10; else return FALSE;
        dst[i] = (a << 4) | b;
    }
    return TRUE;
}

/* a refusal: the kind-5 failure record of 108 bytes, the status at 32, every other byte after the header zero */
static void failure_record( const BYTE *rec, ULONG size, NTSTATUS status )
{
    unsigned int i;
    check(size == WEX_TOKEN_FAILURE_SIZE && wex_token_u32(rec) == 0x00010005 &&
          wex_token_u32(rec + 4) == WEX_TOKEN_FAILURE_SIZE && wex_token_u32(rec + 32) == (ULONG)status);
    for (i = 8; i < WEX_TOKEN_FAILURE_SIZE; i++)
        if (i < 32 || i >= 36) check(rec[i] == 0);
}

/* wine_server_call's carrier (wine/server.h, __server_request_info): the fixed 88-byte request union, the data
 * count, the reply pointer and the data vectors. Checked for both ABIs. */
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

/* ISSUE past ntdll's own size check: the server must refuse what ntdll would not send */
static NTSTATUS raw_issue( HANDLE context, ULONGLONG caps, const BYTE *input, ULONG length, BYTE *reply )
{
    struct raw_call r;
    NTSTATUS status;
    memset(&r, 0, sizeof(r));
    memset(reply, 0xcc, WEX_TOKEN_RESPONSE_SIZE);
    wex_token_put32(r.u.bytes, E1S10_ISSUE_REQ);
    wex_token_put32(r.u.bytes + 4, length);
    wex_token_put32(r.u.bytes + 8, WEX_TOKEN_RESPONSE_SIZE);
    wex_token_put32(r.u.bytes + 12, WEX_TOKEN_ISSUE);
    wex_token_put32(r.u.bytes + 16, (ULONG)(ULONG_PTR)context);
    wex_token_put64(r.u.bytes + 24, caps);
    r.count = length != 0; r.reply = reply;
    r.data[0].ptr = input; r.data[0].size = length;
    status = server_call(&r);
    check(!wex_token_u32(r.u.bytes + 8) && !wex_token_u32(r.u.bytes + 12)); /* the reply's context and token */
    if (wex_token_u32(r.u.bytes + 4)) /* reply data size */
    {
        check(wex_token_u32(r.u.bytes + 4) == WEX_TOKEN_FAILURE_SIZE);
        failure_record(reply, WEX_TOKEN_FAILURE_SIZE, status);
    }
    return status;
}

/* ---- the broker ------------------------------------------------------------------------------------------------ */

struct issue_case
{
    BYTE reg[E1S6_OPEN_SIZE];       /* the S6 registration: broker token ids and generation at 40, 48, 56 */
    HANDLE caller;                  /* the peer process */
    DWORD session;
    ULONGLONG generation;           /* the accounts map's */
    BYTE key[16];                   /* the account's incarnation key in the map */
};

static ULONG request_counter;

/* the request bytes for NAME (synthetic metadata, never evidence; the S6 role and the caller are real) */
static void build_input( const struct issue_case *ic, BYTE *in, const char *name, const BYTE *key, ULONGLONG generation )
{
    memset(in, 0, WEX_TOKEN_FIXTURE_SIZE);
    wex_token_put32(in, 0x00010004); wex_token_put32(in + 4, WEX_TOKEN_REQUEST_SIZE);
    wex_token_put64(in + 8, WEX_TOKEN_CAPS); wex_token_put64(in + 16, ++request_counter);
    memset(in + 24, 0x11, 32); memset(in + 56, 0x12, 32); memset(in + 88, 0x13 + (BYTE)request_counter, 32);
    wex_token_put32(in + 120, 1);
    memcpy(in + 124, key, 16);
    memcpy(in + 140, ic->reg + 40, 8); memcpy(in + 148, ic->reg + 48, 8); memcpy(in + 156, ic->reg + 56, 8);
    wex_token_put64(in + 164, generation);
    wex_token_put64(in + 172, 1); wex_token_put32(in + 180, ic->session);
    memset(in + 188, 0x22 + (BYTE)request_counter, 16);
    memcpy(in + 204, name, strlen(name));
}

/* a fresh context with NAME's request installed; NULL when that failed (a failure of the setup, not a pass) */
static HANDLE installed( struct issue_case *ic, BYTE *in, const char *name, const BYTE *key, ULONGLONG generation )
{
    HANDLE context = NULL, token = NULL;
    ULONGLONG caps = WEX_TOKEN_CAPS;
    ULONG size = 0;
    NTSTATUS status = create_token(WEX_TOKEN_NEGOTIATE, &context, NULL, &caps, NULL, 0, &token, NULL, &size);
    if (!test(!status && context && !token && !size && caps == WEX_TOKEN_CAPS))
    {
        report("NOTE case=%s negotiate status=%08lx\n", current, (unsigned long)status);
        return NULL;
    }
    build_input(ic, in, name, key, generation);
    status = create_token(WEX_TOKEN_TEST_INSTALL, &context, ic->caller, &caps, in, WEX_TOKEN_FIXTURE_SIZE, &token, NULL, &size);
    if (!test(!status && !token && !size))
    {
        report("NOTE case=%s install status=%08lx\n", current, (unsigned long)status);
        CloseHandle(context);
        return NULL;
    }
    return context;
}

/* ISSUE through ntdll; expect DENIED, no token, the failure record */
static void issue_denied( HANDLE context, const BYTE *in )
{
    HANDLE token = NULL;
    ULONGLONG caps = WEX_TOKEN_CAPS;
    BYTE reply[WEX_TOKEN_RESPONSE_SIZE];
    ULONG size = sizeof(reply);
    NTSTATUS status;
    memset(reply, 0xcc, sizeof(reply));
    status = create_token(WEX_TOKEN_ISSUE, &context, NULL, &caps, in, WEX_TOKEN_INPUT_SIZE, &token, reply, &size);
    if (!test(status == DENIED && !token)) report("NOTE case=%s issue status=%08lx\n", current, (unsigned long)status);
    if (token) CloseHandle(token);
    failure_record(reply, size, status);
}

static void begin( const char *name ) { current = name; case_failures = 0; }
static void end( void ) { report("%s=%s\n", current, case_failures ? "FAIL" : "PASS"); }

/* the control and replay: the real account is issued once; the same bytes again are refused */
static void case_accept( struct issue_case *ic, const char *name, PSID expected )
{
    BYTE in[WEX_TOKEN_FIXTURE_SIZE], reply[WEX_TOKEN_RESPONSE_SIZE], user_buf[256];
    HANDLE context, token = NULL;
    ULONGLONG caps = WEX_TOKEN_CAPS;
    ULONG size = sizeof(reply);
    TOKEN_ELEVATION_TYPE elevation = 0;
    DWORD needed;
    NTSTATUS status;

    begin("e1s10_accept_control");
    if ((context = installed(ic, in, name, ic->key, ic->generation)))
    {
        status = create_token(WEX_TOKEN_ISSUE, &context, NULL, &caps, in, WEX_TOKEN_INPUT_SIZE, &token, reply, &size);
        if (!test(!status && token && size == WEX_TOKEN_RESPONSE_SIZE))
            report("NOTE case=%s issue status=%08lx\n", current, (unsigned long)status);
        if (token)
        {
            check(GetTokenInformation(token, TokenUser, user_buf, sizeof(user_buf), &needed) &&
                  EqualSid(((TOKEN_USER *)user_buf)->User.Sid, expected));
            check(GetTokenInformation(token, TokenElevationType, &elevation, sizeof(elevation), &needed) &&
                  elevation == TokenElevationTypeFull);
            CloseHandle(token);
        }
        end();
        begin("e1s10_replay_refused");
        issue_denied(context, in);
        CloseHandle(context);
    }
    else { end(); begin("e1s10_replay_refused"); check(!"no context"); }
    end();
}

/* an installed request whose identity does not resolve to a current administrator */
static void case_identity( struct issue_case *ic, const char *label, const char *name, const BYTE *key, ULONGLONG generation )
{
    BYTE in[WEX_TOKEN_FIXTURE_SIZE];
    HANDLE context;
    begin(label);
    if ((context = installed(ic, in, name, key, generation)))
    {
        issue_denied(context, in);
        CloseHandle(context);
    }
    end();
}

/* the request issued is not the one installed */
static void case_changed( struct issue_case *ic, const char *name )
{
    BYTE in[WEX_TOKEN_FIXTURE_SIZE];
    HANDLE context;
    begin("e1s10_changed_request_refused");
    if ((context = installed(ic, in, name, ic->key, ic->generation)))
    {
        in[24] ^= 1;
        issue_denied(context, in);
        CloseHandle(context);
    }
    end();
}

struct copied { HANDLE context; BYTE input[WEX_TOKEN_INPUT_SIZE]; };

static DWORD WINAPI copied_thread( void *arg )
{
    struct copied *copy = arg;
    issue_denied(copy->context, copy->input);
    return 0;
}

/* the context used by another thread of the broker, or by another process */
static void case_copied( struct issue_case *ic, const char *name, BOOL process )
{
    BYTE in[WEX_TOKEN_FIXTURE_SIZE];
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    struct copied local, *copy = &local;
    HANDLE context, mapping = NULL, thread;
    char exe[MAX_PATH], command[2 * MAX_PATH];
    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};
    DWORD code = 1;

    begin(process ? "e1s10_other_process_refused" : "e1s10_other_thread_refused");
    if (!(context = installed(ic, in, name, ic->key, ic->generation))) { end(); return; }
    if (process)
    {
        if (!test((mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, sizeof(local), NULL))) ||
            !test((copy = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, sizeof(local)))))
            goto done;
    }
    copy->context = NULL;
    memcpy(copy->input, in, sizeof(copy->input));
    if (!test(DuplicateHandle(GetCurrentProcess(), context, GetCurrentProcess(), &copy->context, 0, process,
                              DUPLICATE_SAME_ACCESS)))
        goto done;
    if (process)
    {
        check(GetModuleFileNameA(NULL, exe, sizeof(exe)) != 0);
        snprintf(command, sizeof(command), "\"%s\" copied %llu", exe, (unsigned long long)(ULONG_PTR)mapping);
        si.cb = sizeof(si);
        if (test(CreateProcessA(exe, command, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)))
        {
            check(WaitForSingleObject(pi.hProcess, 10000) == WAIT_OBJECT_0);
            check(GetExitCodeProcess(pi.hProcess, &code) && code == 0);
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        }
    }
    else if (test((thread = CreateThread(NULL, 0, copied_thread, copy, 0, NULL))))
    {
        check(WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0);
        CloseHandle(thread);
    }
    CloseHandle(copy->context);
    /* the owner's own context was not consumed by the copy's attempt, and refuses nothing it should not */
done:
    if (process && copy != &local) UnmapViewOfFile(copy);
    if (mapping) CloseHandle(mapping);
    CloseHandle(context);
    end();
}

/* sizes and fields ntdll would never send */
static void case_raw( struct issue_case *ic, const char *label, const char *name )
{
    BYTE in[WEX_TOKEN_FIXTURE_SIZE + 1], reply[WEX_TOKEN_RESPONSE_SIZE];
    HANDLE context;
    ULONG length = WEX_TOKEN_INPUT_SIZE;
    NTSTATUS status;

    begin(label);
    if (!(context = installed(ic, in, name, ic->key, ic->generation))) { end(); return; }
    in[WEX_TOKEN_FIXTURE_SIZE] = 0x55;
    if (!strcmp(label, "e1s10_raw_short_refused")) length--;
    else if (!strcmp(label, "e1s10_raw_trailing_refused")) length++;
    else if (!strcmp(label, "e1s10_raw_oversized_refused")) length = WEX_TOKEN_FIXTURE_SIZE + 1;
    else in[184] = 1; /* e1s10_raw_field_refused: the reserved word must be zero */
    status = raw_issue(context, WEX_TOKEN_CAPS, in, length, reply);
    if (!test(status == DENIED)) report("NOTE case=%s raw status=%08lx\n", current, (unsigned long)status);
    CloseHandle(context);
    end();
}

static int broker( char **argv )
{
    struct issue_case ic;
    const char *result = argv[2], *pipe_name = argv[3], *name = argv[4], *tomb = argv[8];
    BYTE tomb_key[16], unknown_key[16], marker = 0;
    HANDLE pipe = INVALID_HANDLE_VALUE, token = NULL;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, FALSE};
    PSECURITY_DESCRIPTOR sd = NULL;
    PSID expected = NULL;
    HMODULE host;
    transport_fn transport = NULL;
    UINT64 channel = 0;
    DWORD n, pid, needed;
    BOOL opened = FALSE, greeted = FALSE;

    memset(&ic, 0, sizeof(ic));
    if (!(out = fopen(result, "w")))
    {
        fprintf(stderr, "e1s10 broker: cannot write %s (errno %d)\n", result, errno);
        return 2;
    }
    current = "setup";
    report("e1s10 broker abi=%u\n", (unsigned int)(8 * sizeof(void *)));
    ic.generation = _strtoui64(argv[6], NULL, 10);
    if (!test(ic.generation && unhex(argv[5], ic.key) && unhex(argv[9], tomb_key) &&
              ConvertStringSidToSidA(argv[7], &expected))) goto done;
    memset(unknown_key, 0x5a, sizeof(unknown_key));
    if (!test((host = LoadLibraryW(L"wexhost.dll")) &&
              (transport = (transport_fn)(void *)GetProcAddress(host, "WexE1s6Transport")))) goto done;
    if (!test(ConvertStringSecurityDescriptorToSecurityDescriptorA("D:(A;;GA;;;SY)(A;;GRGW;;;WD)", SDDL_REVISION_1, &sd, NULL)))
        goto done;
    sa.lpSecurityDescriptor = sd;
    pipe = CreateNamedPipeA(pipe_name, PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
                            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                            1, 1, 1, 10000, &sa);
    if (!test(pipe != INVALID_HANDLE_VALUE)) goto done;
    report("e1s10 ready\n");
    if (!test(ConnectNamedPipe(pipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED)) goto done;
    if (!test(ReadFile(pipe, &marker, 1, &n, NULL) && n == 1 && marker == 0x5a)) goto done;
    greeted = TRUE;
    /* open_token needs PROCESS_QUERY_INFORMATION under elevation enforcement; TEST_INSTALL needs LIMITED */
    if (!test(GetNamedPipeClientProcessId(pipe, &pid) &&
              (ic.caller = OpenProcess(PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, pid)) &&
              OpenProcessToken(ic.caller, TOKEN_QUERY, &token) &&
              GetTokenInformation(token, TokenSessionId, &ic.session, sizeof(ic.session), &needed))) goto done;
    if (!test(!transport(pipe, E1S6_OPEN, ic.reg, sizeof(ic.reg), &channel))) goto done;
    opened = TRUE;
    report("e1s10 s6 opened\n");

    case_accept(&ic, name, expected);
    case_identity(&ic, "e1s10_stale_generation_refused", name, ic.key, ic.generation + 1);
    case_identity(&ic, "e1s10_tombstoned_account_refused", tomb, tomb_key, ic.generation);
    case_identity(&ic, "e1s10_unknown_account_refused", "wexs10absent", unknown_key, ic.generation);
    case_identity(&ic, "e1s10_wrong_incarnation_refused", name, unknown_key, ic.generation);
    case_changed(&ic, name);
    case_copied(&ic, name, FALSE);
    case_copied(&ic, name, TRUE);
    case_raw(&ic, "e1s10_raw_short_refused", name);
    case_raw(&ic, "e1s10_raw_trailing_refused", name);
    case_raw(&ic, "e1s10_raw_oversized_refused", name);
    case_raw(&ic, "e1s10_raw_field_refused", name);
    current = "teardown";

done:
    if (opened) check(!transport(pipe, E1S6_CLOSE, NULL, 0, &channel));
    marker = failures ? 1 : 0;
    if (greeted) check(WriteFile(pipe, &marker, 1, &n, NULL) && n == 1);
    if (token) CloseHandle(token);
    if (ic.caller) CloseHandle(ic.caller);
    if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
    LocalFree(sd); LocalFree(expected);
    report("e1s10=%s\n", failures ? "FAIL" : "PASS");
    fclose(out);
    return failures ? 1 : 0;
}

/* ---- the other roles ------------------------------------------------------------------------------------------- */

static int peer( const char *pipe_name )
{
    HANDLE pipe = INVALID_HANDLE_VALUE;
    BYTE marker = 0x5a;
    DWORD n, start = GetTickCount(), error = 0;
    /* the broker is a service its user's server starts: its pipe may not exist yet (WaitNamedPipe does not wait
     * for a pipe that does not exist) */
    while (GetTickCount() - start < 60000)
    {
        pipe = CreateFileA(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (pipe != INVALID_HANDLE_VALUE) break;
        error = GetLastError();
        if (error == ERROR_PIPE_BUSY) WaitNamedPipeA(pipe_name, 1000);
        else Sleep(200);
    }
    if (pipe == INVALID_HANDLE_VALUE) { printf("peer: no pipe %s error=%lu\n", pipe_name, (unsigned long)error); return 2; }
    if (!WriteFile(pipe, &marker, 1, &n, NULL) || !ReadFile(pipe, &marker, 1, &n, NULL) || n != 1)
    {
        printf("peer: exchange %lu\n", (unsigned long)GetLastError());
        CloseHandle(pipe);
        return 2;
    }
    CloseHandle(pipe);
    printf("peer result=%u\n", (unsigned int)marker);
    return marker ? 1 : 0;
}

/* not the endorsed broker: NEGOTIATE, ISSUE on a made-up context and TEST_INSTALL are all refused */
static int denied( void )
{
    HANDLE context = NULL, token = NULL;
    ULONGLONG caps = WEX_TOKEN_CAPS;
    BYTE in[WEX_TOKEN_FIXTURE_SIZE] = {0}, reply[WEX_TOKEN_RESPONSE_SIZE];
    ULONG size = 0;
    NTSTATUS status;

    current = "denied";
    status = create_token(WEX_TOKEN_NEGOTIATE, &context, NULL, &caps, NULL, 0, &token, NULL, &size);
    check(status == DENIED && !context && !token && !caps && !size);
    if (context) CloseHandle(context);
    context = (HANDLE)(ULONG_PTR)0x1234; caps = WEX_TOKEN_CAPS; size = sizeof(reply);
    status = create_token(WEX_TOKEN_ISSUE, &context, NULL, &caps, in, WEX_TOKEN_INPUT_SIZE, &token, reply, &size);
    check(status == DENIED && !token);
    failure_record(reply, size, status);
    size = 0; token = NULL;
    status = create_token(WEX_TOKEN_TEST_INSTALL, &context, GetCurrentProcess(), &caps, in, sizeof(in), &token, NULL, &size);
    check(status == DENIED && !token && !size);
    printf("denied=%s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}

static int copied_child( const char *mapping_text )
{
    HANDLE mapping = (HANDLE)(ULONG_PTR)_strtoui64(mapping_text, NULL, 10);
    struct copied *copy = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(*copy));
    if (!copy) return 2;
    current = "copied";
    issue_denied(copy->context, copy->input);
    UnmapViewOfFile(copy);
    return failures ? 1 : 0;
}

/* the broker runs as the wexelevate service: it tells the service manager it runs, and that it stopped */
static char **broker_argv;
static SERVICE_STATUS_HANDLE service_handle;

static void service_state( DWORD state, DWORD code )
{
    SERVICE_STATUS status = {SERVICE_WIN32_OWN_PROCESS, state, 0, code, 0, 0, 0};
    if (service_handle) SetServiceStatus(service_handle, &status);
}

static DWORD WINAPI service_control( DWORD control, DWORD type, void *data, void *context )
{
    (void)type; (void)data; (void)context;
    return control == SERVICE_CONTROL_INTERROGATE ? NO_ERROR : ERROR_CALL_NOT_IMPLEMENTED;
}

static void WINAPI service_main( DWORD argc, char **argv )
{
    int rc;
    (void)argc; (void)argv;
    service_handle = RegisterServiceCtrlHandlerExA("wexelevate", service_control, NULL);
    service_state(SERVICE_RUNNING, NO_ERROR);
    rc = broker(broker_argv);
    service_state(SERVICE_STOPPED, rc ? ERROR_SERVICE_SPECIFIC_ERROR : NO_ERROR);
}

int main( int argc, char **argv )
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    create_export = (create_fn)(void *)GetProcAddress(ntdll, "NtCreateWexAccountToken");
    server_call = (server_call_fn)(void *)GetProcAddress(ntdll, "wine_server_call");
    if (!create_export || !server_call) { printf("missing ntdll exports\n"); return 2; }
    if (argc == 10 && !strcmp(argv[1], "broker"))
    {
        SERVICE_TABLE_ENTRYA table[] = {{(char *)"wexelevate", service_main}, {NULL, NULL}};
        broker_argv = argv;
        if (StartServiceCtrlDispatcherA(table)) return 0;
        return broker(argv); /* not started by the service manager */
    }
    if (argc == 3 && !strcmp(argv[1], "peer")) return peer(argv[2]);
    if (argc == 2 && !strcmp(argv[1], "denied")) return denied();
    if (argc == 3 && !strcmp(argv[1], "copied")) return copied_child(argv[2]);
    printf("usage: broker RESULT PIPE NAME KEY GENERATION SID TOMBNAME TOMBKEY | peer PIPE | denied\n");
    return 2;
}
