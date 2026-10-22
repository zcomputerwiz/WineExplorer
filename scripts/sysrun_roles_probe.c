/*
 * Windows-side probe for scripts/test_sysrun_server.sh: the system role (docs/ELEVATION_SYSTEM_ROLE_WORKORDER.md, C3).
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Usage: probe.exe <mode> [arguments]
 *   token                      print the facts of the process token (user=, integrity=, admins=, priv=...)
 *   svc <name> <out> <root>    run as a service: write the token facts and whether the machine root can be
 *                              written to <out>, then wait for the stop control
 *   service-tokens             create the three service tokens (needs the system role) and print their facts
 *   proc-denied <image>        the limited or standard user cannot open <image> with any right that matters
 *   proc-open <image>          the process can open <image> for everything
 *   proc-token <image>         print the token facts of every process with this image
 *   admin-open <image>         turn the process token into an administrator token, enable SeDebugPrivilege and open <image>
 *   scm-denied <service>       cannot create or reconfigure a service, cannot set its security
 *   start-service <name> <err> StartService and expect the error <err> (0 for success)
 *   svc-start <name> <allowed|denied>   OpenService(SERVICE_START) follows the descriptor of the service
 *   svc-sd-set <name>          give Authenticated Users SERVICE_START through SetServiceObjectSecurity
 *   scm-allowed                can connect to the SCM and enumerate (positive control)
 *   token-denied               cannot create a token or a service token
 *   grants <expect>            write Control\ServiceCurrent and ComputerName\ActiveComputerName; expect is "denied" or "allowed"
 *   hold                       sleep 120 seconds
 * Prints "ok ..." / "FAIL ..." lines and "<mode>=PASS" or "<mode>=FAIL"; exit status 0 only if everything passed.
 */
#include <windows.h>
#include <winternl.h>
#include <tlhelp32.h>
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int failures;

static void check(int ok, const char *what)
{
    printf(ok ? "ok %s\n" : "FAIL %s (error %lu)\n", what, GetLastError());
    if (!ok) failures++;
}

/* ---- token facts ---------------------------------------------------------------------------------------- */

static void token_facts(FILE *out, HANDLE token)
{
    BYTE buf[4096];
    DWORD len, i, value;
    char *str;

    if (GetTokenInformation(token, TokenUser, buf, sizeof(buf), &len) &&
        ConvertSidToStringSidA(((TOKEN_USER *)buf)->User.Sid, &str))
    {
        fprintf(out, "user=%s\n", str);
        LocalFree(str);
    }
    if (GetTokenInformation(token, TokenOwner, buf, sizeof(buf), &len) &&
        ConvertSidToStringSidA(((TOKEN_OWNER *)buf)->Owner, &str))
    {
        fprintf(out, "owner=%s\n", str);
        LocalFree(str);
    }
    if (GetTokenInformation(token, TokenType, &value, sizeof(value), &len)) fprintf(out, "type=%lu\n", value);
    if (GetTokenInformation(token, TokenElevationType, &value, sizeof(value), &len)) fprintf(out, "elevation=%lu\n", value);
    if (GetTokenInformation(token, TokenIntegrityLevel, buf, sizeof(buf), &len))
    {
        SID *sid = ((TOKEN_MANDATORY_LABEL *)buf)->Label.Sid;
        fprintf(out, "integrity=%lu\n", sid->SubAuthority[sid->SubAuthorityCount - 1]);
    }
    if (GetTokenInformation(token, TokenGroups, buf, sizeof(buf), &len))
    {
        TOKEN_GROUPS *groups = (TOKEN_GROUPS *)buf;
        const char *admins = "absent";

        for (i = 0; i < groups->GroupCount; i++)
        {
            const SID_AND_ATTRIBUTES *g = &groups->Groups[i];

            if (!ConvertSidToStringSidA(g->Sid, &str)) continue;
            fprintf(out, "group=%s:%s\n", str, (g->Attributes & SE_GROUP_USE_FOR_DENY_ONLY) ? "denyonly" :
                    (g->Attributes & SE_GROUP_ENABLED) ? "enabled" : "disabled");
            if (!strcmp(str, "S-1-5-32-544"))
                admins = (g->Attributes & SE_GROUP_USE_FOR_DENY_ONLY) ? "denyonly" :
                         (g->Attributes & SE_GROUP_ENABLED) ? "enabled" : "disabled";
            LocalFree(str);
        }
        fprintf(out, "admins=%s\n", admins);
    }
    if (GetTokenInformation(token, TokenPrivileges, buf, sizeof(buf), &len))
    {
        TOKEN_PRIVILEGES *privs = (TOKEN_PRIVILEGES *)buf;

        for (i = 0; i < privs->PrivilegeCount; i++)
        {
            char name[64];
            DWORD n = sizeof(name);

            if (LookupPrivilegeNameA(NULL, &privs->Privileges[i].Luid, name, &n))
                fprintf(out, "priv=%s:%c\n", name, (privs->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED) ? 'E' : 'D');
        }
    }
    {
        HANDLE linked;

        SetLastError(0);
        if (GetTokenInformation(token, TokenLinkedToken, &linked, sizeof(linked), &len))
        {
            fprintf(out, "linked=present\n");
            CloseHandle(linked);
        }
        else fprintf(out, "linked=none\n");
    }
}

