/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2026 WineExplorer contributors */
/* Probe: can SetSecurityInfo(DACL) succeed on a registry key handle that was created with only KEY_WRITE (no WRITE_DAC)?
 * This is what Chromium's updater does in CreateClientStateMedium. Run on Windows (HKCU) and on Wine. */
#include <windows.h>
#include <aclapi.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    HKEY key = NULL;
    DWORD disp = 0, err;
    PACL old_dacl = NULL, new_dacl = NULL;
    PSECURITY_DESCRIPTOR sd = NULL;
    EXPLICIT_ACCESSW ea = {0};
    SID_IDENTIFIER_AUTHORITY world = SECURITY_WORLD_SID_AUTHORITY;
    PSID everyone = NULL;
    REGSAM sam = KEY_WRITE;
    HKEY root = (argc > 1 && argv[1][0] == 'm') ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    if (argc > 2) sam |= KEY_WOW64_32KEY;

    err = RegCreateKeyExW(root, L"Software\\WineTestKeyWriteDacl", 0, NULL, 0, sam, NULL, &key, &disp);
    printf("RegCreateKeyEx sam=%#lx: %lu disp=%lu\n", sam, err, disp);
    if (err) return 1;
    err = GetSecurityInfo(key, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION, NULL, NULL, &old_dacl, NULL, &sd);
    printf("GetSecurityInfo: %lu\n", err);
    AllocateAndInitializeSid(&world, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &everyone);
    ea.grfAccessPermissions = KEY_READ;
    ea.grfAccessMode = GRANT_ACCESS;
    ea.grfInheritance = NO_INHERITANCE;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    ea.Trustee.ptstrName = (LPWSTR)everyone;
    err = SetEntriesInAclW(1, &ea, old_dacl, &new_dacl);
    printf("SetEntriesInAcl: %lu\n", err);
    err = SetSecurityInfo(key, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION, NULL, NULL, new_dacl, NULL);
    printf("SetSecurityInfo on KEY_WRITE handle: %lu%s\n", err, err == 5 ? " (ACCESS_DENIED)" : "");
    RegCloseKey(key);
    {
        static const REGSAM sams[] = { KEY_WRITE, KEY_READ, KEY_SET_VALUE, KEY_READ | KEY_WRITE, WRITE_DAC };
        unsigned int i;
        for (i = 0; i < sizeof(sams) / sizeof(sams[0]); i++)
        {
            HKEY k2 = NULL;
            err = RegOpenKeyExW(root, L"Software\\WineTestKeyWriteDacl", 0, sams[i], &k2);
            if (err) { printf("reopen sam=%#lx: open error %lu\n", sams[i], err); continue; }
            {
                PACL d = NULL; PSECURITY_DESCRIPTOR s2 = NULL;
                err = GetSecurityInfo(k2, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION, NULL, NULL, &d, NULL, &s2);
                printf("reopen sam=%#lx: GetSecurityInfo %lu\n", sams[i], err);
                if (s2) LocalFree(s2);
            }
            err = SetSecurityInfo(k2, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION, NULL, NULL, new_dacl, NULL);
            printf("reopen sam=%#lx: SetSecurityInfo %lu\n", sams[i], err);
            RegCloseKey(k2);
        }
    }
    RegDeleteKeyW(root, L"Software\\WineTestKeyWriteDacl");
    return 0;
}
