/*
 * _wex_ipc_listinherit.c - Tests whether PROC_THREAD_ATTRIBUTE_HANDLE_LIST
 * delivers a single anonymous CreatePipe write handle at the parent's handle
 * value across 6 token modes.
 *
 * NOTE ON SCOPE AND WIN32 CONTRACT:
 * 1. Per Microsoft documentation for UpdateProcThreadAttribute
 *    (PROC_THREAD_ATTRIBUTE_HANDLE_LIST), bInheritHandles MUST be TRUE when
 *    passing an explicit handle list to CreateProcess / CreateProcessAsUser.
 *    Passing bInheritHandles=FALSE with PROC_THREAD_ATTRIBUTE_HANDLE_LIST is
 *    not a valid Win32 handle-inheritance request. WEX_INHERIT defaults to 1
 *    (bInheritHandles=TRUE), matching Chromium's TargetProcess::Create.
 * 2. This probe only exercises a synchronous WriteFile/ReadFile over an
 *    anonymous pipe (CreatePipe) using a single inherited handle. It does NOT
 *    test Chromium's named duplex overlapped Mojo PlatformChannel
 *    (\\.\pipe\mojo.*), IOCP completion packets, SetThreadToken/RevertToSelf
 *    token lowering, or in-band DuplicateHandle transfer. See
 *    chromium_mojo_bootstrap_probe.c for the full Chromium Mojo bootstrap test.
 *
 * Modes (token used with CreateProcessAsUserW, handle list = [hW]):
 *   plain            - duplicate of primary token
 *   restricted       - CreateRestrictedToken(DISABLE_MAX_PRIVILEGE + restrict list)
 *   il_low           - primary + SetTokenInformation(TokenIntegrityLevel, Low)
 *   restricted_il    - restricted + Low integrity
 *   appcontainer     - CreateAppContainerToken + PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES
 *   appcontainer_job - appcontainer + job object via PROC_THREAD_ATTRIBUTE_JOB_LIST
 *
 * Child writes "CHILDPONG" through the inherited handle value (argv[2]); parent reads.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

extern BOOL WINAPI ConvertStringSidToSidW(LPCWSTR, PSID *);

static FILE *g_clog = NULL;
static void cprint(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vprintf(fmt, ap); va_end(ap);
    if (g_clog) { va_start(ap, fmt); vfprintf(g_clog, fmt, ap); va_end(ap); }
}

static int run_child(int argc, char **argv)
{
    (void)argc;
    const char *lp = getenv("WEX_CHILD_LOG");
    g_clog = lp ? fopen(lp, "w") : NULL;
    HANDLE hW = (HANDLE)(intptr_t)strtoll(argv[2], NULL, 0);
    int valid = (int)GetHandleInformation(hW, NULL);
    cprint("CHILD expected hW=0x%lx valid=%d\n", (unsigned long)(uintptr_t)hW, valid);

    DWORD written = 0;
    if (WriteFile(hW, "CHILDPONG", 9, &written, NULL)) {
        cprint("CHILD WriteFile(0x%lx) OK wrote=%lu -> PRESERVED\n", (unsigned long)(uintptr_t)hW, written);
        fflush(stdout); if (g_clog) fclose(g_clog);
        return 42;   /* inherited at the parent's value */
    }
    cprint("CHILD WriteFile(0x%lx) FAILED err=%lu -> scanning table for pipe write-end\n",
           (unsigned long)(uintptr_t)hW, GetLastError());

    int npipe = 0;
    for (DWORD h = 1; h < 0x400; h++) {
        if (!GetHandleInformation((HANDLE)(uintptr_t)h, NULL)) continue;
        if (GetFileType((HANDLE)(uintptr_t)h) != FILE_TYPE_PIPE) continue;
        npipe++;
        DWORD w2 = 0;
        if (WriteFile((HANDLE)(uintptr_t)h, "CHILDPONG", 9, &w2, NULL)) {
            cprint("CHILD pipe handle 0x%lx WriteFile OK wrote=%lu -> RENUMBERED\n", h, w2);
            fflush(stdout); if (g_clog) fclose(g_clog);
            return 44;   /* present but under a different value */
        } else {
            cprint("CHILD pipe handle 0x%lx write failed err=%lu\n", h, GetLastError());
        }
    }
    cprint("CHILD no usable pipe found (npipe=%d) -> DROPPED\n", npipe);
    fflush(stdout); if (g_clog) fclose(g_clog);
    return 43;   /* not delivered at all */
}

