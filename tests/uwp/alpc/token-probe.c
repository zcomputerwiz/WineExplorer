/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Token probe for a packaged (AppContainer) process, for the dwm.exe server check of dwmapi:
 *   token-probe.exe                 print this process's token: integrity level, IsAppContainer, package SID, capabilities,
 *                                   IsRestricted, the package identity; then the client sequence below when PIDFILE is given
 *   token-probe.exe target PIDFILE  (an ordinary process) grant S-1-15-2-1 (ALL APPLICATION PACKAGES) PROCESS_QUERY_LIMITED_INFORMATION
 *                                   on the process object and TOKEN_QUERY on the token, write the pid to PIDFILE, wait 150 s
 *   token-probe.exe client PIDFILE  OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION) on that pid, OpenProcessToken(TOKEN_QUERY),
 *                                   GetTokenInformation(TokenIntegrityLevel), and the same through PROCESS_QUERY_INFORMATION
 * Build: x86_64-w64-mingw32-gcc -O2 -o token-probe.exe token-probe.c -ladvapi32
 */
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <stdio.h>
#include <string.h>

#ifndef TokenIsAppContainer
#define TokenIsAppContainer 29
#define TokenCapabilities 30
#define TokenAppContainerSid 31
#endif

static void print_sid(const char *what, PSID sid)
{
    WCHAR *text = NULL;

    if (sid && ConvertSidToStringSidW(sid, &text)) { printf("%s %ls\n", what, text); LocalFree(text); }
    else printf("%s (none)\n", what);
}

static void print_token(HANDLE token)
{
    BYTE buf[2048];
    DWORD len, v;

    if (GetTokenInformation(token, TokenIntegrityLevel, buf, sizeof(buf), &len))
    {
        TOKEN_MANDATORY_LABEL *l = (TOKEN_MANDATORY_LABEL *)buf;
        printf("integrity level RID %#lx\n", (unsigned long)*GetSidSubAuthority(l->Label.Sid, *GetSidSubAuthorityCount(l->Label.Sid) - 1));
    }
    else printf("TokenIntegrityLevel: error %lu\n", GetLastError());
    v = 7;
    if (GetTokenInformation(token, (TOKEN_INFORMATION_CLASS)TokenIsAppContainer, &v, sizeof(v), &len)) printf("IsAppContainer %lu\n", v);
    else printf("TokenIsAppContainer: error %lu\n", GetLastError());
    if (GetTokenInformation(token, (TOKEN_INFORMATION_CLASS)TokenAppContainerSid, buf, sizeof(buf), &len))
        print_sid("AppContainer SID", ((TOKEN_APPCONTAINER_INFORMATION *)buf)->TokenAppContainer);
    else printf("TokenAppContainerSid: error %lu\n", GetLastError());
    if (GetTokenInformation(token, (TOKEN_INFORMATION_CLASS)TokenCapabilities, buf, sizeof(buf), &len))
    {
        TOKEN_GROUPS *g = (TOKEN_GROUPS *)buf;
        DWORD i;
        printf("capabilities: %lu\n", g->GroupCount);
        for (i = 0; i < g->GroupCount; i++) print_sid("  capability", g->Groups[i].Sid);
    }
    else printf("TokenCapabilities: error %lu\n", GetLastError());
    printf("IsTokenRestricted %d\n", IsTokenRestricted(token));
    if (GetTokenInformation(token, TokenUser, buf, sizeof(buf), &len)) print_sid("user", ((TOKEN_USER *)buf)->User.Sid);
    if (GetTokenInformation(token, TokenElevationType, &v, sizeof(v), &len)) printf("elevation type %lu\n", v);
}

static void print_identity(void)
{
    typedef LONG (WINAPI *get_name_t)(UINT32 *, WCHAR *);
    get_name_t get = (get_name_t)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetCurrentPackageFullName");
    UINT32 n = 256;
    WCHAR name[256];
    LONG rc = get ? get(&n, name) : -1;

    if (!rc) printf("package full name %ls\n", name);
    else printf("GetCurrentPackageFullName: %ld\n", rc);
}

