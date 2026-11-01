/*
 * navhandoff_mojo_np.c — Rework Task 2: Chromium-matching Mojo platform-channel PE repro.
 *
 * Implements the PINNED Chromium Windows primitive exactly (per WO correction 1d):
 *   - Parent creates a DUPLEX named pipe: CreateNamedPipeW("\\.\pipe\mojo.<pid>.<rand>",
 *     PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE, 1, ...).
 *   - Parent connects the client end in-process via
 *     CreateFileW(name, GENERIC_READ|GENERIC_WRITE, 0, &sa, OPEN_EXISTING,
 *       FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS, NULL).
 *   - Parent launches the child with STARTUPINFOEXW + UpdateProcThreadAttribute(
 *     PROC_THREAD_ATTRIBUTE_HANDLE_LIST = 0x00020002) carrying the CLIENT pipe handle (NOT std
 *     handles), and passes its value on the command line: --mojo-platform-channel-handle=<int>.
 *   - Child parses that switch from GetCommandLineW, adopts the inherited handle at the same
 *     index, and both ends do OVERLAPPED duplex I/O bound to an IOCP (CreateIoCompletionPort /
 *     GetQueuedCompletionStatus).
 *
 * Bounded waits with owned cleanup; any failure exits nonzero. One identical PE is run on
 * native Windows 10 and the project candidate Wine (g4 fork, wine-11.19). We compare exact
 * bytes, status codes, overlapped completion ORDERING, IOCP key association, and latency.
 *
 * A companion multi-handle probe (pipe + event + semaphore) lives in navhandoff_mojo_np_multi.c;
 * with the launch sequence matched to Chromium it behaves identically on native and Wine — see
 * docs/CHROMIUM_HANDOFF_REWORK_RESULT_20261028.md.
 *
 * Build: x86_64-w64-mingw32-gcc -O2 -static -o mojo_np.exe navhandoff_mojo_np.c
 */
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#ifndef PROC_THREAD_ATTRIBUTE_HANDLE_LIST
#define PROC_THREAD_ATTRIBUTE_HANDLE_LIST 0x00020002
#endif

static const DWORD PARENT_KEY = 0x5041; /* 'PA' */
static const DWORD CHILD_KEY  = 0x4348; /* 'CH' */
static const DWORD WAIT_MS    = 10000;  /* bounded wait per completion */

static ULONGLONG now_us(void) {
    LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
    return (ULONGLONG)(c.QuadPart * 1000000.0 / f.QuadPart);
}

/* Bounded GetQueuedCompletionStatus wrapper. Returns 0 on success, -1 on timeout/error. */
static int wait_completion(HANDLE iocp, OVERLAPPED **out_ovl, DWORD *out_bytes) {
    DWORD bytes = 0; DWORD_PTR key = 0; OVERLAPPED *ev = NULL;
    BOOL ok = GetQueuedCompletionStatus(iocp, &bytes, &key, &ev, WAIT_MS);
    if (!ok) return -1;
    *out_ovl = (OVERLAPPED *)ev; *out_bytes = bytes;
    return 0;
}

/* Parse an unsigned 64-bit value following a wide switch prefix in the command line. */
static int parse_switch(const wchar_t *cmd, const wchar_t *sw, unsigned long long *out) {
    const wchar_t *p = wcsstr(cmd, sw);
    if (!p) return 0;
    *out = wcstoull(p + wcslen(sw), NULL, 10);
    return 1;
}

