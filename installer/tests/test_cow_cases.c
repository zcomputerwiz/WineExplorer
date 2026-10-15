/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2026 WineExplorer contributors */

#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "ntdll.lib")

int test_z_symlink(void)
{
    printf("=== Test: Symlink in Z:\\ to an ordinary file writes through ===\n");
    const char *path = "Z:\\tmp\\cow_test_user_link.txt";
    HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FAIL: CreateFileA(%s) failed: %lu\n", path, GetLastError());
        return 1;
    }
    const char data[] = "MODIFIED_CONTENT_THROUGH_SYMLINK";
    DWORD written = 0;
    if (!WriteFile(h, data, sizeof(data), &written, NULL) || written != sizeof(data)) {
        printf("FAIL: WriteFile failed: %lu\n", GetLastError());
        CloseHandle(h);
        return 2;
    }
    CloseHandle(h);
    printf("SUCCESS: Wrote %lu bytes through Z:\\ symlink\n", written);
    return 0;
}

int test_relative_open(void)
{
    printf("=== Test: Relative open with RootDirectory on built-in symlink ===\n");
    HANDLE hDir = CreateFileW(L"C:\\windows\\system32", FILE_LIST_DIRECTORY | SYNCHRONIZE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (hDir == INVALID_HANDLE_VALUE) {
        printf("FAIL: Open system32 directory handle failed: %lu\n", GetLastError());
        return 1;
    }

    UNICODE_STRING file_name;
    RtlInitUnicodeString(&file_name, L"msvcp140.dll");

    OBJECT_ATTRIBUTES attr;
    InitializeObjectAttributes(&attr, &file_name, OBJ_CASE_INSENSITIVE, hDir, NULL);

    HANDLE hFile = NULL;
    IO_STATUS_BLOCK io;
    NTSTATUS status = NtCreateFile(&hFile, GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, &attr, &io, NULL,
                                   FILE_ATTRIBUTE_NORMAL, 0, FILE_OPEN,
                                   FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    CloseHandle(hDir);

    if (status != 0) {
        printf("FAIL: NtCreateFile relative open failed: 0x%08lx\n", status);
        return 2;
    }

    const char data[] = "COW_RELATIVE_TEST_MODIFIED";
    DWORD written = 0;
    if (!WriteFile(hFile, data, sizeof(data), &written, NULL) || written != sizeof(data)) {
        printf("FAIL: WriteFile to relative handle failed: %lu\n", GetLastError());
        CloseHandle(hFile);
        return 3;
    }
    CloseHandle(hFile);
    printf("SUCCESS: Relative open broke built-in symlink and wrote %lu bytes\n", written);
    return 0;
}

int test_delete_symlink(void)
{
    printf("=== Test: DeleteFileW on built-in symlink ===\n");
    const WCHAR *path = L"C:\\windows\\system32\\version.dll";
    if (!DeleteFileW(path)) {
        printf("FAIL: DeleteFileW failed: %lu\n", GetLastError());
        return 1;
    }
    printf("SUCCESS: DeleteFileW unlinked built-in symlink\n");
    return 0;
}

int test_relative_symlink_target(void)
{
    printf("=== Test: Symlink with relative target path breaks on write ===\n");
    const char *path = "C:\\windows\\system32\\rel_target_test.dll";
    HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FAIL: CreateFileA(%s) failed: %lu\n", path, GetLastError());
        return 1;
    }
    const char data[] = "REL_TARGET_COW_MODIFIED";
    DWORD written = 0;
    if (!WriteFile(h, data, sizeof(data), &written, NULL) || written != sizeof(data)) {
        printf("FAIL: WriteFile failed: %lu\n", GetLastError());
        CloseHandle(h);
        return 2;
    }
    CloseHandle(h);
    printf("SUCCESS: Wrote %lu bytes, broke relative-target symlink\n", written);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "z_symlink"))
        return test_z_symlink();
    if (argc > 1 && !strcmp(argv[1], "relative_open"))
        return test_relative_open();
    if (argc > 1 && !strcmp(argv[1], "delete_symlink"))
        return test_delete_symlink();
    if (argc > 1 && !strcmp(argv[1], "relative_target"))
        return test_relative_symlink_target();

    printf("Running all COW tests...\n");
    int r = 0;
    r |= test_z_symlink();
    r |= test_relative_open();
    r |= test_delete_symlink();
    r |= test_relative_symlink_target();
    return r;
}
