/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * g4_memory_probe: tiny read-only diagnostic PE for the Chromium resource-pressure
 * work order (WO-20261027-CHROMIUM-RESOURCE-PRESSURE).
 *
 * It calls GlobalMemoryStatusEx exactly once and prints a single-line JSON object
 * with the bounded MEMORYSTATUSEX fields plus the return value / GetLastError.
 * It performs no allocation, no I/O other than one stdout write, and no state
 * change -- it is a pure read of what the running Wine build reports to a Windows
 * caller, so it exercises the real kernelbase/ntdll GlobalMemoryStatusEx path
 * (the same one Chromium's SystemMemoryPressureEvaluator consumes).
 *
 * Build (x86_64, matching chrome.exe) under WSL Debian mingw-w64:
 *   x86_64-w64-mingw32-gcc -O2 -static -o memory-probe.exe g4_memory_probe.cpp
 *
 * The .exe is a build artifact and must NOT be committed; only this source is.
 */

#include <windows.h>
#include <stdio.h>

int main(void)
{
    MEMORYSTATUSEX msx;
    BOOL ok;
    DWORD err;

    msx.dwLength = sizeof(msx);
    ok  = GlobalMemoryStatusEx( &msx );
    err = GetLastError();   /* only meaningful when ok == FALSE */

    printf( "{\"ok\":%s,\"getlasterror\":%llu,"
            "\"dwMemoryLoad\":%llu,"
            "\"ullTotalPhys\":%llu,\"ullAvailPhys\":%llu,"
            "\"ullTotalPageFile\":%llu,\"ullAvailPageFile\":%llu,"
            "\"ullTotalVirtual\":%llu,\"ullAvailVirtual\":%llu}\n",
            ok ? "true" : "false",
            (unsigned long long)err,
            (unsigned long long)msx.dwMemoryLoad,
            (unsigned long long)msx.ullTotalPhys,
            (unsigned long long)msx.ullAvailPhys,
            (unsigned long long)msx.ullTotalPageFile,
            (unsigned long long)msx.ullAvailPageFile,
            (unsigned long long)msx.ullTotalVirtual,
            (unsigned long long)msx.ullAvailVirtual );

    fflush( stdout );
    return 0;
}
