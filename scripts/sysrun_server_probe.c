/*
 * Windows-side probe for scripts/test_sysrun_server.sh: what a client does against a system role server.
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
 * Usage: probe.exe <mode> [machine-root-as-Z:-path]
 *   files-denied   a limited client cannot create, write, rename or delete in the machine root
 *   files-allowed  a client with the system group can (positive control)
 *   registry       HKU\S-1-5-18 and HKU\.DEFAULT are not writable, HKCU is
 *   hklm-read      HKLM\Software\WexProbe\FromMachine is "machine" (the value comes from the machine root)
 *   hklm-write     writes HKLM\Software\WexProbe\Written
 *   hold           sleeps 120 seconds, to keep the server alive
 * Prints "name=PASS" or "FAIL ..." lines; exit status 0 only if everything passed.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int failures;

static void check(int ok, const char *what)
{
    printf(ok ? "ok %s\n" : "FAIL %s (error %lu)\n", what, GetLastError());
    if (!ok) failures++;
}

static WCHAR machine[MAX_PATH];

static void path_of(WCHAR *out, const WCHAR *rel)
{
    swprintf(out, MAX_PATH, L"%ls\\%ls", machine, rel);
}

static BOOL exists(const WCHAR *path)
{
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static BOOL content_is(const WCHAR *path, const char *want)
{
    char buf[64] = "";
    DWORD n = 0;
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, 0, NULL);

    if (h == INVALID_HANDLE_VALUE) return FALSE;
    ReadFile(h, buf, sizeof(buf) - 1, &n, NULL);
    CloseHandle(h);
    return !strcmp(buf, want);
}

/* every way the server opens a Unix path for a client, against files of the machine root */
static void files_denied(void)
{
    WCHAR marker[MAX_PATH], newfile[MAX_PATH], newdir[MAX_PATH], moved[MAX_PATH], services[MAX_PATH];
    HANDLE h;
    DWORD n;

    path_of(marker, L"drive_c\\windows\\wex_marker.txt");
    path_of(newfile, L"drive_c\\windows\\wex_probe_new.txt");
    path_of(newdir, L"drive_c\\windows\\wex_probe_dir");
    path_of(moved, L"drive_c\\windows\\wex_probe_moved.txt");
    path_of(services, L"drive_c\\windows\\system32\\services.exe");
    check(content_is(marker, "MARKER"), "marker readable");

    /* create (open_fd with O_CREAT) through the machine root and through the user's prefix links */
    h = CreateFileW(newfile, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    check(h == INVALID_HANDLE_VALUE, "create in machine root fails");
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    check(!exists(newfile), "no file was created in the machine root");
    h = CreateFileW(L"C:\\windows\\wex_probe_new2.txt", GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    check(h == INVALID_HANDLE_VALUE, "create through C:\\windows fails");
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    check(!exists(L"C:\\windows\\wex_probe_new2.txt"), "no file was created through C:\\windows");

    /* open an existing file for write, with and without truncation */
    h = CreateFileW(marker, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    check(h == INVALID_HANDLE_VALUE, "open marker for write fails");
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    h = CreateFileW(marker, GENERIC_WRITE, FILE_SHARE_READ, NULL, TRUNCATE_EXISTING, 0, NULL);
    check(h == INVALID_HANDLE_VALUE, "truncate marker fails");
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    h = CreateFileW(services, GENERIC_WRITE | GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    check(h == INVALID_HANDLE_VALUE, "open services.exe for write fails");
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    check(content_is(marker, "MARKER"), "marker content unchanged");

    /* mkdir (open_fd with a directory create) */
    check(!CreateDirectoryW(newdir, NULL), "mkdir in machine root fails");
    check(!exists(newdir), "no directory was created");

    /* rename and hard link (set_fd_name_info), delete (disposition, delete on close) */
    check(!MoveFileW(marker, moved), "rename marker fails");
    check(!CreateHardLinkW(moved, marker, NULL), "hard link to marker fails");
    DeleteFileW(marker);
    check(exists(marker) && content_is(marker, "MARKER"), "marker still there after DeleteFile");
    h = CreateFileW(marker, DELETE | GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                    OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    check(exists(marker) && content_is(marker, "MARKER"), "marker still there after delete on close");
    check(!exists(moved), "marker was not renamed");

    /* writing a file of the user's own prefix still works (the user's rights are not reduced) */
    h = CreateFileW(L"C:\\users\\Public\\wex_user_probe.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    check(h == INVALID_HANDLE_VALUE, "create in the shared users\\Public (machine root) fails");
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    n = GetEnvironmentVariableW(L"APPDATA", newfile, MAX_PATH);
    wcscat(newfile, L"\\wex_user_probe.txt");
    h = CreateFileW(newfile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    check(n && h != INVALID_HANDLE_VALUE, "create in the user's own profile (APPDATA) works");
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

/* a client with the system group: the same operations succeed */
static void files_allowed(void)
{
    WCHAR file[MAX_PATH], dir[MAX_PATH], moved[MAX_PATH], temp[MAX_PATH];
    HANDLE h;
    DWORD n;

    path_of(file, L"drive_c\\windows\\wex_wexsys_made.txt");
    path_of(dir, L"drive_c\\windows\\wex_wexsys_dir");
    path_of(moved, L"drive_c\\windows\\wex_wexsys_moved.txt");
    path_of(temp, L"drive_c\\windows\\wex_wexsys_temp.txt");
    h = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL);
    check(h != INVALID_HANDLE_VALUE, "system group client creates a file in the machine root");
    if (h != INVALID_HANDLE_VALUE)
    {
        WriteFile(h, "MADE", 4, &n, NULL);
        CloseHandle(h);
    }
    check(CreateDirectoryW(dir, NULL), "system group client creates a directory");
    check(MoveFileW(file, moved), "system group client renames a file");
    h = CreateFileW(temp, GENERIC_WRITE | DELETE, 0, NULL, CREATE_NEW, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    check(h != INVALID_HANDLE_VALUE, "system group client creates a delete-on-close file");
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    check(!exists(temp), "the delete-on-close file of the system group client is gone");
    check(DeleteFileW(moved), "system group client deletes a file");
    check(RemoveDirectoryW(dir), "system group client removes a directory");
}

static void registry(void)
{
    HKEY key;
    LONG ret;
    DWORD disp, one = 1;
    static const WCHAR *paths[] = { L"S-1-5-18\\Software\\WexProbe", L"S-1-5-19\\Software\\WexProbe",
                                    L"S-1-5-20\\Software\\WexProbe", L".DEFAULT\\Software\\WexProbe",
                                    L"S-1-5-18", L".DEFAULT", NULL };
    int i;

    for (i = 0; paths[i]; i++)
    {
        char what[128];

        ret = RegCreateKeyExW(HKEY_USERS, paths[i], 0, NULL, 0, KEY_ALL_ACCESS, NULL, &key, &disp);
        if (!ret)
        {
            /* an existing key opened for all access would be the failure; close it */
            RegCloseKey(key);
        }
        snprintf(what, sizeof(what), "HKU\\%ls cannot be opened for write (error %ld)", paths[i], ret);
        check(ret == ERROR_ACCESS_DENIED, what);
        ret = RegOpenKeyExW(HKEY_USERS, paths[i], 0, KEY_SET_VALUE, &key);
        if (!ret) RegCloseKey(key);
        snprintf(what, sizeof(what), "HKU\\%ls cannot be opened for KEY_SET_VALUE (error %ld)", paths[i], ret);
        check(ret == ERROR_ACCESS_DENIED || ret == ERROR_FILE_NOT_FOUND, what);
    }
    ret = RegOpenKeyExW(HKEY_USERS, L"S-1-5-18", 0, KEY_READ, &key);
    check(!ret, "HKU\\S-1-5-18 can be read");
    if (!ret) RegCloseKey(key);
    ret = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\WexProbe", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &key, &disp);
    check(!ret, "HKCU can be written");
    if (!ret)
    {
        check(!RegSetValueExW(key, L"One", 0, REG_DWORD, (BYTE *)&one, sizeof(one)), "HKCU value can be set");
        RegCloseKey(key);
        RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\WexProbe");
    }
}

static void hklm_read(void)
{
    HKEY key;
    WCHAR buf[64] = L"";
    DWORD size = sizeof(buf), type;
    LONG ret = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\WexProbe", 0, KEY_READ, &key);

    check(!ret, "HKLM\\Software\\WexProbe exists");
    if (ret) return;
    ret = RegQueryValueExW(key, L"FromMachine", NULL, &type, (BYTE *)buf, &size);
    RegCloseKey(key);
    check(!ret && !wcscmp(buf, L"machine"), "HKLM value comes from the machine root hive");
}

static void hklm_write(void)
{
    HKEY key;
    DWORD disp;
    LONG ret = RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"Software\\WexProbe", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &key, &disp);

    check(!ret, "HKLM\\Software\\WexProbe opens for write");
    if (ret) return;
    check(!RegSetValueExW(key, L"Written", 0, REG_SZ, (const BYTE *)L"yes", 8), "HKLM value written");
    RegCloseKey(key);
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "";

    if (argc > 2) MultiByteToWideChar(CP_ACP, 0, argv[2], -1, machine, MAX_PATH);
    if (!strcmp(mode, "files-denied")) files_denied();
    else if (!strcmp(mode, "files-allowed")) files_allowed();
    else if (!strcmp(mode, "registry")) registry();
    else if (!strcmp(mode, "hklm-read")) hklm_read();
    else if (!strcmp(mode, "hklm-write")) hklm_write();
    else if (!strcmp(mode, "hold")) Sleep(120000);
    else { printf("usage: probe <mode> [machine-root]\n"); return 2; }
    printf("%s=%s\n", mode, failures ? "FAIL" : "PASS");
    return failures != 0;
}
