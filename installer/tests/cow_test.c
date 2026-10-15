/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2026 WineExplorer contributors */
#include <windows.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "C:\\windows\\system32\\msvcp140.dll";
    printf("Opening %s for write/truncate...\n", path);

    HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FAIL: CreateFileA failed: %lu\n", GetLastError());
        return 1;
    }

    const char data[] = "COW_TEST_MODIFIED_DATA_1234567890";
    DWORD written = 0;
    if (!WriteFile(h, data, sizeof(data), &written, NULL) || written != sizeof(data)) {
        printf("FAIL: WriteFile failed: %lu\n", GetLastError());
        CloseHandle(h);
        return 2;
    }
    CloseHandle(h);
    printf("SUCCESS: Wrote %lu bytes to %s\n", written, path);
    return 0;
}
