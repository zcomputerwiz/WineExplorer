/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * tokprobe: what a sandboxed child (restricted token like Chromium's GPU/utility processes, low integrity) can reach.
 *   tokprobe.exe            runs the checks itself, then again in children under: low IL; restricted (Everyone, Users,
 *                           logon SID, RESTRICTED); restricted + low IL
 *   tokprobe.exe child NAME the checks only */
#include <windows.h>
#include <sddl.h>
#include <stdio.h>

static void check_key(HKEY root, const char *rootname, const char *path, REGSAM sam, BOOL create)
{
    HKEY k;
    LONG r;
    if (create) r = RegCreateKeyExA(root, path, 0, NULL, 0, sam, NULL, &k, NULL);
    else r = RegOpenKeyExA(root, path, 0, sam, &k);
    printf("  %s %s\\%s sam=%#lx -> %ld\n", create ? "create" : "open  ", rootname, path, (unsigned long)sam, r);
    if (!r) RegCloseKey(k);
}

static void checks(const char *name)
{
    DISPLAY_DEVICEA dd = { sizeof(dd) };
    DEVMODEA dm = { 0 };
    HWND hwnd;
    int n = 0;

    printf("== %s\n", name);
    check_key(HKEY_CURRENT_USER, "HKCU", "Software\\Wine", MAXIMUM_ALLOWED, TRUE);
    check_key(HKEY_CURRENT_USER, "HKCU", "Software\\Wine", KEY_READ, FALSE);
    check_key(HKEY_CURRENT_USER, "HKCU", "Software", KEY_READ, FALSE);
    check_key(HKEY_CURRENT_USER, "HKCU", "Software\\Wine\\Temporary System Parameters", MAXIMUM_ALLOWED, TRUE);
    check_key(HKEY_LOCAL_MACHINE, "HKLM", "System\\CurrentControlSet\\Enum", KEY_READ, FALSE);
    check_key(HKEY_LOCAL_MACHINE, "HKLM", "HARDWARE\\DEVICEMAP\\VIDEO", KEY_READ, FALSE);
    check_key(HKEY_LOCAL_MACHINE, "HKLM", "System\\CurrentControlSet\\Hardware Profiles\\Current\\Software\\Fonts", KEY_READ, FALSE);
    check_key(HKEY_LOCAL_MACHINE, "HKLM", "System\\CurrentControlSet\\Control\\Video", KEY_READ, FALSE);
    while (EnumDisplayDevicesA(NULL, n, &dd, 0)) n++;
    printf("  EnumDisplayDevices: %d adapters\n", n);
    dm.dmSize = sizeof(dm);
    printf("  EnumDisplaySettings(current): %d (%lux%lu)\n", EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm),
           (unsigned long)dm.dmPelsWidth, (unsigned long)dm.dmPelsHeight);
    printf("  GetSystemMetrics(SM_CXSCREEN)=%d SM_CMONITORS=%d\n", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CMONITORS));
    hwnd = CreateWindowExA(0, "STATIC", "tokprobe", WS_POPUP | WS_VISIBLE, 10, 10, 100, 100, NULL, NULL, NULL, NULL);
    printf("  CreateWindow: %p, MonitorFromWindow=%p\n", hwnd, hwnd ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL) : NULL);
    if (hwnd) DestroyWindow(hwnd);
    fflush(stdout);
}

static HANDLE make_token(BOOL restricted, BOOL low)
{
    HANDLE tok, out = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &tok)) return NULL;
    if (restricted)
    {
        SID_AND_ATTRIBUTES sids[4];
        PSID everyone, users, rest, logon = NULL;
        BYTE buf[512];
        TOKEN_GROUPS *g = (TOKEN_GROUPS *)buf;
        DWORD size, i, n = 0;
        ConvertStringSidToSidA("S-1-1-0", &everyone);
        ConvertStringSidToSidA("S-1-5-32-545", &users);
        ConvertStringSidToSidA("S-1-5-12", &rest);
        if (GetTokenInformation(tok, TokenGroups, buf, sizeof(buf), &size))
            for (i = 0; i < g->GroupCount; i++) if (g->Groups[i].Attributes & SE_GROUP_LOGON_ID) logon = g->Groups[i].Sid;
        sids[n++].Sid = everyone; sids[n++].Sid = users; sids[n++].Sid = rest;
        if (logon) sids[n++].Sid = logon;
        for (i = 0; i < n; i++) sids[i].Attributes = 0;
        if (!CreateRestrictedToken(tok, DISABLE_MAX_PRIVILEGE, 0, NULL, 0, NULL, n, sids, &out))
        { printf("CreateRestrictedToken %lu\n", GetLastError()); CloseHandle(tok); return NULL; }
    }
    else if (!DuplicateTokenEx(tok, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenPrimary, &out)) out = NULL;
    CloseHandle(tok);
    if (out && low)
    {
        PSID lowsid;
        TOKEN_MANDATORY_LABEL tml;
        ConvertStringSidToSidA("S-1-16-4096", &lowsid);
        tml.Label.Sid = lowsid; tml.Label.Attributes = SE_GROUP_INTEGRITY;
        if (!SetTokenInformation(out, TokenIntegrityLevel, &tml, sizeof(tml) + GetLengthSid(lowsid)))
            printf("set low IL: %lu\n", GetLastError());
    }
    return out;
}

static void run_child(const char *name, BOOL restricted, BOOL low)
{
    char cmd[MAX_PATH + 64], self[MAX_PATH];
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    HANDLE tok = make_token(restricted, low);
    GetModuleFileNameA(NULL, self, MAX_PATH);
    snprintf(cmd, sizeof(cmd), "\"%s\" child %s", self, name);
    if (!tok || !CreateProcessAsUserA(tok, NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
    { printf("== %s: could not start (%lu)\n", name, GetLastError()); return; }
    WaitForSingleObject(pi.hProcess, 60000);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(tok);
}

int main(int argc, char **argv)
{
    if (argc > 2 && !strcmp(argv[1], "child")) { checks(argv[2]); return 0; }
    checks("plain");
    fflush(stdout);
    run_child("low-il", FALSE, TRUE);
    run_child("restricted", TRUE, FALSE);
    run_child("restricted+low-il", TRUE, TRUE);
    return 0;
}