static void token_mode(void)
{
    HANDLE token;

    check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token), "open own token");
    token_facts(stdout, token);
    CloseHandle(token);
}

/* ---- service mode --------------------------------------------------------------------------------------- */

static char svc_name[256], svc_out[MAX_PATH], svc_root[MAX_PATH];
static SERVICE_STATUS_HANDLE status_handle;
static HANDLE stop_event;
static SERVICE_STATUS status = { SERVICE_WIN32_OWN_PROCESS, SERVICE_START_PENDING, 0, 0, 0, 0, 3000 };

static DWORD WINAPI service_handler(DWORD control, DWORD type, void *data, void *context)
{
    if (control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN)
    {
        status.dwCurrentState = SERVICE_STOP_PENDING;
        SetServiceStatus(status_handle, &status);
        SetEvent(stop_event);
    }
    return NO_ERROR;
}

static void WINAPI service_main(DWORD argc, char **argv)
{
    char file[MAX_PATH];
    HANDLE token, h;
    FILE *out;

    stop_event = CreateEventA(NULL, TRUE, FALSE, NULL);
    status_handle = RegisterServiceCtrlHandlerExA(svc_name, service_handler, NULL);
    status.dwCurrentState = SERVICE_RUNNING;
    status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    SetServiceStatus(status_handle, &status);

    /* write to a temporary name first: the test script waits for the final one */
    snprintf(file, sizeof(file), "%s.tmp", svc_out);
    if ((out = fopen(file, "wb")))
    {
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        {
            token_facts(out, token);
            CloseHandle(token);
        }
        snprintf(file, sizeof(file), "%s\\drive_c\\windows\\wex_service_%s.txt", svc_root, svc_name);
        h = CreateFileA(file, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
        fprintf(out, "machine_root_write=%s\n", h == INVALID_HANDLE_VALUE ? "denied" : "allowed");
        if (h != INVALID_HANDLE_VALUE)
        {
            CloseHandle(h);
            DeleteFileA(file);
        }
        fclose(out);
        snprintf(file, sizeof(file), "%s.tmp", svc_out);
        MoveFileExA(file, svc_out, MOVEFILE_REPLACE_EXISTING);
    }
    WaitForSingleObject(stop_event, INFINITE);
    status.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(status_handle, &status);
}

static void service_mode(const char *name, const char *out, const char *root)
{
    SERVICE_TABLE_ENTRYA table[] = { { svc_name, service_main }, { NULL, NULL } };

    snprintf(svc_name, sizeof(svc_name), "%s", name);
    snprintf(svc_out, sizeof(svc_out), "%s", out);
    snprintf(svc_root, sizeof(svc_root), "%s", root);
    StartServiceCtrlDispatcherA(table);
}

/* ---- service tokens ------------------------------------------------------------------------------------- */

#ifdef _WIN64
static NTSTATUS create_service_token(ULONG account, HANDLE *token)
{
    static NTSTATUS (WINAPI *set_info)(HANDLE, ULONG, void *, ULONG);
    HANDLE h = (HANDLE)(ULONG_PTR)account;
    NTSTATUS status;

    if (!set_info) set_info = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtSetInformationProcess");
    status = set_info(GetCurrentProcess(), 1003 /* ProcessWineCreateServiceToken */, &h, sizeof(h));
    if (!status) *token = h;
    return status;
}
#endif

static void service_tokens(void)
{
#ifndef _WIN64
    /* ProcessWineCreateServiceToken is not passed through WoW64: only the native services.exe uses it */
    printf("ok service tokens are made by the native services.exe only (32-bit probe skips)\n");
    return;
#else
    static const char *names[] = { "system", "localservice", "networkservice" };
    HANDLE token;
    ULONG i;
    char what[64];

    for (i = 0; i < 3; i++)
    {
        NTSTATUS status = create_service_token(i, &token);

        snprintf(what, sizeof(what), "create the %s token (0x%lx)", names[i], (unsigned long)status);
        check(!status, what);
        if (status) continue;
        printf("--- %s\n", names[i]);
        token_facts(stdout, token);
        CloseHandle(token);
    }
    check(create_service_token(3, &token) == 0xC000000D /* STATUS_INVALID_PARAMETER */, "account 3 is refused");
#endif
}

/* ---- processes ------------------------------------------------------------------------------------------ */

/* every process with this image name (the probe's own process excluded) */
static int find_pids(const char *image, DWORD *pids, int max)
{
    PROCESSENTRY32 pe = { sizeof(pe) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    int count = 0;

    if (snap == INVALID_HANDLE_VALUE) return 0;
    if (Process32First(snap, &pe))
        do
            if (!lstrcmpiA(pe.szExeFile, image) && pe.th32ProcessID != GetCurrentProcessId() && count < max)
                pids[count++] = pe.th32ProcessID;
        while (Process32Next(snap, &pe));
    CloseHandle(snap);
    return count;
}

static DWORD find_tid(DWORD pid)
{
    THREADENTRY32 te = { sizeof(te) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    DWORD tid = 0;

    if (snap == INVALID_HANDLE_VALUE) return 0;
    if (Thread32First(snap, &te))
        do
            if (te.th32OwnerProcessID == pid) { tid = te.th32ThreadID; break; }
        while (Thread32Next(snap, &te));
    CloseHandle(snap);
    return tid;
}

static void proc_denied_pid(const char *image, DWORD pid)
{
    static const struct { DWORD access; const char *name; } rights[] =
    {
        { PROCESS_VM_WRITE, "PROCESS_VM_WRITE" }, { PROCESS_CREATE_THREAD, "PROCESS_CREATE_THREAD" },
        { PROCESS_DUP_HANDLE, "PROCESS_DUP_HANDLE" }, { PROCESS_SET_INFORMATION, "PROCESS_SET_INFORMATION" },
        { PROCESS_VM_OPERATION, "PROCESS_VM_OPERATION" }, { PROCESS_VM_READ, "PROCESS_VM_READ" },
        { PROCESS_TERMINATE, "PROCESS_TERMINATE" }, { PROCESS_SUSPEND_RESUME, "PROCESS_SUSPEND_RESUME" },
        { PROCESS_CREATE_PROCESS, "PROCESS_CREATE_PROCESS" }, { PROCESS_QUERY_INFORMATION, "PROCESS_QUERY_INFORMATION" },
        { PROCESS_VM_WRITE | PROCESS_CREATE_THREAD | PROCESS_DUP_HANDLE, "write, thread, dup (together)" },
        { PROCESS_ALL_ACCESS, "PROCESS_ALL_ACCESS" },
    };
    static const struct { DWORD access; const char *name; } thread_rights[] =
    {
        { THREAD_SET_CONTEXT, "THREAD_SET_CONTEXT" }, { THREAD_GET_CONTEXT, "THREAD_GET_CONTEXT" },
        { THREAD_SUSPEND_RESUME, "THREAD_SUSPEND_RESUME" }, { THREAD_SET_INFORMATION, "THREAD_SET_INFORMATION" },
        { THREAD_TERMINATE, "THREAD_TERMINATE" }, { THREAD_ALL_ACCESS, "THREAD_ALL_ACCESS" },
    };
    NTSTATUS (WINAPI *get_next)(HANDLE, ACCESS_MASK, ULONG, ULONG, HANDLE *);
    DWORD tid, err;
    HANDLE h, next, last;
    char what[128];
    unsigned int i;
    int found;

    for (i = 0; i < sizeof(rights) / sizeof(rights[0]); i++)
    {
        SetLastError(0);
        h = OpenProcess(rights[i].access, FALSE, pid);
        err = GetLastError();
        snprintf(what, sizeof(what), "OpenProcess(%s) of %s %lu is denied (handle %p, error %lu)", rights[i].name, image, pid, h, err);
        check(!h && err == ERROR_ACCESS_DENIED, what);
        if (h) CloseHandle(h);
    }
    tid = find_tid(pid);
    snprintf(what, sizeof(what), "%s %lu has a thread in the thread list (tid %lu)", image, pid, tid);
    check(tid != 0, what);
    for (i = 0; tid && i < sizeof(thread_rights) / sizeof(thread_rights[0]); i++)
    {
        SetLastError(0);
        h = OpenThread(thread_rights[i].access, FALSE, tid);
        err = GetLastError();
        snprintf(what, sizeof(what), "OpenThread(%s) of %s %lu is denied (handle %p, error %lu)", thread_rights[i].name, image, pid, h, err);
        check(!h && err == ERROR_ACCESS_DENIED, what);
        if (h) CloseHandle(h);
    }
    /* QueueUserAPC, CreateRemoteThread, WriteProcessMemory and DuplicateHandle all need one of the handles above */

    /* the other ways to get a handle: enumerate the processes with the rights that matter */
    get_next = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtGetNextProcess");
    found = 0;
    last = NULL;
    while (get_next && !get_next(last, PROCESS_VM_WRITE | PROCESS_CREATE_THREAD | PROCESS_DUP_HANDLE, 0, 0, &next))
    {
        if (last) CloseHandle(last);
        last = next;
        if (GetProcessId(next) == pid) found = 1;
    }
    if (last) CloseHandle(last);
    check(get_next && !found, "NtGetNextProcess does not hand out a handle with write, thread and dup rights");

    /* attach to its console */
    FreeConsole();
    SetLastError(0);
    check(!AttachConsole(pid), "AttachConsole to the process fails");
}

static void proc_denied(const char *image)
{
    DWORD pids[16];
    int i, n = find_pids(image, pids, 16);
    char what[128];

    snprintf(what, sizeof(what), "%s is in the process list (%d found)", image, n);
    check(n > 0, what);
    for (i = 0; i < n; i++) proc_denied_pid(image, pids[i]);
}

static void proc_open_pid(const char *image, DWORD pid)
{
    DWORD tid;
    HANDLE h;
    char what[128];

    h = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    snprintf(what, sizeof(what), "OpenProcess(PROCESS_ALL_ACCESS) of %s %lu works", image, pid);
    check(h != NULL, what);
    if (h)
    {
        void *mem = VirtualAllocEx(h, NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

        check(mem != NULL, "VirtualAllocEx in it works");
        if (mem) VirtualFreeEx(h, mem, 0, MEM_RELEASE);
        CloseHandle(h);
    }
    if ((tid = find_tid(pid)))
    {
        h = OpenThread(THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, tid);
        check(h != NULL, "OpenThread(THREAD_SET_CONTEXT) works");
        if (h) CloseHandle(h);
    }
}

static void proc_open(const char *image)
{
    DWORD pids[16];
    int i, n = find_pids(image, pids, 16);
    char what[128];

    snprintf(what, sizeof(what), "%s is in the process list (%d found)", image, n);
    check(n > 0, what);
    for (i = 0; i < n; i++) proc_open_pid(image, pids[i]);
}

/* print the token facts of every process with this image name, one "--- pid" block each */
static void proc_token(const char *image)
{
    DWORD pids[16];
    int i, n = find_pids(image, pids, 16);
    char what[128];

    snprintf(what, sizeof(what), "%s is in the process list (%d found)", image, n);
    check(n > 0, what);
    for (i = 0; i < n; i++)
    {
        HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pids[i]), token;

        snprintf(what, sizeof(what), "open %s %lu", image, pids[i]);
        check(process != NULL, what);
        if (!process) continue;
        printf("--- %s %lu\n", image, pids[i]);
        if (OpenProcessToken(process, TOKEN_QUERY, &token))
        {
            token_facts(stdout, token);
            CloseHandle(token);
        }
        else check(0, "open its token");
        CloseHandle(process);
    }
}

static void admin_open(const char *image)
{
    NTSTATUS (WINAPI *set_info)(HANDLE, ULONG, void *, ULONG) =
        (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtSetInformationProcess");
    TOKEN_PRIVILEGES tp;
    HANDLE token;
    NTSTATUS status = set_info(GetCurrentProcess(), 1002 /* ProcessWineGrantAdminToken */, NULL, 0);

    check(!status, "the system role process turns its token into an administrator token");
    check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &token), "open the new token");
    token_facts(stdout, token);
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    LookupPrivilegeValueA(NULL, "SeDebugPrivilege", &tp.Privileges[0].Luid);
    check(AdjustTokenPrivileges(token, FALSE, &tp, 0, NULL, NULL) && GetLastError() == 0, "SeDebugPrivilege is enabled");
    CloseHandle(token);
    proc_open(image);
}

/* ---- SCM and tokens -------------------------------------------------------------------------------------- */

static void scm_denied(const char *service)
{
    SC_HANDLE scm, svc;
    DWORD err;
    char what[128];

    scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE);
    check(scm != NULL, "OpenSCManager(connect, enumerate) works");
    if (scm) CloseServiceHandle(scm);

    SetLastError(0);
    scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    err = GetLastError();
    snprintf(what, sizeof(what), "OpenSCManager(SC_MANAGER_CREATE_SERVICE) is denied (error %lu)", err);
    check(!scm && err == ERROR_ACCESS_DENIED, what);
    if (scm) CloseServiceHandle(scm);

    scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) return;
    SetLastError(0);
    svc = CreateServiceA(scm, "WexProbeNew", NULL, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START,
                         SERVICE_ERROR_NORMAL, "C:\\windows\\system32\\cmd.exe", NULL, NULL, NULL, NULL, NULL);
    err = GetLastError();
    snprintf(what, sizeof(what), "CreateService is denied (error %lu)", err);
    check(!svc && err == ERROR_ACCESS_DENIED, what);
    if (svc) CloseServiceHandle(svc);

    {
        static const struct { DWORD access; const char *name; } rights[] =
        {
            { SERVICE_CHANGE_CONFIG, "SERVICE_CHANGE_CONFIG" }, { SERVICE_START, "SERVICE_START" },
            { SERVICE_STOP, "SERVICE_STOP" }, { WRITE_DAC, "WRITE_DAC" }, { WRITE_OWNER, "WRITE_OWNER" },
            { DELETE, "DELETE" },
        };
        unsigned int i;

        for (i = 0; i < sizeof(rights) / sizeof(rights[0]); i++)
        {
            SetLastError(0);
            svc = OpenServiceA(scm, service, rights[i].access);
            err = GetLastError();
            snprintf(what, sizeof(what), "OpenService(%s) of %s is denied (error %lu)", rights[i].name, service, err);
            check(!svc && err == ERROR_ACCESS_DENIED, what);
            if (svc) CloseServiceHandle(svc);
        }
    }
    svc = OpenServiceA(scm, service, SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG | READ_CONTROL);
    snprintf(what, sizeof(what), "OpenService(query) of %s works (error %lu)", service, GetLastError());
    check(svc != NULL, what);
    if (svc) CloseServiceHandle(svc);
    CloseServiceHandle(scm);
}

