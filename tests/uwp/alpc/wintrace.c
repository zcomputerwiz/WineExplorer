/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box tracer for a packaged application on Windows: activate it by its application user model id, attach as a debugger, put
 * int3 breakpoints at module!RVA addresses as the modules load, and log each hit (time, thread, registers, and the return addresses
 * on the stack resolved to module+offset). Observation of a local program from outside; nothing of it is copied.
 *
 *   wintrace.exe AUMID SECONDS OUTFILE MODULE.DLL:RVA_HEX:LABEL[:MAXHITS] ...
 *   the load of every module is logged too (with the load order), so the order of events can be compared with a Wine run.
 * Build (mingw): x86_64-w64-mingw32-gcc -O2 -o wintrace.exe wintrace.c -lole32 -luuid
 */
#define COBJMACROS
#include <windows.h>
#include <shobjidl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct bp { char mod[80]; DWORD rva; char label[64]; int hits, max; BYTE orig; ULONG_PTR addr; int active, wantret; };
/* a one-shot breakpoint at the return address of a hit (option R): logs rax and what the four argument registers point to */
struct ret { ULONG_PTR addr; DWORD tid; int bp, used, n; BYTE orig; ULONG_PTR args[4]; };
static struct ret rets[64];
struct module { ULONG_PTR base; DWORD size; char name[128]; };

static struct bp bps[128];
static int nbps;
static struct module mods[1024];
static int nmods;
static FILE *out;
static DWORD t0;
static HANDLE proc;
static int pending[65536];

static const char *base_name(const char *path)
{
    const char *s = strrchr(path, '\\');
    return s ? s + 1 : path;
}

static void log_line(const char *fmt, ...)
{
    va_list ap;
    fprintf(out, "[%6lu] ", (unsigned long)(GetTickCount() - t0));
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);
    fputc('\n', out);
    fflush(out);
}

static const char *resolve(ULONG_PTR a, char *buf, size_t n)
{
    int i;

    for (i = 0; i < nmods; i++)
        if (a >= mods[i].base && a < mods[i].base + mods[i].size)
        {
            snprintf(buf, n, "%s+%#lx", mods[i].name, (unsigned long)(a - mods[i].base));
            return buf;
        }
    return NULL;
}

static void add_module(HANDLE file, LPVOID base)
{
    WCHAR wpath[MAX_PATH];
    char path[MAX_PATH] = "?";
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS64 nt;
    SIZE_T got;
    struct module *m = &mods[nmods];
    int i;

    if (file && GetFinalPathNameByHandleW(file, wpath, MAX_PATH, 0)) WideCharToMultiByte(CP_UTF8, 0, wpath, -1, path, sizeof(path), NULL, NULL);
    m->base = (ULONG_PTR)base;
    m->size = 0;
    if (ReadProcessMemory(proc, base, &dos, sizeof(dos), &got) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
        ReadProcessMemory(proc, (char *)base + dos.e_lfanew, &nt, sizeof(nt), &got))
        m->size = nt.OptionalHeader.SizeOfImage;
    snprintf(m->name, sizeof(m->name), "%s", base_name(path));
    log_line("load %s at %p size %#lx", m->name, base, (unsigned long)m->size);
    for (i = 0; i < nbps; i++)
        if (!bps[i].active && !_stricmp(bps[i].mod, m->name))
        {
            BYTE cc = 0xcc;

            bps[i].addr = m->base + bps[i].rva;
            if (ReadProcessMemory(proc, (void *)bps[i].addr, &bps[i].orig, 1, &got) &&
                WriteProcessMemory(proc, (void *)bps[i].addr, &cc, 1, &got))
            {
                FlushInstructionCache(proc, (void *)bps[i].addr, 1);
                bps[i].active = 1;
                log_line("  breakpoint %s at %s+%#lx", bps[i].label, m->name, (unsigned long)bps[i].rva);
            }
            else log_line("  breakpoint %s FAILED (error %lu)", bps[i].label, GetLastError());
        }
    nmods++;
}

static void restore(struct bp *b, BYTE value)
{
    SIZE_T got;

    WriteProcessMemory(proc, (void *)b->addr, &value, 1, &got);
    FlushInstructionCache(proc, (void *)b->addr, 1);
}