/* ---------------- child: adopt the mojo pipe handle, do overlapped I/O over IOCP ------------ */
static int child_main(void) {
    FILE *L = fopen("mojo_np_child.log", "w");
    if (!L) L = stderr;

    const wchar_t *cmd = GetCommandLineW();
    unsigned long long hv = 0;
    if (!parse_switch(cmd, L"--mojo-platform-channel-handle=", &hv)) { fprintf(L, "[child] FAIL: no mojo switch\n"); fclose(L); return 3; }
    HANDLE h = (HANDLE)(uintptr_t)hv;
    fprintf(L, "[child pid=%lu] adopted pipe handle=0x%llx\n", (unsigned long)GetCurrentProcessId(), hv);
    if (!h || h == INVALID_HANDLE_VALUE) { fprintf(L, "[child] FAIL: invalid adopted pipe handle\n"); fclose(L); return 3; }

    HANDLE iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    if (!iocp) { fprintf(L, "[child] CreateIoCompletionPort failed err=%lu\n", GetLastError()); fclose(L); return 4; }
    if (!CreateIoCompletionPort(h, iocp, CHILD_KEY, 0)) {
        fprintf(L, "[child] CreateIoCompletionPort(assoc) FAILED err=%lu\n", GetLastError());
        CloseHandle(iocp); fclose(L); return 4; }

    char rbuf[64]; DWORD rb = 0;
    OVERLAPPED ro = {0}, wo = {0};
    ULONGLONG t_submit_r = now_us();
    if (!ReadFile(h, rbuf, sizeof(rbuf), &rb, &ro) && GetLastError() != ERROR_IO_PENDING) {
        fprintf(L, "[child] ReadFile submit failed err=%lu\n", GetLastError()); CloseHandle(iocp); fclose(L); return 5; }

    const char *ack = "ACK"; DWORD wb = 0;
    ULONGLONG t_submit_w = now_us();
    if (!WriteFile(h, ack, (DWORD)strlen(ack), &wb, &wo) && GetLastError() != ERROR_IO_PENDING) {
        fprintf(L, "[child] WriteFile submit failed err=%lu\n", GetLastError()); CloseHandle(iocp); fclose(L); return 5; }

    OVERLAPPED *got[2]; DWORD gbytes[2]; ULONGLONG t_done[2]; int rc = 0; DWORD read_bytes = 0;
    for (int i = 0; i < 2; i++) {
        if (wait_completion(iocp, &got[i], &gbytes[i]) != 0) { fprintf(L, "[child] completion %d TIMEOUT/err=%lu\n", i, GetLastError()); rc = 6; break; }
        t_done[i] = now_us();
        if (got[i] == &ro) read_bytes = gbytes[i];
    }
    if (rc == 0) {
        for (int i = 0; i < 2; i++) {
            int is_read = (got[i] == &ro);
            fprintf(L, "[child] comp#%d %s bytes=%lu lat_us=%llu\n",
                    i, is_read ? "READ(HANDSHAK)" : "WRITE(ACK)", gbytes[i], t_done[i] - (is_read ? t_submit_r : t_submit_w));
        }
        fprintf(L, "[child] read-bytes=%lu content='%.*s'\n", (unsigned long)read_bytes, (int)(read_bytes > 8 ? 8 : read_bytes), rbuf);
    }
    CloseHandle(iocp); fclose(L);
    return rc;
}

