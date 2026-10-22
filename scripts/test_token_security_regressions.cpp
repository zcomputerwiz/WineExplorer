// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2026 WineExplorer contributors
// Original behavioral probes. Mock helpers are test-only, never production consent.
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static unsigned int failures;
static void check(bool value, const char *name)
{
    if (!value) {printf("FAIL: %s (error %lu)\n", name, GetLastError()); ++failures;}
}

static DWORD WINAPI other_thread(void *)
{
    HANDLE token = NULL;
    BOOL result = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token);
    DWORD error = GetLastError();
    if (token) CloseHandle(token);
    return !result && error == ERROR_NO_TOKEN ? 0 : 1;
}

static bool deny_only()
{
    unsigned int before = failures;
    HANDLE original = NULL, filtered = NULL, impersonation = NULL;
    BYTE world[SECURITY_MAX_SID_SIZE];
    DWORD size = sizeof(world), needed;
    SID_AND_ATTRIBUTES disable;
    std::vector<BYTE> user_data, groups_data;
    GENERIC_MAPPING mapping = {STANDARD_RIGHTS_READ | SYNCHRONIZE,
        STANDARD_RIGHTS_WRITE | EVENT_MODIFY_STATE, STANDARD_RIGHTS_EXECUTE | SYNCHRONIZE, EVENT_ALL_ACCESS};

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &original)) return false;
    if (!CreateWellKnownSid(WinWorldSid, NULL, world, &size)) goto done;
    disable.Sid = world; disable.Attributes = 0;
    if (!CreateRestrictedToken(original, 0, 1, &disable, 0, NULL, 0, NULL, &filtered)) goto done;
    if (!DuplicateTokenEx(filtered, TOKEN_QUERY | TOKEN_IMPERSONATE, NULL, SecurityImpersonation,
                          TokenImpersonation, &impersonation)) goto done;
    needed = 0;
    GetTokenInformation(filtered, TokenGroups, NULL, 0, &needed);
    groups_data.resize(needed);
    if (!GetTokenInformation(filtered, TokenGroups, groups_data.data(), needed, &needed)) goto done;
    {
        TOKEN_GROUPS *groups = (TOKEN_GROUPS *)groups_data.data();
        bool found = false;
        for (DWORD i = 0; i < groups->GroupCount; ++i)
            if (EqualSid(world, groups->Groups[i].Sid))
                found = !(groups->Groups[i].Attributes & SE_GROUP_ENABLED)
                    && (groups->Groups[i].Attributes & SE_GROUP_USE_FOR_DENY_ONLY);
        check(found, "filter made Everyone disabled and deny-only");
    }
    needed = 0;
    GetTokenInformation(original, TokenUser, NULL, 0, &needed);
    user_data.resize(needed);
    if (!GetTokenInformation(original, TokenUser, user_data.data(), needed, &needed)) goto done;

    for (unsigned int kind = 0; kind < 3; ++kind)
    {
        union {DWORD align; BYTE bytes[1024];} acl_buffer;
        union {DWORD align; BYTE bytes[1024];} privileges;
        ACL *acl = (ACL *)acl_buffer.bytes;
        SECURITY_DESCRIPTOR sd;
        PSID user = ((TOKEN_USER *)user_data.data())->User.Sid;
        DWORD privilege_size = sizeof(privileges), granted = 0;
        BOOL allowed = TRUE, expected = kind == 2;
        WCHAR name[128];
        HANDLE event, opened;
        bool impersonating;
        InitializeAcl(acl, sizeof(acl_buffer), ACL_REVISION);
        if (kind == 0) AddAccessDeniedAce(acl, ACL_REVISION, EVENT_MODIFY_STATE, world);
        AddAccessAllowedAce(acl, ACL_REVISION, EVENT_MODIFY_STATE, kind == 1 ? (PSID)world : user);
        InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
        SetSecurityDescriptorOwner(&sd, user, FALSE);
        SetSecurityDescriptorGroup(&sd, user, FALSE);
        SetSecurityDescriptorDacl(&sd, TRUE, acl, FALSE);
        BOOL result = AccessCheck(&sd, impersonation, EVENT_MODIFY_STATE, &mapping,
            (PRIVILEGE_SET *)privileges.bytes, &privilege_size, &granted, &allowed);
        printf("acl_case=%u api_success=%d allowed=%d expected=%d granted=%#lx\n",
               kind, result, allowed, expected, granted);
        check(result && allowed == expected && granted == (expected ? EVENT_MODIFY_STATE : 0),
              "ordered deny / deny-only allow / user allow AccessCheck");
        swprintf(name, 128, L"Local\\WexTokenDeny_%lu_%llu_%u", GetCurrentProcessId(),
                 (unsigned long long)GetTickCount64(), kind);
        SECURITY_ATTRIBUTES attributes = {sizeof(attributes), &sd, FALSE};
        event = CreateEventW(&attributes, FALSE, FALSE, name);
        check(event != NULL, "create owned event");
        if (!event) continue;
        impersonating = SetThreadToken(NULL, impersonation) != FALSE;
        check(impersonating, "set restrictive thread token");
        if (impersonating)
        {
            opened = OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
            DWORD error = GetLastError();
            printf("event_case=%u opened=%d expected=%d error=%lu\n", kind, opened != NULL, expected, error);
            check(expected ? opened != NULL : !opened && error == ERROR_ACCESS_DENIED,
                  "actual event open honors deny-only group");
            if (opened) CloseHandle(opened);
            check(SetEvent(event) != FALSE, "previously granted handle stays usable");
            HANDLE thread = CreateThread(NULL, 0, other_thread, NULL, 0, NULL);
            check(thread != NULL, "create isolation thread");
            if (thread)
            {
                DWORD code = 1;
                check(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0, "isolation thread completes");
                GetExitCodeThread(thread, &code);
                check(code == 0, "other thread did not inherit impersonation");
                CloseHandle(thread);
            }
            check(RevertToSelf() != FALSE, "revert restrictive thread token");
        }
        CloseHandle(event);
    }
