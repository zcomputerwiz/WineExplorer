// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
// Records how Windows treats the UAC linked token (docs/ELEVATION_BOUNDARY_PLAN.md, cases N1-N6).
// Run it on native Windows 10/11, once as a filtered (non-elevated) administrator and once as SYSTEM with
// --tcb, for both the 32-bit and the 64-bit build. Every line is key=value; nothing is asserted here.
#include <windows.h>
#include <wtsapi32.h>
#include <cstdio>
#include <cstring>

typedef LONG (NTAPI *set_info_fn)(HANDLE, ULONG, void *, ULONG);
struct process_access_token {HANDLE token; HANDLE thread;};

static void token_kind(const char *name, HANDLE token)
{
    TOKEN_TYPE type = (TOKEN_TYPE)0;
    SECURITY_IMPERSONATION_LEVEL level = (SECURITY_IMPERSONATION_LEVEL)-1;
    TOKEN_ELEVATION_TYPE elevation = (TOKEN_ELEVATION_TYPE)0;
    DWORD size;
    GetTokenInformation(token, TokenType, &type, sizeof(type), &size);
    if (type == TokenImpersonation) GetTokenInformation(token, TokenImpersonationLevel, &level, sizeof(level), &size);
    GetTokenInformation(token, TokenElevationType, &elevation, sizeof(elevation), &size);
    printf("%s.type=%d %s.level=%d %s.elevation=%d\n", name, type, name, level, name, elevation);
}

static void child_command(char *buffer, size_t size)
{
    char self[MAX_PATH];
    GetModuleFileNameA(NULL, self, MAX_PATH);
    snprintf(buffer, size, "\"%s\" --child", self);
}

static void try_create_as_user(const char *name, HANDLE token)
{
    char command[MAX_PATH + 20];
    STARTUPINFOA startup = {};
    PROCESS_INFORMATION info = {};
    startup.cb = sizeof(startup);
    child_command(command, sizeof(command));
    SetLastError(0);
    BOOL ok = CreateProcessAsUserA(token, NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &info);
    DWORD error = GetLastError(), code = 0;
    if (ok)
    {
        WaitForSingleObject(info.hProcess, 10000);
        GetExitCodeProcess(info.hProcess, &code);
        CloseHandle(info.hProcess);
        CloseHandle(info.hThread);
    }
    printf("%s.ok=%d %s.error=%lu %s.child_exit=%lu\n", name, ok, name, ok ? 0 : error, name, code);
}

static void try_create_with_token(const char *name, HANDLE token)
{
    WCHAR self[MAX_PATH], command[MAX_PATH + 20];
    STARTUPINFOW startup = {};
    PROCESS_INFORMATION info = {};
    startup.cb = sizeof(startup);
    GetModuleFileNameW(NULL, self, MAX_PATH);
    swprintf(command, MAX_PATH + 20, L"\"%ls\" --child", self);
    SetLastError(0);
    BOOL ok = CreateProcessWithTokenW(token, 0, NULL, command, 0, NULL, NULL, &startup, &info);
    DWORD error = GetLastError(), code = 0;
    if (ok)
    {
        WaitForSingleObject(info.hProcess, 10000);
        GetExitCodeProcess(info.hProcess, &code);
        CloseHandle(info.hProcess);
        CloseHandle(info.hThread);
    }
    printf("%s.ok=%d %s.error=%lu %s.child_exit=%lu\n", name, ok, name, ok ? 0 : error, name, code);
}

static void try_duplicate(const char *name, HANDLE token, SECURITY_IMPERSONATION_LEVEL level, TOKEN_TYPE type)
{
    HANDLE copy = NULL;
    SetLastError(0);
    BOOL ok = DuplicateTokenEx(token, MAXIMUM_ALLOWED, NULL, level, type, &copy);
    printf("%s.ok=%d %s.error=%lu\n", name, ok, name, ok ? 0 : GetLastError());
    if (ok)
    {
        token_kind(name, copy);
        char create[80];
        snprintf(create, sizeof(create), "%s.create", name);
        if (type == TokenPrimary) try_create_as_user(create, copy);
        CloseHandle(copy);
    }
}

