/*
 * navhandoff_mojo_pipe_ac.c — Task 2, iteration 2: the DISCRIMINATING variant.
 *
 * Iteration 1 (navhandoff_mojo_pipe.c) showed the generic primitive works identically
 * on native Windows and stock wine-10.0. This variant adds the dimension Task 1 hinges
 * on: the network-service child runs under a RESTRICTED token. We launch the child in an
 * AppContainer (container SID S-1-15-2-... + Medium integrity) via CreateProcessAsUser,
 * passing an inheritable pipe handle, and repeat the same sync+overlapped handshake.
 *
 * Question answered: does a restricted/AppContainer child correctly receive an inherited
 * pipe handle and complete overlapped pipe I/O under Wine vs native? This is the exact
 * context Chromium's sandboxed network service runs in, and the area the g4 fork implements.
 *
 * Build: x86_64-w64-mingw32-gcc -O2 -static -o mojo_pipe_ac.exe navhandoff_mojo_pipe_ac.c
 */
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

/* Some mingw headers omit this prototype at WINVER 0x0601; declare it (advapi32). */
extern BOOL WINAPI ConvertStringSidToSidA(LPCSTR StringSid, PSID *Sid);
/* Kernel only reads a single PSID for TokenIntegrityLevel; avoid header layout dependence. */
typedef struct { PSID Sid; } WEX_TML;
#ifndef TOKEN_ADJUST_SECURITY
#define TOKEN_ADJUST_SECURITY 0x0010
#endif

static void logmsg(FILE *f, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
}

/* ---------------- child (identical handshake; logs to mojo_child_ac.log) ---------------- */
static int child_main(void) {
    FILE *L = fopen("mojo_child_ac.log", "w");
    if (!L) L = stderr;
    HANDLE p2c = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE c2p = GetStdHandle(STD_OUTPUT_HANDLE);

    /* Report the child's own token identity so we can confirm it really is a container. */
    HANDLE tok = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        DWORD sz = 0; PSID csid = NULL;
        BOOL hasc = GetTokenInformation(tok, TokenAppContainerSid, NULL, 0, &sz); /* query size */
        if (!hasc && GetLastError() == ERROR_INSUFFICIENT_BUFFER && sz) {
            csid = (PSID)malloc(sz);
            if (GetTokenInformation(tok, TokenAppContainerSid, csid, sz, &sz))
                logmsg(L, "[child pid=%lu] container-sid=%S\n",
                       (unsigned long)GetCurrentProcessId(), csid);
            else logmsg(L, "[child pid=%lu] GetTokenInformation(ContainerSid) failed err=%lu\n",
                        (unsigned long)GetCurrentProcessId(), GetLastError());
        } else {
            logmsg(L, "[child pid=%lu] no container sid in token\n", (unsigned long)GetCurrentProcessId());
        }
        if (csid) free(csid);
        CloseHandle(tok);
    } else logmsg(L, "[child pid=%lu] OpenProcessToken failed err=%lu\n",
                 (unsigned long)GetCurrentProcessId(), GetLastError());

    if (!p2c || p2c == INVALID_HANDLE_VALUE) {
        logmsg(L, "[child] FAIL: no inherited stdin pipe handle err=%lu\n", GetLastError());
        fclose(L); return 3;
    }

    char rbuf[64]; DWORD got = 0;
    BOOL ok = ReadFile(p2c, rbuf, sizeof(rbuf), &got, NULL);
    logmsg(L, "[child] sync-ReadFile(HANDSHAK) ok=%d got=%lu err=%lu\n",
           (int)ok, (unsigned long)got, ok ? 0 : GetLastError());

    HANDLE ov = CreateEventA(NULL, TRUE, FALSE, NULL);
    OVERLAPPED ovl = {0}; ovl.hEvent = ov;
    char obuf[64]; DWORD ogot = 0;
    DWORD t0 = GetTickCount64();
    BOOL sub = ReadFile(p2c, obuf, sizeof(obuf), &ogot, &ovl);
    DWORD st = (sub || GetLastError() == ERROR_IO_PENDING) ?
               GetOverlappedResult(p2c, &ovl, &ogot, TRUE) : 0;
    DWORD t1 = GetTickCount64();
    logmsg(L, "[child] overlapped-ReadFile(READY) submit=%d complete=%d got=%lu bytes='%.*s' err=%lu lat_ms=%lu\n",
           (int)sub, (int)st, (unsigned long)ogot, (int)ogot, obuf, st ? 0 : GetLastError(),
           (unsigned long)(t1 - t0));

    const char *ack = "ACK"; DWORD w = 0;
    BOOL wok = WriteFile(c2p, ack, (DWORD)strlen(ack), &w, NULL);
    logmsg(L, "[child] sync-WriteFile(ACK) ok=%d wrote=%lu err=%lu\n",
           (int)wok, (unsigned long)w, wok ? 0 : GetLastError());

    CloseHandle(ov); fclose(L);
    return (ok && st && wok) ? 0 : 1;
}