static void hit(struct bp *b, DWORD tid)
{
    CONTEXT ctx;
    HANDLE th = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, tid);
    ULONG_PTR stack[96];
    SIZE_T got = 0;
    char buf[200];
    int i, shown = 0;

    if (!th) return;
    memset(&ctx, 0, sizeof(ctx));
    ctx.ContextFlags = CONTEXT_FULL;
    GetThreadContext(th, &ctx);
    b->hits++;
    if (b->hits <= b->max)
    {
        log_line("HIT %s #%d tid %lu rcx %#llx rdx %#llx r8 %#llx r9 %#llx", b->label, b->hits, (unsigned long)tid,
                 (unsigned long long)ctx.Rcx, (unsigned long long)ctx.Rdx, (unsigned long long)ctx.R8, (unsigned long long)ctx.R9);
        ReadProcessMemory(proc, (void *)ctx.Rsp, stack, sizeof(stack), &got);
        for (i = 0; i < (int)(got / 8) && shown < 12; i++)
            if (resolve(stack[i], buf, sizeof(buf))) { log_line("    [rsp+%#x] %s", i * 8, buf); shown++; }
    }
    if (b->wantret && b->hits <= b->max)
    {
        ULONG_PTR ra = 0;
        int k;

        ReadProcessMemory(proc, (void *)ctx.Rsp, &ra, sizeof(ra), &got);
        for (k = 0; k < 64 && ra; k++)
            if (!rets[k].used)
            {
                BYTE cc = 0xcc;

                rets[k].used = 1; rets[k].addr = ra; rets[k].tid = tid; rets[k].bp = (int)(b - bps); rets[k].n = b->hits;
                rets[k].args[0] = ctx.Rcx; rets[k].args[1] = ctx.Rdx; rets[k].args[2] = ctx.R8; rets[k].args[3] = ctx.R9;
                ReadProcessMemory(proc, (void *)ra, &rets[k].orig, 1, &got);
                WriteProcessMemory(proc, (void *)ra, &cc, 1, &got);
                FlushInstructionCache(proc, (void *)ra, 1);
                break;
            }
    }
    restore(b, b->orig);
    ctx.Rip = b->addr;
    ctx.EFlags |= 0x100;
    SetThreadContext(th, &ctx);
    CloseHandle(th);
    pending[tid & 0xffff] = (int)(b - bps) + 1;
}

/* the return of a traced call: rax, and for each argument that is a readable pointer the value it points to and, if that is an object,
 * the module and offset of its vtable */
static int ret_hit(ULONG_PTR a, DWORD tid)
{
    int k, i;

    for (k = 0; k < 64; k++)
    {
        CONTEXT ctx;
        HANDLE th;
        char buf[200];
        SIZE_T got;

        if (!rets[k].used || rets[k].addr != a || rets[k].tid != tid) continue;
        th = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, tid);
        memset(&ctx, 0, sizeof(ctx));
        ctx.ContextFlags = CONTEXT_FULL;
        if (th) GetThreadContext(th, &ctx);
        log_line("RET %s #%d tid %lu rax %#llx", bps[rets[k].bp].label, rets[k].n, (unsigned long)tid, (unsigned long long)ctx.Rax);
        for (i = 0; i < 4; i++)
        {
            ULONG_PTR p = rets[k].args[i], v = 0, vt = 0;

            if (p < 0x10000 || !ReadProcessMemory(proc, (void *)p, &v, sizeof(v), &got) || got != sizeof(v)) continue;
            if (v > 0x10000 && ReadProcessMemory(proc, (void *)v, &vt, sizeof(vt), &got) && got == sizeof(vt) && resolve(vt, buf, sizeof(buf)))
                log_line("    arg%d %#llx -> %#llx, an object with vtable %s", i, (unsigned long long)p, (unsigned long long)v, buf);
            else
                log_line("    arg%d %#llx -> %#llx", i, (unsigned long long)p, (unsigned long long)v);
        }
        WriteProcessMemory(proc, (void *)a, &rets[k].orig, 1, &got);
        FlushInstructionCache(proc, (void *)a, 1);
        ctx.Rip = a;
        if (th) { SetThreadContext(th, &ctx); CloseHandle(th); }
        rets[k].used = 0;
        return 1;
    }
    return 0;
}

/* attach to processes of the image NAME that the debugger does not know yet (the packaged application may relaunch itself) */
#include <tlhelp32.h>
static DWORD attached[64];
static int nattached;

static void attach_new(DWORD *first, const char *name)
{
    PROCESSENTRY32 pe;
    HANDLE snap;
    int i;

    if (!name || !*name) return;
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    pe.dwSize = sizeof(pe);
    for (i = Process32First(snap, &pe); i; i = Process32Next(snap, &pe))
    {
        int k, known = pe.th32ProcessID == *first;

        if (_stricmp(pe.szExeFile, name)) continue;
        for (k = 0; k < nattached; k++) known |= attached[k] == pe.th32ProcessID;
        if (known || nattached >= 64) continue;
        attached[nattached++] = pe.th32ProcessID;
        if (DebugActiveProcess(pe.th32ProcessID)) log_line("attached to %s %lu", name, (unsigned long)pe.th32ProcessID);
    }
    CloseHandle(snap);
}

