/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 * Hold the event consumer while native state transitions accumulate.
 */
#include <windows.h>
#include <stdio.h>
#include <wexhost.h>
static HANDLE mapped, resume;
static CRITICAL_SECTION lock;
static unsigned transitions;
static BOOL minimized;
static UINT64 window;
static void CALLBACK observed(const WEX_WM_EVENT *event, void *context)
{
    if (event->type == WEX_WM_MAP && !window)
    {
        window=event->window;
        minimized=!!(event->states&WEX_WM_STATE_MINIMIZED);
        SetEvent(mapped);
        WaitForSingleObject(resume,15000);
    }
    else if (event->window == window && event->type == WEX_WM_OBSERVED)
    {
        BOOL value=!!(event->states&WEX_WM_STATE_MINIMIZED);
        EnterCriticalSection(&lock);
        if (value != minimized)
        {
            minimized=value; transitions++;
            printf("STATE_TRANSITION %u %u\n",transitions,value);
        }
        LeaveCriticalSection(&lock);
    }
}
int main(int argc, char **argv)
{
    char gate[1024];
    DWORD start;
    FILE *file=NULL;
    unsigned count;
    BOOL passed;
    if (argc != 4 || !freopen(argv[3],"w",stdout)) return 2;
    setvbuf(stdout,NULL,_IONBF,0);
    InitializeCriticalSection(&lock);
    mapped=CreateEventW(NULL,TRUE,FALSE,NULL); resume=CreateEventW(NULL,TRUE,FALSE,NULL);
    if (!mapped || !resume || WexWmStartEx(WEX_WM_BACKEND_WAYLAND,0,observed,NULL)) return 2;
    if (WaitForSingleObject(mapped,10000) != WAIT_OBJECT_0) return 2;
    puts("READY_CONTROLLER_END");
    snprintf(gate,sizeof(gate),"%s.continue",argv[3]);
    start=GetTickCount();
    do { file=fopen(gate,"r"); if (file) break; Sleep(10); } while (GetTickCount()-start<10000);
    if (file) fclose(file);
    SetEvent(resume);
    start=GetTickCount();
    do {
        EnterCriticalSection(&lock); count=transitions; LeaveCriticalSection(&lock);
        if (count>=4) break;
        Sleep(10);
    } while (GetTickCount()-start<5000);
    WexWmStop();
    passed=file && count==4 && !minimized;
    printf("STATES_%s bits=%u transitions=%u\n",passed?"PASSED":"FAILED",(unsigned)(8*sizeof(void *)),count);
    printf("ASSOCIATION_%s bits=%u failures=%u\n",passed?"PASSED":"FAILED",(unsigned)(8*sizeof(void *)),!passed);
    CloseHandle(mapped); CloseHandle(resume); DeleteCriticalSection(&lock);
    return !passed;
}
