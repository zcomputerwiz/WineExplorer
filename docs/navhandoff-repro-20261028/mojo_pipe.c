/*
 * navhandoff_mojo_pipe.c — focused native-vs-Wine semantic repro (Task 2).
 *
 * Models the exact primitive implicated by Task 1: the browser -> network-service
 * child Mojo channel bootstrap = a pipe handle passed to a freshly-launched child
 * at CreateProcess time, then overlapped pipe I/O completion between the two
 * processes. One identical PE is run on BOTH native Windows and Wine; we capture:
 *   - does the inherited pipe handle arrive usable in the child?
 *   - synchronous write->read handshake: bytes + status codes.
 *   - OVERLAPPED (async) read: submit/complete status, bytes, submit->completion ms.
 *   - parent time-to-ACK (channel "ready" round trip), bounded so it never hangs.
 *   - process identity: PIDs of parent/child.
 * Child diagnostics go to mojo_child.log; only "ACK" crosses the c2p pipe.
 *
 * Build: x86_64-w64-mingw32-gcc -O2 -static -o mojo_pipe.exe navhandoff_mojo_pipe.c
 */
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <stdio.h>

static void logmsg(FILE *f, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
}

/* ---------------- child (logs to mojo_child.log) ---------------- */
static int child_main(void) {
    FILE *L = fopen("mojo_child.log", "w");
    if (!L) L = stderr;
    HANDLE p2c = GetStdHandle(STD_INPUT_HANDLE);   /* read end, inherited from parent */
    HANDLE c2p = GetStdHandle(STD_OUTPUT_HANDLE);  /* write end back to parent */

    logmsg(L, "[child pid=%lu] started\n", (unsigned long)GetCurrentProcessId());
    if (!p2c || p2c == INVALID_HANDLE_VALUE) {
        logmsg(L, "[child] FAIL: no inherited stdin pipe handle err=%lu\n", GetLastError());
        fclose(L); return 3;
    }

    /* 1) Synchronous read of the parent's 8-byte handshake. */
    char rbuf[64]; DWORD got = 0;
    BOOL ok = ReadFile(p2c, rbuf, sizeof(rbuf), &got, NULL);
    logmsg(L, "[child] sync-ReadFile(HANDSHAK) ok=%d got=%lu err=%lu\n",
           (int)ok, (unsigned long)got, ok ? 0 : GetLastError());

    /* 2) OVERLAPPED read: parent writes "READY\n" asynchronously. Bounded wait. */
    HANDLE ov = CreateEventA(NULL, TRUE, FALSE, NULL);
    OVERLAPPED ovl = {0}; ovl.hEvent = ov;
    char obuf[64]; DWORD ogot = 0;
    DWORD t0 = GetTickCount64();
    BOOL sub = ReadFile(p2c, obuf, sizeof(obuf), &ogot, &ovl);
    DWORD st = (sub || GetLastError() == ERROR_IO_PENDING) ?
               GetOverlappedResult(p2c, &ovl, &ogot, TRUE) : 0;   /* wait for completion */
    DWORD t1 = GetTickCount64();
    logmsg(L, "[child] overlapped-ReadFile(READY) submit=%d complete=%d got=%lu bytes='%.*s' err=%lu lat_ms=%lu\n",
           (int)sub, (int)st, (unsigned long)ogot, (int)ogot, obuf, st ? 0 : GetLastError(),
           (unsigned long)(t1 - t0));

    /* 3) Write ACK back to parent over the inherited c2p pipe. */
    const char *ack = "ACK"; DWORD w = 0;
    BOOL wok = WriteFile(c2p, ack, (DWORD)strlen(ack), &w, NULL);
    logmsg(L, "[child] sync-WriteFile(ACK) ok=%d wrote=%lu err=%lu\n",
           (int)wok, (unsigned long)w, wok ? 0 : GetLastError());

    CloseHandle(ov); fclose(L);
    return (ok && st && wok) ? 0 : 1;
}

/* ---------------- parent (logs to stdout/terminal) ---------------- */
int main(int argc, char **argv) {
    if (argc > 1 && _stricmp(argv[1], "--child") == 0) { return child_main(); }

    char self[1024]; GetModuleFileNameA(NULL, self, sizeof(self));
    FILE *out = stdout;

    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };   /* inheritable */
    HANDLE p2c_r, p2c_w, c2p_r, c2p_w;
    if (!CreatePipe(&p2c_r, &p2c_w, &sa, 0) || !SetHandleInformation(p2c_w, HANDLE_FLAG_INHERIT, 0)) {
        fprintf(out, "[parent] CreatePipe(p2c) failed err=%lu\n", GetLastError()); return 4; }
    if (!CreatePipe(&c2p_r, &c2p_w, &sa, 0) || !SetHandleInformation(c2p_r, HANDLE_FLAG_INHERIT, 0)) {
        fprintf(out, "[parent] CreatePipe(c2p) failed err=%lu\n", GetLastError()); return 4; }

    STARTUPINFOA si = {0}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = p2c_r;   /* child reads from here */
    si.hStdOutput = c2p_w;  /* child writes back here */
    si.hStdError = NULL;

    char cmd[1400]; snprintf(cmd, sizeof(cmd), "\"%s\" --child", self);
    PROCESS_INFORMATION pi = {0};
    DWORD t0 = GetTickCount64();
    BOOL cp = CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    if (!cp) { fprintf(out, "[parent] CreateProcess failed err=%lu\n", GetLastError()); return 5; }
    CloseHandle(p2c_r); CloseHandle(c2p_w);   /* parent keeps p2c_w (write) + c2p_r (read) */

    const char *hand = "HANDSHAK"; DWORD hw = 0;
    BOOL hok = WriteFile(p2c_w, hand, (DWORD)strlen(hand), &hw, NULL);
    fprintf(out, "[parent pid=%lu] child pid=%lu CreateProcess ok\n",
            (unsigned long)GetCurrentProcessId(), (unsigned long)pi.dwProcessId);
    fprintf(out, "[parent] WriteFile(HANDSHAK) ok=%d wrote=%lu err=%lu\n",
            (int)hok, (unsigned long)hw, hok ? 0 : GetLastError());

    /* Small controlled gap, then send the byte that completes the child's overlapped read. */
    Sleep(50);
    const char *ready = "READY\n"; DWORD rw = 0;
    BOOL rok = WriteFile(p2c_w, ready, (DWORD)strlen(ready), &rw, NULL);
    fprintf(out, "[parent] +%lu ms WriteFile(READY) ok=%d wrote=%lu err=%lu\n",
            (unsigned long)(GetTickCount64() - t0), (int)rok, (unsigned long)rw, rok ? 0 : GetLastError());

    /* Read the child's ACK. Child exits after writing it, closing c2p_w -> read returns. */
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
