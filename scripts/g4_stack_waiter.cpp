// SPDX-License-Identifier: GPL-2.0-or-later
// Original disposable calibration child. Readiness schema is NOT ownership proof.
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

struct State { HANDLE release,ready; ULONGLONG wait_end; volatile LONG error; };
static DWORD remaining(ULONGLONG end) {
    ULONGLONG now=GetTickCount64();return now>=end?0:(DWORD)(end-now);
}
static bool name_thread(const wchar_t *name) {
    typedef HRESULT (WINAPI *SetDescription)(HANDLE,PCWSTR);
    HMODULE module=GetModuleHandleW(L"kernel32.dll");if (!module) return false;
    FARPROC address=GetProcAddress(module,"SetThreadDescription");
    if (!address) return true; // optional on older systems; no name proof claimed
    SetDescription description=nullptr;
    static_assert(sizeof(description)==sizeof(address),"Windows function pointer size");
    memcpy(&description,&address,sizeof(description));
    return SUCCEEDED(description(GetCurrentThread(),name));
}
static DWORD WINAPI worker(void *value) {
    State *state=(State*)value;
    if (!name_thread(L"WEXStackWorker")) {
        InterlockedExchange(&state->error,1);
        if (!SetEvent(state->ready)) InterlockedExchange(&state->error,2);
        return (DWORD)InterlockedCompareExchange(&state->error,0,0);
    }
    if (!SetEvent(state->ready)) { InterlockedExchange(&state->error,2);return 2; }
    DWORD result=WaitForSingleObject(state->release,remaining(state->wait_end));
    if (result!=WAIT_OBJECT_0) {
        LONG code=result==WAIT_TIMEOUT?3:4;InterlockedExchange(&state->error,code);return (DWORD)code;
    }
    return 0;
}
static int error(const char *message) {
    fprintf(stderr,"stack waiter: %s (Win32 %lu)\n",message,GetLastError());return 1;
}
int main(int argc,char **argv) {
    if (argc!=3 || (strcmp(argv[1],"--wait") && strcmp(argv[1],"--release")) || strlen(argv[2])!=32)
        return error("exact --wait/--release NONCE32 required");
    wchar_t event_name[96]=L"Local\\WEXStackCalibration-";size_t offset=wcslen(event_name);
    for (size_t i=0;i<32;i++) {
        char c=argv[2][i];if (!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F'))) return error("nonce32hex required");
        event_name[offset+i]=(wchar_t)c;
    }
    if (!strcmp(argv[1],"--release")) {
        HANDLE event=OpenEventW(EVENT_MODIFY_STATE,FALSE,event_name);
        if (!event) return error("release event unavailable");
        BOOL ok=SetEvent(event),closed=CloseHandle(event);
        return ok && closed?0:error("release failed");
    }
    ULONGLONG end=GetTickCount64()+30000;
    // Reserve1s INSIDE30s for cooperative cancellation/join on any failure.
    State state={};state.wait_end=end-1000;
    state.release=CreateEventW(NULL,TRUE,FALSE,event_name);DWORD created_error=GetLastError();
    if (!state.release) return error("create unique release event");
    if (created_error==ERROR_ALREADY_EXISTS) {
        if (!CloseHandle(state.release)) error("collision handle close failed");
        return error("event collision; never reused");
    }
    state.ready=CreateEventW(NULL,TRUE,FALSE,NULL);
    if (!state.ready) {
        if (!CloseHandle(state.release)) error("release handle close failed");
        return error("create worker handshake");
    }
    DWORD main_tid=GetCurrentThreadId(),worker_tid=0;HANDLE thread=NULL;int result=1;
    FILETIME creation={},exit={},kernel={},user={};
    bool setup=name_thread(L"WEXStackMain") && GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user);
    if (setup) thread=CreateThread(NULL,0,worker,&state,0,&worker_tid);
    if (thread && main_tid && worker_tid && worker_tid!=main_tid &&
        WaitForSingleObject(state.ready,remaining(state.wait_end)<3000?remaining(state.wait_end):3000)==WAIT_OBJECT_0 &&
        !InterlockedCompareExchange(&state.error,0,0)) {
        unsigned long long ticks=((unsigned long long)creation.dwHighDateTime<<32)|creation.dwLowDateTime;
        if (ticks && remaining(state.wait_end) &&
            printf("{\"pid\":%lu,\"main_tid\":%lu,\"worker_tid\":%lu,\"creation_time\":%llu,\"nonce\":\"%s\"}\n",
                   GetCurrentProcessId(),main_tid,worker_tid,ticks,argv[2])>0 && !fflush(stdout)) {
            if (WaitForSingleObject(state.release,remaining(state.wait_end))==WAIT_OBJECT_0) result=0;
        }
    }
    // On failure wake ONLY our uniquely created event; never an existing event.
    if (result && !SetEvent(state.release)) error("cooperative cancellation failed");
    if (thread) {
        DWORD code=1;
        if (WaitForSingleObject(thread,remaining(end))!=WAIT_OBJECT_0 || !GetExitCodeThread(thread,&code) || code) result=1;
        if (!CloseHandle(thread)) result=1;
    }
    if (!CloseHandle(state.ready)) result=1;
    if (!CloseHandle(state.release)) result=1;
    if (result) error("setup/handshake/release/wait/join failure or timeout");
    return result; // process exit also bounds a worker that failed to join
}
