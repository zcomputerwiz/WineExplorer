/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Included only by the standalone S13 lab probe, never shipping code.
 */
#include <userenv.h>
#include <wchar.h>
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#include "../wine/include/wine/wex_profile.h"
typedef NTSTATUS (WINAPI *hku_fn)(ULONG,HANDLE,HANDLE,ACCESS_MASK,const void*,ULONG,HANDLE*,ULONG*);
static BOOL e1s13_loaded;
static unsigned int e1s13_loads;
static char e1s13_bob_denied_path[512];
static HANDLE e1s14_stale;
static HANDLE e1s14_cached_imp;
static ULONGLONG e1s14_previous_generation;

typedef NTSTATUS (WINAPI *hku_ex_fn)(ULONG,HANDLE,HANDLE,ACCESS_MASK,const void*,ULONG,HANDLE*,ULONG*,ULONGLONG*);

static void e1s14_stale_check(void)
{
    typedef NTSTATUS (WINAPI *query_fn)(HANDLE,ULONG,void*,ULONG,ULONG*);
    query_fn query = (query_fn)(void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryKey");
    BYTE data[1024]; ULONG size;
    if (!e1s14_stale) return;
    last_status = query ? query(e1s14_stale, 3, data, sizeof(data), &size) : (NTSTATUS)0xc00000bb;
    check(last_status == (NTSTATUS)0xc000017c); /* STATUS_KEY_DELETED, including after reload */
}

/* Runs in the SYSTEM broker in the machine prefix, on the shared server.
 * Registry APIs and a same-server suspended child exercise lifetime. Native
 * persistence/recovery checks run in the shell against Bob's profile path. */
static void e1s14_lifecycle( HANDLE token, HANDLE imp, const char *mode )
{
    hku_fn load = (hku_fn)(void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtLoadWexProfile");
    hku_ex_fn load_ex = (hku_ex_fn)(void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtLoadWexProfileEx");
    HANDLE unused = NULL, proxy = NULL;
    HKEY child = NULL;
    ULONG managed = 0; ULONGLONG before = 0, after = 0;
    DWORD marker = 0x14a11ce, got = 0, length = sizeof(got), start;
    NTSTATUS status;
    STARTUPINFOA si = {0}; PROCESS_INFORMATION pi = {0};
    char exe[MAX_PATH], command[2 * MAX_PATH];
    typedef BOOL (WINAPI *profile_fn)(HANDLE,LPPROFILEINFOA);
    typedef BOOL (WINAPI *unload_fn)(HANDLE,HANDLE);
    typedef ULONG (WINAPI *error_fn)(NTSTATUS);
    HMODULE userenv = NULL;
    profile_fn profile = NULL; unload_fn unload = NULL;
    error_fn status_error = NULL;
    PROFILEINFOA profile_info = {0};
    check(load && load_ex && imp);
    if (!load || !load_ex || !imp) return;
    e1s14_stale_check();
    if (e1s14_stale) { CloseHandle(e1s14_stale); e1s14_stale = NULL; }
    check(!load_ex(WEX_HKU_QUERY,NULL,token,0,NULL,0,&unused,&managed,&before) && managed && before);
    check(before > e1s14_previous_generation);
    e1s14_previous_generation = before;
    if (e1s14_cached_imp)
    {
        /* Same effective token as the preceding load: only the hive generation
         * changed. This exercises kernelbase's generation key, not TokenId. */
        check(SetThreadToken(NULL,e1s14_cached_imp));
        check(!RegOpenKeyExA(HKEY_CURRENT_USER,"Software\\Wine\\E1S14",0,KEY_READ,&child));
        if (child)
        {
            check(!RegQueryValueExA(child,"Persisted",NULL,NULL,(BYTE *)&got,&length) && got == marker);
            RegCloseKey(child); child = NULL;
        }
        check(RevertToSelf());
        CloseHandle(e1s14_cached_imp); e1s14_cached_imp = NULL;
    }
    check(DuplicateHandle(GetCurrentProcess(),imp,GetCurrentProcess(),&e1s14_cached_imp,0,FALSE,DUPLICATE_SAME_ACCESS));
    check(!load_ex(WEX_HKU_CACHE_CURRENT,NULL,token,KEY_READ,NULL,0,&proxy,&managed,&after) && proxy && after == before);
    check(SetThreadToken(NULL, imp));
    check(!RegCreateKeyExA(HKEY_CURRENT_USER,"Software\\Wine\\E1S14",0,NULL,0,KEY_ALL_ACCESS,NULL,&child,NULL));
    if (child)
    {
        if (e1s13_loads) check(!RegQueryValueExA(child,"Persisted",NULL,NULL,(BYTE *)&got,&length) && got == marker);
        check(!RegSetValueExA(child,"Persisted",0,REG_DWORD,(BYTE *)&marker,sizeof(marker)));
        RegCloseKey(child); child = NULL;
    }
    /* Warm both predefined caches while impersonating, then revert. Their
     * proxy handles must neither pin this hive nor survive its generation. */
    check(!RegOpenKeyExA(HKEY_CLASSES_ROOT,"E1S13Merge",0,KEY_READ,&child));
    if (child) { RegCloseKey(child); child = NULL; }
    check(RevertToSelf());
    if (e1s13_loads && !strcmp(mode,"unload"))
    {
        userenv = LoadLibraryA("userenv.dll");
        profile = userenv ? (profile_fn)(void *)GetProcAddress(userenv,"LoadUserProfileA") : NULL;
        unload = userenv ? (unload_fn)(void *)GetProcAddress(userenv,"UnloadUserProfile") : NULL;
        status_error = (error_fn)(void *)GetProcAddress(GetModuleHandleA("ntdll.dll"),"RtlNtStatusToDosError");
        profile_info.dwSize = sizeof(profile_info); profile_info.lpUserName = (char *)"ignored-label";
        check(profile && unload && status_error && profile(token,&profile_info) && profile_info.hProfile);
    }
    if (!strncmp(mode,"crash-",6))
    {
        ULONG crash = strtoul(mode + 6, NULL, 10);
        LSTATUS flush_status;
        if (failures) return;
        check(!load(WEX_HKU_OPEN,NULL,token,KEY_READ,NULL,0,(HANDLE *)&child,&managed) && child && managed == 1);
        if (failures) { if (child) RegCloseKey(child); return; }
        check(!load(WEX_HKU_TEST_CRASH,NULL,token,crash,NULL,0,&unused,&managed) && !unused && managed == 1);
        if (failures) { RegCloseKey(child); return; }
        printf("E1S14 CRASH ARMED %lu\n", (unsigned long)crash); fflush(stdout);
        /* Flush saves synchronously with the peer still waiting in ReadFile;
         * it has no unload last-user gate. Server EOF may abort this thread
         * with status 0 instead of returning an error. The native parent must
         * prove the server's _exit(86), independently of either Wine client. */
        flush_status = RegFlushKey(child);
        printf("E1S14 FLUSH RETURNED status=%ld\n", (long)flush_status); fflush(stdout);
        check(FALSE); /* any returned flush, including an error, is a failure */
        RegCloseKey(child);
        return;
    }
    /* Hold a deleted descendant; walking only linked keys would miss it. */
    check(SetThreadToken(NULL, imp));
    check(!RegCreateKeyExA(HKEY_CURRENT_USER,"Software\\Wine\\E1S14Busy",0,NULL,0,KEY_ALL_ACCESS,NULL,&child,NULL));
    check(RevertToSelf());
    status = load(WEX_HKU_UNLOAD_S14,NULL,token,0,NULL,0,&unused,&managed);
    check(status == (NTSTATUS)0xc0000121 && !unused && !managed);
    check(SetThreadToken(NULL, imp));
    if (child) check(!RegDeleteKeyA(HKEY_CURRENT_USER,"Software\\Wine\\E1S14Busy"));
    check(RevertToSelf());
    status = load(WEX_HKU_UNLOAD_S14,NULL,token,0,NULL,0,&unused,&managed);
    check(status == (NTSTATUS)0xc0000121 && !unused && !managed);
    if (child) { RegCloseKey(child); child = NULL; }
    /* This driver intentionally holds no key handle. The live-process check
     * runs first and has a distinct status from the descendant-handle check. */
    check(GetModuleFileNameA(NULL, exe, sizeof(exe)) != 0);
    snprintf(command,sizeof(command),"\"%s\" hku-hold",exe);
    si.cb = sizeof(si);
    check(CreateProcessAsUserA(token,exe,command,NULL,NULL,FALSE,CREATE_SUSPENDED | CREATE_NO_WINDOW,NULL,NULL,&si,&pi));
    if (pi.hProcess)
    {
        check(!load(WEX_HKU_TEST_TRANSFER,pi.hProcess,token,0,NULL,0,&unused,&managed) && managed == 1);
        status = load(WEX_HKU_UNLOAD_S14,NULL,token,0,NULL,0,&unused,&managed);
        check(status == (NTSTATUS)0xc0000708 && !unused && !managed);
        check(TerminateProcess(pi.hProcess,0));
        check(WaitForSingleObject(pi.hProcess,5000) == WAIT_OBJECT_0);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    }
    start = GetTickCount();
    do
    {
        if (e1s13_loads)
        {
            BOOL ok = unload && profile_info.hProfile && unload(token,profile_info.hProfile);
            DWORD error = GetLastError();
            if (ok) { status = 0; unused = NULL; managed = 1; profile_info.hProfile = NULL; }
            else if (status_error && error == status_error((NTSTATUS)0xc0000708)) status = (NTSTATUS)0xc0000708;
            else { printf("E1S14 UnloadUserProfile error=%lu\n",(unsigned long)error); status = (NTSTATUS)0xc0000001; }
        }
        else status = load(WEX_HKU_UNLOAD_S14,NULL,token,0,NULL,0,&unused,&managed);
        if (status != (NTSTATUS)0xc0000708) break; /* final native death can lag the process signal */
        Sleep(10);
    } while (GetTickCount() - start < 5000);
    check(!status && !unused && managed == 1);
    if (!status) e1s13_loaded = FALSE;
    check(!load_ex(WEX_HKU_QUERY,NULL,token,0,NULL,0,&unused,&managed,&after) && managed && !after);
    e1s14_stale = proxy;
    e1s14_stale_check();
    check(SetThreadToken(NULL, imp));
    check(RegOpenKeyExA(HKEY_CURRENT_USER,"Software\\Wine\\E1S14",0,KEY_READ,&child) != ERROR_SUCCESS && !child);
    check(RevertToSelf());
    status = load(WEX_HKU_UNLOAD_S14,NULL,token,0,NULL,0,&unused,&managed);
    check(status == (NTSTATUS)0xc000003a && !unused && !managed);
    if (profile_info.hProfile) RegCloseKey(profile_info.hProfile);
    if (userenv) FreeLibrary(userenv);
    if (!failures) { puts("E1S14 UNLOAD OK"); fflush(stdout); }
}

static hku_fn hku_export(void)
{
    hku_fn fn = (hku_fn)(void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtLoadWexProfile");
    check(fn != NULL);
    return fn;
}

static void e1s13_key_path( HKEY key, const char *sid )
{
    struct { ULONG bytes; WCHAR name[512]; } value = {0};
    WCHAR expected[256], sidW[128];
    ULONG size;
    typedef NTSTATUS (WINAPI *query_fn)(HANDLE,ULONG,void*,ULONG,ULONG*);
    query_fn query = (query_fn)(void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryKey");
    check(MultiByteToWideChar(CP_UTF8, 0, sid, -1, sidW, ARRAY_SIZE(sidW)) != 0);
    swprintf(expected, ARRAY_SIZE(expected), L"\\Registry\\User\\%ls", sidW);
    last_nt_call = "NtQueryKey(KeyNameInformation)";
    last_status = query ? query(key, 3, &value, sizeof(value), &size) : (NTSTATUS)0xc00000bb;
    check(query && !last_status);
    if (last_status)
    {
        printf("E1S13 key=%p NtQueryKey status=%08lx\n", key, (unsigned long)last_status);
        fflush(stdout);
        return;
    }
    if (value.bytes < sizeof(value.name) && !(value.bytes % sizeof(WCHAR)))
    { value.name[value.bytes / sizeof(WCHAR)] = 0; check(!_wcsicmp(value.name, expected)); }
    else check(FALSE);
}

static void e1s13_descriptor( HKEY key, PSID expected )
{
    BYTE buffer[1024] = {0}, system[SECURITY_MAX_SID_SIZE], admins[SECURITY_MAX_SID_SIZE];
    DWORD size = sizeof(buffer), i, count = 0, length = sizeof(system);
    PACL acl = NULL; PSID owner = NULL;
    BOOL present, def;
    check(CreateWellKnownSid(WinLocalSystemSid, NULL, system, &length));
    length = sizeof(admins); check(CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, admins, &length));
    check(!RegGetKeySecurity(key, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, buffer, &size));
    check(GetSecurityDescriptorOwner(buffer, &owner, &def) && owner && EqualSid(owner, expected));
    check(GetSecurityDescriptorDacl(buffer, &present, &acl, &def) && present && acl && acl->AceCount == 3);
    if (!acl) return;
    for (i = 0; i < acl->AceCount; i++)
    {
        ACCESS_ALLOWED_ACE *ace = NULL;
        check(GetAce(acl, i, (void **)&ace));
        if (!ace) continue;
        check(ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
              (ace->Header.AceFlags & CONTAINER_INHERIT_ACE) && ace->Mask == KEY_ALL_ACCESS);
        if (EqualSid(&ace->SidStart, expected)) count |= 1;
        else if (EqualSid(&ace->SidStart, system)) count |= 2;
        else if (EqualSid(&ace->SidStart, admins)) count |= 4;
        else check(FALSE);
    }
    check(count == 7);
}

static void e1s13_current_user( REGSAM access, const char *sid )
{
    HKEY key = NULL;
    LSTATUS status = RegOpenCurrentUser(access, &key);
    printf("E1S13 RegOpenCurrentUser access=%08lx status=%ld key=%p\n",
           (unsigned long)access, (long)status, key);
    fflush(stdout);
    check(!status && key && key != HKEY_CURRENT_USER);
    if (!status && key && key != HKEY_CURRENT_USER) e1s13_key_path(key, sid);
    if (key) RegCloseKey(key);
}

static void e1s13_identity( const char *sid )
{
    e1s13_current_user(KEY_READ, sid);
}

static void e1s13_peer_state( BOOL after )
{
    static char *sid;
    BYTE user[256]; HANDLE token = NULL; HKEY key = NULL;
    DWORD marker = 0x13b0b, size = sizeof(marker), got = 0;
    /* The regression starts with a primary token, without impersonation. */
    check(!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, FALSE, &token) && GetLastError() == ERROR_NO_TOKEN);
    if (token) { CloseHandle(token); token = NULL; }
    if (!after)
    {
        check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token));
        check(token && info(token, TokenUser, user, sizeof(user)) && ConvertSidToStringSidA(((TOKEN_USER *)user)->User.Sid, &sid));
        if (token) { CloseHandle(token); token = NULL; }
    }
    if (sid)
    {
        e1s13_current_user(KEY_READ, sid);
        e1s13_current_user(KEY_ALL_ACCESS, sid);
    }
    check(!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, FALSE, &token) && GetLastError() == ERROR_NO_TOKEN);
    if (token) { CloseHandle(token); token = NULL; }
    if (!after)
    {
        BOOL impersonated = ImpersonateSelf(SecurityIdentification);
        check(impersonated);
        if (impersonated)
        {
            SECURITY_IMPERSONATION_LEVEL level = SecurityAnonymous;
            LSTATUS status = RegOpenCurrentUser(KEY_READ, &key);
            check((status == ERROR_BAD_IMPERSONATION_LEVEL || status == ERROR_ACCESS_DENIED) && !key);
            if (key) { RegCloseKey(key); key = NULL; }
            /* A failed open must leave the caller's impersonation intact. */
            check(OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &token));
            check(token && info(token, TokenImpersonationLevel, &level, sizeof(level)) && level == SecurityIdentification);
            if (token) { CloseHandle(token); token = NULL; }
            check(RevertToSelf());
        }
    }
    check(!RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Wine\\E1S13Bob", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &key, NULL));
    if (key)
    {
        if (!after) check(!RegSetValueExA(key, "Marker", 0, REG_DWORD, (BYTE *)&marker, sizeof(marker)));
        check(!RegQueryValueExA(key, "Marker", NULL, NULL, (BYTE *)&got, &size) && got == marker);
        RegCloseKey(key);
    }
    {
        typedef BOOL (WINAPI *get_prof_fn)(HANDLE, LPWSTR, LPDWORD);
        HMODULE hUserenv = LoadLibraryA("userenv.dll");
        if (hUserenv)
        {
            get_prof_fn pGetUserProfileDirectoryW = (get_prof_fn)(void *)GetProcAddress(hUserenv, "GetUserProfileDirectoryW");
            if (pGetUserProfileDirectoryW)
            {
                WCHAR dir[MAX_PATH];
                DWORD dsize = ARRAY_SIZE(dir);
                HANDLE proc_tok = NULL;
                check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &proc_tok) && proc_tok);
                check(pGetUserProfileDirectoryW(proc_tok, dir, &dsize));
                if (proc_tok) CloseHandle(proc_tok);
                check(wcsstr(dir, L".e1s") == NULL);
            }
            FreeLibrary(hUserenv);
        }
    }
    if (after) { LocalFree(sid); sid = NULL; }
    if (after && e1s13_bob_denied_path[0])
    {
        HANDLE file = CreateFileA(e1s13_bob_denied_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  NULL, OPEN_EXISTING, 0, NULL);
        check(file == INVALID_HANDLE_VALUE && GetLastError() == ERROR_ACCESS_DENIED);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    }
}