static void try_assign(const char *name, HANDLE token)
{
    set_info_fn set_info = reinterpret_cast<set_info_fn>(reinterpret_cast<void *>(
        GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtSetInformationProcess")));
    char command[MAX_PATH + 20];
    STARTUPINFOA startup = {};
    PROCESS_INFORMATION info = {};
    startup.cb = sizeof(startup);
    child_command(command, sizeof(command));
    if (!CreateProcessA(NULL, command, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &startup, &info))
    {
        printf("%s.suspended_child_error=%lu\n", name, GetLastError());
        return;
    }
    process_access_token assign = {token, info.hThread};
    LONG status = set_info(info.hProcess, 9 /* ProcessAccessToken */, &assign, sizeof(assign));
    ResumeThread(info.hThread);
    DWORD code = 0;
    WaitForSingleObject(info.hProcess, 10000);
    GetExitCodeProcess(info.hProcess, &code);
    printf("%s.status=0x%08lx %s.child_exit=%lu\n", name, (unsigned long)status, name, code);
    CloseHandle(info.hProcess);
    CloseHandle(info.hThread);
}

static void try_impersonate_open(const char *name, HANDLE token)
{
    SetLastError(0);
    BOOL ok = SetThreadToken(NULL, token);
    printf("%s.set_thread_token=%d %s.set_error=%lu\n", name, ok, name, ok ? 0 : GetLastError());
    if (!ok) return;
    WCHAR path[MAX_PATH];
    GetSystemDirectoryW(path, MAX_PATH);
    lstrcatW(path, L"\\kernel32.dll");
    SetLastError(0);
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD file_error = file == INVALID_HANDLE_VALUE ? GetLastError() : 0;
    HKEY key = NULL;
    LONG reg_error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &key);
    HANDLE process_token = NULL;
    SetLastError(0);
    BOOL opened = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &process_token);
    DWORD token_error = opened ? 0 : GetLastError();
    RevertToSelf();
    printf("%s.open_file_error=%lu %s.open_key_error=%ld %s.open_process_token_error=%lu\n",
           name, file_error, name, reg_error, name, token_error);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (key) RegCloseKey(key);
    if (process_token) CloseHandle(process_token);
}

static void probe_linked(const char *prefix, HANDLE token)
{
    TOKEN_LINKED_TOKEN linked = {};
    DWORD size;
    char name[64];
    SetLastError(0);
    if (!GetTokenInformation(token, TokenLinkedToken, &linked, sizeof(linked), &size))
    {
        printf("%s.linked_error=%lu\n", prefix, GetLastError());
        return;
    }
    snprintf(name, sizeof(name), "%s.N1.linked", prefix); token_kind(name, linked.LinkedToken);
    snprintf(name, sizeof(name), "%s.N2.create_as_user_linked", prefix); try_create_as_user(name, linked.LinkedToken);
    snprintf(name, sizeof(name), "%s.N2.create_with_token_linked", prefix); try_create_with_token(name, linked.LinkedToken);
    snprintf(name, sizeof(name), "%s.N3.dup_primary", prefix); try_duplicate(name, linked.LinkedToken, SecurityIdentification, TokenPrimary);
    snprintf(name, sizeof(name), "%s.N3.dup_impersonation", prefix); try_duplicate(name, linked.LinkedToken, SecurityImpersonation, TokenImpersonation);
    snprintf(name, sizeof(name), "%s.N4.assign_linked", prefix); try_assign(name, linked.LinkedToken);
    snprintf(name, sizeof(name), "%s.N5.impersonate_linked", prefix); try_impersonate_open(name, linked.LinkedToken);
    CloseHandle(linked.LinkedToken);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1 && !strcmp(argv[1], "--child"))
    {
        HANDLE token;
        TOKEN_ELEVATION_TYPE type = (TOKEN_ELEVATION_TYPE)0;
        DWORD size;
        OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token);
        GetTokenInformation(token, TokenElevationType, &type, sizeof(type), &size);
        return 100 + type;
    }
    // RtlGetVersion reports the real version even without a compatibility manifest.
    typedef LONG (NTAPI *get_version_fn)(OSVERSIONINFOW *);
    get_version_fn get_version = reinterpret_cast<get_version_fn>(reinterpret_cast<void *>(
        GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlGetVersion")));
    OSVERSIONINFOW version = {};
    version.dwOSVersionInfoSize = sizeof(version);
    get_version(&version);
    printf("os=%lu.%lu.%lu arch=%s\n", version.dwMajorVersion, version.dwMinorVersion, version.dwBuildNumber,
           sizeof(void *) == 8 ? "x64" : "x86");

    HANDLE self;
    OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &self);
    token_kind("self", self);

    if (argc > 1 && !strcmp(argv[1], "--tcb"))
    {
        // N6: as SYSTEM, take the interactive user's token, then ask for its linked token.
        HANDLE user = NULL;
        DWORD session = WTSGetActiveConsoleSessionId();
        if (!WTSQueryUserToken(session, &user)) {printf("tcb.query_user_token_error=%lu\n", GetLastError()); return 1;}
        token_kind("tcb.user", user);
        probe_linked("tcb", user);
        CloseHandle(user);
        return 0;
    }

    probe_linked("user", self);

    // N2/N4 controls: an impersonation copy of our own token, a primary copy and a restricted copy.
    HANDLE copy = NULL;
    if (DuplicateTokenEx(self, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenImpersonation, &copy))
    {try_create_as_user("user.N2.create_as_user_own_impersonation", copy); CloseHandle(copy);}
    if (DuplicateTokenEx(self, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenPrimary, &copy))
    {try_assign("user.N4.assign_own_primary_copy", copy); CloseHandle(copy);}
    if (CreateRestrictedToken(self, DISABLE_MAX_PRIVILEGE, 0, NULL, 0, NULL, 0, NULL, &copy))
    {try_assign("user.N4.assign_own_restricted", copy); CloseHandle(copy);}
    return 0;
}
