/*
 * Windows-side probe for scripts/test_sysrun_server.sh: the elevation broker (docs/ELEVATION_BROKER_WORKORDER.md).
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
 *   run <image> <expect> <out> [flags [args]]
 *                      ask the broker to start <image> as "image" child <out> [args]; expect that error (0 =
 *                      success: then wait for the process, check exit code 42, the parent and the child's facts).
 *                      flags: letters, e = the request carries an environment block with WEX_TEST_VAR
 *   args <image> <out> start the child with long, Unicode and quoted arguments, compare its command line
 *   concurrent <imageA> <imageB> <outA> <outB>
 *                      two requests at once; A must run and B must be refused (ERROR_CANCELLED)
 *   forge <image> <out> a request whose advisory_pid names another process; the parent must be this process
 *   provenance <self|transceive|write-impersonate|transceive-impersonate|delegate|delegate-transceive|dacl-restore> <image> <out>
 *                      lab-only consumed-writer controls; delegated and impersonating writes must deny before consent
 *   impersonate <image> <out>
 *                      open the broker pipe while the thread impersonates its own token; the broker must deny
 *                      (ERROR_ACCESS_DENIED) and start nothing
 *   imagerace <image> <out>
 *                      ask the broker, and while the consent is pending (the harness holds it open with
 *                      wait-for-gate and approve.<name>) let the harness rename a different file over the image
 *                      (it signals imgrace.ready and waits for imgrace.done); the broker must deny
 *                      (ERROR_FILE_CORRUPT) and start nothing
 *   writerfirst <image> <out>
 *                      open the image for writing with another handle, then ask the broker; the broker must deny
 *                      (ERROR_SHARING_VIOLATION) and start nothing
 *   callerdeath <image> <out> <tag>
 *                      send a raw request, then disconnect without reading the reply; after a pause no process
 *                      with <tag> in its command line may still be running
 *   attack <image> <out> <tag>
 *                      start the child held, then try to open it and its threads; none of the dangerous rights
 *                      may be granted. Releases the child at the end.
 *   leasekill <image> <out> <tag>
 *                      start the held child, then wait: a writer that opens the image (the harness does it from
 *                      Unix) must end the child before it is released, and the child must not exit with 42
 *   spoofcmd <image> <fake> <out>
 *                      like run, but the command line names <fake> as the program (the consent window must still
 *                      show the real image)
 *   consentattack      try to open the consent process (consent_test.exe) with the rights that would let a limited
 *                      process approve or steer it; every one must be denied
 *   winwrite <path>    open <path> for writing through Win32 and append a byte (a Wine program as the writer)
 *   child <out> [hold <tag>|chain <admin image>|args...]
 *                      what the elevated process does: write its facts to <out>, create a folder in
 *                      "C:\Program Files", exit with 42. "chain": it is elevated already, so ShellExecuteEx "runas" of
 *                      itself and a plain open of <admin image> (a requireAdministrator program) run directly without a
 *                      prompt; the errors go to <out> as chain_runas= and chain_open=
 *   facts <out>        the same facts for this process (the control: a limited process)
 *   createproc <image> <expect> <out>
 *                      CreateProcess of <image> with "child <out>"; expect that error (740 for a manifest that needs
 *                      elevation the caller's token cannot give, 0 = ran and exited with 42)
 *   msi <package> <properties> <expect> [temp]
 *                      MsiInstallProduct without UI (TEMP set to <temp> first); the result must be <expect>
 *   shexec <verb|-> <image> <expect> <out> <elev|-> [extra|raw:<parameters>]
 *                      ShellExecuteEx (docs/ELEVATION_CALLERS_WORKORDER.md) of <image> with the parameters "child <out>"
 *                      and SEE_MASK_NOCLOSEPROCESS; expect that Win32 error (0 = success: wait for the handle, exit code 42,
 *                      and the child's token elevation type must be <elev> unless "-"). A failure must leave hProcess NULL
 *                      and hInstApp at an SE_ERR value (5 for cancel, 50 and 21)
 *   hold               sleep 120 seconds
 * Prints "ok ..." / "FAIL ..." lines and "<mode>=PASS" or "<mode>=FAIL"; exit status 0 only if everything passed.
 */
#include <windows.h>
#include <tlhelp32.h>
#include <sddl.h>
#include <winternl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <shellapi.h>
#include <msi.h>

#include "wexelevate.h"

#define POLICY_DIR "Z:\\tmp\\wexelev\\"

static int failures;

static void check(int ok, const char *what)
{
    printf(ok ? "ok %s\n" : "FAIL %s (error %lu)\n", what, GetLastError());
    if (!ok) failures++;
}

static void checkf(int ok, const char *what, const char *detail)
{
    printf(ok ? "ok %s\n" : "FAIL %s (%s)\n", what, detail);
    if (!ok) failures++;
}

static WCHAR *widen(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    WCHAR *w = calloc(n + 1, sizeof(WCHAR));

    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static char *narrow(const WCHAR *w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *s = calloc(n + 1, 1);

    WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    return s;
}

/* ---- the child: what an elevated process reports ---------------------------------------------------------- */

static DWORD parent_pid(void)
{
    PROCESSENTRY32W pe = { sizeof(pe) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    DWORD me = GetCurrentProcessId(), parent = 0;

    if (snap == INVALID_HANDLE_VALUE) return 0;
    if (Process32FirstW(snap, &pe))
        do { if (pe.th32ProcessID == me) parent = pe.th32ParentProcessID; } while (Process32NextW(snap, &pe));
    CloseHandle(snap);
    return parent;
}

static void facts(FILE *out)
{
    BYTE buf[4096];
    DWORD len, value, i;
    HANDLE token;
    WCHAR var[256];
    char *str, dir[MAX_PATH];

    fprintf(out, "pid=%lu\nparent=%lu\n", GetCurrentProcessId(), parent_pid());
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        if (GetTokenInformation(token, TokenUser, buf, sizeof(buf), &len) &&
            ConvertSidToStringSidA(((TOKEN_USER *)buf)->User.Sid, &str))
        {
            fprintf(out, "user=%s\n", str);
            LocalFree(str);
        }
        if (GetTokenInformation(token, TokenElevationType, &value, sizeof(value), &len)) fprintf(out, "elevation=%lu\n", value);
        if (GetTokenInformation(token, TokenType, &value, sizeof(value), &len)) fprintf(out, "type=%lu\n", value);
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
                if (!ConvertSidToStringSidA(groups->Groups[i].Sid, &str)) continue;
                if (!strcmp(str, "S-1-5-32-544"))
                    admins = (groups->Groups[i].Attributes & SE_GROUP_USE_FOR_DENY_ONLY) ? "denyonly" :
                             (groups->Groups[i].Attributes & SE_GROUP_ENABLED) ? "enabled" : "disabled";
                LocalFree(str);
            }
            fprintf(out, "admins=%s\n", admins);
        }
        CloseHandle(token);
    }
    sprintf(dir, "C:\\Program Files\\WexElevTest-%lu", GetCurrentProcessId());
    if (CreateDirectoryA(dir, NULL))
    {
        fprintf(out, "program_files=writable\n");
        RemoveDirectoryA(dir);
    }
    else fprintf(out, "program_files=denied:%lu\n", GetLastError());
    if (GetEnvironmentVariableW(L"WEX_TEST_VAR", var, 256))
    {
        char *n = narrow(var);
        fprintf(out, "var=%s\n", n);
        free(n);
    }
    else fprintf(out, "var=absent\n");
    fflush(out);
}