static void e1s13_denied( BOOL normal )
{
    hku_fn load = hku_export();
    HANDLE token = NULL, key;
    ULONG op, managed; NTSTATUS status;
    BYTE record[WEX_HKU_RECORD_SIZE] = {0};
    check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token));
    if (!load || !token) return;
    for (op = WEX_HKU_LOAD; op <= WEX_HKU_TEST_TRANSFER; op++)
    {
        key = (HANDLE)1; managed = ~0u;
        status = load(op, NULL, token, op == WEX_HKU_QUERY ? 0 : KEY_READ,
            op == WEX_HKU_LOAD ? record : NULL, op == WEX_HKU_LOAD ? sizeof(record) : 0, &key, &managed);
        if (normal) check(status == (NTSTATUS)0xc00000bb);
        else if (op == WEX_HKU_QUERY) check(!status);
        else check(status != 0);
        check(!key && !managed);
        if (key) CloseHandle(key);
    }
    CloseHandle(token);
}

static int e1s16_descendant_child( int argc, char **argv )
{
    if (argc >= 3 && !strcmp(argv[2], "child"))
    {
        HANDLE tok = NULL;
        TOKEN_ELEVATION_TYPE elev = 0;
        char exe_c[MAX_PATH], cmd_c[2 * MAX_PATH];
        STARTUPINFOA si_c = {0}; PROCESS_INFORMATION pi_c = {0};
        HANDLE hCR = NULL, hCE = NULL;

        check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok) && tok);
        if (tok)
        {
            check(info(tok, TokenElevationType, &elev, sizeof(elev)) && elev == TokenElevationTypeFull);
            CloseHandle(tok);
        }

        check(GetModuleFileNameA(NULL, exe_c, sizeof(exe_c)) != 0);
        snprintf(cmd_c, sizeof(cmd_c), "\"%s\" hku-descendant grandchild", exe_c);
        si_c.cb = sizeof(si_c);
        check(CreateProcessA(exe_c, cmd_c, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si_c, &pi_c));

        hCR = OpenEventA(EVENT_MODIFY_STATE, FALSE, "E1S16_ChildReady");
        check(hCR != NULL);
        if (hCR) { SetEvent(hCR); CloseHandle(hCR); }

        hCE = OpenEventA(SYNCHRONIZE, FALSE, "E1S16_ChildExit");
        check(hCE != NULL);
        if (hCE) { WaitForSingleObject(hCE, 15000); CloseHandle(hCE); }

        if (pi_c.hProcess)
        {
            CloseHandle(pi_c.hProcess);
            CloseHandle(pi_c.hThread);
        }
        return failures ? 1 : 0;
    }
    else if (argc >= 3 && !strcmp(argv[2], "grandchild"))
    {
        HANDLE tok = NULL;
        TOKEN_ELEVATION_TYPE elev = 0;
        HANDLE hGR = NULL, hGE = NULL;

        check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok) && tok);
        if (tok)
        {
            check(info(tok, TokenElevationType, &elev, sizeof(elev)) && elev == TokenElevationTypeFull);
            CloseHandle(tok);
        }

        hGR = OpenEventA(EVENT_MODIFY_STATE, FALSE, "E1S16_GrandReady");
        check(hGR != NULL);
        if (hGR) { SetEvent(hGR); CloseHandle(hGR); }

        hGE = OpenEventA(SYNCHRONIZE, FALSE, "E1S16_GrandExit");
        check(hGE != NULL);
        if (hGE) { WaitForSingleObject(hGE, 15000); CloseHandle(hGE); }

        return failures ? 1 : 0;
    }
    return 2;
}

