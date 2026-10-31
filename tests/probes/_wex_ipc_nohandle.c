/*
 * _wex_ipc_nohandle.c - Isolate whether a RESTRICTED token alone (NO handle list, NO attr list)
 * fails CreateProcessAsUserW. Launches target (argv[2]) with the given token mode.
 *   argv[1] = "restr" | "plain"
 *   argv[2] = target exe path (default: self)
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
extern BOOL WINAPI ConvertStringSidToSidW(LPCWSTR, PSID *);

int main(int argc, char **argv) {
    const char *mode = (argc >= 2) ? argv[1] : "plain";
    wchar_t target[MAX_PATH];
    if (argc >= 3) MultiByteToWideChar(CP_ACP,0,argv[2],-1,target,MAX_PATH);
    else GetModuleFileNameW(GetModuleHandleW(NULL),target,MAX_PATH);

    HANDLE hBase;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &hBase)) { printf("OpenProcessToken fail %lu\n",GetLastError()); return 1; }

    HANDLE hTok = NULL;
    if (strcmp(mode,"restr")==0) {
        static const wchar_t *dis[] = { L"S-1-5-32-544", L"S-1-5-32-546" };
        static const wchar_t *res[] = { L"S-1-5-32-545", L"S-1-5-32-547", L"S-1-16-8192", L"S-1-16-4096" };
        SID_AND_ATTRIBUTES disl[2], resl[4];
        for (int i=0;i<2;i++){ ConvertStringSidToSidW(dis[i],&disl[i].Sid); disl[i].Attributes=0; }
        for (int i=0;i<4;i++){ ConvertStringSidToSidW(res[i],&resl[i].Sid); resl[i].Attributes=SE_GROUP_INTEGRITY; }
        if (!CreateRestrictedToken(hBase, DISABLE_MAX_PRIVILEGE, 2, disl, 0, NULL, 4, resl, &hTok)) { printf("CreateRestrictedToken fail %lu\n",GetLastError()); return 1; }
    } else {
        if (!DuplicateTokenEx(hBase, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenPrimary, &hTok)) { printf("DuplicateTokenEx fail %lu\n",GetLastError()); return 1; }
    }
    CloseHandle(hBase);

    STARTUPINFOW si; memset(&si,0,sizeof(si)); si.cb=sizeof(si);
    PROCESS_INFORMATION pi; memset(&pi,0,sizeof(pi));
    if (!CreateProcessAsUserW(hTok, target, NULL, NULL, NULL, TRUE /*bInheritHandles*/, 0, NULL, NULL, &si, &pi)) {
        printf("NOHANDLE mode=%s CreateProcessAsUserW FAILED err=%lu\n", mode, GetLastError());
        return 1;
    }
    WaitForSingleObject(pi.hProcess, 5000);
    DWORD ec=0xFFFFFFFF; GetExitCodeProcess(pi.hProcess,&ec);
    printf("NOHANDLE mode=%s OK child-exit=%lu\n", mode, ec);
    return 0;
}
