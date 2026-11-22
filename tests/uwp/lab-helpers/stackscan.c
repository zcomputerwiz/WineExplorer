/*
 * Copyright 2026 WineExplorer contributors
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later
 * version. SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Lab tool: what is a program waiting for. For every thread of the first process whose executable name contains the
 * argument it suspends the thread, reads its stack (up to 128 KiB above the stack pointer) and prints every value that points
 * into a module of the process, as module+offset: a conservative call chain, enough to find the code that waits when the
 * program has no symbols. usage: stackscan NAME_PART
 */
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

struct mod
{
    ULONG_PTR base, size;
    char name[64];
};

static struct mod mods[256];
static int mod_count;

static BOOL has_part( const char *name, const char *part )
{
    char a[MAX_PATH], b[MAX_PATH];
    int i;

    for (i = 0; name[i] && i < MAX_PATH - 1; i++) a[i] = tolower( name[i] );
    a[i] = 0;
    for (i = 0; part[i] && i < MAX_PATH - 1; i++) b[i] = tolower( part[i] );
    b[i] = 0;
    return strstr( a, b ) != NULL;
}

int main( int argc, char **argv )
{
    HANDLE snap, process;
    PROCESSENTRY32 pe = { sizeof(pe) };
    MODULEENTRY32 me = { sizeof(me) };
    THREADENTRY32 te = { sizeof(te) };
    DWORD pid = 0;

    if (argc < 2) return 2;
    snap = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
    for (Process32First( snap, &pe ); ; )
    {
        if (has_part( pe.szExeFile, argv[1] )) { pid = pe.th32ProcessID; break; }
        if (!Process32Next( snap, &pe )) break;
    }
    CloseHandle( snap );
    if (!pid) { printf( "no process\n" ); return 1; }
    printf( "pid %lu\n", pid );
    if (!(process = OpenProcess( PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid ))) { printf( "OpenProcess %lu\n", GetLastError() ); return 1; }

    snap = CreateToolhelp32Snapshot( TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid );
    if (snap != INVALID_HANDLE_VALUE && Module32First( snap, &me ))
    {
        do
        {
            if (mod_count < 256)
            {
                mods[mod_count].base = (ULONG_PTR)me.modBaseAddr;
                mods[mod_count].size = me.modBaseSize;
                lstrcpynA( mods[mod_count].name, me.szModule, sizeof(mods[0].name) );
                mod_count++;
            }
        } while (Module32Next( snap, &me ));
        CloseHandle( snap );
    }
    printf( "%d modules\n", mod_count );

    snap = CreateToolhelp32Snapshot( TH32CS_SNAPTHREAD, 0 );
    for (Thread32First( snap, &te ); ; )
    {
        if (te.th32OwnerProcessID == pid)
        {
            HANDLE thread = OpenThread( THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, te.th32ThreadID );
            CONTEXT ctx = { 0 };
            ULONG_PTR *stack;
            SIZE_T got = 0, i;
            int shown = 0;

            printf( "=== thread %lu open %p err %lu\n", te.th32ThreadID, (void *)thread, GetLastError() );
            if (thread && SuspendThread( thread ) == (DWORD)-1) printf( "SuspendThread err %lu\n", GetLastError() );
            else if (thread)
            {
                ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
                if (!GetThreadContext( thread, &ctx )) printf( "GetThreadContext err %lu\n", GetLastError() );
                else
                {
                    stack = malloc( 128 * 1024 );
                    ReadProcessMemory( process, (void *)ctx.Rsp, stack, 128 * 1024, &got );
                    printf( "rip %p rsp %p read %zu\n", (void *)ctx.Rip, (void *)ctx.Rsp, (size_t)got );
                    for (i = 0; i < got / sizeof(*stack) && shown < 48; i++)
                    {
                        int m;
                        for (m = 0; m < mod_count; m++)
                            if (stack[i] >= mods[m].base && stack[i] < mods[m].base + mods[m].size)
                            {
                                if (!has_part( mods[m].name, "kernel" ) && !has_part( mods[m].name, "ntdll" ))
                                {
                                    printf( "  +%04zx  %s+%#llx\n", i * 8, mods[m].name, (unsigned long long)(stack[i] - mods[m].base) );
                                    shown++;
                                }
                                break;
                            }
                    }
                    free( stack );
                }
                ResumeThread( thread );
            }
            if (thread) CloseHandle( thread );
        }
        if (!Thread32Next( snap, &te )) break;
    }
    CloseHandle( snap );
    return 0;
}