static int e1s16_handle_held_child( int argc, char **argv )
{
    DWORD broker_pid;
    HKEY hDescendant = NULL;
    char pipe_name[MAX_PATH];
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    DWORD written = 0, read_bytes = 0;
    ULONG_PTR val = 0;
    BYTE ack = 0;

    if (argc < 3) return 2;
    broker_pid = strtoul(argv[2], NULL, 10);

    check(!RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Wine\\DescendantHeldKey\\Sub1\\Sub2", 0, NULL, 0,
                           KEY_ALL_ACCESS, NULL, &hDescendant, NULL));
    check(hDescendant != NULL);

    snprintf(pipe_name, sizeof(pipe_name), "\\\\.\\pipe\\e1s16-held-%lu", (unsigned long)broker_pid);
    WaitNamedPipeA(pipe_name, 5000);
    hPipe = CreateFileA(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    check(hPipe != INVALID_HANDLE_VALUE);
    if (hPipe != INVALID_HANDLE_VALUE)
    {
        val = (ULONG_PTR)hDescendant;
        check(WriteFile(hPipe, &val, sizeof(val), &written, NULL) && written == sizeof(val));
        ReadFile(hPipe, &ack, 1, &read_bytes, NULL);
        CloseHandle(hPipe);
    }
    if (hDescendant) RegCloseKey(hDescendant);
    return failures ? 1 : 0;
}

