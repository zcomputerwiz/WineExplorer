/*
 * _wex_ipc_mininherit.c - Minimal test: does wine preserve INHERITED handle VALUES?
 *
 * Parent creates one anonymous pipe, marks the write end inheritable, and spawns
 *   "_wex_ipc_mininherit.exe child <hWrite>" with plain CreateProcessW + bInheritHandles=TRUE
 *   (NO handle list, NO special token). On Windows the child receives the same
 *   handle value. The child writes a marker through that value; the parent reads
 *   the other end and reports whether it arrived.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int run_child(int argc, char **argv)
{
    (void)argc;
    const char *lp = getenv("WEX_CHILD_LOG");
    FILE *f = lp ? fopen(lp, "w") : NULL;
    HANDLE hW = (HANDLE)(intptr_t)strtoll(argv[2], NULL, 0);
    int ok = 1;
    if (f) fprintf(f, "CHILD hW=0x%lx valid=%d\n",
                   (unsigned long)(uintptr_t)hW, (int)GetHandleInformation(hW, NULL));
    if (f) {
        fprintf(f, "CHILD pipe scan:\n");
        for (DWORD h = 1; h < 0x400; h++) {
            DWORD sz = 0;
            if (GetHandleInformation((HANDLE)(uintptr_t)h, &sz)) {
                DWORD a=0,t=0;
                if (PeekNamedPipe((HANDLE)(uintptr_t)h, NULL, 0, NULL, &a, &t))
                    fprintf(f, "CHILD   pipe 0x%lx avail=%lu total=%lu\n", h, a, t);
            }
        }
    }
    const char *msg = "CHILDPONG";
    DWORD written = 0;
    if (!WriteFile(hW, msg, (DWORD)strlen(msg), &written, NULL)) {
        if (f) fprintf(f, "CHILD WriteFile(0x%lx) FAILED err=%lu\n",
                       (unsigned long)(uintptr_t)hW, GetLastError());
        ok = 0;
    } else {
        if (f) fprintf(f, "CHILD WriteFile OK wrote=%lu\n", written);
    }
    fflush(stdout);
    if (f) fclose(f);
    return ok ? 42 : 43;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc >= 2 && strcmp(argv[1], "child") == 0) return run_child(argc, argv);

    HANDLE hR = 0, hW = 0;
    if (!CreatePipe(&hR, &hW, NULL, 65536)) { printf("CreatePipe fail %lu\n", GetLastError()); return 1; }
    SetHandleInformation(hW, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);

    wchar_t img[MAX_PATH];
    GetModuleFileNameW(GetModuleHandleW(NULL), img, MAX_PATH);
    wchar_t cmdline[512];
    swprintf(cmdline, 512, L"\"%ls\" child 0x%lx", img, (unsigned long)(uintptr_t)hW);

    PROCESS_INFORMATION pi; STARTUPINFOW si;
    memset(&pi, 0, sizeof(pi)); memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
    if (!CreateProcessW(img, cmdline, NULL, NULL, TRUE /*bInheritHandles*/, 0, NULL, NULL, &si, &pi)) {
        printf("CreateProcessW fail %lu\n", GetLastError()); return 1;
    }

    char buf[64] = {0}; DWORD got = 0;
    int ready = 0;
    for (int t = 0; t < 80; t++) {
        DWORD a=0,tot=0;
        if (PeekNamedPipe(hR, NULL, 0, NULL, &a, &tot) && a > 0) { ready = 1; break; }
        Sleep(100);
    }
    int gotdata = 0;
    if (ready) gotdata = (int)ReadFile(hR, buf, sizeof(buf)-1, &got, NULL);
    buf[got] = 0;

    WaitForSingleObject(pi.hProcess, 15000);
    DWORD cexit = 0xFFFFFFFF; GetExitCodeProcess(pi.hProcess, &cexit);

    printf("PARENT hW=0x%lx child-exit=%lu read-ready=%d gotdata=%d data=\"%s\"\n",
           (unsigned long)(uintptr_t)hW, cexit, ready, gotdata, buf);
    printf("MIN_VERDICT %s\n", (gotdata && strncmp(buf, "CHILDPONG", 9) == 0) ? "PRESERVED" : "NOT-PRESERVED");
    return 0;
}