/* ---------------- parent: launch child in an AppContainer token ---------------- */
int main(int argc, char **argv) {
    if (argc > 1 && _stricmp(argv[1], "--child") == 0) { return child_main(); }

    char self[1024]; GetModuleFileNameA(NULL, self, sizeof(self));
    FILE *out = stdout;

    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE p2c_r, p2c_w, c2p_r, c2p_w;
    if (!CreatePipe(&p2c_r, &p2c_w, &sa, 0) || !SetHandleInformation(p2c_w, HANDLE_FLAG_INHERIT, 0)) {
        fprintf(out, "[parent] CreatePipe(p2c) failed err=%lu\n", GetLastError()); return 4; }
    if (!CreatePipe(&c2p_r, &c2p_w, &sa, 0) || !SetHandleInformation(c2p_r, HANDLE_FLAG_INHERIT, 0)) {
        fprintf(out, "[parent] CreatePipe(c2p) failed err=%lu\n", GetLastError()); return 4; }

    /* Build a restricted AppContainer primary token from the current user. */
    DWORD tokwant = TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_ADJUST_SECURITY | STANDARD_RIGHTS_READ;
    HANDLE selftok = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), tokwant, &selftok)) {
        fprintf(out, "[parent] OpenProcessToken failed err=%lu\n", GetLastError()); return 6; }
    HANDLE ctok = NULL;
    if (!DuplicateTokenEx(selftok, TOKEN_ASSIGN_PRIMARY | TOKEN_ADJUST_SECURITY | STANDARD_RIGHTS_READ, NULL,
                          SecurityIdentification, TokenPrimary, &ctok)) {
        fprintf(out, "[parent] DuplicateTokenEx failed err=%lu\n", GetLastError()); return 6; }

    PSID integ = NULL;
    ConvertStringSidToSidA("S-1-16-8192", &integ);   /* Medium integrity */
    WEX_TML ml = {0}; ml.Sid = integ;
    if (!SetTokenInformation(ctok, TokenIntegrityLevel, &ml, sizeof(ml)))
        fprintf(out, "[parent] SetTokenInformation(Integrity) FAILED err=%lu\n", GetLastError());

    PSID cont = NULL;
    ConvertStringSidToSidA("S-1-15-2-1000-2000-3000-4096", &cont);   /* AppContainer SID */
    if (!SetTokenInformation(ctok, TokenAppContainerSid, &cont, sizeof(cont)))
        fprintf(out, "[parent] SetTokenInformation(ContainerSid) FAILED err=%lu\n", GetLastError());

    STARTUPINFOA si = {0}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = p2c_r;
    si.hStdOutput = c2p_w;
    si.hStdError = NULL;

    char cmd[1400]; snprintf(cmd, sizeof(cmd), "\"%s\" --child", self);
    PROCESS_INFORMATION pi = {0};
    DWORD t0 = GetTickCount64();
    if (!CreateProcessAsUserA(ctok, NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        fprintf(out, "[parent] CreateProcessAsUser(container) FAILED err=%lu\n", GetLastError());
        CloseHandle(ctok); CloseHandle(selftok); return 7; }
    CloseHandle(selftok); CloseHandle(ctok);
    CloseHandle(p2c_r); CloseHandle(c2p_w);

    fprintf(out, "[parent pid=%lu] container-child pid=%lu CreateProcessAsUser ok\n",
            (unsigned long)GetCurrentProcessId(), (unsigned long)pi.dwProcessId);

    const char *hand = "HANDSHAK"; DWORD hw = 0;
    BOOL hok = WriteFile(p2c_w, hand, (DWORD)strlen(hand), &hw, NULL);
    fprintf(out, "[parent] WriteFile(HANDSHAK) ok=%d wrote=%lu err=%lu\n",
            (int)hok, (unsigned long)hw, hok ? 0 : GetLastError());

    Sleep(50);
    const char *ready = "READY\n"; DWORD rw = 0;
    BOOL rok = WriteFile(p2c_w, ready, (DWORD)strlen(ready), &rw, NULL);
    fprintf(out, "[parent] +%lu ms WriteFile(READY) ok=%d wrote=%lu err=%lu\n",
            (unsigned long)(GetTickCount64() - t0), (int)rok, (unsigned long)rw, rok ? 0 : GetLastError());

    char abuf[64]; DWORD agot = 0;
    BOOL aok = ReadFile(c2p_r, abuf, sizeof(abuf), &agot, NULL);
    DWORD t1 = GetTickCount64();
    fprintf(out, "[parent] ack-ReadFile ok=%d got=%lu content='%.*s' err=%lu  time_to_ack_ms=%lu\n",
            (int)aok, (unsigned long)agot, (int)agot, abuf, aok ? 0 : GetLastError(),
            (unsigned long)(t1 - t0));

    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    CloseHandle(p2c_w); CloseHandle(c2p_r);
    return 0;
}