/* start a service and expect an error code (0 for success) */
static void start_service(const char *name, DWORD expect)
{
    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT), svc;
    DWORD err = 0;
    char what[128];

    check(scm != NULL, "OpenSCManager works");
    if (!scm) return;
    svc = OpenServiceA(scm, name, SERVICE_START | SERVICE_QUERY_STATUS);
    check(svc != NULL, "OpenService(SERVICE_START) works");
    if (svc)
    {
        if (!StartServiceA(svc, 0, NULL)) err = GetLastError();
        snprintf(what, sizeof(what), "StartService(%s) gives error %lu, expected %lu", name, err, expect);
        check(err == expect, what);
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
}

/* OpenService(SERVICE_START) is allowed or denied by the descriptor of the service */
static void svc_start(const char *name, const char *expect)
{
    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT), svc;
    int want_allowed = !strcmp(expect, "allowed");
    DWORD err;
    char what[160];

    check(scm != NULL, "OpenSCManager works");
    if (!scm) return;
    SetLastError(0);
    svc = OpenServiceA(scm, name, SERVICE_START);
    err = GetLastError();
    snprintf(what, sizeof(what), "OpenService(SERVICE_START) of %s is %s (error %lu)", name, expect, err);
    check(want_allowed ? svc != NULL : (!svc && err == ERROR_ACCESS_DENIED), what);
    if (svc) CloseServiceHandle(svc);
    CloseServiceHandle(scm);
}