done:
    RevertToSelf();
    if (impersonation) CloseHandle(impersonation);
    if (filtered) CloseHandle(filtered);
    if (original) CloseHandle(original);
    return impersonation && failures == before;
}

static bool shell_helpers()
{
    // Never modify the native workstation's HKLM by accident.
    if (!getenv("WINEPREFIX") || !getenv("WEX_TOKEN_REGRESSION_TEST")) return false;
    unsigned int before = failures;
    WCHAR module[MAX_PATH], helper[MAX_PATH + 100];
    HKEY key;
    DWORD disposition, old_type = 0, old_size = 0;
    std::vector<BYTE> old_value;
    GetModuleFileNameW(NULL, module, MAX_PATH);
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Wine\\Elevation", 0, NULL, 0,
                        KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &key, &disposition)) return false;
    bool had_value = RegQueryValueExW(key, L"Helper", NULL, &old_type, NULL, &old_size) == ERROR_SUCCESS;
    if (had_value)
    {
        old_value.resize(old_size);
        if (RegQueryValueExW(key, L"Helper", NULL, &old_type, old_value.data(), &old_size))
        {RegCloseKey(key); return false;}
    }
    for (DWORD result : {DWORD(ERROR_CANCELLED), DWORD(ERROR_NOT_SUPPORTED), DWORD(0)})
    {
        WCHAR event_name[128], parameters[180];
        HANDLE release_event = NULL;
        if (!result)
        {
            swprintf(event_name, 128, L"Local\\WexElevRelease_%lu_%llu", GetCurrentProcessId(),
                     (unsigned long long)GetTickCount64());
            release_event = CreateEventW(NULL, TRUE, FALSE, event_name);
            check(release_event != NULL, "create target release event");
            swprintf(parameters, 180, L"--child-wait %ls 37", event_name);
        }
        swprintf(helper, MAX_PATH + 100, L"\"%ls\" --helper-exit %lu", module, result);
        check(RegSetValueExW(key, L"Helper", 0, REG_SZ, (BYTE *)helper,
                            (lstrlenW(helper) + 1) * sizeof(WCHAR)) == ERROR_SUCCESS, "configure test-only helper");
        SHELLEXECUTEINFOW info = {};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
        info.lpVerb = L"runas"; info.lpFile = module;
        info.lpParameters = result ? L"--child-exit 37" : parameters;
        info.nShow = SW_HIDE;
        BOOL launched = ShellExecuteExW(&info);
        DWORD error = GetLastError();
        if (result)
            check(!launched && !info.hProcess && error == result, "helper refusal/error preserved; no child handle");
        else
        {
            check(launched && info.hProcess, "mock approval returns actual child handle");
            if (info.hProcess)
            {
                DWORD code = 0;
                check(WaitForSingleObject(info.hProcess, 20) == WAIT_TIMEOUT, "target still alive, not helper handle");
                check(release_event && SetEvent(release_event), "release actual target");
                check(WaitForSingleObject(info.hProcess, 10000) == WAIT_OBJECT_0, "target exits");
                check(GetExitCodeProcess(info.hProcess, &code) && code == 37, "target exit code 37");
            }
        }
        if (info.hProcess) CloseHandle(info.hProcess);
        if (release_event) {SetEvent(release_event); CloseHandle(release_event);}
    }

    /* Negative helper configuration audit regressions:
     * wrong registry types, odd bytes, whitespace paths fail closed with accurate errors.
     * Empty string selects stock compatibility mode (HELPER_UNSET). */
    {
        DWORD bad_dw = 1;
        check(RegSetValueExW(key, L"Helper", 0, REG_DWORD, (BYTE *)&bad_dw, sizeof(bad_dw)) == ERROR_SUCCESS,
              "configure helper as REG_DWORD");
        SHELLEXECUTEINFOW info = {};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
        info.lpVerb = L"runas"; info.lpFile = module;
        info.lpParameters = L"--child-exit 37";
        info.nShow = SW_HIDE;
        SetLastError(0xdeadbeef);
        BOOL launched = ShellExecuteExW(&info);
        DWORD error = GetLastError();
        check(!launched && !info.hProcess && error == ERROR_DATATYPE_MISMATCH,
              "wrong registry type fails closed with ERROR_DATATYPE_MISMATCH and no child handle");
        if (info.hProcess) CloseHandle(info.hProcess);
    }
    {
        BYTE odd_bytes[5] = {'a', 0, 'b', 0, 'c'};
        check(RegSetValueExW(key, L"Helper", 0, REG_SZ, odd_bytes, sizeof(odd_bytes)) == ERROR_SUCCESS,
              "configure helper with odd byte length");
        SHELLEXECUTEINFOW info = {};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
        info.lpVerb = L"runas"; info.lpFile = module;
        info.lpParameters = L"--child-exit 37";
        info.nShow = SW_HIDE;
        SetLastError(0xdeadbeef);
        BOOL launched = ShellExecuteExW(&info);
        DWORD error = GetLastError();
        check(!launched && !info.hProcess && error == ERROR_INVALID_DATA,
              "odd byte length fails closed with ERROR_INVALID_DATA and no child handle");
        if (info.hProcess) CloseHandle(info.hProcess);
    }
    {
        WCHAR ws[] = L"   \t  ";
        check(RegSetValueExW(key, L"Helper", 0, REG_SZ, (BYTE *)ws, sizeof(ws)) == ERROR_SUCCESS,
              "configure helper as whitespace path");
        SHELLEXECUTEINFOW info = {};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
        info.lpVerb = L"runas"; info.lpFile = module;
        info.lpParameters = L"--child-exit 37";
        info.nShow = SW_HIDE;
        SetLastError(0xdeadbeef);
        BOOL launched = ShellExecuteExW(&info);
        DWORD error = GetLastError();
        check(!launched && !info.hProcess && error == ERROR_BAD_PATHNAME,
              "whitespace path fails closed with ERROR_BAD_PATHNAME and no child handle");
        if (info.hProcess) CloseHandle(info.hProcess);
    }
    {
        WCHAR empty[] = L"";
        check(RegSetValueExW(key, L"Helper", 0, REG_SZ, (BYTE *)empty, sizeof(empty)) == ERROR_SUCCESS,
              "configure helper as empty string (stock compatibility)");
        WCHAR event_name[128], parameters[180];
        swprintf(event_name, 128, L"Local\\WexElevRelease_%lu_%llu", GetCurrentProcessId(),
                 (unsigned long long)GetTickCount64());
        HANDLE release_event = CreateEventW(NULL, TRUE, FALSE, event_name);
        check(release_event != NULL, "create target release event for empty helper");
        swprintf(parameters, 180, L"--child-wait %ls 37", event_name);
        SHELLEXECUTEINFOW info = {};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
        info.lpVerb = L"runas"; info.lpFile = module;
        info.lpParameters = parameters;
        info.nShow = SW_HIDE;
        BOOL launched = ShellExecuteExW(&info);
        check(launched && info.hProcess, "empty helper selects stock compatibility elevation");
        if (info.hProcess)
        {
            DWORD code = 0;
            check(WaitForSingleObject(info.hProcess, 20) == WAIT_TIMEOUT, "target still alive");
            check(release_event && SetEvent(release_event), "release target");
            check(WaitForSingleObject(info.hProcess, 10000) == WAIT_OBJECT_0, "target exits");
            check(GetExitCodeProcess(info.hProcess, &code) && code == 37, "target exit code 37");
            CloseHandle(info.hProcess);
        }
        if (release_event) {SetEvent(release_event); CloseHandle(release_event);}
    }

    if (had_value) RegSetValueExW(key, L"Helper", 0, old_type, old_value.data(), old_size);
    else RegDeleteValueW(key, L"Helper");
    RegCloseKey(key);
    return failures == before;
}

