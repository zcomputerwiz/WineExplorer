// SPDX-License-Identifier: GPL-2.0-or-later
// Read-only top-level window and Win32 owner inventory; not Linux PID mapping.
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <stdio.h>
#include <string.h>
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
static BOOL CALLBACK inspect(HWND hwnd, LPARAM count) {
    wchar_t title[32768]={}, cls[256]={}, image[32768]={};
    int length=GetWindowTextLengthW(hwnd);
    if (length>=32767) return FALSE; // fail the whole observation, never truncate readiness
    GetWindowTextW(hwnd,title,32768); GetClassNameW(hwnd,cls,256);
    DWORD pid=0; GetWindowThreadProcessId(hwnd,&pid);
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    DWORD size=32768;
    if (process) { QueryFullProcessImageNameW(process,0,image,&size); CloseHandle(process); }
    RECT outer={}, client={}; POINT origin={};
    BOOL valid=GetWindowRect(hwnd,&outer) && GetClientRect(hwnd,&client) && ClientToScreen(hwnd,&origin);
    OffsetRect(&client,origin.x,origin.y);
    if ((*(int*)count)++) printf(",");
    printf("{\"hwnd\":%llu,\"pid\":%lu,\"visible\":%s,\"geometry_valid\":%s,\"fixed\":%llu,\"title\":",
        (unsigned long long)(ULONG_PTR)hwnd,pid,IsWindowVisible(hwnd)?"true":"false",valid?"true":"false",
        (unsigned long long)(ULONG_PTR)GetPropW(hwnd,L"__wine_wayland_render_scale"));
    string(title); printf(",\"class\":");string(cls);printf(",\"image\":");string(image);
    printf(",\"window\":");rectangle(outer);printf(",\"client\":");rectangle(client);printf("}");
    return TRUE;
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
    if (argc!=1) return 1;
    SetProcessDPIAware(); int count=0;
    printf("["); BOOL ok=EnumWindows(inspect,(LPARAM)&count); printf("]\n");
    return ok ? 0 : 1;
}