static void write_whole(const char *path, const void *data, DWORD size)
{
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD done;

    if (f == INVALID_HANDLE_VALUE) return;
    WriteFile(f, data, size, &done, NULL);
    CloseHandle(f);
}

static int child_mode(int argc, char **argv)
{
    char path[MAX_PATH + 16];
    const WCHAR *cmd = GetCommandLineW();
    FILE *out;
    int i;

    if (argc < 3) return 1;
    sprintf(path, "%s.cmd", argv[2]);
    write_whole(path, cmd, lstrlenW(cmd) * sizeof(WCHAR));    /* the command line, byte for byte */
    if (!(out = fopen(argv[2], "w"))) return 1;
    facts(out);
    if (argc > 3 && !strcmp(argv[3], "again"))
    {
        /* an elevated process asks for elevation: nothing to elevate to */
        char self[MAX_PATH];
        WCHAR wself[MAX_PATH];
        DWORD err;

        GetModuleFileNameA(NULL, self, sizeof(self));
        MultiByteToWideChar(CP_UTF8, 0, self, -1, wself, MAX_PATH);
        err = wexelevate_run(wself, L"x", NULL, NULL, 0, SW_SHOWNORMAL, 0, NULL, NULL);
        fprintf(out, "again=%lu\n", err);
    }
    if (argc > 4 && !strcmp(argv[3], "chain"))
    {
        WCHAR self[MAX_PATH], params[2 * MAX_PATH], *admin = widen(argv[4]), *wout = widen(argv[2]);
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        DWORD code;
        int pass;

        GetModuleFileNameW(NULL, self, MAX_PATH);
        for (pass = 0; pass < 2; pass++)
        {
            swprintf(params, sizeof(params) / sizeof(WCHAR), L"child %ls.chain%d", wout, pass);
            memset(&sei, 0, sizeof(sei));
            sei.cbSize = sizeof(sei);
            sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
            sei.lpVerb = pass ? NULL : L"runas";
            sei.lpFile = pass ? admin : self;
            sei.lpParameters = params;
            sei.nShow = SW_SHOWNORMAL;
            code = 0;
            if (ShellExecuteExW(&sei))
            {
                WaitForSingleObject(sei.hProcess, 60000);
                GetExitCodeProcess(sei.hProcess, &code);
                CloseHandle(sei.hProcess);
                fprintf(out, "chain_%s=0 exit=%lu\n", pass ? "open" : "runas", code);
            }
            else fprintf(out, "chain_%s=%lu\n", pass ? "open" : "runas", GetLastError());
        }
    }
    if (argc > 3 && !strcmp(argv[3], "spawn"))
    {
        /* a child of an elevated process is elevated too, and keeps the rights of the machine root */
        char self[MAX_PATH], cmd[2 * MAX_PATH + 32];
        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi;

        GetModuleFileNameA(NULL, self, sizeof(self));
        sprintf(cmd, "\"%s\" facts %s.sub", self, argv[2]);
        if (CreateProcessA(self, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
        {
            WaitForSingleObject(pi.hProcess, 30000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        else fprintf(out, "spawn=failed:%lu\n", GetLastError());
    }
    fprintf(out, "ready=1\n");
    fclose(out);
    if (argc > 4 && !strcmp(argv[3], "hold"))
    {
        sprintf(path, POLICY_DIR "release.%s", argv[4]);
        for (i = 0; i < 600 && GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES; i++) Sleep(100);
    }
    return 42;
}

/* ---- the caller ------------------------------------------------------------------------------------------- */

static char *read_text(const char *path)
{
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    DWORD size, done;
    char *buf;

    if (f == INVALID_HANDLE_VALUE) return NULL;
    size = GetFileSize(f, NULL);
    buf = calloc(size + 1, 1);
    ReadFile(f, buf, size, &done, NULL);
    CloseHandle(f);
    return buf;
}

static char *fact(const char *text, const char *key)
{
    static char value[256];
    char pattern[64];
    const char *p;
    size_t n;

    sprintf(pattern, "\n%s=", key);
    if (!text) return NULL;
    if (!strncmp(text, pattern + 1, strlen(pattern) - 1)) p = text + strlen(pattern) - 1;
    else if ((p = strstr(text, pattern))) p += strlen(pattern);
    else return NULL;
    n = strcspn(p, "\r\n");
    if (n >= sizeof(value)) n = sizeof(value) - 1;
    memcpy(value, p, n);
    value[n] = 0;
    return value;
}

static int fact_is(const char *text, const char *key, const char *want)
{
    char *got = fact(text, key);
    return got && !strcmp(got, want);
}

/* WCHAR environment block with WEX_TEST_VAR */
static const WCHAR test_env[] = L"WEX_TEST_VAR=h\u00e9llo \u4e2d\u6587\0PATH=C:\\windows\\system32\0";

/* start <image> through the broker; *process gets the handle */
static DWORD ask(const char *image, const char *out, const char *flags, const char *args, HANDLE *process)
{
    WCHAR *wimage = widen(image), *cmd;
    char *narrow_cmd = malloc(2 * strlen(image) + 2 * strlen(out) + (args ? strlen(args) : 0) + 64);
    BOOL inherit = flags && strchr(flags, 'e');
    DWORD err;

    sprintf(narrow_cmd, "\"%s\" child %s %s", image, out, args ? args : "");
    cmd = widen(narrow_cmd);
    err = wexelevate_run(wimage, cmd, L"C:\\windows", inherit ? test_env : NULL, inherit ? sizeof(test_env) / sizeof(WCHAR) : 0,
                         SW_SHOWNORMAL, WEXELEVATE_WANT_HANDLE, process, NULL);
    free(wimage); free(narrow_cmd); free(cmd);
    return err;
}

static int wait_exit(HANDLE process, DWORD want)
{
    DWORD code = 0;

    if (WaitForSingleObject(process, 60000) != WAIT_OBJECT_0) return 0;
    return GetExitCodeProcess(process, &code) && code == want;
}

static void report_child(const char *out, int expect_var)
{
    char *text = read_text(out);
    char buf[64];

    check(text != NULL, "the elevated process wrote its facts");
    if (!text) return;
    sprintf(buf, "%lu", GetCurrentProcessId());
    checkf(fact_is(text, "parent", buf), "the parent of the elevated process is the caller", fact(text, "parent") ? fact(text, "parent") : "none");
    checkf(fact_is(text, "elevation", "2"), "token elevation type is Full", fact(text, "elevation") ? fact(text, "elevation") : "none");
    checkf(fact_is(text, "integrity", "12288"), "integrity is High", fact(text, "integrity") ? fact(text, "integrity") : "none");
    checkf(fact_is(text, "admins", "enabled"), "Administrators is enabled", fact(text, "admins") ? fact(text, "admins") : "none");
    checkf(fact_is(text, "type", "1"), "the token is a primary token", fact(text, "type") ? fact(text, "type") : "none");
    checkf(fact_is(text, "program_files", "writable"), "the elevated process can create a folder in Program Files",
           fact(text, "program_files") ? fact(text, "program_files") : "none");
    if (expect_var) checkf(fact_is(text, "var", "h\xc3\xa9llo \xe4\xb8\xad\xe6\x96\x87"), "the environment block of the request arrived",
                           fact(text, "var") ? fact(text, "var") : "none");
    else checkf(fact_is(text, "var", "absent"), "the default environment of the user has no WEX_TEST_VAR", fact(text, "var") ? fact(text, "var") : "none");
    free(text);
}

static void run_mode(const char *image, DWORD expect, const char *out, const char *flags, const char *args)
{
    HANDLE process = NULL;
    DWORD err = ask(image, out, flags, args, &process);
    char what[64];

    SetLastError(err);
    sprintf(what, "the broker answers with error %lu (got %lu)", expect, err);
    check(err == expect, what);
    if (err || expect) return;
    check(process != NULL, "a process handle came back");
    check(wait_exit(process, 42), "the handle waits for the process and returns its exit code");
    report_child(out, flags && strchr(flags, 'e'));
    CloseHandle(process);
}

static void args_mode(const char *image, const char *out)
{
    static const WCHAR tail[] = L" \"quoted arg with spaces\" \"with \\\"embedded\\\" quotes\" caf\u00e9 \u4e2d\u6587 \U0001F600 back\\\\slash"
                                L" \"trailing\\\\\" a\tb \"\" end";
    WCHAR *wimage = widen(image), *cmd = calloc(32768, sizeof(WCHAR)), *p, *head;
    char path[MAX_PATH + 16], *narrow_head = malloc(2 * strlen(image) + 2 * strlen(out) + 64);
    HANDLE process = NULL;
    DWORD err, size = 0, done, i;
    BYTE *got;
    HANDLE f;

    sprintf(narrow_head, "\"%s\" child %s", image, out);
    head = widen(narrow_head);
    lstrcpyW(cmd, head);
    p = cmd + lstrlenW(cmd);
    *p++ = ' ';
    for (i = 0; i < 20000; i++) *p++ = L'a' + (WCHAR)(i % 26);   /* a long argument */
    lstrcpyW(p, tail);
    err = wexelevate_run(wimage, cmd, L"C:\\windows", NULL, 0, SW_SHOWNORMAL, WEXELEVATE_WANT_HANDLE, &process, NULL);
    SetLastError(err);
    check(err == 0, "the request with long, Unicode and quoted arguments is served");
    if (!err)
    {
        check(wait_exit(process, 42), "the process ran and exited");
        sprintf(path, "%s.cmd", out);
        f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        check(f != INVALID_HANDLE_VALUE, "the child wrote its command line");
        if (f != INVALID_HANDLE_VALUE)
        {
            size = GetFileSize(f, NULL);
            got = calloc(size + 2, 1);
            ReadFile(f, got, size, &done, NULL);
            CloseHandle(f);
            check(size == lstrlenW(cmd) * sizeof(WCHAR) && !memcmp(got, cmd, size), "the command line arrived byte for byte");
            free(got);
        }
        CloseHandle(process);
    }
    free(wimage); free(head); free(narrow_head); free(cmd);
}

struct conc { const char *image, *out; DWORD err; HANDLE process; };

static DWORD WINAPI conc_thread(void *param)
{
    struct conc *c = param;

    c->err = ask(c->image, c->out, NULL, NULL, &c->process);
    return 0;
}

static void concurrent_mode(const char *imageA, const char *imageB, const char *outA, const char *outB)
{
    struct conc a = { imageA, outA }, b = { imageB, outB };
    HANDLE t[2];

    t[0] = CreateThread(NULL, 0, conc_thread, &a, 0, NULL);
    t[1] = CreateThread(NULL, 0, conc_thread, &b, 0, NULL);
    WaitForMultipleObjects(2, t, TRUE, 120000);
    SetLastError(a.err);
    check(a.err == 0, "request A, approved, was served");
    SetLastError(b.err);
    check(b.err == ERROR_CANCELLED, "request B, not approved, was refused with ERROR_CANCELLED");
    if (!a.err) { check(wait_exit(a.process, 42), "A ran"); CloseHandle(a.process); }
    check(GetFileAttributesA(outB) == INVALID_FILE_ATTRIBUTES, "B did not run");
}

static void forge_mode(const char *image, const char *out)
{
    /* a raw request whose advisory_pid names a bystander process of the same user */
    PROCESS_INFORMATION pi;
    STARTUPINFOA si = { sizeof(si) };
    char self[MAX_PATH], cmdA[2 * MAX_PATH];
    struct wexelevate_request *req;
    struct wexelevate_reply reply;
    WCHAR *wimage = widen(image), *cmd, *data;
    char narrow_cmd[1024];
    DWORD size, done, image_len = lstrlenW(wimage), cmd_len;
    HANDLE pipe;
    char buf[64], *text;

    GetModuleFileNameA(NULL, self, sizeof(self));
    sprintf(cmdA, "\"%s\" hold", self);
    check(CreateProcessA(self, cmdA, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi), "a bystander process started");
    sprintf(narrow_cmd, "\"%s\" child %s", image, out);
    cmd = widen(narrow_cmd);
    cmd_len = lstrlenW(cmd);
    size = sizeof(*req) + (image_len + cmd_len) * sizeof(WCHAR);
    req = calloc(1, size);
    req->magic = WEXELEVATE_MAGIC;
    req->version = WEXELEVATE_VERSION;
    req->flags = WEXELEVATE_WANT_HANDLE;
    req->show = SW_SHOWNORMAL;
    req->advisory_pid = pi.dwProcessId;
    req->image_len = image_len;
    req->cmdline_len = cmd_len;
    data = (WCHAR *)(req + 1);
    memcpy(data, wimage, image_len * sizeof(WCHAR));
    memcpy(data + image_len, cmd, cmd_len * sizeof(WCHAR));
    check(wexelevate_connect(&pipe) == 0, "connected to the broker");
    check(WriteFile(pipe, req, size, &done, NULL) && done == size, "sent the forged request");
    check(ReadFile(pipe, &reply, sizeof(reply), &done, NULL) && done == sizeof(reply), "got a reply");
    SetLastError(reply.error);
    check(reply.error == 0, "the request was served");
    if (!reply.error)
    {
        HANDLE h = (HANDLE)(ULONG_PTR)reply.handle;
        check(wait_exit(h, 42), "the elevated process ran");
        CloseHandle(h);
        text = read_text(out);
        sprintf(buf, "%lu", GetCurrentProcessId());
        checkf(text && fact_is(text, "parent", buf), "the parent is the real caller", text && fact(text, "parent") ? fact(text, "parent") : "none");
        sprintf(buf, "%lu", pi.dwProcessId);
        checkf(text && !fact_is(text, "parent", buf), "the parent is not the process named in the request", buf);
        free(text);
    }
    CloseHandle(pipe);
    TerminateProcess(pi.hProcess, 0);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    free(wimage); free(cmd); free(req);
}

/* ---- caller binding (E1-H2): the opener must not be impersonating, and a dead caller leaves no child ----------- */

/* Read another process's command line from its PEB (the pattern from scripts/webview2-probe.c). The structures are
 * minimal local copies so the probe does not depend on the toolchain's winternl.h defining them. */
typedef struct wex_unicode_string { USHORT length, maximum; WCHAR *buffer; } wex_unicode_string;
typedef struct wex_process_parameters
{
    ULONG maximum_length, length, flags, debug_flags;
    HANDLE console_handle;
    ULONG console_flags;
    HANDLE standard_input, standard_output, standard_error;
    wex_unicode_string current_directory;
    HANDLE current_directory_handle;
    wex_unicode_string image_path_name;
    wex_unicode_string command_line;
} wex_process_parameters;

static int cmdline_contains(DWORD pid, const WCHAR *needle)
{
    typedef NTSTATUS (WINAPI *wex_query_process)(HANDLE, PROCESSINFOCLASS, void *, ULONG, ULONG *);
    static wex_query_process query = NULL;
    PROCESS_BASIC_INFORMATION info;
    BYTE peb[64];
    wex_process_parameters params;
    HANDLE process;
    ULONG_PTR params_addr;
    WCHAR *cmd = NULL;
    int found = 0;

    if (!query) query = (wex_query_process)(void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess");
    if (!query) return 0;
    process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) return 0;
    if (!query(process, ProcessBasicInformation, &info, sizeof(info), NULL) &&
        ReadProcessMemory(process, info.PebBaseAddress, peb, sizeof(peb), NULL))
    {
#ifdef _WIN64
        memcpy(&params_addr, peb + 0x20, sizeof(params_addr));
#else
        memcpy(&params_addr, peb + 0x18, sizeof(params_addr));
#endif
        if (params_addr && ReadProcessMemory(process, (void *)params_addr, &params, sizeof(params), NULL) &&
            params.command_line.length && params.command_line.buffer)
        {
            cmd = calloc(params.command_line.length + sizeof(WCHAR), 1);
            if (cmd && ReadProcessMemory(process, params.command_line.buffer, cmd, params.command_line.length, NULL))
                found = wcsstr(cmd, needle) != NULL;
            free(cmd);
        }
    }
    CloseHandle(process);
    return found;
}

/* a process (not this one) whose command line names <image> and the child argument: the elevated child */
static int child_running(const char *image)
{
    PROCESSENTRY32W pe = { sizeof(pe) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    WCHAR *wimage = widen(image);
    static const WCHAR child[] = L"child";
    int found = 0;

    if (snap != INVALID_HANDLE_VALUE && Process32FirstW(snap, &pe))
        do { if (pe.th32ProcessID != GetCurrentProcessId() &&
                 cmdline_contains(pe.th32ProcessID, wimage) && cmdline_contains(pe.th32ProcessID, child)) found = 1; }
        while (Process32NextW(snap, &pe));
    if (snap != INVALID_HANDLE_VALUE) CloseHandle(snap);
    free(wimage);
    return found;
}

static void impersonate_mode(const char *image, const char *out)
{
    HANDLE process = NULL;
    DWORD err;

    check(ImpersonateSelf(SecurityImpersonation), "the thread impersonates its own token");
    err = ask(image, out, NULL, NULL, &process);
    RevertToSelf();
    SetLastError(err);
    check(err == ERROR_ACCESS_DENIED, "the broker denies an impersonating opener with ERROR_ACCESS_DENIED");
    check(GetFileAttributesA(out) == INVALID_FILE_ATTRIBUTES, "the impersonating request started nothing");
}

/* E1-S3 lab client. No broker/server barrier is needed: the parent opens the
 * endpoint before starting a different process with a duplicate of that handle.
 * Run only in the disposable sysrun fixture with a bounded consent hook. */
static DWORD WINAPI provenance_watchdog(void *unused)
{
    Sleep(150000);
    ExitProcess(3);
    return 0;
}

static void provenance_send(HANDLE pipe, int transceive, int impersonate, int delegated,
                            const char *image, const char *out)
{
    WCHAR *wimage = widen(image), *cmd, *data;
    char *command = malloc(strlen(image) + strlen(out) + 32);
    struct wexelevate_request *req;
    struct wexelevate_reply reply = { 0 };
    DWORD size, done = 0, mode = PIPE_READMODE_MESSAGE;
    BOOL ok = FALSE;

    check(command && wimage, "allocated the lab request strings");
    if (!command || !wimage) { free(command); free(wimage); return; }
    sprintf(command, "\"%s\" child \"%s\"", image, out);
    cmd = widen(command);
    free(command);
    if (!cmd) { check(0, "allocated the lab command line"); free(wimage); return; }
    size = sizeof(*req) + (lstrlenW(wimage) + lstrlenW(cmd)) * sizeof(WCHAR);
    req = calloc(1, size);
    check(req != NULL, "allocated the lab request");
    if (!req) { free(wimage); free(cmd); return; }
    req->magic = WEXELEVATE_MAGIC;
    req->version = WEXELEVATE_VERSION;
    req->show = SW_SHOWNORMAL;
    req->image_len = lstrlenW(wimage);
    req->cmdline_len = lstrlenW(cmd);
    data = (WCHAR *)(req + 1);
    memcpy(data, wimage, req->image_len * sizeof(WCHAR));
    memcpy(data + req->image_len, cmd, req->cmdline_len * sizeof(WCHAR));
    if (!SetNamedPipeHandleState(pipe, &mode, NULL, NULL)) check(0, "selected message read mode");
    else if (impersonate && !ImpersonateSelf(SecurityImpersonation)) check(0, "installed a thread token after open");
    else
    {
        if (transceive) ok = TransactNamedPipe(pipe, req, size, &reply, sizeof(reply), &done, NULL);
        else
        {
            ok = WriteFile(pipe, req, size, &done, NULL) && done == size;
            check(ok, "submitted the request message");
            if (impersonate) check(RevertToSelf(), "reverted after submission");
            if (ok) ok = ReadFile(pipe, &reply, sizeof(reply), &done, NULL);
        }
        if (impersonate && transceive) check(RevertToSelf(), "reverted after transceive");
        check(ok && done == sizeof(reply) && reply.magic == WEXELEVATE_MAGIC, "received the bounded broker reply");
        if (ok && done == sizeof(reply) && reply.magic == WEXELEVATE_MAGIC)
        {
            SetLastError(reply.error);
            check(reply.error == (impersonate || delegated ? ERROR_ACCESS_DENIED : ERROR_SUCCESS),
                  "the consumed writer has the expected result");
            if (impersonate || delegated)
            {
                check(!reply.pid && !reply.tid && !reply.handle, "denial has zero result fields");
                check(GetFileAttributesA(out) == INVALID_FILE_ATTRIBUTES, "the denied writer started nothing");
            }
        }
    }
    free(req); free(cmd); free(wimage);
}

static void provenance_mode(const char *which, const char *image, const char *out)
{
    HANDLE pipe = INVALID_HANDLE_VALUE, inherited = NULL, watchdog;
    int delegated = !strncmp(which, "delegate", 8), transceive = strstr(which, "transceive") != NULL;
    int impersonate = strstr(which, "impersonate") != NULL;
    int modified = !strcmp(which, "dacl-restore");

    watchdog = CreateThread(NULL, 0, provenance_watchdog, NULL, 0, NULL);
    check(watchdog != NULL, "started the lab timeout guard");
    if (!watchdog) return;
    CloseHandle(watchdog);
    check(GetFileAttributesA(out) == INVALID_FILE_ATTRIBUTES, "the output is absent before the probe");
    check(wexelevate_connect(&pipe) == 0, "opened the pipe as self");
    if (pipe == INVALID_HANDLE_VALUE) return;
    if (modified)
    {
        BYTE saved[8192]; /* bounded fixture token */
        TOKEN_DEFAULT_DACL empty = { NULL };
        HANDLE token;
        DWORD len;

        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_DEFAULT, &token))
        {
            if (GetTokenInformation(token, TokenDefaultDacl, saved, sizeof(saved), &len))
            {
                check(SetTokenInformation(token, TokenDefaultDacl, &empty, sizeof(empty)), "changed the default DACL after open");
                check(SetTokenInformation(token, TokenDefaultDacl, saved, len), "restored the original default DACL");
            }
            else check(0, "read the original default DACL");
            CloseHandle(token);
        }
        else check(0, "opened the primary token for the mutation probe");
    }
    if (!delegated) provenance_send(pipe, transceive, impersonate, modified, image, out);
    else if (DuplicateHandle(GetCurrentProcess(), pipe, GetCurrentProcess(), &inherited, 0, TRUE, DUPLICATE_SAME_ACCESS))
    {
        PROCESS_INFORMATION pi;
        STARTUPINFOA si = { sizeof(si) };
        char self[MAX_PATH], *command;
        DWORD exit_code = 1;

        GetModuleFileNameA(NULL, self, sizeof(self));
        command = malloc(strlen(self) + strlen(image) + strlen(out) + 96);
        check(command != NULL, "allocated the delegated command");
        if (command)
        {
            sprintf(command, "\"%s\" provenance-child %llu %d \"%s\" \"%s\"", self,
                    (unsigned long long)(ULONG_PTR)inherited, transceive, image, out);
            if (CreateProcessA(self, command, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
            {
                DWORD wait = WaitForSingleObject(pi.hProcess, 140000);
                check(wait == WAIT_OBJECT_0, "the delegated writer completed within the deadline");
                if (wait != WAIT_OBJECT_0) { TerminateProcess(pi.hProcess, 3); WaitForSingleObject(pi.hProcess, 5000); }
                if (GetExitCodeProcess(pi.hProcess, &exit_code)) check(exit_code == 0, "the different writer was denied");
                else check(0, "queried the delegated writer exit status");
                CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
            }
            else check(0, "started the delegated writer");
            free(command);
        }
        CloseHandle(inherited);
    }
    else check(0, "duplicated the pipe for another process");
    CloseHandle(pipe);
}

/* the image is replaced while the consent is pending: the harness (from Unix, where the rename is not blocked by
 * the broker's read handle) renames a different file over the path after the probe signals imgrace.ready, and
 * writes imgrace.done; the broker must deny (ERROR_FILE_CORRUPT) and start nothing */
static void imagerace_mode(const char *image, const char *out)
{
    struct conc c = { image, out };
    HANDLE t;
    char name[MAX_PATH], path[2 * MAX_PATH], *base;
    DWORD i;

    t = CreateThread(NULL, 0, conc_thread, &c, 0, NULL);
    base = strrchr(image, '\\');
    strncpy(name, base ? base + 1 : image, sizeof(name) - 1);
    name[sizeof(name) - 1] = 0;
    sprintf(path, POLICY_DIR "started.%s", name);
    for (i = 0; i < 600 && GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES; i++) Sleep(100);
    check(GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES, "the consent is pending (the stub wrote started.<name>)");
    write_whole(POLICY_DIR "imgrace.ready", "x", 1);
    for (i = 0; i < 600 && GetFileAttributesA(POLICY_DIR "imgrace.done") == INVALID_FILE_ATTRIBUTES; i++) Sleep(100);
    check(GetFileAttributesA(POLICY_DIR "imgrace.done") != INVALID_FILE_ATTRIBUTES, "the harness replaced the image");
    write_whole(POLICY_DIR "gate", "x", 1);
    WaitForSingleObject(t, 120000);
    CloseHandle(t);
    SetLastError(c.err);
    check(c.err == ERROR_FILE_CORRUPT, "the broker denies a request whose image was replaced while the consent was pending");
    check(GetFileAttributesA(out) == INVALID_FILE_ATTRIBUTES, "the replaced image started nothing");
    check(!child_running(image), "no child is running after the refusal");
}

/* a writer holds the image open when the request is made: the broker's read open of the image must fail
 * (ERROR_SHARING_VIOLATION) and start nothing */
static void writerfirst_mode(const char *image, const char *out)
{
    HANDLE f = CreateFileA(image, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    HANDLE process = NULL;
    DWORD err;

    check(f != INVALID_HANDLE_VALUE, "the image opens for writing with another handle");
    if (f == INVALID_HANDLE_VALUE) return;
    err = ask(image, out, NULL, NULL, &process);
    CloseHandle(f);
    SetLastError(err);
    check(err == ERROR_SHARING_VIOLATION, "the broker denies a request with an existing writer on the image");
    check(GetFileAttributesA(out) == INVALID_FILE_ATTRIBUTES, "the request with an existing writer started nothing");
    check(!child_running(image), "no child is running after the refusal");
}

static void callerdeath_mode(const char *image, const char *out, const char *tag)
{
    struct wexelevate_request *req;
    WCHAR *wimage = widen(image), *cmd, *data, *wtag = widen(tag);
    char *narrow_cmd = malloc(2 * strlen(image) + 2 * strlen(out) + strlen(tag) + 64);
    DWORD size, done, image_len = lstrlenW(wimage), cmd_len;
    HANDLE pipe;
    PROCESSENTRY32W pe = { sizeof(pe) };
    HANDLE snap;
    int found = 0;

    /* "hold <tag>": a child that was started would stay alive until the release file exists */
    sprintf(narrow_cmd, "\"%s\" child %s hold %s", image, out, tag);
    cmd = widen(narrow_cmd);
    cmd_len = lstrlenW(cmd);
    size = sizeof(*req) + (image_len + cmd_len) * sizeof(WCHAR);
    req = calloc(1, size);
    req->magic = WEXELEVATE_MAGIC;
    req->version = WEXELEVATE_VERSION;
    req->flags = WEXELEVATE_WANT_HANDLE;
    req->show = SW_SHOWNORMAL;
    req->advisory_pid = 0;
    req->image_len = image_len;
    req->cmdline_len = cmd_len;
    data = (WCHAR *)(req + 1);
    memcpy(data, wimage, image_len * sizeof(WCHAR));
    memcpy(data + image_len, cmd, cmd_len * sizeof(WCHAR));
    check(wexelevate_connect(&pipe) == 0, "connected to the broker");
    check(WriteFile(pipe, req, size, &done, NULL) && done == size, "sent the request");
    CloseHandle(pipe);   /* the caller goes away without reading the reply */
    Sleep(3000);
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE && Process32FirstW(snap, &pe))
        do { if (pe.th32ProcessID != GetCurrentProcessId() && cmdline_contains(pe.th32ProcessID, wtag)) found = 1; } while (Process32NextW(snap, &pe));
    if (snap != INVALID_HANDLE_VALUE) CloseHandle(snap);
    check(!found, "no process with the tag is running after the caller disconnected");
    {
        char release[MAX_PATH];
        sprintf(release, POLICY_DIR "release.%s", tag);   /* end a child that was started anyway */
        write_whole(release, "x", 1);
    }
    free(wimage); free(cmd); free(req); free(wtag); free(narrow_cmd);
}

static void denied(DWORD pid, DWORD access, const char *name)
{
    char what[128];
    HANDLE h = OpenProcess(access, FALSE, pid);
    DWORD err = GetLastError();

    sprintf(what, "OpenProcess(%s) of the elevated process is denied", name);
    if (h) CloseHandle(h);
    SetLastError(err);
    check(!h && err == ERROR_ACCESS_DENIED, what);
}

static void attack_mode(const char *image, const char *out, const char *tag)
{
    HANDLE process = NULL, h, snap, tok, dup;
    THREADENTRY32 te = { sizeof(te) };
    DWORD pid = 0, err, i, threads = 0, got;
    char *text, path[MAX_PATH];
    WCHAR *wimage = widen(image), *cmd;
    char narrow_cmd[2048];
    static const DWORD tokrights[] = { TOKEN_DUPLICATE, TOKEN_IMPERSONATE, TOKEN_ASSIGN_PRIMARY, TOKEN_ADJUST_PRIVILEGES,
                                       TOKEN_ADJUST_DEFAULT, TOKEN_ADJUST_GROUPS };
    static const char *const tokname[] = { "TOKEN_DUPLICATE", "TOKEN_IMPERSONATE", "TOKEN_ASSIGN_PRIMARY",
                                           "TOKEN_ADJUST_PRIVILEGES", "TOKEN_ADJUST_DEFAULT", "TOKEN_ADJUST_GROUPS" };

    sprintf(narrow_cmd, "\"%s\" child %s hold %s", image, out, tag);
    cmd = widen(narrow_cmd);
    err = wexelevate_run(wimage, cmd, L"C:\\windows", NULL, 0, SW_SHOWNORMAL, WEXELEVATE_WANT_HANDLE, &process, &pid);
    SetLastError(err);
    check(err == 0 && process && pid, "the held elevated process started");
    if (err || !process) return;
    for (i = 0; i < 300; i++)
    {
        text = read_text(out);
        if (text && strstr(text, "ready=1")) break;
        free(text);
        text = NULL;
        Sleep(100);
    }
    check(text != NULL, "the elevated process is up");
    free(text);

    denied(pid, PROCESS_VM_WRITE, "PROCESS_VM_WRITE");
    denied(pid, PROCESS_CREATE_THREAD, "PROCESS_CREATE_THREAD");
    denied(pid, PROCESS_DUP_HANDLE, "PROCESS_DUP_HANDLE");
    denied(pid, PROCESS_VM_OPERATION, "PROCESS_VM_OPERATION");
    denied(pid, PROCESS_VM_READ, "PROCESS_VM_READ");
    denied(pid, PROCESS_SET_INFORMATION, "PROCESS_SET_INFORMATION");
    denied(pid, PROCESS_TERMINATE, "PROCESS_TERMINATE");
    denied(pid, PROCESS_SUSPEND_RESUME, "PROCESS_SUSPEND_RESUME");
    denied(pid, PROCESS_CREATE_PROCESS, "PROCESS_CREATE_PROCESS");
    denied(pid, PROCESS_QUERY_INFORMATION, "PROCESS_QUERY_INFORMATION");
    denied(pid, WRITE_DAC, "WRITE_DAC");
    denied(pid, WRITE_OWNER, "WRITE_OWNER");
    denied(pid, DELETE, "DELETE");
    denied(pid, PROCESS_ALL_ACCESS, "PROCESS_ALL_ACCESS");
    h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    check(h != NULL, "the limited query is allowed (control)");
    if (h)
    {
        for (i = 0; i < sizeof(tokrights) / sizeof(tokrights[0]); i++)
        {
            char what[128];
            tok = NULL;
            sprintf(what, "the token of the elevated process cannot be opened with %s", tokname[i]);
            SetLastError(0);
            check(!OpenProcessToken(h, tokrights[i], &tok), what);
            if (tok) CloseHandle(tok);
        }
        CloseHandle(h);
    }

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap != INVALID_HANDLE_VALUE && Thread32First(snap, &te))
    {
        do
        {
            static const DWORD rights[] = { THREAD_SET_CONTEXT, THREAD_GET_CONTEXT, THREAD_SUSPEND_RESUME, THREAD_SET_INFORMATION,
                                            THREAD_TERMINATE, THREAD_SET_THREAD_TOKEN, THREAD_IMPERSONATE, WRITE_DAC, THREAD_ALL_ACCESS };
            if (te.th32OwnerProcessID != pid) continue;
            threads++;
            for (i = 0; i < sizeof(rights) / sizeof(rights[0]); i++)
            {
                h = OpenThread(rights[i], FALSE, te.th32ThreadID);
                err = GetLastError();
                if (h) CloseHandle(h);
                SetLastError(err);
                check(!h && err == ERROR_ACCESS_DENIED, "a thread of the elevated process cannot be opened for the dangerous rights");
            }
            h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
            check(h != NULL, "the limited thread query is allowed (control)");
            if (h) CloseHandle(h);
        } while (Thread32Next(snap, &te));
        CloseHandle(snap);
    }
    check(threads > 0, "the elevated process has threads to try");

    /* the handle the broker gave: wait and query, nothing else */
    SetLastError(0);
    check(!TerminateProcess(process, 1) && GetLastError() == ERROR_ACCESS_DENIED, "the handle from the broker cannot terminate the process");
    check(!ReadProcessMemory(process, (void *)0x10000, path, 4, NULL), "the handle from the broker cannot read the memory");
    check(!WriteProcessMemory(process, (void *)0x10000, path, 4, NULL), "the handle from the broker cannot write the memory");
    check(!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), process, &dup, 0, FALSE, DUPLICATE_SAME_ACCESS),
          "no handle can be duplicated into the elevated process");
    check(GetExitCodeProcess(process, &got) && got == STILL_ACTIVE, "the handle from the broker queries the exit code");

    /* the harness looks at the Unix side of the process now */
    sprintf(path, POLICY_DIR "continue.%s", tag);
    for (i = 0; i < 600 && GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES; i++) Sleep(100);
    sprintf(path, POLICY_DIR "release.%s", tag);
    write_whole(path, "go", 2);
    check(wait_exit(process, 42), "the released process exits with its code");
    CloseHandle(process);
    free(wimage); free(cmd);
}

static void shexec_mode(const char *verb, const char *image, DWORD expect, const char *out, const char *elev, const char *extra)
{
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    WCHAR *wimage = widen(image), params[2 * MAX_PATH], *wverb = strcmp(verb, "-") ? widen(verb) : NULL, *wout = widen(out);
    BOOL ret;
    DWORD err, code = 0, want_exit = 42;
    char what[96], detail[64];
    int waited;

    /* "raw:<parameters>" runs a program that is not this probe (an installer); its exit code must be 0 */
    if (extra && !strncmp(extra, "raw:", 4))
    {
        WCHAR *wextra = widen(extra + 4);

        swprintf(params, sizeof(params) / sizeof(WCHAR), L"%ls", wextra);
        free(wextra);
        want_exit = 0;
        elev = "-";
    }
    else if (extra)
    {
        WCHAR *wextra = widen(extra);

        swprintf(params, sizeof(params) / sizeof(WCHAR), L"child %ls %ls", wout, wextra);
        free(wextra);
    }
    else swprintf(params, sizeof(params) / sizeof(WCHAR), L"child %ls", wout);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = wverb;
    sei.lpFile = wimage;
    sei.lpParameters = params;
    sei.nShow = SW_SHOWNORMAL;
    SetLastError(0xdeadbeef);
    ret = ShellExecuteExW(&sei);
    err = ret ? 0 : GetLastError();
    sprintf(what, "ShellExecuteEx error %lu (got %lu, hInstApp %lu)", expect, err, (unsigned long)(ULONG_PTR)sei.hInstApp);
    check(err == expect, what);
    if (expect)
    {
        check(!ret && sei.hProcess == NULL, "a failed ShellExecuteEx returns no process handle");
        check((ULONG_PTR)sei.hInstApp <= 32, "hInstApp is an SE_ERR value");
        if (expect == ERROR_CANCELLED || expect == ERROR_NOT_SUPPORTED || expect == ERROR_NOT_READY)
            check((ULONG_PTR)sei.hInstApp == SE_ERR_ACCESSDENIED, "hInstApp is SE_ERR_ACCESSDENIED");
    }
    else
    {
        check(ret && (ULONG_PTR)sei.hInstApp > 32, "ShellExecuteEx succeeded, hInstApp above 32");
        check(sei.hProcess != NULL, "SEE_MASK_NOCLOSEPROCESS returned a process handle");
        if (sei.hProcess)
        {
            sprintf(what, "the process handle waits and returns the exit code %lu", want_exit);
            detail[0] = 0;
            waited = WaitForSingleObject(sei.hProcess, 600000) == WAIT_OBJECT_0 && GetExitCodeProcess(sei.hProcess, &code);
            sprintf(detail, "wait %d exit code %lu", waited, code);
            checkf(waited && code == want_exit, what, detail);
            CloseHandle(sei.hProcess);
        }
        if (elev && strcmp(elev, "-"))
        {
            char *text = read_text(out);
            checkf(text && fact_is(text, "elevation", elev), "the started process has the expected token elevation type",
                   text && fact(text, "elevation") ? fact(text, "elevation") : "none");
            free(text);
        }
    }
    free(wimage); free(wout); free(wverb);
}

static void createproc_mode(const char *image, DWORD expect, const char *out)
{
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    char cmd[2 * MAX_PATH + 32], what[96];
    DWORD err, code = 0;
    BOOL ret;

    sprintf(cmd, "\"%s\" child %s", image, out);
    SetLastError(0xdeadbeef);
    ret = CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    err = ret ? 0 : GetLastError();
    sprintf(what, "CreateProcess error %lu (got %lu)", expect, err);
    check(err == expect, what);
    if (ret)
    {
        check(WaitForSingleObject(pi.hProcess, 60000) == WAIT_OBJECT_0 && GetExitCodeProcess(pi.hProcess, &code) && code == 42,
              "the process ran and returned 42");
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

static void msi_mode(const char *path, const char *props, DWORD expect, const char *temp)
{
    DWORD r;
    char what[96];

    if (temp)
    {
        SetEnvironmentVariableA("TEMP", temp);
        SetEnvironmentVariableA("TMP", temp);
    }
    MsiSetInternalUI(INSTALLUILEVEL_NONE, NULL);
    r = MsiInstallProductA(path, props);
    sprintf(what, "MsiInstallProduct returns %lu (got %lu)", expect, r);
    check(r == expect, what);
}

static void leasekill_mode(const char *image, const char *out, const char *tag)
{
    HANDLE process = NULL;
    char path[MAX_PATH + 32], args[64];
    DWORD err, code = 0, wait;

    sprintf(args, "hold %s", tag);
    err = ask(image, out, NULL, args, &process);
    SetLastError(err);
    check(err == 0, "the held elevated process started");
    if (err) return;
    wait = WaitForSingleObject(process, 60000);
    check(wait == WAIT_OBJECT_0, "the elevated process was ended by the writer");
    sprintf(path, POLICY_DIR "release.%s", tag);
    check(GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES, "it ended before the harness released it");
    check(GetExitCodeProcess(process, &code) && code != 42 && code != STILL_ACTIVE, "it did not exit by itself");
    CloseHandle(process);
}

static void spoofcmd_mode(const char *image, const char *fake, const char *out)
{
    WCHAR *wimage = widen(image), *cmd;
    char *narrow_cmd = malloc(2 * strlen(fake) + 2 * strlen(out) + 64);
    HANDLE process = NULL;
    DWORD err;

    sprintf(narrow_cmd, "\"%s\" child %s", fake, out);
    cmd = widen(narrow_cmd);
    err = wexelevate_run(wimage, cmd, L"C:\\windows", NULL, 0, SW_SHOWNORMAL, WEXELEVATE_WANT_HANDLE, &process, NULL);
    SetLastError(err);
    check(err == 0, "the request with another program named in the command line is served");
    if (!err) { check(wait_exit(process, 42), "the image ran"); CloseHandle(process); }
    free(wimage); free(narrow_cmd); free(cmd);
}

static void consentattack_mode(void)
{
    static const struct { DWORD access; const char *name; } rights[] = {
        { PROCESS_TERMINATE, "PROCESS_TERMINATE" }, { PROCESS_VM_WRITE, "PROCESS_VM_WRITE" },
        { PROCESS_VM_OPERATION, "PROCESS_VM_OPERATION" }, { PROCESS_CREATE_THREAD, "PROCESS_CREATE_THREAD" },
        { PROCESS_DUP_HANDLE, "PROCESS_DUP_HANDLE" }, { PROCESS_SET_INFORMATION, "PROCESS_SET_INFORMATION" },
        { PROCESS_SUSPEND_RESUME, "PROCESS_SUSPEND_RESUME" }, { PROCESS_VM_READ, "PROCESS_VM_READ" },
        { PROCESS_QUERY_INFORMATION, "PROCESS_QUERY_INFORMATION" }, { WRITE_DAC, "WRITE_DAC" },
        { WRITE_OWNER, "WRITE_OWNER" }, { DELETE, "DELETE" } };
    PROCESSENTRY32W pe = { sizeof(pe) };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0), h, token;
    DWORD pid = 0;
    char what[96];
    unsigned int i;

    if (snap != INVALID_HANDLE_VALUE && Process32FirstW(snap, &pe))
        do { if (!lstrcmpiW(pe.szExeFile, L"consent_test.exe")) pid = pe.th32ProcessID; } while (Process32NextW(snap, &pe));
    if (snap != INVALID_HANDLE_VALUE) CloseHandle(snap);
    check(pid != 0, "the consent process exists");
    if (!pid) return;
    {
        /* its parent is the broker (not this process, not the caller's tree) */
        DWORD parent = 0;
        WCHAR pname[MAX_PATH] = L"";

        snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE && Process32FirstW(snap, &pe))
            do { if (pe.th32ProcessID == pid) parent = pe.th32ParentProcessID; } while (Process32NextW(snap, &pe));
        if (snap != INVALID_HANDLE_VALUE && Process32FirstW(snap, &pe))
            do { if (pe.th32ProcessID == parent) lstrcpyW(pname, pe.szExeFile); } while (Process32NextW(snap, &pe));
        if (snap != INVALID_HANDLE_VALUE) CloseHandle(snap);
        check(!lstrcmpiW(pname, L"wexelevate_test.exe") && parent != GetCurrentProcessId(), "the consent process is a child of the broker");
    }
    for (i = 0; i < sizeof(rights) / sizeof(rights[0]); i++)
    {
        SetLastError(0);
        h = OpenProcess(rights[i].access, FALSE, pid);
        sprintf(what, "a limited process cannot open the consent process with %s", rights[i].name);
        check(!h && GetLastError() == ERROR_ACCESS_DENIED, what);
        if (h) CloseHandle(h);
    }
    h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    check(h != NULL, "the limited query right is allowed");
    if (h)
    {
        SetLastError(0);
        check(!OpenProcessToken(h, TOKEN_DUPLICATE | TOKEN_QUERY, &token) && GetLastError() == ERROR_ACCESS_DENIED,
              "the token of the consent process cannot be opened");
        CloseHandle(h);
    }
}

static void winwrite_mode(const char *path)
{
    HANDLE f = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    DWORD done;
    LARGE_INTEGER zero = { 0 };

    check(f != INVALID_HANDLE_VALUE, "the image opens for writing through Win32");
    if (f == INVALID_HANDLE_VALUE) return;
    SetFilePointerEx(f, zero, NULL, FILE_END);
    check(WriteFile(f, "x", 1, &done, NULL) && done == 1, "the byte was written");
    CloseHandle(f);
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "";

    if (!strcmp(mode, "child")) return child_mode(argc, argv);
    if (!strcmp(mode, "hold")) { Sleep(120000); return 0; }
    if (!strcmp(mode, "run") && argc > 4) run_mode(argv[2], atoi(argv[3]), argv[4], argc > 5 ? argv[5] : NULL, argc > 6 ? argv[6] : NULL);
    else if (!strcmp(mode, "args") && argc > 3) args_mode(argv[2], argv[3]);
    else if (!strcmp(mode, "concurrent") && argc > 5) concurrent_mode(argv[2], argv[3], argv[4], argv[5]);
    else if (!strcmp(mode, "forge") && argc > 3) forge_mode(argv[2], argv[3]);
    else if (!strcmp(mode, "impersonate") && argc > 3) impersonate_mode(argv[2], argv[3]);
    else if (!strcmp(mode, "provenance") && argc > 4) provenance_mode(argv[2], argv[3], argv[4]);
    else if (!strcmp(mode, "provenance-child") && argc > 5)
    {
        HANDLE pipe = (HANDLE)(ULONG_PTR)strtoull(argv[2], NULL, 10);
        provenance_send(pipe, atoi(argv[3]), 0, 1, argv[4], argv[5]);
        CloseHandle(pipe);
    }
    else if (!strcmp(mode, "imagerace") && argc > 3) imagerace_mode(argv[2], argv[3]);
    else if (!strcmp(mode, "writerfirst") && argc > 3) writerfirst_mode(argv[2], argv[3]);
    else if (!strcmp(mode, "callerdeath") && argc > 4) callerdeath_mode(argv[2], argv[3], argv[4]);
    else if (!strcmp(mode, "attack") && argc > 4) attack_mode(argv[2], argv[3], argv[4]);
    else if (!strcmp(mode, "leasekill") && argc > 4) leasekill_mode(argv[2], argv[3], argv[4]);
    else if (!strcmp(mode, "spoofcmd") && argc > 4) spoofcmd_mode(argv[2], argv[3], argv[4]);
    else if (!strcmp(mode, "shexec") && argc > 6) shexec_mode(argv[2], argv[3], atoi(argv[4]), argv[5], argv[6], argc > 7 ? argv[7] : NULL);
    else if (!strcmp(mode, "createproc") && argc > 4) createproc_mode(argv[2], atoi(argv[3]), argv[4]);
    else if (!strcmp(mode, "msi") && argc > 4) msi_mode(argv[2], argv[3], atoi(argv[4]), argc > 5 ? argv[5] : NULL);
    else if (!strcmp(mode, "consentattack")) consentattack_mode();
    else if (!strcmp(mode, "winwrite") && argc > 2) winwrite_mode(argv[2]);
    else if (!strcmp(mode, "facts") && argc > 2)
    {
        FILE *out = fopen(argv[2], "w");
        if (out) { facts(out); fclose(out); }
        printf("facts=PASS\n");
        return 0;
    }
    else
    {
        fprintf(stderr, "bad mode\n");
        return 2;
    }
    printf("%s=%s\n", mode, failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