// Every registry state of HKLM\Software\Wine\Elevation\Helper against a child that requests administrator
// rights, through CreateProcessW (kernelbase) and ShellExecuteExW without a verb (shell32). The process runs
// with a limited token. Unset states let the loader elevate silently, so the child reports a full token.
// Every other state must refuse with ERROR_ELEVATION_REQUIRED (CreateProcessW) or the shell32 error.
// A key whose read is denied by its DACL is not covered: Wine's registry does not enforce key DACLs for reads.
struct helper_state
{
    const char *name;
    enum {DELETE_KEY, DELETE_VALUE, STRING, BINARY, DWORD_VALUE, MOCK_HELPER} kind;
    std::wstring text;      // STRING
    DWORD type;             // BINARY
    std::vector<BYTE> data; // BINARY
    bool unset;
    DWORD shell_error;      // ShellExecuteExW error for refused states
};

static std::vector<BYTE> wide_bytes(const std::wstring &text)
{
    const BYTE *begin = (const BYTE *)text.c_str();
    return std::vector<BYTE>(begin, begin + (text.size() + 1) * sizeof(WCHAR));
}

static bool run_child(BOOL (*launch)(const WCHAR *, HANDLE *, DWORD *), const WCHAR *child, DWORD *code,
                      DWORD *error)
{
    HANDLE process = NULL;
    SetLastError(0xdeadbeef);
    BOOL launched = launch(child, &process, error);
    if (launched && process)
    {
        check(WaitForSingleObject(process, 20000) == WAIT_OBJECT_0, "manifest child exits");
        check(GetExitCodeProcess(process, code), "manifest child exit code");
        CloseHandle(process);
    }
    else check(!process, "refused launch returns no process handle");
    return launched != FALSE;
}