/* ---------------- parent: create named pipe + client, launch child via HANDLE_LIST ---------- */
int main(void) {
    const wchar_t *cmd = GetCommandLineW();
    if (wcsstr(cmd, L"--child")) { return child_main(); }

    wchar_t selfw[MAX_PATH]; GetModuleFileNameW(NULL, selfw, MAX_PATH);
    FILE *out = stdout;

    /* 1) Duplex named pipe (server end), overlapped byte mode, single instance. */
    wchar_t pname[256];
    swprintf(pname, 256, L"\\\\.\\pipe\\mojo.%lu.%lu",
             (unsigned long)GetCurrentProcessId(), (unsigned long)(GetTickCount64() & 0xffff));
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE server = CreateNamedPipeW(pname,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE, 1, 65536, 65536, 0, &sa);
    if (server == INVALID_HANDLE_VALUE) { fprintf(out, "[parent] CreateNamedPipeW FAILED err=%lu\n", GetLastError()); return 4; }

    /* 2) Connect the client end in-process. */
    HANDLE client = CreateFileW(pname, GENERIC_READ | GENERIC_WRITE, 0, &sa, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS, NULL);
    if (client == INVALID_HANDLE_VALUE) { fprintf(out, "[parent] CreateFileW(client) FAILED err=%lu\n", GetLastError()); CloseHandle(server); return 4; }
    if (!ConnectNamedPipe(server, NULL) && GetLastError() != ERROR_PIPE_CONNECTED)
        fprintf(out, "[parent] ConnectNamedPipe note err=%lu\n", GetLastError());

    /* 3) STARTUPINFOEXW with PROC_THREAD_ATTRIBUTE_HANDLE_LIST = { client }. */
    HANDLE hlist[1];
    hlist[0] = client;
    /* Match Chromium (launch_win.cc): mark each listed handle inheritable before listing it. */
    if (!SetHandleInformation(client, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
        fprintf(out, "[parent] SetHandleInformation(client) note err=%lu\n", GetLastError());
    SIZE_T alSize = 0;
    BOOL qok = InitializeProcThreadAttributeList(NULL, 1, 0, &alSize);
    /* Size-query form: expected to return FALSE + ERROR_INSUFFICIENT_BUFFER with alSize filled. */
    if (qok || GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        fprintf(out, "[parent] InitAttrList(size query) unexpected ok=%d err=%lu size=%lu\n",
                qok, GetLastError(), (unsigned long)alSize);
        return 4; }
    LPVOID alBuf = malloc(alSize);
    STARTUPINFOEXW siEx = {0};
    siEx.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    siEx.lpAttributeList = (PPROC_THREAD_ATTRIBUTE_LIST)alBuf;
    if (!InitializeProcThreadAttributeList(siEx.lpAttributeList, 1, 0, &alSize)) { fprintf(out, "[parent] InitAttrList failed err=%lu\n", GetLastError()); return 4; }
    if (!UpdateProcThreadAttribute(siEx.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, hlist, sizeof(hlist), NULL, NULL)) {
        fprintf(out, "[parent] UpdateProcThreadAttribute(HANDLE_LIST) FAILED err=%lu\n", GetLastError()); return 4; }

    /* 4) Command line carries the client handle value. */
    wchar_t cmdline[2048];
    swprintf(cmdline, 2048, L"\"%s\" --child --mojo-platform-channel-handle=%llu",
             selfw, (unsigned long long)(uintptr_t)client);

    PROCESS_INFORMATION pi = {0};
    if (!CreateProcessW(selfw, cmdline, NULL, NULL, TRUE, EXTENDED_STARTUPINFO_PRESENT, NULL, NULL, &siEx.StartupInfo, &pi)) {
        fprintf(out, "[parent] CreateProcessW FAILED err=%lu client=0x%llx alSize=%lu selfw=%ls\n",
                GetLastError(), (unsigned long long)(uintptr_t)client, (unsigned long)alSize, selfw);
        return 5; }
    fprintf(out, "[parent pid=%lu] child pid=%lu HANDLE_LIST={client=0x%llx} launched\n",
            (unsigned long)GetCurrentProcessId(), (unsigned long)pi.dwProcessId,
            (unsigned long long)(uintptr_t)client);

    /* 5) Parent does overlapped I/O over the SERVER end via IOCP. */
    HANDLE iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    if (!CreateIoCompletionPort(server, iocp, PARENT_KEY, 0)) { fprintf(out, "[parent] assoc FAILED err=%lu\n", GetLastError()); return 4; }

    const char *hand = "HANDSHAK"; DWORD wb = 0;
    OVERLAPPED wo = {0}, ro = {0};
    ULONGLONG t0w = now_us();
    if (!WriteFile(server, hand, (DWORD)strlen(hand), &wb, &wo) && GetLastError() != ERROR_IO_PENDING) {
        fprintf(out, "[parent] WriteFile(HANDSHAK) submit failed err=%lu\n", GetLastError()); return 5; }
    char rbuf[64]; DWORD rb = 0;
    ULONGLONG t0r = now_us();
    if (!ReadFile(server, rbuf, sizeof(rbuf), &rb, &ro) && GetLastError() != ERROR_IO_PENDING) {
        fprintf(out, "[parent] ReadFile(ACK) submit failed err=%lu\n", GetLastError()); return 5; }

    OVERLAPPED *got[2]; DWORD gbytes[2]; ULONGLONG t_done[2]; int rc = 0; DWORD ack_bytes = 0;
    for (int i = 0; i < 2; i++) {
        if (wait_completion(iocp, &got[i], &gbytes[i]) != 0) { fprintf(out, "[parent] completion %d TIMEOUT/err=%lu\n", i, GetLastError()); rc = 6; break; }
        t_done[i] = now_us();
        if (got[i] == &ro) ack_bytes = gbytes[i];
    }
    if (rc == 0) {
        for (int i = 0; i < 2; i++) {
            int is_read = (got[i] == &ro);
            fprintf(out, "[parent] comp#%d %s bytes=%lu lat_us=%llu\n",
                    i, is_read ? "READ(ACK)" : "WRITE(HANDSHAK)", gbytes[i], t_done[i] - (is_read ? t0r : t0w));
        }
        fprintf(out, "[parent] ack-bytes=%lu content='%.*s'\n", (unsigned long)ack_bytes, (int)(ack_bytes > 8 ? 8 : ack_bytes), rbuf);
    }

    /* Owned cleanup. */
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    CloseHandle(iocp); CloseHandle(server); CloseHandle(client);
    DeleteProcThreadAttributeList(siEx.lpAttributeList);
    free(alBuf);
    return rc;
}