static int grant_all_app_packages(HANDLE object, DWORD rights, const char *what)
{
    SID_IDENTIFIER_AUTHORITY app = {{0, 0, 0, 0, 0, 15}};
    PSID sid = NULL;
    EXPLICIT_ACCESSW ea;
    PACL old = NULL, acl = NULL;
    PSECURITY_DESCRIPTOR sd = NULL;
    DWORD rc;

    if (!AllocateAndInitializeSid(&app, 2, 2, 1, 0, 0, 0, 0, 0, 0, &sid)) return 1;
    memset(&ea, 0, sizeof(ea));
    ea.grfAccessPermissions = rights;
    ea.grfAccessMode = GRANT_ACCESS;
    ea.grfInheritance = NO_INHERITANCE;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    ea.Trustee.ptstrName = (WCHAR *)sid;
    rc = GetSecurityInfo(object, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &old, NULL, &sd);
    if (!rc) rc = SetEntriesInAclW(1, &ea, old, &acl);
    if (!rc) rc = SetSecurityInfo(object, SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, acl, NULL);
    printf("grant S-1-15-2-1 %s: %lu\n", what, rc);
    LocalFree(acl);
    LocalFree(sd);
    FreeSid(sid);
    return rc != 0;
}

static void try_open(DWORD pid, DWORD access, const char *name)
{
    HANDLE process = OpenProcess(access, FALSE, pid), token = NULL;
    BYTE buf[256];
    DWORD len;

    if (!process) { printf("OpenProcess(%s): error %lu\n", name, GetLastError()); return; }
    printf("OpenProcess(%s): ok\n", name);
    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) { printf("  OpenProcessToken(TOKEN_QUERY): error %lu\n", GetLastError()); CloseHandle(process); return; }
    printf("  OpenProcessToken(TOKEN_QUERY): ok\n");
    if (GetTokenInformation(token, TokenIntegrityLevel, buf, sizeof(buf), &len))
    {
        TOKEN_MANDATORY_LABEL *l = (TOKEN_MANDATORY_LABEL *)buf;
        printf("  GetTokenInformation(TokenIntegrityLevel): RID %#lx\n",
               (unsigned long)*GetSidSubAuthority(l->Label.Sid, *GetSidSubAuthorityCount(l->Label.Sid) - 1));
    }
    else printf("  GetTokenInformation(TokenIntegrityLevel): error %lu\n", GetLastError());
    CloseHandle(token);
    CloseHandle(process);
}

int main(int argc, char **argv)
{
    HANDLE token = NULL;
    FILE *f;
    DWORD pid = 0;

    if (argc == 4) { freopen(argv[3], "w", stdout); argc = 3; }   /* a packaged process has no console: the output goes to a file */
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc == 3 && !strcmp(argv[1], "target"))
    {
        HANDLE t = NULL;

        print_identity();
        OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | READ_CONTROL | WRITE_DAC, &t);
        print_token(t);
        grant_all_app_packages(GetCurrentProcess(), PROCESS_QUERY_LIMITED_INFORMATION, "PROCESS_QUERY_LIMITED_INFORMATION on the process");
        if (t) grant_all_app_packages(t, TOKEN_QUERY, "TOKEN_QUERY on the token");
        if ((f = fopen(argv[2], "w"))) { fprintf(f, "%lu\n", GetCurrentProcessId()); fclose(f); }
        Sleep(150000);
        return 0;
    }
    print_identity();
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) print_token(token);
    else printf("OpenProcessToken: error %lu\n", GetLastError());
    if (argc == 3 && !strcmp(argv[1], "client"))
    {
        if ((f = fopen(argv[2], "r"))) { fscanf(f, "%lu", &pid); fclose(f); }
        printf("target pid %lu\n", pid);
        try_open(pid, PROCESS_QUERY_LIMITED_INFORMATION, "PROCESS_QUERY_LIMITED_INFORMATION");
        try_open(pid, PROCESS_QUERY_INFORMATION, "PROCESS_QUERY_INFORMATION");
    }
    return 0;
}