static BOOL launch_createprocess(const WCHAR *child, HANDLE *process, DWORD *error)
{
    STARTUPINFOW startup = {};
    PROCESS_INFORMATION info = {};
    startup.cb = sizeof(startup);
    BOOL ok = CreateProcessW(child, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &info);
    *error = GetLastError();
    if (ok) {*process = info.hProcess; CloseHandle(info.hThread);}
    return ok;
}

static BOOL launch_shellexecute(const WCHAR *child, HANDLE *process, DWORD *error)
{
    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    info.lpFile = child;
    info.nShow = SW_HIDE;
    BOOL ok = ShellExecuteExW(&info);
    *error = GetLastError();
    *process = info.hProcess;
    return ok;
}

static bool helper_states()
{
    if (!getenv("WINEPREFIX") || !getenv("WEX_TOKEN_REGRESSION_TEST")) return false;
    unsigned int before = failures;
    WCHAR module[MAX_PATH], child[MAX_PATH + 40];
    GetModuleFileNameW(NULL, module, MAX_PATH);
    WCHAR *slash = wcsrchr(module, L'\\');
    if (!slash) return false;
    slash[1] = 0;
    swprintf(child, MAX_PATH + 40, L"%lstoken-manifest-child-%ls.exe", module, sizeof(void *) == 8 ? L"x86_64" : L"i386");
    GetModuleFileNameW(NULL, module, MAX_PATH);
    check(GetFileAttributesW(child) != INVALID_FILE_ATTRIBUTES, "manifest child exists");

    HANDLE token;
    TOKEN_ELEVATION_TYPE elevation = (TOKEN_ELEVATION_TYPE)0;
    DWORD size;
    check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) &&
          GetTokenInformation(token, TokenElevationType, &elevation, sizeof(elevation), &size) &&
          elevation == TokenElevationTypeLimited, "test process has a limited token");
    CloseHandle(token);

    std::wstring mock = L"\"" + std::wstring(module) + L"\" --helper-exit 0";
    std::wstring oversized(MAX_PATH + 10, L'a');
    static const BYTE odd[5] = {'a', 0, 'b', 0, 'c'};
    DWORD one = 1;
    std::vector<helper_state> states = {
        {"absent key", helper_state::DELETE_KEY, L"", 0, {}, true, 0},
        {"absent value", helper_state::DELETE_VALUE, L"", 0, {}, true, 0},
        {"empty string", helper_state::STRING, L"", 0, {}, true, 0},
        {"zero-length REG_SZ", helper_state::BINARY, L"", REG_SZ, {}, true, 0},
        {"REG_DWORD", helper_state::DWORD_VALUE, L"", 0, std::vector<BYTE>((BYTE *)&one, (BYTE *)&one + 4), false,
         ERROR_DATATYPE_MISMATCH},
        {"odd length", helper_state::BINARY, L"", REG_SZ, std::vector<BYTE>(odd, odd + 5), false, ERROR_INVALID_DATA},
        {"whitespace only", helper_state::STRING, L"   \t  ", 0, {}, false, ERROR_BAD_PATHNAME},
        {"control character", helper_state::STRING, std::wstring(L"a\x01") + L"b", 0, {}, false, ERROR_BAD_PATHNAME},
        {"oversized", helper_state::STRING, oversized, 0, {}, false, ERROR_FILENAME_EXCED_RANGE},
        {"mock helper approves", helper_state::MOCK_HELPER, mock, 0, {}, false, 0},
    };

    HKEY key;
    DWORD old_type = 0, old_size = 0;
    std::vector<BYTE> old_value;
    bool had_key = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Wine\\Elevation", 0, KEY_READ, &key) == ERROR_SUCCESS;
    bool had_value = false;
    if (had_key)
    {
        had_value = RegQueryValueExW(key, L"Helper", NULL, &old_type, NULL, &old_size) == ERROR_SUCCESS;
        if (had_value)
        {
            old_value.resize(old_size);
            RegQueryValueExW(key, L"Helper", NULL, &old_type, old_value.data(), &old_size);
        }
        RegCloseKey(key);
    }

    for (const helper_state &state : states)
    {
        if (state.kind == helper_state::DELETE_KEY)
        {
            LSTATUS status = RegDeleteKeyW(HKEY_LOCAL_MACHINE, L"Software\\Wine\\Elevation");
            check(status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND, "delete Elevation key");
        }
        else
        {
            DWORD disposition;
            check(RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Wine\\Elevation", 0, NULL, 0,
                                  KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &key, &disposition) == ERROR_SUCCESS,
                  "create Elevation key");
            LSTATUS status = ERROR_SUCCESS;
            if (state.kind == helper_state::DELETE_VALUE)
            {
                status = RegDeleteValueW(key, L"Helper");
                if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
            }
            else if (state.kind == helper_state::STRING || state.kind == helper_state::MOCK_HELPER)
            {
                std::vector<BYTE> bytes = wide_bytes(state.text);
                status = RegSetValueExW(key, L"Helper", 0, REG_SZ, bytes.data(), bytes.size());
            }
            else if (state.kind == helper_state::DWORD_VALUE)
                status = RegSetValueExW(key, L"Helper", 0, REG_DWORD, state.data.data(), state.data.size());
            else
                status = RegSetValueExW(key, L"Helper", 0, state.type, state.data.data(), state.data.size());
            check(status == ERROR_SUCCESS, "configure helper state");
            RegCloseKey(key);
        }

        bool allowed = state.unset || state.kind == helper_state::MOCK_HELPER;
        DWORD code = 0, error = 0;
        bool launched = run_child(launch_createprocess, child, &code, &error);
        printf("helper_state %-20s CreateProcessW launched=%d error=%lu code=%lu\n", state.name, launched, error, code);
        if (allowed)
        {
            // A mock helper is only consulted by shell32; CreateProcessW still demands elevation.
            if (state.unset) check(launched && code == 100 + TokenElevationTypeFull,
                                   "unset helper: CreateProcessW starts the child with a full token");
            else check(!launched && error == ERROR_ELEVATION_REQUIRED,
                       "configured helper: CreateProcessW requires elevation");
        }
        else
            check(!launched && error == ERROR_ELEVATION_REQUIRED,
                  "invalid helper: CreateProcessW requires elevation");

        code = error = 0;
        launched = run_child(launch_shellexecute, child, &code, &error);
        printf("helper_state %-20s ShellExecuteExW launched=%d error=%lu code=%lu\n", state.name, launched, error, code);
        if (allowed)
            check(launched && code == 100 + TokenElevationTypeFull,
                  "unset or mock-approved helper: ShellExecuteExW starts the child with a full token");
        else
            check(!launched && error == state.shell_error, "invalid helper: ShellExecuteExW fails with the exact error");
    }

    if (had_key)
    {
        DWORD disposition;
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Wine\\Elevation", 0, NULL, 0,
                            KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &key, &disposition) == ERROR_SUCCESS)
        {
            if (had_value) RegSetValueExW(key, L"Helper", 0, old_type, old_value.data(), old_size);
            else RegDeleteValueW(key, L"Helper");
            RegCloseKey(key);
        }
    }
    else
    {
        RegDeleteKeyW(HKEY_LOCAL_MACHINE, L"Software\\Wine\\Elevation");
    }
    return failures == before;
}