static int e1s16_host_uid_child( int argc, char **argv )
{
    unsigned long exp_uid, exp_gid;
    HANDLE tok = NULL;
    BYTE user[256];
    TOKEN_ELEVATION_TYPE elev = 0;
    char *str_sid = NULL;
    HANDLE fStatus = INVALID_HANDLE_VALUE;
    HANDLE hAliceFile = INVALID_HANDLE_VALUE;
    HANDLE hBobFile = INVALID_HANDLE_VALUE;
    const char *adm_name = (argc >= 5) ? argv[4] : "wexadm";
    const char *std_name = (argc >= 6) ? argv[5] : "wexstd";
    char alice_path[MAX_PATH], bob_path[MAX_PATH];

    if (argc < 4) return 2;
    exp_uid = strtoul(argv[2], NULL, 10);
    exp_gid = strtoul(argv[3], NULL, 10);

    check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok) && tok);
    if (tok)
    {
        check(info(tok, TokenElevationType, &elev, sizeof(elev)) && elev == TokenElevationTypeFull);
        check(info(tok, TokenUser, user, sizeof(user)));
        if (ConvertSidToStringSidA(((TOKEN_USER *)user)->User.Sid, &str_sid))
        {
            check(strstr(str_sid, "S-1-5-21-") != NULL);
            LocalFree(str_sid);
        }
        CloseHandle(tok);
    }

    fStatus = CreateFileA("Z:\\proc\\self\\status", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    check(fStatus != INVALID_HANDLE_VALUE);
    if (fStatus != INVALID_HANDLE_VALUE)
    {
        char buf[4096] = {0};
        DWORD r = 0;
        check(ReadFile(fStatus, buf, sizeof(buf) - 1, &r, NULL));
        CloseHandle(fStatus);
        char *pUid = strstr(buf, "Uid:\t");
        check(pUid != NULL);
        if (pUid)
        {
            unsigned long r_uid = 0, e_uid = 0, s_uid = 0;
            sscanf(pUid + 5, "%lu\t%lu\t%lu", &r_uid, &e_uid, &s_uid);
            check(r_uid == exp_uid);
            check(e_uid == exp_uid);
            check(s_uid == exp_uid);
        }
        char *pGid = strstr(buf, "Gid:\t");
        check(pGid != NULL);
        if (pGid)
        {
            unsigned long r_gid = 0, e_gid = 0, s_gid = 0;
            sscanf(pGid + 5, "%lu\t%lu\t%lu", &r_gid, &e_gid, &s_gid);
            check(r_gid == exp_gid);
        }
    }

    snprintf(alice_path, sizeof(alice_path), "Z:\\home\\%s\\s16-alice-secret.txt", adm_name);
    hAliceFile = CreateFileA(alice_path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    check(hAliceFile == INVALID_HANDLE_VALUE && (GetLastError() == ERROR_ACCESS_DENIED || GetLastError() == ERROR_PATH_NOT_FOUND || GetLastError() == ERROR_FILE_NOT_FOUND));
    if (hAliceFile != INVALID_HANDLE_VALUE) CloseHandle(hAliceFile);

    snprintf(bob_path, sizeof(bob_path), "Z:\\home\\%s\\s16-bob-data.txt", std_name);
    hBobFile = CreateFileA(bob_path, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    check(hBobFile != INVALID_HANDLE_VALUE);
    if (hBobFile != INVALID_HANDLE_VALUE)
    {
        char bdata[64] = {0}; DWORD br = 0;
        check(ReadFile(hBobFile, bdata, sizeof(bdata) - 1, &br, NULL));
        check(strstr(bdata, "bob data") != NULL);
        CloseHandle(hBobFile);
    }

    /* In a Wine PE environment, a process with Alice's Windows token retains Bob's
     * host Linux credentials (/proc/self/status Uid/Gid above), and raw Linux
     * syscalls from PE binaries cannot be used to act as another host user. */

    if (!failures) { puts("E1S16 HOST-UID CHILD OK"); fflush(stdout); }
    return failures ? 1 : 0;
}

static void e1s16_two_admins( HANDLE alice_token, HANDLE alice_imp, HANDLE process,
                             const BYTE *in, const char *car_spec )
{
    hku_fn load = hku_export();
    hku_ex_fn load_ex = (hku_ex_fn)(void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtLoadWexProfileEx");
    char car_name[64] = {0}, car_key_hex[64] = {0}, car_sid[128] = {0}, car_gen_str[32] = {0};
    HANDLE car_context = NULL, car_token = NULL, car_imp = NULL, car_key = NULL;
    HANDLE unused = NULL;
    ULONG car_managed = 0, car_size = 0, managed = 0;
    ULONGLONG car_caps = WEX_TOKEN_CAPS, gen = 0;
    BYTE car_in[WEX_TOKEN_FIXTURE_SIZE] = {0};
    BYTE car_out[WEX_TOKEN_RESPONSE_SIZE] = {0};
    BYTE car_record[WEX_HKU_RECORD_SIZE] = {0};
    NTSTATUS status;
    HKEY hAlice = NULL, hCarol = NULL, hFail = NULL;
    DWORD valA = 0xa11ce, valC = 0xca01, got = 0, len = sizeof(got);
    const char *c0, *c1, *c2, *c3, *c4;

    check(load && load_ex && alice_token && alice_imp && process && car_spec);
    if (!load || !load_ex || !alice_token || !alice_imp || !process || !car_spec) return;

    c0 = strchr(car_spec, ':');
    c1 = c0 ? strchr(c0 + 1, ':') : NULL;
    c2 = c1 ? strchr(c1 + 1, ':') : NULL;
    c3 = c2 ? strchr(c2 + 1, ':') : NULL;
    c4 = c3 ? strchr(c3 + 1, ':') : NULL;
    if (!c0 || !c1 || !c2 || !c3 || !c4) { check(FALSE); return; }
    memcpy(car_name, c1 + 1, c2 - (c1 + 1));
    memcpy(car_key_hex, c2 + 1, c3 - (c2 + 1));
    memcpy(car_sid, c3 + 1, c4 - (c3 + 1));
    strncpy(car_gen_str, c4 + 1, sizeof(car_gen_str) - 1);

    memcpy(car_in, in, WEX_TOKEN_FIXTURE_SIZE);
    wex_token_put64(car_in + 16, 200 + e1s13_loads);
    check(unhex(car_key_hex, car_in + 124));
    memset(car_in + 204, 0, 64);
    memcpy(car_in + 204, car_name, strlen(car_name));

    status = create_token(WEX_TOKEN_NEGOTIATE, &car_context, NULL, &car_caps, NULL, 0, &car_token, NULL, &car_size);
    check(!status && car_context && !car_token);

    status = create_token(WEX_TOKEN_TEST_INSTALL, &car_context, process, &car_caps, car_in, sizeof(car_in), &car_token, NULL, &car_size);
    check(!status && !car_token);

    car_size = sizeof(car_out);
    status = create_token(WEX_TOKEN_ISSUE, &car_context, NULL, &car_caps, car_in, WEX_TOKEN_INPUT_SIZE, &car_token, car_out, &car_size);
    check(!status && car_token && car_size == sizeof(car_out));
    if (!car_token) { if (car_context) CloseHandle(car_context); return; }

    wex_token_put32(car_record, 1);
    wex_token_put32(car_record + 4, sizeof(car_record));
    memcpy(car_record + 8, car_in + 124, 16);
    memcpy(car_record + 24, car_in + 164, 8);
    wex_token_put64(car_record + 32, _strtoui64(car_gen_str, NULL, 10));

    status = load(WEX_HKU_LOAD, car_context, car_token, KEY_ALL_ACCESS, car_record, sizeof(car_record), &car_key, &car_managed);
    check(!status && car_key && car_managed == 1);
    if (car_key) RegCloseKey(car_key);

    check(DuplicateTokenEx(car_token, TOKEN_QUERY | TOKEN_IMPERSONATE, NULL, SecurityImpersonation, TokenImpersonation, &car_imp));

    check(SetThreadToken(NULL, alice_imp));
    check(!RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Wine\\TwoAdminsAlice", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hAlice, NULL));
    if (hAlice)
    {
        check(!RegSetValueExA(hAlice, "AliceVal", 0, REG_DWORD, (BYTE *)&valA, sizeof(valA)));
        RegCloseKey(hAlice);
    }
    check(RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Wine\\TwoAdminsCarol", 0, KEY_READ, &hFail) == ERROR_FILE_NOT_FOUND && !hFail);
    check(RevertToSelf());

    check(SetThreadToken(NULL, car_imp));
    check(RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Wine\\TwoAdminsAlice", 0, KEY_READ, &hFail) == ERROR_FILE_NOT_FOUND && !hFail);
    check(!RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Wine\\TwoAdminsCarol", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hCarol, NULL));
    if (hCarol)
    {
        check(!RegSetValueExA(hCarol, "CarolVal", 0, REG_DWORD, (BYTE *)&valC, sizeof(valC)));
        RegCloseKey(hCarol);
    }
    check(RevertToSelf());

    check(SetThreadToken(NULL, alice_imp));
    check(!RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Wine\\TwoAdminsAlice", 0, KEY_READ, &hAlice));
    if (hAlice)
    {
        len = sizeof(got); got = 0;
        check(!RegQueryValueExA(hAlice, "AliceVal", NULL, NULL, (BYTE *)&got, &len) && got == valA);
        RegCloseKey(hAlice);
    }
    check(RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Wine\\TwoAdminsCarol", 0, KEY_READ, &hFail) == ERROR_FILE_NOT_FOUND && !hFail);
    check(RevertToSelf());

    status = load(WEX_HKU_UNLOAD_S14, NULL, alice_token, 0, NULL, 0, &unused, &managed);
    check(!status && managed == 1);
    check(!load_ex(WEX_HKU_QUERY, NULL, alice_token, 0, NULL, 0, &unused, &managed, &gen) && managed && !gen);
    check(!load_ex(WEX_HKU_QUERY, NULL, car_token, 0, NULL, 0, &unused, &managed, &gen) && managed && gen > 0);

    check(SetThreadToken(NULL, car_imp));
    check(!RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Wine\\TwoAdminsCarol", 0, KEY_READ, &hCarol));
    if (hCarol)
    {
        len = sizeof(got); got = 0;
        check(!RegQueryValueExA(hCarol, "CarolVal", NULL, NULL, (BYTE *)&got, &len) && got == valC);
        RegCloseKey(hCarol);
    }
    check(RevertToSelf());

    status = load(WEX_HKU_UNLOAD_S14, NULL, car_token, 0, NULL, 0, &unused, &managed);
    check(!status && managed == 1);
    check(!load_ex(WEX_HKU_QUERY, NULL, car_token, 0, NULL, 0, &unused, &managed, &gen) && managed && !gen);

    if (car_imp) CloseHandle(car_imp);
    if (car_token) CloseHandle(car_token);
    if (car_context) CloseHandle(car_context);
    e1s13_loaded = FALSE;
    if (!failures) { puts("E1S16 TWO-ADMINS OK"); fflush(stdout); }
}

static void e1s16_descendants( HANDLE token )
{
    hku_fn load = hku_export();
    HANDLE hChildReady = NULL, hChildExit = NULL, hGrandReady = NULL, hGrandExit = NULL;
    char exe[MAX_PATH], command[2 * MAX_PATH];
    STARTUPINFOA si = {0}; PROCESS_INFORMATION pi = {0};
    HANDLE unused = NULL; ULONG managed = 0; NTSTATUS status;
    DWORD start;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, FALSE};
    PSECURITY_DESCRIPTOR sd = NULL;

    check(load && token);
    if (!load || !token) return;

    check(ConvertStringSecurityDescriptorToSecurityDescriptorA("D:(A;;GA;;;WD)", SDDL_REVISION_1, &sd, NULL));
    sa.lpSecurityDescriptor = sd;

    hChildReady = CreateEventA(&sa, TRUE, FALSE, "E1S16_ChildReady");
    hChildExit = CreateEventA(&sa, TRUE, FALSE, "E1S16_ChildExit");
    hGrandReady = CreateEventA(&sa, TRUE, FALSE, "E1S16_GrandReady");
    hGrandExit = CreateEventA(&sa, TRUE, FALSE, "E1S16_GrandExit");
    check(hChildReady && hChildExit && hGrandReady && hGrandExit);
    if (sd) LocalFree(sd);

    check(GetModuleFileNameA(NULL, exe, sizeof(exe)) != 0);
    snprintf(command, sizeof(command), "\"%s\" hku-descendant child", exe);
    si.cb = sizeof(si);
    check(CreateProcessAsUserA(token, exe, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi));

    check(WaitForSingleObject(hChildReady, 10000) == WAIT_OBJECT_0);
    check(WaitForSingleObject(hGrandReady, 10000) == WAIT_OBJECT_0);

    status = load(WEX_HKU_UNLOAD_S14, NULL, token, 0, NULL, 0, &unused, &managed);
    check(status == (NTSTATUS)0xc0000708 && !unused && !managed);

    SetEvent(hChildExit);
    if (pi.hProcess)
    {
        check(WaitForSingleObject(pi.hProcess, 5000) == WAIT_OBJECT_0);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    status = load(WEX_HKU_UNLOAD_S14, NULL, token, 0, NULL, 0, &unused, &managed);
    check(status == (NTSTATUS)0xc0000708 && !unused && !managed);

    SetEvent(hGrandExit);

    start = GetTickCount();
    do
    {
        status = load(WEX_HKU_UNLOAD_S14, NULL, token, 0, NULL, 0, &unused, &managed);
        if (status != (NTSTATUS)0xc0000708) break;
        Sleep(20);
    } while (GetTickCount() - start < 5000);
    check(!status && !unused && managed == 1);

    if (hChildReady) CloseHandle(hChildReady);
    if (hChildExit) CloseHandle(hChildExit);
    if (hGrandReady) CloseHandle(hGrandReady);
    if (hGrandExit) CloseHandle(hGrandExit);
    e1s13_loaded = FALSE;
    if (!failures) { puts("E1S16 DESCENDANTS OK"); fflush(stdout); }
}

static void e1s16_handle_held( HANDLE token )
{
    hku_fn load = hku_export();
    char exe[MAX_PATH], command[2 * MAX_PATH], pipe_name[MAX_PATH];
    STARTUPINFOA si = {0}; PROCESS_INFORMATION pi = {0};
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    HANDLE unused = NULL; ULONG managed = 0; NTSTATUS status;
    HKEY hDup = NULL;
    ULONG_PTR val = 0;
    DWORD read_bytes = 0, written = 0, start;
    BYTE ack = 1;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, FALSE};
    PSECURITY_DESCRIPTOR sd = NULL;

    check(load && token);
    if (!load || !token) return;

    check(ConvertStringSecurityDescriptorToSecurityDescriptorA("D:(A;;GA;;;WD)", SDDL_REVISION_1, &sd, NULL));
    sa.lpSecurityDescriptor = sd;

    snprintf(pipe_name, sizeof(pipe_name), "\\\\.\\pipe\\e1s16-held-%lu", (unsigned long)GetCurrentProcessId());
    hPipe = CreateNamedPipeA(pipe_name, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_WAIT, 1, 64, 64, 10000, &sa);
    check(hPipe != INVALID_HANDLE_VALUE);

    check(GetModuleFileNameA(NULL, exe, sizeof(exe)) != 0);
    snprintf(command, sizeof(command), "\"%s\" hku-handle-held %lu", exe, (unsigned long)GetCurrentProcessId());
    si.cb = sizeof(si);
    check(CreateProcessAsUserA(token, exe, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi));

    check(ConnectNamedPipe(hPipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED);
    check(ReadFile(hPipe, &val, sizeof(val), &read_bytes, NULL) && read_bytes == sizeof(val));
    if (pi.hProcess && val)
    {
        check(DuplicateHandle(pi.hProcess, (HANDLE)val, GetCurrentProcess(), (HANDLE *)&hDup,
                              0, FALSE, DUPLICATE_SAME_ACCESS));
    }
    WriteFile(hPipe, &ack, 1, &written, NULL);
    CloseHandle(hPipe);
    if (sd) LocalFree(sd);

    if (pi.hProcess)
    {
        check(WaitForSingleObject(pi.hProcess, 5000) == WAIT_OBJECT_0);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    check(hDup != NULL);

    /* Wait for child process to be reaped in wineserver so live-process check clears */
    start = GetTickCount();
    do
    {
        status = load(WEX_HKU_UNLOAD_S14, NULL, token, 0, NULL, 0, &unused, &managed);
        if (status != (NTSTATUS)0xc0000708) break;
        Sleep(20);
    } while (GetTickCount() - start < 5000);
    check(status == (NTSTATUS)0xc0000121 && !unused && !managed);

    if (hDup) RegCloseKey(hDup);

    start = GetTickCount();
    do
    {
        status = load(WEX_HKU_UNLOAD_S14, NULL, token, 0, NULL, 0, &unused, &managed);
        if (status != (NTSTATUS)0xc0000708) break;
        Sleep(20);
    } while (GetTickCount() - start < 5000);
    check(!status && !unused && managed == 1);

    e1s13_loaded = FALSE;
    if (!failures) { puts("E1S16 HANDLE-HELD OK"); fflush(stdout); }
}

static void e1s16_save_fail( HANDLE token, HANDLE imp, const char *gate )
{
    hku_fn load = hku_export();
    hku_ex_fn load_ex = (hku_ex_fn)(void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtLoadWexProfileEx");
    HKEY child = NULL;
    DWORD marker = 0x5afe, got = 0, len = sizeof(got);
    HANDLE unused = NULL; ULONG managed = 0; ULONGLONG after = 0;
    NTSTATUS status;
    LSTATUS flush_status;

    check(load && load_ex && token && imp);
    if (!load || !load_ex || !token || !imp) return;

    if (e1s13_loads)
    {
        status = load(WEX_HKU_UNLOAD_S14, NULL, token, 0, NULL, 0, &unused, &managed);
        check(status == (NTSTATUS)0xc000014c);
        if (!failures) { puts("E1S16 SAVE-DENIED OK"); fflush(stdout); }
        return;
    }

    check(SetThreadToken(NULL, imp));
    check(!RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Wine\\SaveFail", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &child, NULL));
    if (child)
    {
        check(!RegSetValueExA(child, "Marker", 0, REG_DWORD, (BYTE *)&marker, sizeof(marker)));
    }
    check(RevertToSelf());

    puts("E1S16 SAVE-DENIED LOADED"); fflush(stdout);
    if (gate)
    {
        while (GetFileAttributesA(gate) == INVALID_FILE_ATTRIBUTES) Sleep(20);
    }

    if (child)
    {
        len = sizeof(got); got = 0;
        check(!RegQueryValueExA(child, "Marker", NULL, NULL, (BYTE *)&got, &len) && got == marker);
        flush_status = RegFlushKey(child);
        check(flush_status != ERROR_SUCCESS);
        RegCloseKey(child);
        child = NULL;
    }

    check(!load_ex(WEX_HKU_QUERY, NULL, token, 0, NULL, 0, &unused, &managed, &after) && managed && !after);

    status = load(WEX_HKU_UNLOAD_S14, NULL, token, 0, NULL, 0, &unused, &managed);
    check(status == (NTSTATUS)0xc000014c);

    check(SetThreadToken(NULL, imp));
    check(RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Wine\\SaveFail", 0, KEY_READ, &child) != ERROR_SUCCESS && !child);
    check(RevertToSelf());

    e1s13_loaded = FALSE;
    if (!failures) { puts("E1S16 SAVE-DENIED OK"); fflush(stdout); }
}

static void e1s16_post_subst( HANDLE token, HANDLE imp, const char *gate )
{
    hku_fn load = hku_export();
    HKEY child = NULL;
    DWORD marker = 0x5057;
    HANDLE unused = NULL; ULONG managed = 0;
    NTSTATUS status;
    LSTATUS flush_status;

    check(load && token && imp);
    if (!load || !token || !imp) return;

    if (e1s13_loads)
    {
        status = load(WEX_HKU_UNLOAD_S14, NULL, token, 0, NULL, 0, &unused, &managed);
        check(status == (NTSTATUS)0xc000014c || status == (NTSTATUS)0xc0000098);
        if (!failures) { puts("E1S16 POST-SUBST OK"); fflush(stdout); }
        return;
    }

    check(SetThreadToken(NULL, imp));
    check(!RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Wine\\PostSubst", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &child, NULL));
    if (child)
    {
        check(!RegSetValueExA(child, "Marker", 0, REG_DWORD, (BYTE *)&marker, sizeof(marker)));
    }
    check(RevertToSelf());

    puts("E1S16 POST-SUBST LOADED"); fflush(stdout);
    if (gate)
    {
        while (GetFileAttributesA(gate) == INVALID_FILE_ATTRIBUTES) Sleep(20);
    }

    if (child)
    {
        flush_status = RegFlushKey(child);
        check(flush_status != ERROR_SUCCESS);
        RegCloseKey(child);
        child = NULL;
    }

    status = load(WEX_HKU_UNLOAD_S14, NULL, token, 0, NULL, 0, &unused, &managed);
    check(status == (NTSTATUS)0xc000014c || status == (NTSTATUS)0xc0000098);

    check(SetThreadToken(NULL, imp));
    check(RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Wine\\PostSubst", 0, KEY_READ, &child) != ERROR_SUCCESS && !child);
    check(RevertToSelf());

    e1s13_loaded = FALSE;
    if (!failures) { puts("E1S16 POST-SUBST OK"); fflush(stdout); }
}

static void e1s16_host_uid( HANDLE token, const char *spec )
{
    hku_fn load = hku_export();
    char exe[MAX_PATH], command[2 * MAX_PATH];
    STARTUPINFOA si = {0}; PROCESS_INFORMATION pi = {0};
    unsigned long bob_uid = 0, bob_gid = 0;
    const char *p0, *p1, *p2;
    char adm_name[64] = "wexadm", std_name[64] = "wexstd";
    DWORD childRc = ~0u, start;
    HANDLE unused = NULL; ULONG managed = 0; NTSTATUS status;

    check(load && token && spec);
    if (!load || !token || !spec) return;

    p0 = strchr(spec, ':');
    p1 = p0 ? strchr(p0 + 1, ':') : NULL;
    p2 = p1 ? strchr(p1 + 1, ':') : NULL;
    if (p1 && p2)
    {
        const char *p3 = strchr(p2 + 1, ':');
        const char *p4 = p3 ? strchr(p3 + 1, ':') : NULL;
        bob_uid = strtoul(p1 + 1, NULL, 10);
        bob_gid = strtoul(p2 + 1, NULL, 10);
        if (p3 && p4)
        {
            memcpy(adm_name, p3 + 1, p4 - (p3 + 1)); adm_name[p4 - (p3 + 1)] = 0;
            strncpy(std_name, p4 + 1, sizeof(std_name) - 1);
        }
    }

    check(GetModuleFileNameA(NULL, exe, sizeof(exe)) != 0);
    snprintf(command, sizeof(command), "\"%s\" hku-host-uid %lu %lu %s %s",
             exe, bob_uid, bob_gid, adm_name, std_name);
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    check(CreateProcessAsUserA(token, exe, command, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi));

    if (pi.hProcess)
    {
        check(WaitForSingleObject(pi.hProcess, 10000) == WAIT_OBJECT_0);
        check(GetExitCodeProcess(pi.hProcess, &childRc) && childRc == 0);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    start = GetTickCount();
    do
    {
        status = load(WEX_HKU_UNLOAD_S14, NULL, token, 0, NULL, 0, &unused, &managed);
        if (status == (NTSTATUS)0xc0000708) Sleep(20);
    } while (status == (NTSTATUS)0xc0000708 && GetTickCount() - start < 3000);

    check(!status && managed == 1);
    e1s13_loaded = FALSE;

    if (!failures) { puts("E1S16 HOST-UID OK"); fflush(stdout); }
}

static void e1s13_load_checks( HANDLE context, HANDLE token, PSID expected, const char *sid,
                              const BYTE *input, const char *spec, HANDLE process, const char *gate )
{
    BYTE record[WEX_HKU_RECORD_SIZE] = {0};
    HANDLE key = NULL, replay = NULL, imp = NULL;
    ULONG managed = 0; NTSTATUS status, wanted = 0;
    HKEY visible = NULL, classes = NULL, machine = NULL;
    char name[160], mode[64];
    DWORD value = 13, machine_value = 7, got = 0, length = sizeof(got);
    hku_fn load = hku_export();
    const char *colon = spec ? strchr(spec, ':') : NULL;
    if (!load || !colon || (size_t)(colon - spec) >= sizeof(mode)) { check(FALSE); return; }
    memcpy(mode, spec, colon - spec); mode[colon - spec] = 0;
    if (!strcmp(mode,"absent") || !strcmp(mode,"unprovisioned")) wanted = (NTSTATUS)0xc000003a;
    else if (!strcmp(mode,"wrong-incarnation") || !strcmp(mode,"wrong-reference") || !strcmp(mode,"owner")) wanted = (NTSTATUS)0xc0000022;
    else if (!strcmp(mode,"recovery")) wanted = (NTSTATUS)0xc000014c;
    else if (e1s13_loads && (!strcmp(mode,"save-fail") || !strcmp(mode,"post-subst"))) wanted = (NTSTATUS)0xc000014c;
    else if (strcmp(mode,"accept") && strcmp(mode,"already-loaded") && strcmp(mode,"replay") &&
             strcmp(mode,"unload") && strncmp(mode,"crash-",6) &&
             strcmp(mode,"two-admins") && strcmp(mode,"descendants") && strcmp(mode,"handle-held") &&
             strcmp(mode,"save-fail") && strcmp(mode,"post-subst") && strcmp(mode,"host-uid")) { check(FALSE); return; }
    if (!wanted && e1s13_loaded) wanted = (NTSTATUS)0xc0000035;
    wex_token_put32(record, 1); wex_token_put32(record + 4, sizeof(record));
    memcpy(record + 8, input + 124, 16); memcpy(record + 24, input + 164, 8);
    wex_token_put64(record + 32, _strtoui64(colon + 1, NULL, 10));
    if (!strcmp(mode,"wrong-reference")) record[8] ^= 1;
    check(!RegOpenCurrentUser(KEY_READ, &visible));
    if (visible) { e1s13_key_path(visible, "S-1-5-18"); RegCloseKey(visible); visible = NULL; }
    check(!RegCreateKeyExA(HKEY_CURRENT_USER,"Software\\Wine\\E1S13SystemCache",0,NULL,0,KEY_READ,NULL,&visible,NULL));
    if (visible) { RegCloseKey(visible); visible = NULL; }
    check(DuplicateTokenEx(token, TOKEN_QUERY | TOKEN_IMPERSONATE, NULL, SecurityImpersonation, TokenImpersonation, &imp));
    if (!e1s13_loads && imp)
    {
        check(SetThreadToken(NULL, imp));
        check(RegOpenCurrentUser(KEY_READ, &visible) != ERROR_SUCCESS && !visible);
        check(RevertToSelf());
    }
    status = load(WEX_HKU_LOAD, context, token, KEY_ALL_ACCESS, record, sizeof(record), &key, &managed);
    last_nt_call = "NtLoadWexProfile(WEX_HKU_LOAD)"; last_status = status;
    check(status == wanted && (!!key == !wanted) && managed == !wanted);
    if (!status) e1s13_loaded = TRUE;
    if (key) { e1s13_key_path(key, sid); e1s13_descriptor(key, expected); RegCloseKey(key); }
    managed = ~0u;
    status = load(WEX_HKU_LOAD, context, token, KEY_READ, record, sizeof(record), &replay, &managed);
    check(status == (NTSTATUS)0xc0000022 && !replay && !managed);
    if (replay) CloseHandle(replay);
    snprintf(name, sizeof(name), "%s", sid);
    status = RegOpenKeyExA(HKEY_USERS, name, 0, KEY_READ, &visible);
    check(e1s13_loaded ? !status :
          (!strcmp(mode,"save-fail") || !strcmp(mode,"post-subst")) ? status != ERROR_SUCCESS :
          status == ERROR_FILE_NOT_FOUND);
    if (visible) { e1s13_descriptor(visible, expected); RegCloseKey(visible); visible = NULL; }
    if (!strcmp(mode,"recovery"))
    {
        hku_ex_fn load_ex = (hku_ex_fn)(void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtLoadWexProfileEx");
        HANDLE absent_key = (HANDLE)1;
        ULONG managed_identity = ~0u;
        ULONGLONG generation = ~(ULONGLONG)0;
        /* Storage recovery must deny before either HKU root is published.
         * QUERY still recognizes the managed token, but has no loaded hive. */
        check(!e1s13_loaded);
        check(load_ex && !load_ex(WEX_HKU_QUERY,NULL,token,0,NULL,0,&absent_key,&managed_identity,&generation) &&
              !absent_key && managed_identity == 1 && !generation);
        if (load_ex && absent_key && absent_key != (HANDLE)1) CloseHandle(absent_key);
        snprintf(name, sizeof(name), "%s_Classes", sid);
        check(RegOpenKeyExA(HKEY_USERS,name,0,KEY_READ,&classes) == ERROR_FILE_NOT_FOUND && !classes);
        if (classes) { RegCloseKey(classes); classes = NULL; }
        check(SetThreadToken(NULL, imp));
        check(RegOpenCurrentUser(KEY_READ, &visible) != ERROR_SUCCESS && !visible);
        if (visible) { RegCloseKey(visible); visible = NULL; }
        check(RevertToSelf());
        if (!failures) { puts("E1S14 RECOVERY DENIED"); fflush(stdout); }
    }
    else if (e1s13_loads && !strcmp(mode,"save-fail"))
    {
        e1s16_save_fail(token, imp, gate);
    }
    else if (e1s13_loads && !strcmp(mode,"post-subst"))
    {
        e1s16_post_subst(token, imp, gate);
    }
    if (e1s13_loaded)
    {
        typedef BOOL (WINAPI *profile_fn)(HANDLE,LPPROFILEINFOA);
        PROFILEINFOA pi = {0}; HMODULE userenv = LoadLibraryA("userenv.dll");
        profile_fn profile = userenv ? (profile_fn)(void *)GetProcAddress(userenv,"LoadUserProfileA") : NULL;
        pi.dwSize = sizeof(pi); pi.lpUserName = (char *)"ignored-path-label";
        check(profile && profile(token, &pi) && pi.hProfile && pi.hProfile != HKEY_CURRENT_USER);
        if (pi.hProfile && pi.hProfile != HKEY_CURRENT_USER) RegCloseKey(pi.hProfile);
        if (userenv) FreeLibrary(userenv);
        snprintf(name, sizeof(name), "%s_Classes", sid);
        check(!RegOpenKeyExA(HKEY_USERS, name, 0, KEY_READ, &classes));
        if (classes) { e1s13_descriptor(classes, expected); RegCloseKey(classes); classes = NULL; }
        check(!RegCreateKeyExA(HKEY_LOCAL_MACHINE, "Software\\Classes\\E1S13Merge", 0, NULL, 0,
                              KEY_ALL_ACCESS, NULL, &machine, NULL));
        if (machine) check(!RegSetValueExA(machine,"Machine",0,REG_DWORD,(BYTE *)&value,sizeof(value)));
        if (machine) check(!RegSetValueExA(machine,"User",0,REG_DWORD,(BYTE *)&machine_value,sizeof(machine_value)));
        if (imp)
        {
            /* Warm SYSTEM HKCU above, then switch effective user in the SAME
             * process. The legacy predefined cache must never supply SYSTEM. */
            check(SetThreadToken(NULL, imp));
            e1s13_identity(sid);
            /* The profile directory lives in the caller prefix; this system-role broker runs in the machine
             * prefix, so a file-ownership check there cannot see it. That check moves to S15 (launch prep). */
            /* S15 checks: effective Alice token sees her profile path and variables */
            {
                typedef BOOL (WINAPI *get_prof_fn)(HANDLE, LPWSTR, LPDWORD);
                typedef BOOL (WINAPI *create_env_fn)(LPVOID*, HANDLE, BOOL);
                typedef BOOL (WINAPI *destroy_env_fn)(LPVOID);
                typedef BOOL (WINAPI *expand_env_fn)(HANDLE, LPCWSTR, LPWSTR, DWORD);
                const char *acc_name = (const char *)(input + 204);
                WCHAR acc_nameW[128] = {0};
                MultiByteToWideChar(CP_ACP, 0, acc_name, -1, acc_nameW, ARRAY_SIZE(acc_nameW));
                HMODULE hUserenv = LoadLibraryA("userenv.dll");
                if (hUserenv)
                {
                    get_prof_fn pGetUserProfileDirectoryW = (get_prof_fn)(void *)GetProcAddress(hUserenv, "GetUserProfileDirectoryW");
                    create_env_fn pCreateEnvironmentBlock = (create_env_fn)(void *)GetProcAddress(hUserenv, "CreateEnvironmentBlock");
                    destroy_env_fn pDestroyEnvironmentBlock = (destroy_env_fn)(void *)GetProcAddress(hUserenv, "DestroyEnvironmentBlock");
                    expand_env_fn pExpandEnvironmentStringsForUserW = (expand_env_fn)(void *)GetProcAddress(hUserenv, "ExpandEnvironmentStringsForUserW");

                    if (pGetUserProfileDirectoryW)
                    {
                        WCHAR prof_dir[MAX_PATH];
                        DWORD size = ARRAY_SIZE(prof_dir);
                        check(pGetUserProfileDirectoryW(NULL, prof_dir, &size));
                        check(wcsstr(prof_dir, L".e1s") != NULL);
                        check(wcsstr(prof_dir, acc_nameW) != NULL);
                        check(wcsstr(prof_dir, L"bob") == NULL);
                    }

                    if (pCreateEnvironmentBlock && pDestroyEnvironmentBlock)
                    {
                        LPVOID env = NULL;
                        check(pCreateEnvironmentBlock(&env, NULL, FALSE) && env);
                        if (env) pDestroyEnvironmentBlock(env);
                    }

                    if (pExpandEnvironmentStringsForUserW)
                    {
                        WCHAR exp[MAX_PATH];
                        check(pExpandEnvironmentStringsForUserW(NULL, L"%USERPROFILE%", exp, ARRAY_SIZE(exp)));
                        check(wcsstr(exp, L".e1s") != NULL);
                        check(wcsstr(exp, acc_nameW) != NULL);
                        check(wcsstr(exp, L"bob") == NULL);
                    }
                    FreeLibrary(hUserenv);
                }

                /* Shell folder CSIDL_PROFILE check */
                {
                    typedef HRESULT (WINAPI *sh_get_folder_path_fn)(HWND, int, HANDLE, DWORD, LPWSTR);
                    HMODULE hShell32 = LoadLibraryA("shell32.dll");
                    if (hShell32)
                    {
                        sh_get_folder_path_fn pSHGetFolderPathW = (sh_get_folder_path_fn)(void *)GetProcAddress(hShell32, "SHGetFolderPathW");
                        if (pSHGetFolderPathW)
                        {
                            WCHAR shell_prof[MAX_PATH];
                            check(SUCCEEDED(pSHGetFolderPathW(NULL, 0x28 /* CSIDL_PROFILE */, NULL, 0, shell_prof)));
                            check(wcsstr(shell_prof, L".e1s") != NULL);
                            check(wcsstr(shell_prof, acc_nameW) != NULL);
                            check(wcsstr(shell_prof, L"bob") == NULL);
                        }
                        FreeLibrary(hShell32);
                    }
                }

                /* Volatile Environment key values check */
                {
                    HKEY hVol = NULL;
                    check(RegOpenKeyExA(HKEY_CURRENT_USER, "Volatile Environment", 0, KEY_READ, &hVol) == ERROR_SUCCESS && hVol);
                    if (hVol)
                    {
                        char str_buf[MAX_PATH];
                        DWORD dlen = sizeof(str_buf);
                        check(RegQueryValueExA(hVol, "USERNAME", NULL, NULL, (BYTE *)str_buf, &dlen) == ERROR_SUCCESS && !strcmp(str_buf, acc_name));
                        dlen = sizeof(str_buf);
                        check(RegQueryValueExA(hVol, "USERPROFILE", NULL, NULL, (BYTE *)str_buf, &dlen) == ERROR_SUCCESS && strstr(str_buf, ".e1s"));
                        dlen = sizeof(str_buf);
                        check(RegQueryValueExA(hVol, "HOMEDRIVE", NULL, NULL, (BYTE *)str_buf, &dlen) == ERROR_SUCCESS && !strcmp(str_buf, "C:"));
                        dlen = sizeof(str_buf);
                        check(RegQueryValueExA(hVol, "HOMEPATH", NULL, NULL, (BYTE *)str_buf, &dlen) == ERROR_SUCCESS && strstr(str_buf, ".e1s"));
                        dlen = sizeof(str_buf);
                        check(RegQueryValueExA(hVol, "APPDATA", NULL, NULL, (BYTE *)str_buf, &dlen) == ERROR_SUCCESS && strstr(str_buf, "AppData\\Roaming"));
                        dlen = sizeof(str_buf);
                        check(RegQueryValueExA(hVol, "LOCALAPPDATA", NULL, NULL, (BYTE *)str_buf, &dlen) == ERROR_SUCCESS && strstr(str_buf, "AppData\\Local"));
                        dlen = sizeof(str_buf);
                        check(RegQueryValueExA(hVol, "TEMP", NULL, NULL, (BYTE *)str_buf, &dlen) == ERROR_SUCCESS && strstr(str_buf, "Temp"));
                        dlen = sizeof(str_buf);
                        check(RegQueryValueExA(hVol, "TMP", NULL, NULL, (BYTE *)str_buf, &dlen) == ERROR_SUCCESS && strstr(str_buf, "Temp"));
                        RegCloseKey(hVol);
                    }
                }
            }
            check(!RegCreateKeyExA(HKEY_CURRENT_USER,"Software\\Classes\\E1S13Merge",0,NULL,0,
                                   KEY_ALL_ACCESS,NULL,&classes,NULL));
            if (classes) { check(!RegSetValueExA(classes,"User",0,REG_DWORD,(BYTE *)&value,sizeof(value))); RegCloseKey(classes); classes = NULL; }
            check(!RegOpenKeyExA(HKEY_CLASSES_ROOT,"E1S13Merge",0,KEY_READ,&classes));
            if (classes)
            {
                check(!RegQueryValueExA(classes,"Machine",NULL,NULL,(BYTE *)&got,&length) && got == value);
                length = sizeof(got); got = 0;
                check(!RegQueryValueExA(classes,"User",NULL,NULL,(BYTE *)&got,&length) && got == value);
                RegCloseKey(classes);
            }
            check(!RegOpenKeyExA(HKEY_CLASSES_ROOT,"E1S13Merge",0,KEY_READ | KEY_WOW64_32KEY,&classes));
            if (classes) RegCloseKey(classes);
            classes = NULL;
            check(!RegOpenKeyExA(HKEY_CLASSES_ROOT,"E1S13Merge",0,KEY_READ | KEY_WOW64_64KEY,&classes));
            if (classes) RegCloseKey(classes);
            check(RevertToSelf());
        }
        if (machine) RegCloseKey(machine);
        if (!strcmp(mode,"unload") || !strncmp(mode,"crash-",6)) e1s14_lifecycle(token,imp,mode);
        else if (!strcmp(mode,"two-admins")) e1s16_two_admins(token, imp, process, input, spec);
        else if (!strcmp(mode,"descendants")) e1s16_descendants(token);
        else if (!strcmp(mode,"handle-held")) e1s16_handle_held(token);
        else if (!strcmp(mode,"save-fail")) e1s16_save_fail(token, imp, gate);
        else if (!strcmp(mode,"post-subst")) e1s16_post_subst(token, imp, gate);
        else if (!strcmp(mode,"host-uid")) e1s16_host_uid(token, spec);
    }
    if (imp) CloseHandle(imp);
    e1s13_loads++;
    if (!failures) { puts("E1S13 HKU CHECK OK"); fflush(stdout); }
}
