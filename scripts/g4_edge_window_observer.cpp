// SPDX-License-Identifier: GPL-2.0-or-later
// Read-only top-level window and Win32 owner inventory; not Linux PID mapping.
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <tlhelp32.h>
#include <vector>

static void string(const wchar_t *text) {
    int n=WideCharToMultiByte(CP_UTF8,0,text,-1,NULL,0,NULL,NULL);
    std::vector<char> bytes(n > 0 ? n : 1);
    if (n>0) WideCharToMultiByte(CP_UTF8,0,text,-1,bytes.data(),n,NULL,NULL);
    printf("\"");
    for (int i=0;i<n-1;i++) {
        unsigned char c=bytes[i];
        if (c=='"' || c=='\\') printf("\\%c",c);
        else if (c<32) printf("\\u%04x",c);
        else putchar(c);
    }
    printf("\"");
}
static void rectangle(RECT r) {
    printf("{\"x\":%ld,\"y\":%ld,\"width\":%ld,\"height\":%ld}",r.left,r.top,r.right-r.left,r.bottom-r.top);
}
static unsigned long long ticks(FILETIME t) { return ((unsigned long long)t.dwHighDateTime<<32)|t.dwLowDateTime; }
static BOOL CALLBACK inspect(HWND hwnd, LPARAM count) {
    wchar_t title[32768]={}, cls[256]={}, image[32768]={};
    int length=GetWindowTextLengthW(hwnd);
    if (length>=32767) return FALSE; // fail the whole observation, never truncate readiness
    GetWindowTextW(hwnd,title,32768); GetClassNameW(hwnd,cls,256);
    DWORD pid=0; GetWindowThreadProcessId(hwnd,&pid);
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    DWORD size=32768; FILETIME created={},exit={},kernel={},user={};
    if (process) {
        QueryFullProcessImageNameW(process,0,image,&size);
        GetProcessTimes(process,&created,&exit,&kernel,&user); CloseHandle(process);
    }
    RECT outer={}, client={}; POINT origin={};
    BOOL valid=GetWindowRect(hwnd,&outer) && GetClientRect(hwnd,&client) && ClientToScreen(hwnd,&origin);
    OffsetRect(&client,origin.x,origin.y);
    if ((*(int*)count)++) printf(",");
    printf("{\"hwnd\":%llu,\"pid\":%lu,\"visible\":%s,\"geometry_valid\":%s,\"fixed\":%llu,\"title\":",
        (unsigned long long)(ULONG_PTR)hwnd,pid,IsWindowVisible(hwnd)?"true":"false",valid?"true":"false",
        (unsigned long long)(ULONG_PTR)GetPropW(hwnd,L"__wine_wayland_render_scale"));
    string(title); printf(",\"class\":");string(cls);printf(",\"image\":");string(image);
    printf(",\"creation_time\":%llu,\"top_level\":%s",ticks(created),
           GetAncestor(hwnd,GA_ROOT)==hwnd && !(GetWindowLongPtrW(hwnd,GWL_STYLE)&WS_CHILD) && !GetWindow(hwnd,GW_OWNER)?"true":"false");
    printf(",\"window\":");rectangle(outer);printf(",\"client\":");rectangle(client);printf("}");
    return TRUE;
}
struct Identity {
    DWORD pid,gui; unsigned long long creation; RECT window,client;
    wchar_t title[2049],image[512];
};
static bool pinned_image(const wchar_t *image) {
    return !_wcsicmp(image,L"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe") ||
           !_wcsicmp(image,L"Z:\\home\\debian\\wex-g4\\edge-install-r1-v2-20261022\\prefix\\drive_c\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe");
}
static bool identity(HWND hwnd,DWORD pid,unsigned long long creation,const wchar_t *image,
                     const wchar_t *route,Identity &v) {
    v={}; wchar_t cls[256]={};
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd) || GetAncestor(hwnd,GA_ROOT)!=hwnd ||
        (GetWindowLongPtrW(hwnd,GWL_STYLE)&WS_CHILD) || GetWindow(hwnd,GW_OWNER)) return false;
    v.gui=GetWindowThreadProcessId(hwnd,&v.pid);
    if (!v.gui || v.pid!=pid || GetWindowTextLengthW(hwnd)>2048 ||
        !GetWindowTextW(hwnd,v.title,2049) || !wcsstr(v.title,route) ||
        !GetClassNameW(hwnd,cls,256) || wcscmp(cls,L"Chrome_WidgetWin_1")) return false;
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if (!process) return false;
    DWORD size=512,code=0; FILETIME c={},e={},k={},u={};
    bool ok=QueryFullProcessImageNameW(process,0,v.image,&size) && pinned_image(v.image) &&
            !_wcsicmp(v.image,image) && GetProcessTimes(process,&c,&e,&k,&u) &&
            GetExitCodeProcess(process,&code) && code==STILL_ACTIVE;
    CloseHandle(process);v.creation=ticks(c);
    POINT origin={};
    ok=ok && v.creation==creation && GetWindowRect(hwnd,&v.window) && GetClientRect(hwnd,&v.client) &&
       ClientToScreen(hwnd,&origin);
    OffsetRect(&v.client,origin.x,origin.y);
    return ok && v.client.right>v.client.left && v.client.bottom>v.client.top;
}
static void identity_json(const Identity &v) {
    printf("{\"pid\":%lu,\"gui_tid\":%lu,\"creation_time\":%llu,\"image\":",v.pid,v.gui,v.creation);
    string(v.image);printf(",\"title\":");string(v.title);
    printf(",\"window\":");rectangle(v.window);printf(",\"client\":");rectangle(v.client);printf("}");
}
static int failure(const char *what) {
    printf("{\"ok\":false,\"error\":\"%s\",\"win32_error\":%lu}\n",what,GetLastError());return 1;
}
static bool number(const char *s,unsigned long long max,unsigned long long &value) {
    if (!*s) return false;
    for (const char *p=s;*p;p++) if (*p<'0'||*p>'9') return false;
    errno=0;char *end=nullptr;value=strtoull(s,&end,10);
    return !errno && end && !*end && value && value<=max;
}
struct Thread {
    DWORD tid,owner,error,exit_code; unsigned long long creation;
    bool queried; long name_error; wchar_t name[257];
};
static int owned_threads(char **argv) {
    unsigned long long handle,pid,creation;
    if (!number(argv[2],~0ULL,handle) || !number(argv[3],0xffffffffULL,pid) ||
        !number(argv[4],~0ULL,creation) || strlen(argv[5])!=32) return failure("arguments");
    for (const char *p=argv[5];*p;p++) if (!(*p>='0'&&*p<='9') && !(*p>='a'&&*p<='f')) return failure("nonce");
    wchar_t image[512]={},route[80]={},nonce[33]={};
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,argv[6],-1,image,512) || !pinned_image(image) ||
        !MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,argv[5],-1,nonce,33)) return failure("image/nonce");
    swprintf(route,80,L"/%ls/index.html",nonce);
    HWND hwnd=(HWND)(ULONG_PTR)handle; Identity before={},after={};
    if (!identity(hwnd,(DWORD)pid,creation,image,route,before)) return failure("initial owner identity");
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
    if (snapshot==INVALID_HANDLE_VALUE) return failure("thread snapshot");
    typedef HRESULT (WINAPI *Description)(HANDLE,PWSTR*);
    FARPROC address=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetThreadDescription");
    Description description=nullptr;
    static_assert(sizeof(description)==sizeof(address),"Windows function pointer size");
    memcpy(&description,&address,sizeof(description));
    std::vector<Thread> threads;THREADENTRY32 entry={};entry.dwSize=sizeof(entry);
    ULONGLONG start=GetTickCount64(); bool ok=Thread32First(snapshot,&entry),gui_seen=false;
    if (!ok) { CloseHandle(snapshot);return failure("Thread32First"); }
    do {
        if (GetTickCount64()-start>2000) { CloseHandle(snapshot);return failure("native deadline"); }
        if (entry.th32OwnerProcessID!=(DWORD)pid) continue;
        if (threads.size()>=128) { CloseHandle(snapshot);return failure("thread limit exceeded"); }
        Thread t={};t.tid=entry.th32ThreadID;t.owner=entry.th32OwnerProcessID;
        HANDLE thread=OpenThread(THREAD_QUERY_LIMITED_INFORMATION,FALSE,t.tid);
        if (!thread) t.error=GetLastError();
        else {
            if (GetProcessIdOfThread(thread)!=(DWORD)pid || GetThreadId(thread)!=t.tid) {
                CloseHandle(thread);CloseHandle(snapshot);return failure("thread owner identity");
            }
            FILETIME c={},e={},k={},u={};
            if (!GetThreadTimes(thread,&c,&e,&k,&u) || !GetExitCodeThread(thread,&t.exit_code)) t.error=GetLastError();
            else { t.queried=true;t.creation=ticks(c); }
            if (description) {
                PWSTR text=nullptr;HRESULT hr=description(thread,&text);t.name_error=(long)hr;
                if (SUCCEEDED(hr) && text) {
                    size_t n=wcsnlen(text,257);
                    if (n>256) { LocalFree(text);CloseHandle(thread);CloseHandle(snapshot);return failure("thread name limit exceeded"); }
                    memcpy(t.name,text,(n+1)*sizeof(wchar_t));
                }
                if (text) LocalFree(text);
            }
            CloseHandle(thread);
        }
        if (t.tid==before.gui) gui_seen=true;
        threads.push_back(t);
    } while (Thread32Next(snapshot,&entry));
    DWORD enumeration_error=GetLastError();CloseHandle(snapshot);
    if (enumeration_error!=ERROR_NO_MORE_FILES || !gui_seen) return failure("incomplete thread snapshot");
    if (!identity(hwnd,(DWORD)pid,creation,image,route,after) || before.gui!=after.gui ||
        wcscmp(before.title,after.title) || memcmp(&before.window,&after.window,sizeof(RECT)) ||
        memcmp(&before.client,&after.client,sizeof(RECT))) return failure("final owner identity");
    printf("{\"ok\":true,\"hwnd\":%llu,\"nonce\":",handle);string(nonce);
    printf(",\"before\":");identity_json(before);printf(",\"after\":");identity_json(after);
    printf(",\"description_available\":%s,\"threads\":[",description?"true":"false");
    for (size_t i=0;i<threads.size();i++) {
        const Thread &t=threads[i];if(i)printf(",");
        printf("{\"tid\":%lu,\"owner_pid\":%lu,\"queried\":%s,\"query_error\":%lu,\"creation_time\":%llu,\"exit_code\":%lu,\"name_error\":%ld,\"name\":",
               t.tid,t.owner,t.queried?"true":"false",t.error,t.creation,t.exit_code,t.name_error);string(t.name);printf("}");
    }
    printf("]}\n");return 0;
}
static int fixture_file(const char *path) {
    // Only this lab's staged ORIGINAL index.html. Never open Microsoft payloads.
    const char *root="Z:\\home\\debian\\wex-g4\\edge-headful-";
    const char *suffix="-stage\\scripts\\g4-edge-probe\\index.html";
    size_t n=strlen(path), r=strlen(root), s=strlen(suffix);
    bool allowed=n>r+s && n<512 && !strncmp(path,root,r) && !strcmp(path+n-s,suffix);
    for (size_t i=r;allowed && i<n-s;i++) {
        char c=path[i]; allowed=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.';
    }
    if (!allowed) { printf("{\"ok\":false,\"error\":\"path rejected\"}\n");return 1; }
    wchar_t wide[512]={};
    if (!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,512)) return 1;
    WIN32_FILE_ATTRIBUTE_DATA attr={};
    if (!GetFileAttributesExW(wide,GetFileExInfoStandard,&attr) || (attr.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) {
        printf("{\"ok\":false,\"error\":%lu}\n",GetLastError());return 1;
    }
    HANDLE file=CreateFileW(wide,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,
                            OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,NULL);
    if (file==INVALID_HANDLE_VALUE) { printf("{\"ok\":false,\"error\":%lu}\n",GetLastError());return 1; }
    unsigned char bytes[64]={};DWORD read=0;
    BOOL ok=ReadFile(file,bytes,sizeof(bytes),&read,NULL);DWORD error=ok?0:GetLastError();CloseHandle(file);
    printf("{\"ok\":%s,\"error\":%lu,\"size\":%llu,\"read\":%lu,\"prefix_hex\":\"",ok?"true":"false",error,
        ((unsigned long long)attr.nFileSizeHigh<<32)|attr.nFileSizeLow,read);
    for (DWORD i=0;i<read;i++) printf("%02x",bytes[i]);
    printf("\"}\n");return ok?0:1;
}
int main(int argc,char **argv) {
    if (argc==3 && !strcmp(argv[1],"--fixture-file")) return fixture_file(argv[2]);
    if (argc==7 && !strcmp(argv[1],"--owned-threads")) { SetProcessDPIAware();return owned_threads(argv); }
    if (argc!=1) return 1;
    SetProcessDPIAware(); int count=0;
    printf("["); BOOL ok=EnumWindows(inspect,(LPARAM)&count); printf("]\n");
    return ok ? 0 : 1;
}