// The linked (full) admin token must not be usable by a non-elevated process while the elevation helper is
// configured. Expected values are the ones measured natively on Windows 10.0.26300
// (docs/ELEVATION_BOUNDARY_PLAN.md, "Native results"). A suspended copy of this program is the target, and it
// reports 100 + its token elevation type (Default 1, Full 2, Limited 3).
typedef LONG (NTAPI *set_information_process_fn)(HANDLE, ULONG, void *, ULONG);
struct access_token_info {HANDLE token; HANDLE thread;};

static const LONG STATUS_BAD_IMPERSONATION_LEVEL_VALUE = (LONG)0xc00000a5;
static const LONG STATUS_PRIVILEGE_NOT_HELD_VALUE = (LONG)0xc0000061;

static set_information_process_fn get_set_information_process()
{
    return reinterpret_cast<set_information_process_fn>(reinterpret_cast<void *>(
        GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtSetInformationProcess")));
}

static bool start_suspended_self(PROCESS_INFORMATION *info)
{
    WCHAR module[MAX_PATH], command[MAX_PATH + 40];
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    GetModuleFileNameW(NULL, module, MAX_PATH);
    swprintf(command, MAX_PATH + 40, L"\"%ls\" --child-elevation", module);
    return CreateProcessW(NULL, command, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &startup, info) != FALSE;
}

static DWORD finish_child(PROCESS_INFORMATION *info)
{
    DWORD code = 0;
    ResumeThread(info->hThread);
    check(WaitForSingleObject(info->hProcess, 20000) == WAIT_OBJECT_0, "token child exits");
    GetExitCodeProcess(info->hProcess, &code);
    CloseHandle(info->hProcess);
    CloseHandle(info->hThread);
    return code;
}

// Assign a token to a suspended child with ProcessAccessToken. Returns the NTSTATUS and the child's report.
static LONG assign_to_child(HANDLE token, DWORD *child_code)
{
    PROCESS_INFORMATION info = {};
    *child_code = 0;
    if (!start_suspended_self(&info)) {check(false, "start suspended token child"); return -1;}
    access_token_info assign = {token, info.hThread};
    LONG status = get_set_information_process()(info.hProcess, 9 /* ProcessAccessToken */, &assign, sizeof(assign));
    *child_code = finish_child(&info);
    return status;
}

static DWORD create_as_user(HANDLE token, bool with_token_api, DWORD *child_code)
{
    WCHAR module[MAX_PATH], command[MAX_PATH + 40];
    STARTUPINFOW startup = {};
    PROCESS_INFORMATION info = {};
    startup.cb = sizeof(startup);
    GetModuleFileNameW(NULL, module, MAX_PATH);
    swprintf(command, MAX_PATH + 40, L"\"%ls\" --child-elevation", module);
    *child_code = 0;
    SetLastError(0xdeadbeef);
    BOOL ok = with_token_api
        ? CreateProcessWithTokenW(token, 0, NULL, command, 0, NULL, NULL, &startup, &info)
        : CreateProcessAsUserW(token, NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &info);
    DWORD error = ok ? 0 : GetLastError();
    if (ok)
    {
        check(WaitForSingleObject(info.hProcess, 20000) == WAIT_OBJECT_0, "token child exits");
        GetExitCodeProcess(info.hProcess, child_code);
        CloseHandle(info.hProcess);
        CloseHandle(info.hThread);
    }
    return error;
}

static bool linked_token_routes()
{
    if (!getenv("WINEPREFIX") || !getenv("WEX_TOKEN_REGRESSION_TEST")) return false;
    unsigned int before = failures;
    const DWORD limited = 100 + TokenElevationTypeLimited;

    HKEY key;
    DWORD disposition, old_type = 0, old_size = 0;
    std::vector<BYTE> old_value;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Wine\\Elevation", 0, NULL, 0,
                        KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &key, &disposition)) return false;
    bool had_value = RegQueryValueExW(key, L"Helper", NULL, &old_type, NULL, &old_size) == ERROR_SUCCESS;
    if (had_value)
    {
        old_value.resize(old_size);
        if (RegQueryValueExW(key, L"Helper", NULL, &old_type, old_value.data(), &old_size))
        {RegCloseKey(key); return false;}
    }
    // Any configured helper turns enforcement on; nothing here runs it.
    static const WCHAR configured[] = L"C:\\windows\\system32\\wex-test-helper-never-run.exe";
    check(RegSetValueExW(key, L"Helper", 0, REG_SZ, (const BYTE *)configured, sizeof(configured)) == ERROR_SUCCESS,
          "configure helper to turn enforcement on");

    HANDLE self, linked_handle = NULL;
    TOKEN_LINKED_TOKEN linked = {};
    TOKEN_ELEVATION_TYPE elevation = (TOKEN_ELEVATION_TYPE)0;
    DWORD size;
    check(OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &self), "open own token");
    check(GetTokenInformation(self, TokenElevationType, &elevation, sizeof(elevation), &size) &&
          elevation == TokenElevationTypeLimited, "test process has a limited token");
    check(GetTokenInformation(self, TokenLinkedToken, &linked, sizeof(linked), &size), "query the linked token");
    linked_handle = linked.LinkedToken;

    // N1: the linked token is an identification-level impersonation token.
    TOKEN_TYPE type = (TOKEN_TYPE)0;
    SECURITY_IMPERSONATION_LEVEL level = (SECURITY_IMPERSONATION_LEVEL)-1;
    check(GetTokenInformation(linked_handle, TokenType, &type, sizeof(type), &size) && type == TokenImpersonation,
          "N1 linked token is an impersonation token");
    check(GetTokenInformation(linked_handle, TokenImpersonationLevel, &level, sizeof(level), &size) &&
          level == SecurityIdentification, "N1 linked token is identification level");

    DWORD code = 0, error;

    // N2: neither process-creation API accepts it.
    error = create_as_user(linked_handle, false, &code);
    printf("linked_route CreateProcessAsUserW error=%lu code=%lu\n", error, code);
    check(error == ERROR_BAD_IMPERSONATION_LEVEL && code == 0, "N2 CreateProcessAsUserW(linked) is refused with 1346");
    error = create_as_user(linked_handle, true, &code);
    printf("linked_route CreateProcessWithTokenW error=%lu code=%lu\n", error, code);
    check(error == ERROR_PRIVILEGE_NOT_HELD && code == 0, "N2 CreateProcessWithTokenW(linked) is refused with 1314");

    // N3: it cannot be raised to a primary token.
    HANDLE copy = NULL;
    SetLastError(0xdeadbeef);
    BOOL ok = DuplicateTokenEx(linked_handle, MAXIMUM_ALLOWED, NULL, SecurityIdentification, TokenPrimary, &copy);
    error = GetLastError();
    printf("linked_route DuplicateTokenEx(primary) ok=%d error=%lu\n", ok, ok ? 0 : error);
    check(!ok && error == ERROR_BAD_IMPERSONATION_LEVEL, "N3 duplicate linked token to primary is refused with 1346");
    if (ok && copy) CloseHandle(copy);
    copy = NULL;
    SetLastError(0xdeadbeef);
    ok = DuplicateTokenEx(linked_handle, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenImpersonation, &copy);
    error = GetLastError();
    check(!ok && error == ERROR_BAD_IMPERSONATION_LEVEL, "N3 raise linked token to impersonation level is refused with 1346");
    if (ok && copy) CloseHandle(copy);

    // N4: it cannot be assigned to a process.
    LONG status = assign_to_child(linked_handle, &code);
    printf("linked_route ProcessAccessToken(linked) status=0x%08lx code=%lu\n", (unsigned long)status, code);
    check(status == STATUS_BAD_IMPERSONATION_LEVEL_VALUE && code == limited,
          "N4 assigning the linked token fails and the child stays limited");

    // The grant-admin request must also fail, aimed at a child so that a failure cannot elevate this process.
    {
        PROCESS_INFORMATION info = {};
        if (start_suspended_self(&info))
        {
            status = get_set_information_process()(info.hProcess, 1002 /* ProcessWineGrantAdminToken */, NULL, 0);
            code = finish_child(&info);
            printf("linked_route ProcessWineGrantAdminToken status=0x%08lx code=%lu\n", (unsigned long)status, code);
            check(status == STATUS_PRIVILEGE_NOT_HELD_VALUE && code == limited,
                  "ProcessWineGrantAdminToken is refused without SeTcbPrivilege and the child stays limited");
        }
        else check(false, "start suspended child for the grant-admin request");
    }

    // N5: impersonating the linked token must not open objects.
    check(SetThreadToken(NULL, linked_handle), "N5 SetThreadToken(linked) succeeds");
    {
        WCHAR path[MAX_PATH];
        GetSystemDirectoryW(path, MAX_PATH);
        lstrcatW(path, L"\\kernel32.dll");
        SetLastError(0xdeadbeef);
        HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        DWORD file_error = file == INVALID_HANDLE_VALUE ? GetLastError() : 0;
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        HANDLE process_token = NULL;
        SetLastError(0xdeadbeef);
        BOOL opened = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &process_token);
        DWORD token_error = opened ? 0 : GetLastError();
        if (process_token) CloseHandle(process_token);
        HKEY software = NULL;
        LONG reg_error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft", 0, KEY_READ, &software);
        if (software) RegCloseKey(software);
        RevertToSelf();
        printf("linked_route impersonation file=%lu process_token=%lu key=%ld\n", file_error, token_error, reg_error);
        check(file_error == ERROR_BAD_IMPERSONATION_LEVEL, "N5 opening a file while impersonating the linked token fails with 1346");
        check(token_error == ERROR_BAD_IMPERSONATION_LEVEL, "N5 opening the process token while impersonating fails with 1346");
        check(reg_error == ERROR_ACCESS_DENIED, "N5 opening a registry key while impersonating fails with 5");
    }

    // Controls that Windows accepts: the caller's own token.
    HANDLE own_impersonation = NULL, own_primary = NULL, own_restricted = NULL;
    if (DuplicateTokenEx(self, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenImpersonation, &own_impersonation))
    {
        error = create_as_user(own_impersonation, false, &code);
        check(error == 0 && code == limited, "control: own impersonation token starts a limited child");
        CloseHandle(own_impersonation);
    }
    else check(false, "control: duplicate own token as impersonation");
    if (DuplicateTokenEx(self, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenPrimary, &own_primary))
    {
        status = assign_to_child(own_primary, &code);
        check(status == 0 && code == limited, "control: own primary copy is assigned and the child stays limited");
        CloseHandle(own_primary);
    }
    else check(false, "control: duplicate own token as primary");
    if (CreateRestrictedToken(self, DISABLE_MAX_PRIVILEGE, 0, NULL, 0, NULL, 0, NULL, &own_restricted))
    {
        status = assign_to_child(own_restricted, &code);
        check(status == 0 && code == limited, "control: own restricted copy is assigned");
        CloseHandle(own_restricted);
    }
    else check(false, "control: create restricted token");

    CloseHandle(linked_handle);
    CloseHandle(self);

    // Restore the original helper value.
    if (had_value) RegSetValueExW(key, L"Helper", 0, old_type, old_value.data(), old_size);
    else RegDeleteValueW(key, L"Helper");
    RegCloseKey(key);
    return failures == before;
}

