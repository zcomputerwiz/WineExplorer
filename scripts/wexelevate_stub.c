/*
 * The approval stub of the test harness (docs/ELEVATION_BROKER_WORKORDER.md, B4). It exists only in the test
 * harness: scripts/test_sysrun_server.sh builds it, with a broker built with -DWEXELEVATE_TEST_APPROVAL, into a
 * throw-away machine root. No shipping build contains it or calls it.
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
 * Usage: wexelevate_stub.exe <request id> <image>
 * Writes started.<name of the image> to the policy directory, waits for the file "gate" there, then approves
 * (exit code 0) if approve.<name of the image> exists and refuses (exit code 1) otherwise. With no approve.all and
 * no gate file in the directory it decides at once (the directory is where the harness puts its answers).
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define POLICY_DIR "Z:\\tmp\\wexelev\\"

int main(int argc, char **argv)
{
    char name[MAX_PATH], path[2 * MAX_PATH], *base;
    DWORD i;
    FILE *f;

    if (argc < 3) return 2;
    base = strrchr(argv[2], '\\');
    strncpy(name, base ? base + 1 : argv[2], sizeof(name) - 1);
    name[sizeof(name) - 1] = 0;

    sprintf(path, POLICY_DIR "started.%s", name);
    if ((f = fopen(path, "w")))
    {
        fprintf(f, "%s\n", argv[1]);
        fclose(f);
    }
    /* wait for the harness to open the gate: it lets the requests be pending together */
    if (GetFileAttributesA(POLICY_DIR "wait-for-gate") != INVALID_FILE_ATTRIBUTES)
        for (i = 0; i < 600 && GetFileAttributesA(POLICY_DIR "gate") == INVALID_FILE_ATTRIBUTES; i++) Sleep(100);

    sprintf(path, POLICY_DIR "approve.%s", name);
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES || GetFileAttributesA(POLICY_DIR "approve.all") != INVALID_FILE_ATTRIBUTES)
        return 0;
    return 1;
}
