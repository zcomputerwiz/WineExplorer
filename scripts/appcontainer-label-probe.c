/*
 * WineExplorer - mandatory label probe for SetNamedSecurityInfoW()
 *
 * Copyright 2026 WineExplorer Desktop Environment Project
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/* Sets and reads the mandatory label of a scratch directory and file the way Chromium's AppContainer profile code
 * does (DACL | LABEL | UNPROTECTED_DACL in one SetNamedSecurityInfoW call) and prints the Win32 error of every call,
 * so that Wine and a native Windows host can be compared line by line.
 * Build both flavors; the 32-bit one is the WoW64 regression check:
 *   x86_64-w64-mingw32-gcc -O2 -Wall -o appcontainer-label-probe64.exe appcontainer-label-probe.c -ladvapi32
 *   i686-w64-mingw32-gcc   -O2 -Wall -o appcontainer-label-probe32.exe appcontainer-label-probe.c -ladvapi32
 * Usage: appcontainer-label-probe.exe
 * Exit status: 0 ran to the end, 1 setup failed. The results are the output, not the exit status.
 * Run as a standard user. Measured on a native Windows host and on Wine with the advapi32 label fix (see
 * docs/CHROMIUM_APPCONTAINER_LABEL_20261101.md): every call returns 0, except that Windows rejects a system
 * integrity label with 1299 and both reject SetSecurityInfo() on a handle without WRITE_OWNER with 5. Without the
 * fix Wine returns 5 for every SetNamedSecurityInfoW() call that carries a label.
 */

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <stdio.h>

#ifndef LABEL_SECURITY_INFORMATION
#define LABEL_SECURITY_INFORMATION 0x00000010L
#endif

static void show(const char *what, DWORD err)
{
    printf("%-64s -> %lu\n", what, err);
}

static BOOL make_label_acl(ACL *acl, DWORD size, DWORD flags, DWORD mask, const char *sid_string)
{
    PSID sid;
    BOOL ret;

    if (!ConvertStringSidToSidA(sid_string, &sid)) return FALSE;
    ret = InitializeAcl(acl, size, ACL_REVISION) && AddMandatoryAce(acl, ACL_REVISION, flags, mask, sid);
    LocalFree(sid);
    return ret;
}

static void dump_label(const char *what, const WCHAR *path)
{
    PSECURITY_DESCRIPTOR sd = NULL;
    ACL *sacl = NULL;
    char *str = NULL;
    DWORD err = GetNamedSecurityInfoW(path, SE_FILE_OBJECT, LABEL_SECURITY_INFORMATION, NULL, NULL, NULL, &sacl, &sd);

    printf("%-64s -> get err %lu sacl=%p", what, err, (void *)sacl);
    if (!err && sd && ConvertSecurityDescriptorToStringSecurityDescriptorA(sd, SDDL_REVISION_1,
                                                                           LABEL_SECURITY_INFORMATION, &str, NULL))
    {
        printf(" sddl=%s", str);
        LocalFree(str);
    }
    printf("\n");
    if (sd) LocalFree(sd);
}

