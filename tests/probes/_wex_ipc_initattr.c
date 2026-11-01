/*
 * _wex_ipc_initattr.c - Determine the working call pattern for
 * InitializeProcThreadAttributeList in Wine.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

static LONG WINAPI exfilt(EXCEPTION_POINTERS *ep) {
    printf("EXC code=0x%lx at %p\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    fflush(stdout); ExitProcess(98);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    SetUnhandledExceptionFilter(exfilt);
    const DWORD count = 1;

    /* Pattern P1: two-phase (query with NULL, HeapAlloc, init in place) */
    printf("P1: two-phase\n"); fflush(stdout);
    {
        SIZE_T size = 0;
        BOOL q = InitializeProcThreadAttributeList(NULL, count, 0, &size);
        printf("  P1 query r=%d size=%lu err=%lu\n", (int)q, (unsigned long)size, GetLastError()); fflush(stdout);
        if (size > 0) {
            LPVOID mem = HeapAlloc(GetProcessHeap(), 0, size);
            BOOL r = InitializeProcThreadAttributeList((LPPROC_THREAD_ATTRIBUTE_LIST)mem, count, 0, &size);
            printf("  P1 init   r=%d mem=%p err=%lu\n", (int)r, mem, GetLastError()); fflush(stdout);
            if (r && mem) {
                HANDLE h = (HANDLE)(uintptr_t)0x3c; SIZE_T d=0;
                BOOL u = UpdateProcThreadAttribute((LPPROC_THREAD_ATTRIBUTE_LIST)mem, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &h, sizeof(HANDLE), NULL, &d);
                printf("  P1 update r=%d err=%lu\n", (int)u, GetLastError()); fflush(stdout);
                DeleteProcThreadAttributeList((LPPROC_THREAD_ATTRIBUTE_LIST)mem);
                HeapFree(GetProcessHeap(), 0, mem);
            }
        }
    }

    /* Pattern P2: single-phase, &pList + &size(0) */
    printf("P2: single-phase &pList &size\n"); fflush(stdout);
    {
        LPPROC_THREAD_ATTRIBUTE_LIST pList = NULL; SIZE_T size = 0;
        BOOL r = InitializeProcThreadAttributeList((LPPROC_THREAD_ATTRIBUTE_LIST)&pList, count, 0, &size);
        printf("  P2 init r=%d pList=%p size=%lu err=%lu\n", (int)r, (void*)pList, (unsigned long)size, GetLastError()); fflush(stdout);
    }

    /* Pattern P3: single-phase, &pList + NULL size */
    printf("P3: single-phase &pList NULL-size\n"); fflush(stdout);
    {
        LPPROC_THREAD_ATTRIBUTE_LIST pList = NULL;
        BOOL r = InitializeProcThreadAttributeList((LPPROC_THREAD_ATTRIBUTE_LIST)&pList, count, 0, NULL);
        printf("  P3 init r=%d pList=%p err=%lu\n", (int)r, (void*)pList, GetLastError()); fflush(stdout);
    }

    printf("DONE\n"); fflush(stdout);
    return 0;
}