static LPTOP_LEVEL_EXCEPTION_FILTER g_prevFilter = NULL;
static LONG WINAPI exfilt(EXCEPTION_POINTERS *ep) {
    printf("PARENT UNHANDLED-EXCEPTION code=0x%lx at %p\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    fflush(stdout);
    ExitProcess(98);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc >= 3 && strcmp(argv[1], "child") == 0) return run_child(argc, argv);
    const char *mode = (argc >= 3) ? argv[2] : "plain";
    /* WEX_INHERIT: 0 => bInheritHandles=FALSE, else TRUE (default; required by Win32 and used by Chromium). */
    BOOL bInherit = (getenv("WEX_INHERIT") && atoi(getenv("WEX_INHERIT")) == 0) ? FALSE : TRUE;
    g_prevFilter = SetUnhandledExceptionFilter(exfilt);

    HANDLE hR = 0, hW = 0;
    if (!CreatePipe(&hR, &hW, NULL, 65536)) { printf("CreatePipe fail %lu\n", GetLastError()); return 1; }
    SetHandleInformation(hW, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    printf("STEP1 pipe ok hR=%p hW=0x%lx\n", (void*)hR, (unsigned long)(uintptr_t)hW); fflush(stdout);

    /* ---- token setup ---- */
    HANDLE hTok = NULL;
    BOOL isAC = (strcmp(mode,"appcontainer")==0 || strcmp(mode,"appcontainer_job")==0);
    BOOL isJob = (strcmp(mode,"appcontainer_job")==0);
    SECURITY_CAPABILITIES sc; memset(&sc,0,sizeof(sc));
    PSID acsid = NULL;

    if (isAC) {
        /* CreateAppContainerToken in kernelbase.dll takes (HANDLE, PSECURITY_CAPABILITIES, PHANDLE). */
        typedef BOOL (WINAPI *fnAC)(HANDLE, SECURITY_CAPABILITIES *, HANDLE *);
        static const wchar_t *acmods[] = { L"kernelbase.dll", L"kernel32.dll", L"advapi32.dll", L"ntdll.dll" };
        fnAC pfnAC = NULL;
        for (int i = 0; i < 4 && !pfnAC; i++) {
            HMODULE m = GetModuleHandleW(acmods[i]);
            if (m) pfnAC = (fnAC)GetProcAddress(m, "CreateAppContainerToken");
        }
        HANDLE hBase;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &hBase)) {
            printf("OpenProcessToken fail %lu\n", GetLastError()); return 1;
        }
        if (!ConvertStringSidToSidW(L"S-1-15-2-3094097068-3553367332-4200492865-707594790", &acsid)) {
            printf("AC sid parse fail\n"); CloseHandle(hBase); return 1;
        }
        memset(&sc, 0, sizeof(sc));
        sc.AppContainerSid = acsid;
        sc.Capabilities = NULL;
        sc.CapabilityCount = 0;
        sc.Reserved = 0;
        if (pfnAC && pfnAC(hBase, &sc, &hTok)) {
            CloseHandle(hBase);
            printf("PARENT appcontainer token created sid=%p\n", (void*)acsid);
        } else {
            printf("PARENT CreateAppContainerToken FAILED err=%lu pfn=%p\n", GetLastError(), (void*)pfnAC);
            if (hBase) CloseHandle(hBase);
            return 1;
        }
    } else {
        HANDLE hBase;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &hBase)) {
            printf("OpenProcessToken fail %lu\n", GetLastError()); return 1;
        }
        if (strcmp(mode,"restricted")==0 || strcmp(mode,"restricted_il")==0) {
            static const wchar_t *dis[] = { L"S-1-5-32-544", L"S-1-5-32-546" };
            static const wchar_t *res[] = { L"S-1-5-32-545", L"S-1-5-32-547", L"S-1-16-8192", L"S-1-16-4096" };
            SID_AND_ATTRIBUTES disl[2], resl[4];
            for (int i=0;i<2;i++){ if(!ConvertStringSidToSidW(dis[i], &disl[i].Sid)){ printf("sid fail %ls\n",dis[i]); return 1;} disl[i].Attributes=0; }
            for (int i=0;i<4;i++){ if(!ConvertStringSidToSidW(res[i], &resl[i].Sid)){ printf("sid fail %ls\n",res[i]); return 1;} resl[i].Attributes=SE_GROUP_INTEGRITY; }
            if (!CreateRestrictedToken(hBase, DISABLE_MAX_PRIVILEGE, 2, disl, 0, NULL, 4, resl, &hTok)) {
                printf("CreateRestrictedToken fail %lu\n", GetLastError()); return 1;
            }
            for (int i=0;i<2;i++) FreeSid(disl[i].Sid);
            for (int i=0;i<4;i++) FreeSid(resl[i].Sid);
        } else {
            if (!DuplicateTokenEx(hBase, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenPrimary, &hTok)) {
                printf("DuplicateTokenEx fail %lu\n", GetLastError()); return 1;
            }
        }
        CloseHandle(hBase);
    }
    printf("STEP2 token ok hTok=%p isAC=%d\n", (void*)hTok, (int)isAC); fflush(stdout);

    /* ---- integrity level Low for il_low / restricted_il ---- */
    if (strcmp(mode,"il_low")==0 || strcmp(mode,"restricted_il")==0) {
        PSID lowSid = NULL;
        if (!ConvertStringSidToSidW(L"S-1-16-4096", &lowSid)) { printf("low sid fail\n"); return 1; }
        TOKEN_MANDATORY_LABEL tml;
        tml.Label.Sid = lowSid;
        tml.Label.Attributes = 0;
        if (!SetTokenInformation(hTok, TokenIntegrityLevel, &tml, sizeof(tml))) {
            printf("SetTokenInformation(IL) fail %lu\n", GetLastError()); return 1;
        }
        FreeSid(lowSid);
    }

    /* ---- job object for appcontainer_job ---- */
    HANDLE hJob = NULL;
    if (isJob) {
        hJob = CreateJobObjectW(NULL, L"wexipcjob");
        if (!hJob) printf("PARENT CreateJobObject fail %lu\n", GetLastError());
    }

    /* ---- attribute list ---- */
    int nattr = 1 + (isAC?1:0) + (isJob?1:0);
    STARTUPINFOEXW si; memset(&si,0,sizeof(si)); si.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(NULL, nattr, 0, &size);
    if (size == 0) { printf("attrlist query gave size=0 err=%lu\n", GetLastError()); return 1; }
    LPVOID listMem = HeapAlloc(GetProcessHeap(), 0, size);
    if (!listMem) { printf("HeapAlloc attrlist fail %lu\n", GetLastError()); return 1; }
    if (!InitializeProcThreadAttributeList((LPPROC_THREAD_ATTRIBUTE_LIST)listMem, nattr, 0, &size)) {
        printf("InitAttrList fail %lu\n", GetLastError()); return 1;
    }
    LPPROC_THREAD_ATTRIBUTE_LIST pList = (LPPROC_THREAD_ATTRIBUTE_LIST)listMem;
    printf("STEP2b attrlist init ok pList=%p size=%lu\n", (void*)pList, (unsigned long)size); fflush(stdout);
    SIZE_T dummy=0;
    if (!UpdateProcThreadAttribute(pList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &hW, sizeof(HANDLE), NULL, &dummy)) {
        printf("UpdateAttr HANDLE_LIST fail %lu\n", GetLastError()); return 1;
    }
    printf("STEP2c handle_list added ret=1\n"); fflush(stdout);
    if (isAC) {
        if (!UpdateProcThreadAttribute(pList, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, &sc, sizeof(sc), NULL, &dummy)) {
            printf("UpdateAttr CAPABILITIES fail %lu\n", GetLastError()); return 1;
        }
    }
    if (isJob) {
        HANDLE joblist[1] = { hJob };
        if (!UpdateProcThreadAttribute(pList, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, joblist, sizeof(joblist), NULL, &dummy)) {
            printf("UpdateAttr JOB_LIST fail %lu\n", GetLastError()); return 1;
        }
    }
    si.lpAttributeList = (void*)pList;

    wchar_t img[MAX_PATH];
    GetModuleFileNameW(GetModuleHandleW(NULL), img, MAX_PATH);
    wchar_t cmdline[512];
    swprintf(cmdline, 512, L"\"%ls\" child 0x%lx", img, (unsigned long)(uintptr_t)hW);

    PROCESS_INFORMATION pi; memset(&pi,0,sizeof(pi));
    printf("STEP4 attrlist built pList=%p nattr=%d bInheritHandles=%d calling CreateProcessAsUserW\n", (void*)pList, nattr, (int)bInherit); fflush(stdout);
    if (!CreateProcessAsUserW(hTok, img, cmdline, NULL, NULL, bInherit,
                              EXTENDED_STARTUPINFO_PRESENT, NULL, NULL, &si.StartupInfo, &pi)) {
        printf("PARENT CreateProcessAsUserW FAILED err=%lu\n", GetLastError());
        printf("LIST_VERDICT LAUNCH_FAIL\n");
        return 1;
    }

    char buf[64]={0}; DWORD got=0; int ready=0;
    for (int t=0;t<80;t++){ DWORD a=0,tot=0; if(PeekNamedPipe(hR,NULL,0,NULL,&a,&tot)&&a>0){ready=1;break;} Sleep(100); }
    int gotdata = ready ? (int)ReadFile(hR, buf, sizeof(buf)-1, &got, NULL) : 0;
    buf[got]=0;

    WaitForSingleObject(pi.hProcess, 15000);
    DWORD cexit=0xFFFFFFFF; GetExitCodeProcess(pi.hProcess,&cexit);

    const char *verdict;
    if (cexit == 42) verdict = "PRESERVED";
    else if (cexit == 44) verdict = "RENUMBERED(present, different value)";
    else if (gotdata && strncmp(buf,"CHILDPONG",9)==0) verdict = "DATA-ARRIVED(child-exit=";
    else verdict = "DROPPED(not delivered)";
    printf("PARENT mode=%s hW=0x%lx child-exit=%lu read-ready=%d gotdata=%d data=\"%s\"\n",
           mode, (unsigned long)(uintptr_t)hW, cexit, ready, gotdata, buf);
    printf("LIST_VERDICT %s\n", verdict);

    printf("PARENT handle-list count=1 (hW only)\n");
    DeleteProcThreadAttributeList(pList);
    HeapFree(GetProcessHeap(), 0, listMem);
    if (acsid) FreeSid(acsid);
    return 0;
}
