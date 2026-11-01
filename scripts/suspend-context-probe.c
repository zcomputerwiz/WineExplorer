/*
 * WineExplorer - thread suspend/context probe
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

/* A sampler thread suspends the main thread, reads its context and resumes it, over and over, as Chromium's
 * stack sampling profiler does. If the first thread's data block is not aligned to the signal stack size, the
 * suspend signal handler of the main thread reads a wrong thread_data and the sampler never gets a context.
 * Build both flavors; the 32-bit one is the WoW64 regression check:
 *   x86_64-w64-mingw32-gcc -O2 -Wall -o suspend-context-probe64.exe suspend-context-probe.c
 *   i686-w64-mingw32-gcc   -O2 -Wall -o suspend-context-probe32.exe suspend-context-probe.c
 * Usage: suspend-context-probe.exe [iterations]    (default 1000)
 * Exit status: 0 ok, 1 setup failed, 2 SuspendThread failed, 3 GetThreadContext failed, 4 no progress for 10 s,
 * 5 ResumeThread failed.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static HANDLE main_thread;
static volatile LONG done_flag;
static volatile LONG iterations;

static DWORD WINAPI sampler( void *arg )
{
    int count = (int)(INT_PTR)arg, i;
    CONTEXT ctx;

    for (i = 0; i < count; i++)
    {
        if (SuspendThread( main_thread ) == ~0u)
        {
            printf( "SuspendThread failed: %lu\n", GetLastError() );
            fflush( stdout );
            ExitProcess( 2 );
        }
        memset( &ctx, 0, sizeof(ctx) );
        ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        if (!GetThreadContext( main_thread, &ctx ))
        {
            printf( "GetThreadContext failed: %lu\n", GetLastError() );
            fflush( stdout );
            ExitProcess( 3 );
        }
        if (ResumeThread( main_thread ) == ~0u)
        {
            printf( "ResumeThread failed: %lu\n", GetLastError() );
            fflush( stdout );
            ExitProcess( 5 );
        }
        InterlockedIncrement( &iterations );
    }
    InterlockedExchange( &done_flag, 1 );
    return 0;
}

static DWORD WINAPI watchdog( void *arg )
{
    LONG last = -1;
    int idle = 0;

    (void)arg;
    while (!done_flag)
    {
        Sleep( 100 );
        if (iterations != last) idle = 0;
        else if (++idle >= 100)
        {
            printf( "stuck after %ld iterations\n", (long)iterations );
            fflush( stdout );
            TerminateProcess( GetCurrentProcess(), 4 );
        }
        last = iterations;
    }
    return 0;
}

int main( int argc, char **argv )
{
    int count = argc > 1 ? atoi( argv[1] ) : 1000;
    HANDLE thread;
    unsigned int n = 0;

    if (!DuplicateHandle( GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &main_thread,
                          0, FALSE, DUPLICATE_SAME_ACCESS ))
        return 1;
    thread = CreateThread( NULL, 0, sampler, (void *)(INT_PTR)count, 0, NULL );
    if (!thread || !CreateThread( NULL, 0, watchdog, NULL, 0, NULL )) return 1;
    while (!done_flag)
    {
        if (!(++n % 64)) Sleep( 1 );
        else SwitchToThread();
    }
    WaitForSingleObject( thread, INFINITE );
    printf( "ok %ld iterations, %d-bit\n", (long)iterations, (int)(8 * sizeof(void *)) );
    return 0;
}
