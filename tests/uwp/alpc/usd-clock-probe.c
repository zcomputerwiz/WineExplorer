/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Probe (Windows 11 and Wine): do the clocks in KUSER_SHARED_DATA (0x7ffe0000) advance as a program reads them directly:
 * InterruptTime (+0x08), SystemTime (+0x14), TickCount (+0x320), and how do they compare with QueryPerformanceCounter and
 * QueryInterruptTime / QueryUnbiasedInterruptTime?
 */
#include <windows.h>
#include <stdio.h>

static ULONG64 ksystime(ULONG off)
{
    volatile ULONG *p = (volatile ULONG *)(0x7ffe0000 + off);
    ULONG hi, lo, hi2;
    do { hi = p[1]; lo = p[0]; hi2 = p[2]; } while (hi != hi2);
    return ((ULONG64)hi << 32) | lo;
}

int main(void)
{
    LARGE_INTEGER f, q;
    ULONGLONG ub;
    int i;

    QueryPerformanceFrequency(&f);
    printf("qpc frequency %lld\n", f.QuadPart);
    for (i = 0; i < 6; i++)
    {
        QueryPerformanceCounter(&q);
        QueryUnbiasedInterruptTime(&ub);
        printf("interrupt %llu  system %llu  tick %llu  qpc %lld  unbiased %llu\n", ksystime(0x08), ksystime(0x14),
               ksystime(0x320), q.QuadPart, ub);
        Sleep(i < 3 ? 1 : 40);
    }
    {
        ULONG64 a = ksystime(0x08), b;
        DWORD t = GetTickCount();
        while (GetTickCount() - t < 30) ;
        b = ksystime(0x08);
        printf("busy 30 ms: interrupt time advanced by %llu (100 ns units)\n", b - a);
    }
    return 0;
}