static int service_dacl(SC_HANDLE svc, char **sddl)
{
    BYTE buf[1024];
    DWORD needed;

    return QueryServiceObjectSecurity(svc, DACL_SECURITY_INFORMATION, (PSECURITY_DESCRIPTOR)buf, sizeof(buf), &needed) &&
           ConvertSecurityDescriptorToStringSecurityDescriptorA((PSECURITY_DESCRIPTOR)buf, SDDL_REVISION_1,
                                                                DACL_SECURITY_INFORMATION, sddl, NULL);
}

/* read the descriptor of a service, give Authenticated Users SERVICE_START, read it back */
static void svc_sd_set(const char *name)
{
    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT), svc;
    PSECURITY_DESCRIPTOR sd;
    char *before, *after;

    check(scm != NULL, "OpenSCManager works");
    if (!scm) return;
    svc = OpenServiceA(scm, name, WRITE_DAC | READ_CONTROL);
    check(svc != NULL, "OpenService(WRITE_DAC | READ_CONTROL) works for the system role");
    if (svc)
    {
        check(service_dacl(svc, &before), "QueryServiceObjectSecurity returns the DACL");
        printf("before=%s\n", before);
        check(strstr(before, ";;;AU)") != NULL, "the default descriptor has an entry for Authenticated Users");
        check(strstr(before, "(A;;RP;;;AU)") == NULL, "Authenticated Users cannot start the service by default");
        check(ConvertStringSecurityDescriptorToSecurityDescriptorA("D:(A;;0x2018d;;;AU)(A;;0x10;;;AU)(A;;0x201fd;;;SY)(A;;0xf01ff;;;BA)",
                                                                   SDDL_REVISION_1, &sd, NULL), "build the new DACL");
        check(SetServiceObjectSecurity(svc, DACL_SECURITY_INFORMATION, sd), "SetServiceObjectSecurity(DACL) works for the system role");
        LocalFree(sd);
        check(service_dacl(svc, &after), "QueryServiceObjectSecurity returns the DACL again");
        printf("after=%s\n", after);
        check(strstr(after, "(A;;RP;;;AU)") != NULL, "the new DACL is what the service has now");
        CloseServiceHandle(svc);
    }
    CloseServiceHandle(scm);
}

