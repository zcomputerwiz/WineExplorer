// SPDX-License-Identifier: GPL-2.0-or-later
// Read-only top-level window and Win32 owner inventory; not Linux PID mapping.
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <stdio.h>
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
int main() {
    SetProcessDPIAware(); int count=0;
    printf("["); BOOL ok=EnumWindows(inspect,(LPARAM)&count); printf("]\n");
    return ok ? 0 : 1;
}