struct dialog_search {DWORD pid; HWND window;};
static BOOL CALLBACK find_dialog(HWND window, LPARAM parameter)
{
    dialog_search *search = (dialog_search *)parameter;
    DWORD pid;
    WCHAR name[32];
    GetWindowThreadProcessId(window, &pid);
    if (pid == search->pid && GetClassNameW(window, name, 32) && !wcscmp(name, L"#32770"))
    {search->window = window; return FALSE;}
    return TRUE;
}

static bool shipping_consent(const WCHAR *path)
{
    if (!getenv("WINEPREFIX") || !getenv("WEX_TOKEN_REGRESSION_TEST")) return false;
    unsigned int before = failures;
    HKEY key;
    DWORD old_type = 0, old_size = 0, disposition;
    std::vector<BYTE> old;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System", 0, NULL, 0,
            KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &key, &disposition)) return false;
    bool had = RegQueryValueExW(key, L"ConsentPromptBehaviorAdmin", NULL, &old_type, NULL, &old_size) == ERROR_SUCCESS;
    if (had)
    {
        old.resize(old_size);
        if (RegQueryValueExW(key, L"ConsentPromptBehaviorAdmin", NULL, &old_type, old.data(), &old_size))
        {RegCloseKey(key); return false;}
    }
    for (unsigned int mode = 0; mode < 8; ++mode)
    {
        bool unsupported = mode == 3 || mode >= 5;
        DWORD policy = mode == 3 ? 1 : mode == 5 ? 3 : mode == 6 ? 99 : 2;
        check(RegSetValueExW(key, L"ConsentPromptBehaviorAdmin", 0, mode == 7 ? REG_BINARY : REG_DWORD,
                            (BYTE *)&policy, sizeof(policy)) == ERROR_SUCCESS, "set isolated consent policy");
        SetEnvironmentVariableW(L"WINE_CONSENT_AUTO_APPROVE", mode == 0 ? L"1" : mode == 1 ? L"0" : NULL);
        SetEnvironmentVariableW(L"WINE_CONSENT_AUTO_DENY", L"1");
        WCHAR command[1024];
        swprintf(command, 1024, L"\"%ls\" /cmd C:\\windows\\system32\\notepad.exe", path);
        STARTUPINFOW startup = {};
        PROCESS_INFORMATION process = {};
        startup.cb = sizeof(startup);
        BOOL launched = CreateProcessW(path, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process);
        check(launched != FALSE, "launch actual shipping consent");
        if (!launched) continue;
        if (!unsupported)
        {
            dialog_search search = {process.dwProcessId, NULL};
            ULONGLONG start = GetTickCount64();
            while (!search.window && WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT
                   && GetTickCount64() - start < 8000)
            {EnumWindows(find_dialog, (LPARAM)&search); Sleep(20);}
            check(search.window && WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT,
                  "fixture and former environment switches cannot auto-approve or auto-deny");
            if (search.window)
            {
                check(LOWORD(SendMessageW(search.window, DM_GETDEFID, 0, 0)) == IDNO,
                      "shipping consent defaults to Cancel, not approval");
                GUITHREADINFO gui = {};
                gui.cbSize = sizeof(gui);
                check(GetGUIThreadInfo(GetWindowThreadProcessId(search.window, NULL), &gui)
                      && gui.hwndFocus == GetDlgItem(search.window, IDNO),
                      "keyboard focus is on Cancel, not approval");
                if (mode == 0) PostMessageW(search.window, WM_CLOSE, 0, 0);
                else PostMessageW(search.window, WM_COMMAND, mode == 4 ? IDYES : mode == 2 ? IDCANCEL : IDNO, 0);
            }
        }
        DWORD waited = WaitForSingleObject(process.hProcess, 10000), code = 0;
        check(waited == WAIT_OBJECT_0, "consent exits after cancel or unsupported credential request");
        if (waited != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 1);
        BOOL got_exit = GetExitCodeProcess(process.hProcess, &code);
        DWORD expected = unsupported ? ERROR_NOT_SUPPORTED : mode == 4 ? ERROR_SUCCESS : ERROR_CANCELLED;
        printf("consent_case=%u policy=%lu exit=%lu expected=%lu\n", mode, policy, code, expected);
        check(got_exit && code == expected,
              "actual shipping consent exit; unsupported credentials fail closed");
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
    }
    SetEnvironmentVariableW(L"WINE_CONSENT_AUTO_APPROVE", NULL);
    SetEnvironmentVariableW(L"WINE_CONSENT_AUTO_DENY", NULL);
    if (had) RegSetValueExW(key, L"ConsentPromptBehaviorAdmin", 0, old_type, old.data(), old_size);
    else RegDeleteValueW(key, L"ConsentPromptBehaviorAdmin");
    RegCloseKey(key);
    return failures == before;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    // The shell appends request metadata after a configured mock helper's args.
    if (argc >= 3 && !strcmp(argv[1], "--helper-exit")) return atoi(argv[2]);
    if (argc >= 3 && !strcmp(argv[1], "--child-exit")) {Sleep(500); return atoi(argv[2]);}
    if (argc >= 2 && !strcmp(argv[1], "--child-elevation"))
    {
        HANDLE token;
        TOKEN_ELEVATION_TYPE type = (TOKEN_ELEVATION_TYPE)0;
        DWORD size;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return 2;
        if (!GetTokenInformation(token, TokenElevationType, &type, sizeof(type), &size)) return 3;
        return 100 + (int)type;
    }
    if (argc >= 4 && !strcmp(argv[1], "--child-wait"))
    {
        WCHAR name[128];
        if (!MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, name, 128)) return 2;
        HANDLE event = OpenEventW(SYNCHRONIZE, FALSE, name);
        if (!event) return 2;
        DWORD waited = WaitForSingleObject(event, 10000);
        CloseHandle(event);
        return waited == WAIT_OBJECT_0 ? atoi(argv[3]) : 3;
    }
    if (argc >= 2 && !strcmp(argv[1], "--hold"))
    {
        puts("fixture_ready");
        for (unsigned int i = 0; i < 600; ++i) {Sleep(500); puts("fixture_heartbeat");}
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--consent"))
    {
        WCHAR path[MAX_PATH];
        if (!MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, path, MAX_PATH)) return 2;
        bool consent = shipping_consent(path);
        printf("shipping_consent_has_no_approval_bypass=%s\n", consent ? "PASS" : "FAIL");
        return consent ? 0 : 1;
    }
    bool token = deny_only();
    printf("deny_only_acl_and_object_open=%s\n", token ? "PASS" : "FAIL");
    if (argc == 2 && !strcmp(argv[1], "--shell-tests"))
    {
        bool shell = shell_helpers();
        printf("elevation_helper_errors_and_child_handle=%s\n", shell ? "PASS" : "FAIL");
        bool states = helper_states();
        printf("elevation_helper_states_manifest_child=%s\n", states ? "PASS" : "FAIL");
        bool routes = linked_token_routes();
        printf("linked_token_routes_closed=%s\n", routes ? "PASS" : "FAIL");
        return token && shell && states && routes ? 0 : 1;
    }
    return token ? 0 : 1;
}