static HANDLE open_dir(const WCHAR *dir, DWORD access)
{
    return CreateFileW(dir, access | READ_CONTROL | SYNCHRONIZE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
}

int main(void)
{
    SID_IDENTIFIER_AUTHORITY world = {SECURITY_WORLD_SID_AUTHORITY};
    char low_buf[128], system_buf[128];
    ACL *low = (ACL *)low_buf, *system = (ACL *)system_buf;
    ACL *dacl = NULL, *new_dacl = NULL;
    PSECURITY_DESCRIPTOR sd = NULL;
    WCHAR dir[MAX_PATH], file[MAX_PATH];
    EXPLICIT_ACCESSW ea;
    PSID everyone;
    HANDLE h;
    DWORD err;

    GetTempPathW(MAX_PATH, dir);
    wsprintfW(dir + lstrlenW(dir), L"wexlbl_%lu", GetCurrentProcessId());
    wsprintfW(file, L"%s\\f.txt", dir);
    printf("dir=%ls\n", dir);
    if (!CreateDirectoryW(dir, NULL))
    {
        printf("CreateDirectory failed %lu\n", GetLastError());
        return 1;
    }
    h = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);

    if (!AllocateAndInitializeSid(&world, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &everyone))
    {
        printf("AllocateAndInitializeSid failed %lu\n", GetLastError());
        return 1;
    }

    err = GetNamedSecurityInfoW(dir, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, &dacl, NULL, &sd);
    show("GetNamedSecurityInfoW(dir, DACL)", err);
    memset(&ea, 0, sizeof(ea));
    ea.grfAccessPermissions = GENERIC_ALL;
    ea.grfAccessMode = GRANT_ACCESS;
    ea.grfInheritance = CONTAINER_INHERIT_ACE | OBJECT_INHERIT_ACE;
    BuildTrusteeWithSidW(&ea.Trustee, everyone);
    err = SetEntriesInAclW(1, &ea, dacl, &new_dacl);
    show("SetEntriesInAclW", err);

    /* S-1-16-4096 is the low integrity level, S-1-16-16384 the system integrity level. */
    if (!make_label_acl(low, sizeof(low_buf), OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE,
                        SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, "S-1-16-4096"))
        printf("make_label_acl (low) failed %lu\n", GetLastError());
    if (!make_label_acl(system, sizeof(system_buf), 0, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, "S-1-16-16384"))
        printf("make_label_acl (system) failed %lu\n", GetLastError());

    err = SetNamedSecurityInfoW(dir, SE_FILE_OBJECT,
                                DACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION,
                                NULL, NULL, new_dacl, low);
    show("SetNamedSecurityInfoW(dir, DACL|LABEL|UNPROTECTED_DACL)", err);
    dump_label("after DACL|LABEL set on dir", dir);

    err = SetNamedSecurityInfoW(dir, SE_FILE_OBJECT, LABEL_SECURITY_INFORMATION, NULL, NULL, NULL, low);
    show("SetNamedSecurityInfoW(dir, LABEL only)", err);

    err = SetNamedSecurityInfoW(file, SE_FILE_OBJECT, LABEL_SECURITY_INFORMATION, NULL, NULL, NULL, low);
    show("SetNamedSecurityInfoW(file, LABEL only)", err);
    dump_label("after LABEL set on file", file);

    err = SetNamedSecurityInfoW(dir, SE_FILE_OBJECT, LABEL_SECURITY_INFORMATION, NULL, NULL, NULL, system);
    show("SetNamedSecurityInfoW(dir, LABEL system IL)", err);

    h = open_dir(dir, WRITE_DAC);
    if (h == INVALID_HANDLE_VALUE) printf("open WRITE_DAC failed %lu\n", GetLastError());
    else
    {
        err = SetSecurityInfo(h, SE_FILE_OBJECT, LABEL_SECURITY_INFORMATION, NULL, NULL, NULL, low);
        show("SetSecurityInfo(handle WRITE_DAC only, LABEL)", err);
        CloseHandle(h);
    }
    h = open_dir(dir, WRITE_OWNER);
    if (h == INVALID_HANDLE_VALUE) printf("open WRITE_OWNER failed %lu\n", GetLastError());
    else
    {
        err = SetSecurityInfo(h, SE_FILE_OBJECT, LABEL_SECURITY_INFORMATION, NULL, NULL, NULL, low);
        show("SetSecurityInfo(handle WRITE_OWNER, LABEL)", err);
        CloseHandle(h);
    }

    LocalFree(new_dacl);
    LocalFree(sd);
    FreeSid(everyone);
    DeleteFileW(file);
    RemoveDirectoryW(dir);
    printf("done (%u-bit)\n", (unsigned int)(8 * sizeof(void *)));
    return 0;
}