static void scm_allowed(void)
{
    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_ENUMERATE_SERVICE);

    check(scm != NULL, "OpenSCManager(connect, enumerate) works");
    if (scm) CloseServiceHandle(scm);
}

static void token_denied(void)
{
    NTSTATUS (WINAPI *create)(HANDLE *, ACCESS_MASK, OBJECT_ATTRIBUTES *, TOKEN_TYPE, LUID *, LARGE_INTEGER *,
                              TOKEN_USER *, TOKEN_GROUPS *, TOKEN_PRIVILEGES *, TOKEN_OWNER *, TOKEN_PRIMARY_GROUP *,
                              TOKEN_DEFAULT_DACL *, TOKEN_SOURCE *) =
        (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtCreateToken");
    SID_IDENTIFIER_AUTHORITY nt = { SECURITY_NT_AUTHORITY };
    OBJECT_ATTRIBUTES attr = { sizeof(attr) };
    TOKEN_USER user;
    TOKEN_GROUPS groups = { 1 };
    TOKEN_PRIVILEGES privs = { 0 };
    TOKEN_OWNER owner;
    TOKEN_PRIMARY_GROUP pgroup;
    TOKEN_DEFAULT_DACL dacl = { 0 };
    TOKEN_SOURCE source = { "wexprobe", { 1, 0 } };
    LARGE_INTEGER expire = { { 0, 0 } };
    LUID luid = { 1, 0 };
    PSID sid;
    HANDLE token;
    NTSTATUS status;
    char what[128];

    AllocateAndInitializeSid(&nt, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &sid);
    user.User.Sid = sid;
    user.User.Attributes = 0;
    groups.Groups[0].Sid = sid;
    groups.Groups[0].Attributes = SE_GROUP_ENABLED;
    owner.Owner = sid;
    pgroup.PrimaryGroup = sid;
    status = create ? create(&token, TOKEN_ALL_ACCESS, &attr, TokenPrimary, &luid, &expire, &user, &groups, &privs,
                             &owner, &pgroup, &dacl, &source) : 1;
    snprintf(what, sizeof(what), "NtCreateToken is refused with STATUS_PRIVILEGE_NOT_HELD (0x%lx)", (unsigned long)status);
    check(status == 0xC0000061, what);
    if (!status) CloseHandle(token);
    FreeSid(sid);

#ifdef _WIN64
    status = create_service_token(0, &token);
    snprintf(what, sizeof(what), "creating a service token is refused with STATUS_PRIVILEGE_NOT_HELD (0x%lx)", (unsigned long)status);
    check(status == 0xC0000061, what);
    if (!status) CloseHandle(token);
#endif
}

static void grants(const char *expect)
{
    static const WCHAR *keys[] =
    {
        L"System\\CurrentControlSet\\Control\\ServiceCurrent",
        L"System\\CurrentControlSet\\Control\\ComputerName\\ActiveComputerName",
    };
    int want_denied = !strcmp(expect, "denied");
    unsigned int i;

    for (i = 0; i < 2; i++)
    {
        HKEY key;
        DWORD disp, one = 1;
        LONG ret = RegCreateKeyExW(HKEY_LOCAL_MACHINE, keys[i], 0, NULL, 0, KEY_SET_VALUE, NULL, &key, &disp);
        char what[200];

        if (!ret)
        {
            ret = RegSetValueExW(key, L"WexProbe", 0, REG_DWORD, (BYTE *)&one, sizeof(one));
            RegDeleteValueW(key, L"WexProbe");
            RegCloseKey(key);
        }
        snprintf(what, sizeof(what), "HKLM\\%ls write is %s (error %ld)", keys[i], expect, ret);
        check(want_denied ? ret == ERROR_ACCESS_DENIED : ret == ERROR_SUCCESS, what);
    }
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "";
    const char *arg = argc > 2 ? argv[2] : "";

    if (!strcmp(mode, "token")) token_mode();
    else if (!strcmp(mode, "svc") && argc > 4) { service_mode(argv[2], argv[3], argv[4]); return 0; }
    else if (!strcmp(mode, "service-tokens")) service_tokens();
    else if (!strcmp(mode, "proc-denied")) proc_denied(arg);
    else if (!strcmp(mode, "proc-open")) proc_open(arg);
    else if (!strcmp(mode, "proc-token")) proc_token(arg);
    else if (!strcmp(mode, "admin-open")) admin_open(arg);
    else if (!strcmp(mode, "scm-denied")) scm_denied(arg);
    else if (!strcmp(mode, "scm-allowed")) scm_allowed();
    else if (!strcmp(mode, "svc-start") && argc > 3) svc_start(argv[2], argv[3]);
    else if (!strcmp(mode, "svc-sd-set")) svc_sd_set(arg);
    else if (!strcmp(mode, "start-service") && argc > 3) start_service(argv[2], atoi(argv[3]));
    else if (!strcmp(mode, "token-denied")) token_denied();
    else if (!strcmp(mode, "grants")) grants(arg);
    else if (!strcmp(mode, "hold")) Sleep(120000);
    else { printf("usage: probe <mode> [arguments]\n"); return 2; }
    printf("%s=%s\n", mode, failures ? "FAIL" : "PASS");
    return failures != 0;
}
