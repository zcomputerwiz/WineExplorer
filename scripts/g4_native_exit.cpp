// SPDX-License-Identifier: GPL-2.0-or-later
// Original ordinary parent; no debugger, process discovery, privileges or jobs.
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "g4_native_exit_contract.h"

struct State {
    wex_launch launch;
    HANDLE journal;
    PROCESS_INFORMATION child;
    wchar_t command[32768],image[32768];
    unsigned long long created;
    DWORD error,wait_result,code;
};

static unsigned long long ticks(FILETIME t) { return ((unsigned long long)t.dwHighDateTime<<32)|t.dwLowDateTime; }
static bool escaped(const wchar_t *src,char *out,size_t cap) {
    char utf8[32768];int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,src,-1,utf8,sizeof(utf8),NULL,NULL);
    if (!n) return false;
    size_t used=0;
    for (int i=0;i<n-1;i++) {
        unsigned char c=(unsigned char)utf8[i];
        if (c<32 || used+3>=cap) return false;
        if (c=='"' || c=='\\') out[used++]='\\';
        out[used++]=(char)c;
    }
    out[used]=0;return true;
}
static bool line(State *s,const char *text) {
    DWORD written=0;size_t count=strlen(text);
    if (count>16384 || !WriteFile(s->journal,text,(DWORD)count,&written,NULL) || written!=count || !FlushFileBuffers(s->journal)) {
        s->error=GetLastError();fprintf(stderr,"native exit: journal write/flush failed (%lu)\n",s->error);return false;
    }
    return true;
}
static int create(void *ptr) {
    State *s=(State*)ptr;STARTUPINFOW startup={};startup.cb=sizeof(startup);
    // Mutable command; explicit application; flags0, no handle inheritance,
    // environmentNULL/current-directoryNULL, no security attributes/token/job.
    BOOL ok=CreateProcessW(s->launch.image,s->command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&s->child);
    s->error=ok?0:GetLastError();return ok!=FALSE;
}
static int close_thread(void *ptr) {
    State *s=(State*)ptr;BOOL ok=CloseHandle(s->child.hThread);
    if (ok) s->child.hThread=NULL;else s->error=GetLastError();
    return ok!=FALSE;
}
static uint32_t poll(void *ptr) {
    State *s=(State*)ptr;s->wait_result=WaitForSingleObject(s->child.hProcess,0);
    s->error=s->wait_result==WAIT_FAILED?GetLastError():0;return s->wait_result;
}
static int identity(void *ptr) {
    State *s=(State*)ptr;FILETIME c={},e={},k={},u={};DWORD size=32768;
    if (GetProcessId(s->child.hProcess)!=s->child.dwProcessId) { s->error=ERROR_INVALID_DATA;return 0; }
    if (!QueryFullProcessImageNameW(s->child.hProcess,0,s->image,&size) ||
        !GetProcessTimes(s->child.hProcess,&c,&e,&k,&u)) { s->error=GetLastError();return 0; }
    s->created=ticks(c);
    if (!s->created || _wcsicmp(s->image,s->launch.image)) { s->error=ERROR_INVALID_DATA;return 0; }
    s->error=0;return 1;
}
static int bound(void *ptr) {
    State *s=(State*)ptr;char image[32768],nonce[64],command[32768],text[16385];
    // Reconstruct original command: CreateProcess may modify its input buffer.
    wchar_t original[32768];
    if (!wex_command(original,32768,&s->launch) || !escaped(original,command,sizeof(command)) ||
        !escaped(s->image,image,sizeof(image)) || !escaped(s->launch.nonce,nonce,sizeof(nonce))) return 0;
    const char *manifest=s->launch.calibration?"null":"\"" WEX_EXIT_MANIFEST "\"";
    int n=snprintf(text,sizeof(text),"{\"v\":1,\"event\":\"bound\",\"mode\":\"%s\",\"nonce\":\"%s\",\"helper_pid\":%lu,\"child_pid\":%lu,\"creation_time\":%llu,\"image\":\"%s\",\"command_line\":\"%s\",\"creation_flags\":0,\"inherit_handles\":false,\"environment\":\"inherited_NULL\",\"manifest_sha256\":%s,\"manifest_verified_by_native\":false,\"tick_ms\":%llu}\n",
        s->launch.calibration?"calibration":"chromium",nonce,GetCurrentProcessId(),s->child.dwProcessId,s->created,image,command,manifest,GetTickCount64());
    return n>0 && (size_t)n<sizeof(text) && line(s,text);
}
static uint32_t wait_child(void *ptr) {
    State *s=(State*)ptr;s->wait_result=WaitForSingleObject(s->child.hProcess,s->launch.wait_ms);
    s->error=s->wait_result==WAIT_FAILED?GetLastError():0;return s->wait_result;
}
static int query_exit(void *ptr) {
    State *s=(State*)ptr;FILETIME c={},e={},k={},u={};
    if (!GetProcessTimes(s->child.hProcess,&c,&e,&k,&u) || !GetExitCodeProcess(s->child.hProcess,&s->code)) {
        s->error=GetLastError();return 0;
    }
    if (GetProcessId(s->child.hProcess)!=s->child.dwProcessId || ticks(c)!=s->created) { s->error=ERROR_INVALID_DATA;return 0; }
    s->error=0;return 1; // code259 is legitimate after signaled wait
}
static int exited(void *ptr) {
    State *s=(State*)ptr;char nonce[64],text[1024],hex[11];wex_status_hex(s->code,hex);
    if (!escaped(s->launch.nonce,nonce,sizeof(nonce))) return 0;
    int n=snprintf(text,sizeof(text),"{\"v\":1,\"event\":\"exit\",\"nonce\":\"%s\",\"child_pid\":%lu,\"creation_time\":%llu,\"wait_result\":0,\"exit_code\":%lu,\"exit_hex\":\"%s\",\"tick_ms\":%llu}\n",
        nonce,s->child.dwProcessId,s->created,s->code,hex,GetTickCount64());
    return n>0 && (size_t)n<sizeof(text) && line(s,text);
}
static int timeout(void *ptr) {
    State *s=(State*)ptr;char nonce[64],text[1024];
    if (!escaped(s->launch.nonce,nonce,sizeof(nonce))) return 0;
    int n=snprintf(text,sizeof(text),"{\"v\":1,\"event\":\"timeout\",\"nonce\":\"%s\",\"child_pid\":%lu,\"creation_time\":%llu,\"wait_result\":258,\"wait_ms\":%lu,\"tick_ms\":%llu}\n",
        nonce,s->child.dwProcessId,s->created,(DWORD)s->launch.wait_ms,GetTickCount64());
    return n>0 && (size_t)n<sizeof(text) && line(s,text);
}
static int failure(void *ptr,const char *phase) {
    State *s=(State*)ptr;char nonce[64],text[1024];
    if (escaped(s->launch.nonce,nonce,sizeof(nonce))) {
        int n=snprintf(text,sizeof(text),"{\"v\":1,\"event\":\"failure\",\"nonce\":\"%s\",\"phase\":\"%s\",\"win32_error\":%lu,\"child_pid\":%lu,\"wait_result\":%lu,\"tick_ms\":%llu}\n",
            nonce,phase,s->error,s->child.dwProcessId,s->wait_result,GetTickCount64());
        if (n>0 && (size_t)n<sizeof(text)) line(s,text);
    }
    fprintf(stderr,"native exit: %s failed (Win32 %lu, wait %lu); NOT a child-exit verdict\n",phase,s->error,s->wait_result);
    return 1;
}
static bool missing(const wchar_t *path) {
    if (GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES) return false;
    DWORD error=GetLastError();return error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND;
}
int wmain(int argc,wchar_t **argv) {
    State s={};
    if (!wex_validate(argc,argv,&s.launch) || !wex_command(s.command,32768,&s.launch)) {
        fprintf(stderr,"native exit: exact pinned launch schema required; no process created\n");return 2;
    }
    if (!s.launch.calibration && (!missing(WEX_EXIT_OUTPUT L"\\profile") || !missing(WEX_EXIT_OUTPUT L"\\chromium.log"))) {
        fprintf(stderr,"native exit: fresh fixed profile/log required; no process created\n");return 2;
    }
    // Noninheritable, fresh journal; never truncate previous evidence.
    s.journal=CreateFileW(s.launch.events,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if (s.journal==INVALID_HANDLE_VALUE) { fprintf(stderr,"native exit: fresh journal failed (%lu)\n",GetLastError());return 2; }
    wex_ops ops={&s,create,close_thread,identity,bound,poll,wait_child,query_exit,exited,timeout,failure};
    int result=wex_observe(&ops);
    if (s.child.hThread && !CloseHandle(s.child.hThread)) result=1;
    if (s.child.hProcess && !CloseHandle(s.child.hProcess)) result=1;
    if (!CloseHandle(s.journal)) result=1;
    // On timeout/error child may remain alive: future runner must reap observer
    // BEFORE owned-prefix cleanup. No TerminateProcess or liveness-from259 here.
    return result;
}
