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
    for (op = WEX_HKU_LOAD; op <= WEX_HKU_CLASSES; op++)
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

static void e1s13_load_checks( HANDLE context, HANDLE token, PSID expected, const char *sid,
                              const BYTE *input, const char *spec )
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
    else if (strcmp(mode,"accept") && strcmp(mode,"already-loaded") && strcmp(mode,"replay")) { check(FALSE); return; }
    if (!wanted && e1s13_loads) wanted = (NTSTATUS)0xc0000035;
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
    check(status == wanted && (!!key == !wanted) && managed == !wanted);
    if (!status) e1s13_loaded = TRUE;
    if (key) { e1s13_key_path(key, sid); e1s13_descriptor(key, expected); RegCloseKey(key); }
    managed = ~0u;
    status = load(WEX_HKU_LOAD, context, token, KEY_READ, record, sizeof(record), &replay, &managed);
    check(status == (NTSTATUS)0xc0000022 && !replay && !managed);
    if (replay) CloseHandle(replay);
    snprintf(name, sizeof(name), "%s", sid);
    status = RegOpenKeyExA(HKEY_USERS, name, 0, KEY_READ, &visible);
    check(e1s13_loaded ? !status : status == ERROR_FILE_NOT_FOUND);
    if (visible) { e1s13_descriptor(visible, expected); RegCloseKey(visible); visible = NULL; }
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
        status = load(WEX_HKU_UNLOAD_S14,NULL,token,0,NULL,0,&key,&managed);
        check(status == (NTSTATUS)0xc00000bb && !key && !managed);
    }
    if (imp) CloseHandle(imp);
    e1s13_loads++;
    if (!failures) { puts("E1S13 HKU CHECK OK"); fflush(stdout); }
}