int main(int argc, char **argv)
{
    const char *watch = getenv("WINTRACE_WATCH");   /* image name of processes to attach to as they appear, e.g. Notepad.exe */
    IApplicationActivationManager *mgr = NULL;
    WCHAR aumid[256];
    DWORD pid = 0, limit, i;
    HRESULT hr;
    DEBUG_EVENT ev;

    if (argc < 5) { fprintf(stderr, "usage: wintrace AUMID SECONDS OUTFILE MODULE:RVA:LABEL[:MAX]...\n"); return 2; }
    out = fopen(argv[3], "w");
    if (!out) return 2;
    limit = (DWORD)atoi(argv[2]) * 1000;
    for (i = 4; i < (DWORD)argc && nbps < 128; i++)
    {
        char *a = strtok(argv[i], ":"), *r = strtok(NULL, ":"), *l = strtok(NULL, ":"), *m = strtok(NULL, ":"), *w = strtok(NULL, ":");

        if (!a || !r || !l) continue;
        bps[nbps].wantret = w && *w == 'R';
        snprintf(bps[nbps].mod, sizeof(bps[nbps].mod), "%s", a);
        bps[nbps].rva = (DWORD)strtoul(r, NULL, 16);
        snprintf(bps[nbps].label, sizeof(bps[nbps].label), "%s", l);
        bps[nbps].max = m ? atoi(m) : 30;
        nbps++;
    }
    MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, aumid, 256);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    hr = CoCreateInstance(&CLSID_ApplicationActivationManager, NULL, CLSCTX_LOCAL_SERVER, &IID_IApplicationActivationManager, (void **)&mgr);
    if (SUCCEEDED(hr)) hr = IApplicationActivationManager_ActivateApplication(mgr, aumid, NULL, AO_NOERRORUI, &pid);
    t0 = GetTickCount();
    fprintf(out, "activate %s: hr %08lx pid %lu\n", argv[1], (unsigned long)hr, (unsigned long)pid);
    if (FAILED(hr) || !pid) return 1;
    DebugSetProcessKillOnExit(FALSE);
    if (!DebugActiveProcess(pid)) { fprintf(out, "DebugActiveProcess: error %lu\n", GetLastError()); return 1; }
    for (i = 0; i < 65536; i++) pending[i] = 0;
    while (GetTickCount() - t0 < limit)
    {
        DWORD cont = DBG_CONTINUE;

        if (!WaitForDebugEvent(&ev, 10)) { attach_new(&pid, watch); continue; }
        switch (ev.dwDebugEventCode)
        {
        case CREATE_PROCESS_DEBUG_EVENT:
        {
            int k;

            /* a new debuggee (the application may start a second process): its modules and breakpoints are its own */
            nmods = 0;
            for (k = 0; k < nbps; k++) bps[k].active = 0;
            log_line("process %lu attached", (unsigned long)ev.dwProcessId);
            proc = ev.u.CreateProcessInfo.hProcess;
            add_module(ev.u.CreateProcessInfo.hFile, ev.u.CreateProcessInfo.lpBaseOfImage);
            break;
        }
        case LOAD_DLL_DEBUG_EVENT:
            add_module(ev.u.LoadDll.hFile, ev.u.LoadDll.lpBaseOfDll);
            break;
        case EXCEPTION_DEBUG_EVENT:
        {
            ULONG_PTR a = (ULONG_PTR)ev.u.Exception.ExceptionRecord.ExceptionAddress;
            DWORD code = ev.u.Exception.ExceptionRecord.ExceptionCode;
            int k, found = 0;

            if (code == EXCEPTION_BREAKPOINT)
            {
                if (ret_hit(a, ev.dwThreadId)) found = 1;
                for (k = 0; k < nbps && !found; k++)
                    if (bps[k].active && bps[k].addr == a) { hit(&bps[k], ev.dwThreadId); found = 1; break; }
            }
            else if (code == EXCEPTION_SINGLE_STEP && pending[ev.dwThreadId & 0xffff])
            {
                struct bp *b = &bps[pending[ev.dwThreadId & 0xffff] - 1];
                restore(b, 0xcc);
                pending[ev.dwThreadId & 0xffff] = 0;
                found = 1;
            }
            if (!found && code != EXCEPTION_BREAKPOINT)
            {
                if (ev.u.Exception.dwFirstChance) log_line("exception %08lx at %p tid %lu", (unsigned long)code, (void *)a, (unsigned long)ev.dwThreadId);
                cont = DBG_EXCEPTION_NOT_HANDLED;
            }
            break;
        }
        case EXIT_PROCESS_DEBUG_EVENT:
            log_line("process %lu exited, code %lu", (unsigned long)ev.dwProcessId, (unsigned long)ev.u.ExitProcess.dwExitCode);
            if (ev.dwProcessId == pid && !watch) { ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, DBG_CONTINUE); goto done; }
            break;
        }
        ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, cont);
        attach_new(&pid, watch);
    }
done:
    for (i = 0; i < (DWORD)nbps; i++)
    {
        log_line("summary %s: %d hits", bps[i].label, bps[i].hits);
        if (bps[i].active && proc) restore(&bps[i], bps[i].orig);   /* the process must not trap after the detach */
    }
    DebugActiveProcessStop(pid);
    return 0;
}
