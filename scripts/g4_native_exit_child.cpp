// SPDX-License-Identifier: GPL-2.0-or-later
// Original calibration child ONLY; never executed by offline tests in Wine.
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <stdio.h>
#include "g4_native_exit_contract.h"
int wmain(int argc,wchar_t **argv) {
    if (argc!=5 || wcscmp(argv[1],L"--exit-status") || wcscmp(argv[2],L"3221225599") ||
        wcscmp(argv[3],L"--nonce") || !wex_nonce(argv[4])) return 2;
    printf("original native exit calibration child; status=0xc000007f\n");fflush(stdout);
    Sleep(1000);ExitProcess(0xc000007fUL);
}
