/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2026 WineExplorer contributors
 *
 * Black-box tracer for a packaged application on Windows: activate it by its application user model id, attach as a debugger, put
 * int3 breakpoints at real entry points as the modules load, and log each hit (time, thread, registers, and the return addresses
 * on the stack resolved to module+offset). Observation of a local program from outside; nothing of it is copied.
 *
 *   wintrace.exe AUMID SECONDS OUTFILE TARGET:LABEL[:MAXHITS[:R]] ...
 *
 * A target is an entry point that this program finds at run time, in its own process, from the same module the application loads. A raw
 * address or RVA is not accepted, and no code bytes are logged or stored (the one byte that the breakpoint replaces is only kept in memory
 * to be put back):
 *   MODULE.DLL!ExportName        an export, by GetProcAddress by name
 *   MODULE.DLL!#ORDINAL          an export, by GetProcAddress by ordinal
 *   dcomp.dll!desktop[N]         slot N of the vtable of a live IDCompositionDesktopDevice that this program creates
 *   dcomp.dll!visual[N]          slot N of the vtable of a live IDCompositionVisual that this program creates
 *   winrt!Class.Name@{IID}[N]    slot N of the vtable of the interface IID of the activation factory of the class (RoGetActivationFactory)
 *   d3d11.dll!device@{IID}[N]    slot N of the vtable of the interface IID of a Direct3D 11 device (hardware, BGRA) that this program creates
 *   dwrite.dll!analyzer@{IID}[N] slot N of the vtable of the interface IID of a DirectWrite text analyzer that this program creates
 *   d2d1.dll!context@{IID}[N]    slot N of the vtable of the interface IID of a Direct2D device context that this program creates
 *   d2d1.dll!d2ddevice@{IID}[N]  slot N of the vtable of the interface IID of a Direct2D device that this program creates
 *   d2d1.dll!factory@{IID}[N]    slot N of the vtable of the interface IID of a Direct2D factory that this program creates
 * For the last five, only calls on an object with that vtable are counted (the slots may share code with other objects).
 * The label may end in _D (the 32 qwords at rcx), _E (48 qwords at rdx and r8, at entry and at return) or _F (the low float and double of
 * xmm0 to xmm3); the fifth field R logs the return value and what the argument registers point to.
 * The load of every module is logged too (with the load order), so the order of events can be compared with a Wine run.
 * Build (mingw): x86_64-w64-mingw32-gcc -O2 -o wintrace.exe wintrace.c -lole32 -luuid
 */
#define COBJMACROS
#include <windows.h>
#include <shobjidl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

struct bp { char mod[80]; DWORD rva; char label[64]; int hits, max; BYTE orig; ULONG_PTR addr; int active, wantret;
            DWORD vt_rva; ULONG_PTR base; };   /* vt_rva: count only the calls whose object (rcx) has the vtable at module base + vt_rva */
/* a one-shot breakpoint at the return address of a hit (option R): logs rax and what the four argument registers point to */
struct ret { ULONG_PTR addr; DWORD tid; int bp, used, n; BYTE orig; ULONG_PTR args[4]; };
static struct ret rets[64];
static void dump_mem(const char *tag, ULONG_PTR p);
struct module { ULONG_PTR base; DWORD size; char name[128]; };

static struct bp bps[128];
static int nbps;
static struct module mods[1024];
static int nmods;
static FILE *out;
static DWORD t0;
static HANDLE proc;
static int pending[65536];
static DWORD event_tid;   /* thread of the debug event being handled (logged with module loads) */

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
    log_line("load %s at %p size %#lx tid %lu", m->name, base, (unsigned long)m->size, (unsigned long)event_tid);
    for (i = 0; i < nbps; i++)
        if (!bps[i].active && !_stricmp(bps[i].mod, m->name))
        {
            BYTE cc = 0xcc;

            bps[i].addr = m->base + bps[i].rva;
            bps[i].base = m->base;
            if (ReadProcessMemory(proc, (void *)bps[i].addr, &bps[i].orig, 1, &got) &&
                WriteProcessMemory(proc, (void *)bps[i].addr, &cc, 1, &got))
            {
                FlushInstructionCache(proc, (void *)bps[i].addr, 1);
                BYTE now = 0;

                bps[i].active = 1;
                ReadProcessMemory(proc, (void *)bps[i].addr, &now, 1, &got);
                (void)now;
                log_line("  breakpoint %s set at %s+%#lx", bps[i].label, m->name, (unsigned long)bps[i].rva);
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
    if (b->vt_rva)
    {
        ULONG_PTR vt = 0;

        /* not the object of interest: let it run without a count (the breakpoint is stepped over as usual) */
        if (!ReadProcessMemory(proc, (void *)ctx.Rcx, &vt, sizeof(vt), &got) || vt != b->base + b->vt_rva)
        {
            restore(b, b->orig);
            ctx.Rip = b->addr;
            ctx.EFlags |= 0x100;
            SetThreadContext(th, &ctx);
            CloseHandle(th);
            pending[tid & 0xffff] = (int)(b - bps) + 1;
            return;
        }
    }
    b->hits++;
    if (b->hits <= b->max)
    {
        log_line("HIT %s #%d tid %lu rcx %#llx rdx %#llx r8 %#llx r9 %#llx", b->label, b->hits, (unsigned long)tid,
                 (unsigned long long)ctx.Rcx, (unsigned long long)ctx.Rdx, (unsigned long long)ctx.R8, (unsigned long long)ctx.R9);
        if (strstr(b->label, "_F"))   /* _F: the low float and the low double of xmm0..xmm3 */
        {
            float f0, f1, f2, f3;
            double d0, d1, d2, d3;

            memcpy(&f0, &ctx.Xmm0, 4); memcpy(&f1, &ctx.Xmm1, 4); memcpy(&f2, &ctx.Xmm2, 4); memcpy(&f3, &ctx.Xmm3, 4);
            memcpy(&d0, &ctx.Xmm0, 8); memcpy(&d1, &ctx.Xmm1, 8); memcpy(&d2, &ctx.Xmm2, 8); memcpy(&d3, &ctx.Xmm3, 8);
            log_line("    xmm floats %g %g %g %g, doubles %g %g %g %g, stack args %#llx", f0, f1, f2, f3, d0, d1, d2, d3, 0ull);
        }
        if (strstr(b->label, "_D"))   /* a label ending in _D: the 32 qwords at rcx (the object) */
        {
            ULONG_PTR obj[32];
            SIZE_T n = 0;

            if (ReadProcessMemory(proc, (void *)ctx.Rcx, obj, sizeof(obj), &n))
                for (i = 0; i < (int)(n / 8); i += 4)
                    log_line("    obj+%#02x: %016llx %016llx %016llx %016llx", i * 8, (unsigned long long)obj[i], (unsigned long long)obj[i + 1],
                             (unsigned long long)obj[i + 2], (unsigned long long)obj[i + 3]);
        }
        if (strstr(b->label, "_E"))   /* _E: 48 qwords at rdx and r8 */
        {
            dump_mem("rdx in", ctx.Rdx);
            dump_mem("r8 in", ctx.R8);
        }
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

static void dump_mem(const char *tag, ULONG_PTR p)
{
    ULONG_PTR q[48];
    SIZE_T n = 0;
    int i;

    if (p < 0x10000 || !ReadProcessMemory(proc, (void *)p, q, sizeof(q), &n)) { log_line("    %s %#llx unreadable", tag, (unsigned long long)p); return; }
    for (i = 0; i + 3 < (int)(n / 8); i += 4)
        log_line("    %s+%02x: %016llx %016llx %016llx %016llx", tag, i * 8, (unsigned long long)q[i], (unsigned long long)q[i + 1],
                 (unsigned long long)q[i + 2], (unsigned long long)q[i + 3]);
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
        if (strstr(bps[rets[k].bp].label, "_E"))
        {
            dump_mem("rdx out", rets[k].args[1]);
            dump_mem("r8 out", rets[k].args[2]);
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

/* the module and RVA of an address of this process (the same file is loaded in the application) */
static int rva_of(void *addr, char *mod, size_t modsize, DWORD *rva)
{
    HMODULE hm = NULL;
    char path[MAX_PATH];

    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &hm) || !hm) return 0;
    if (!GetModuleFileNameA(hm, path, sizeof(path))) return 0;
    snprintf(mod, modsize, "%s", base_name(path));
    *rva = (DWORD)((char *)addr - (char *)hm);
    return 1;
}

typedef HRESULT (WINAPI *create3_t)(IUnknown *, REFIID, void **);
typedef HRESULT (WINAPI *ro_init_t)(int);
typedef HRESULT (WINAPI *ro_factory_t)(void *, REFIID, void **);
typedef HRESULT (WINAPI *ws_create_t)(const WCHAR *, UINT32, void **);

/* the RVA of the vtable of the object of the last target resolved, when only calls on such objects are to be counted (0: all) */
static DWORD resolved_vt_rva;

/* turns a target of the command line into module and RVA, as the usage text says; 0 when it cannot be found */
static int resolve_target(const char *target, char *mod, size_t modsize, DWORD *rva)
{
    static const GUID iid_desktop = {0x5f4633fe, 0x1e08, 0x4cb8, {0x8c, 0x75, 0xce, 0x24, 0x33, 0x3f, 0x56, 0x02}};
    char spec[300], *bang, *br;
    int slot = -1;

    snprintf(spec, sizeof(spec), "%s", target);
    if (!(bang = strchr(spec, '!'))) return 0;
    *bang++ = 0;
    if ((br = strchr(bang, '[')))
    {
        slot = atoi(br + 1);
        *br = 0;
    }

    if (!_stricmp(spec, "winrt"))
    {
        HMODULE combase = LoadLibraryA("combase.dll");
        ro_init_t ro_init = combase ? (ro_init_t)GetProcAddress(combase, "RoInitialize") : NULL;
        ro_factory_t ro_factory = combase ? (ro_factory_t)GetProcAddress(combase, "RoGetActivationFactory") : NULL;
        ws_create_t ws_create = combase ? (ws_create_t)GetProcAddress(combase, "WindowsCreateString") : NULL;
        char *at = strchr(bang, '@');
        WCHAR cls[256], iidw[64];
        GUID iid;
        void *hs = NULL, *obj = NULL;

        if (!ro_init || !ro_factory || !ws_create || !at || slot < 0) return 0;
        *at++ = 0;
        MultiByteToWideChar(CP_UTF8, 0, bang, -1, cls, 256);
        MultiByteToWideChar(CP_UTF8, 0, at, -1, iidw, 64);
        if (FAILED(IIDFromString(iidw, &iid))) return 0;
        ro_init(1);   /* RO_INIT_MULTITHREADED */
        if (FAILED(ws_create(cls, (UINT32)wcslen(cls), &hs))) return 0;
        if (FAILED(ro_factory(hs, &iid, &obj)) || !obj) return 0;
        return rva_of((*(void ***)obj)[slot], mod, modsize, rva);
    }

    /* d3d11.dll!device@{IID}[N]: slot N of the interface IID of a hardware device with BGRA support that this program creates */
    if (!_stricmp(spec, "d3d11.dll") && !strncmp(bang, "device@", 7) && slot >= 0)
    {
        typedef HRESULT (WINAPI *create_t)(void *, int, HMODULE, UINT, const void *, UINT, UINT, void **, void *, void **);
        HMODULE d3d = LoadLibraryA("d3d11.dll");
        create_t create = d3d ? (create_t)GetProcAddress(d3d, "D3D11CreateDevice") : NULL;
        WCHAR iidw[64];
        GUID iid;
        void *device = NULL, *obj = NULL;

        MultiByteToWideChar(CP_UTF8, 0, bang + 7, -1, iidw, 64);
        if (!create || FAILED(IIDFromString(iidw, &iid))) return 0;
        /* D3D_DRIVER_TYPE_HARDWARE, D3D11_CREATE_DEVICE_BGRA_SUPPORT, D3D11_SDK_VERSION */
        if (FAILED(create(NULL, 1, NULL, 0x20, NULL, 0, 7, &device, NULL, NULL)) || !device) return 0;
        if (FAILED(((HRESULT (STDMETHODCALLTYPE *)(void *, const GUID *, void **))(*(void ***)device)[0])(device, &iid, &obj)) || !obj)
            return 0;
        {
            /* the slots may share code with other objects of d3d11: count only calls on an object with this vtable */
            char vmod[80];
            DWORD vrva;

            if (rva_of(*(void **)obj, vmod, sizeof(vmod), &vrva) && !_stricmp(vmod, "d3d11.dll")) resolved_vt_rva = vrva;
        }
        if (!rva_of((*(void ***)obj)[slot], mod, modsize, rva)) return 0;
        if (_stricmp(mod, "d3d11.dll")) resolved_vt_rva = 0;   /* the filter compares with the base of the module of the slot */
        return 1;
    }

    /* dwrite.dll!analyzer@{IID}[N]: slot N of the interface IID of a text analyzer of a shared DirectWrite factory that this program
     * creates */
    if (!_stricmp(spec, "dwrite.dll") && !strncmp(bang, "analyzer@", 9) && slot >= 0)
    {
        static const GUID iid_factory = {0xb859ee5a, 0xd838, 0x4b5b, {0xa2, 0xe8, 0x1a, 0xdc, 0x7d, 0x93, 0xdb, 0x48}};
        typedef HRESULT (WINAPI *dw_create_t)(int, const GUID *, void **);
        HMODULE dw = LoadLibraryA("dwrite.dll");
        dw_create_t dw_create = dw ? (dw_create_t)GetProcAddress(dw, "DWriteCreateFactory") : NULL;
        WCHAR iidw[64];
        GUID iid;
        void *factory = NULL, *analyzer = NULL, *obj = NULL;
        char vmod[80];
        DWORD vrva;

        MultiByteToWideChar(CP_UTF8, 0, bang + 9, -1, iidw, 64);
        if (!dw_create || FAILED(IIDFromString(iidw, &iid))) return 0;
        if (FAILED(dw_create(0 /* DWRITE_FACTORY_TYPE_SHARED */, &iid_factory, &factory)) || !factory) return 0;
        /* CreateTextAnalyzer is slot 21 of IDWriteFactory */
        if (FAILED(((HRESULT (STDMETHODCALLTYPE *)(void *, void **))(*(void ***)factory)[21])(factory, &analyzer)) || !analyzer) return 0;
        if (FAILED(((HRESULT (STDMETHODCALLTYPE *)(void *, const GUID *, void **))(*(void ***)analyzer)[0])(analyzer, &iid, &obj)) || !obj)
            return 0;
        if (rva_of(*(void **)obj, vmod, sizeof(vmod), &vrva) && !_stricmp(vmod, "dwrite.dll")) resolved_vt_rva = vrva;
        if (!rva_of((*(void ***)obj)[slot], mod, modsize, rva)) return 0;
        if (_stricmp(mod, "dwrite.dll")) resolved_vt_rva = 0;
        return 1;
    }

    /* d2d1.dll!factory@{IID}[N] / factorymt@{IID}[N]: slot N of the interface IID of a single-threaded / multithreaded Direct2D
     * factory that this program creates */
    if (!_stricmp(spec, "d2d1.dll") && (!strncmp(bang, "factory@", 8) || !strncmp(bang, "factorymt@", 10)) && slot >= 0)
    {
        int mt = !strncmp(bang, "factorymt@", 10);
        static const GUID iid_factory1 = {0xbb12d362, 0xdaee, 0x4b9a, {0xaa, 0x1d, 0x14, 0xba, 0x40, 0x1c, 0xfa, 0x1f}};
        typedef HRESULT (WINAPI *d2d_create_t)(int, const GUID *, const void *, void **);
        HMODULE d2dm = LoadLibraryA("d2d1.dll");
        d2d_create_t d2d_create = d2dm ? (d2d_create_t)GetProcAddress(d2dm, "D2D1CreateFactory") : NULL;
        void *factory = NULL, *obj = NULL;
        WCHAR iidw[64];
        GUID iid;
        char vmod[80];
        DWORD vrva;

        MultiByteToWideChar(CP_UTF8, 0, bang + (mt ? 10 : 8), -1, iidw, 64);
        if (!d2d_create || FAILED(IIDFromString(iidw, &iid))) return 0;
        if (FAILED(d2d_create(mt, &iid_factory1, NULL, &factory)) || !factory) return 0;
        if (FAILED(((HRESULT (STDMETHODCALLTYPE *)(void *, const GUID *, void **))(*(void ***)factory)[0])(factory, &iid, &obj)) || !obj)
            return 0;
        if (rva_of(*(void **)obj, vmod, sizeof(vmod), &vrva) && !_stricmp(vmod, "d2d1.dll")) resolved_vt_rva = vrva;
        if (!rva_of((*(void ***)obj)[slot], mod, modsize, rva)) return 0;
        if (_stricmp(mod, "d2d1.dll")) resolved_vt_rva = 0;
        return 1;
    }

    /* d2d1.dll!context@{IID}[N]: slot N of the interface IID of a Direct2D device context, on a hardware Direct3D 11 device with BGRA
     * support, that this program creates; d2d1.dll!d2ddevice@{IID}[N]: the same for the Direct2D device (of a multithreaded factory) */
    if (!_stricmp(spec, "d2d1.dll") && (!strncmp(bang, "context@", 8) || !strncmp(bang, "d2ddevice@", 10)) && slot >= 0)
    {
        int on_device = !strncmp(bang, "d2ddevice@", 10);
        static const GUID iid_dxgi_device = {0x54ec77fa, 0x1377, 0x44e6, {0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c}};
        static const GUID iid_factory1 = {0xbb12d362, 0xdaee, 0x4b9a, {0xaa, 0x1d, 0x14, 0xba, 0x40, 0x1c, 0xfa, 0x1f}};
        typedef HRESULT (WINAPI *d3d_create_t)(void *, int, HMODULE, UINT, const void *, UINT, UINT, void **, void *, void **);
        typedef HRESULT (WINAPI *d2d_create_t)(int, const GUID *, const void *, void **);
        typedef HRESULT (STDMETHODCALLTYPE *qi_t)(void *, const GUID *, void **);
        HMODULE d3dm = LoadLibraryA("d3d11.dll"), d2dm = LoadLibraryA("d2d1.dll");
        d3d_create_t d3d_create = d3dm ? (d3d_create_t)GetProcAddress(d3dm, "D3D11CreateDevice") : NULL;
        d2d_create_t d2d_create = d2dm ? (d2d_create_t)GetProcAddress(d2dm, "D2D1CreateFactory") : NULL;
        void *d3d = NULL, *dxgi = NULL, *factory = NULL, *device = NULL, *context = NULL, *obj = NULL;
        WCHAR iidw[64];
        GUID iid;
        char vmod[80];
        DWORD vrva;

        MultiByteToWideChar(CP_UTF8, 0, bang + (on_device ? 10 : 8), -1, iidw, 64);
        if (!d3d_create || !d2d_create || FAILED(IIDFromString(iidw, &iid))) return 0;
        if (FAILED(d3d_create(NULL, 1, NULL, 0x20, NULL, 0, 7, &d3d, NULL, NULL)) || !d3d) return 0;
        if (FAILED(((qi_t)(*(void ***)d3d)[0])(d3d, &iid_dxgi_device, &dxgi)) || !dxgi) return 0;
        /* D2D1_FACTORY_TYPE_SINGLE_THREADED for a context, MULTI_THREADED (as the lifted compositor uses) for a device */
        if (FAILED(d2d_create(on_device, &iid_factory1, NULL, &factory)) || !factory) return 0;
        /* ID2D1Factory1::CreateDevice is slot 17, ID2D1Device::CreateDeviceContext slot 4 */
        if (FAILED(((HRESULT (STDMETHODCALLTYPE *)(void *, void *, void **))(*(void ***)factory)[17])(factory, dxgi, &device)) || !device)
            return 0;
        if (on_device) context = device;
        else if (FAILED(((HRESULT (STDMETHODCALLTYPE *)(void *, int, void **))(*(void ***)device)[4])(device, 0, &context)) || !context)
            return 0;
        if (FAILED(((qi_t)(*(void ***)context)[0])(context, &iid, &obj)) || !obj) return 0;
        if (rva_of(*(void **)obj, vmod, sizeof(vmod), &vrva) && !_stricmp(vmod, "d2d1.dll")) resolved_vt_rva = vrva;
        if (!rva_of((*(void ***)obj)[slot], mod, modsize, rva)) return 0;
        if (_stricmp(mod, "d2d1.dll")) resolved_vt_rva = 0;
        return 1;
    }

    if (!_stricmp(spec, "dcomp.dll") && (!strcmp(bang, "desktop") || !strcmp(bang, "visual")) && slot >= 0)
    {
        HMODULE dc = LoadLibraryA("dcomp.dll");
        create3_t create3 = dc ? (create3_t)GetProcAddress(dc, "DCompositionCreateDevice3") : NULL;
        void *desktop = NULL, *visual = NULL;

        if (!create3 || FAILED(create3(NULL, &iid_desktop, &desktop)) || !desktop) return 0;
        if (!strcmp(bang, "desktop")) return rva_of((*(void ***)desktop)[slot], mod, modsize, rva);
        /* CreateVisual is slot 6 of IDCompositionDesktopDevice */
        if (FAILED(((HRESULT (STDMETHODCALLTYPE *)(void *, void **))(*(void ***)desktop)[6])(desktop, &visual)) || !visual) return 0;
        return rva_of((*(void ***)visual)[slot], mod, modsize, rva);
    }

    {
        HMODULE hm = LoadLibraryA(spec);
        FARPROC p;

        if (!hm) return 0;
        p = bang[0] == '#' ? GetProcAddress(hm, MAKEINTRESOURCEA(atoi(bang + 1))) : GetProcAddress(hm, bang);
        if (!p) return 0;
        return rva_of((void *)p, mod, modsize, rva);
    }
}

int main(int argc, char **argv)
{
    const char *watch = getenv("WINTRACE_WATCH");   /* image name of processes to attach to as they appear, e.g. Notepad.exe */
    IApplicationActivationManager *mgr = NULL;
    WCHAR aumid[256];
    DWORD pid = 0, limit, i;
    HRESULT hr;
    DEBUG_EVENT ev;

    if (argc < 5) { fprintf(stderr, "usage: wintrace AUMID SECONDS OUTFILE TARGET:LABEL[:MAX[:R]]... (targets: see the header of wintrace.c)\n"); return 2; }
    out = fopen(argv[3], "w");
    if (!out) return 2;
    limit = (DWORD)atoi(argv[2]) * 1000;
    for (i = 4; i < (DWORD)argc && nbps < 128; i++)
    {
        char *a = strtok(argv[i], ":"), *l = strtok(NULL, ":"), *m = strtok(NULL, ":"), *w = strtok(NULL, ":");

        if (!a || !l) continue;
        bps[nbps].wantret = w && *w == 'R';
        resolved_vt_rva = 0;
        if (!resolve_target(a, bps[nbps].mod, sizeof(bps[nbps].mod), &bps[nbps].rva))
        {
            fprintf(stderr, "target %s: not an entry point that can be found at run time (a raw RVA is not accepted)\n", a);
            fprintf(out, "target %s: not found\n", a);
            return 2;
        }
        bps[nbps].vt_rva = resolved_vt_rva;
        fprintf(out, "target %s -> %s+%#lx%s\n", a, bps[nbps].mod, (unsigned long)bps[nbps].rva,
                resolved_vt_rva ? " (calls on objects of this vtable only)" : "");
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
        event_tid = ev.dwThreadId;
        switch (ev.dwDebugEventCode)
        {
        case CREATE_THREAD_DEBUG_EVENT:
            log_line("thread %lu created", (unsigned long)ev.dwThreadId);
            break;
        case EXIT_THREAD_DEBUG_EVENT:
            log_line("thread %lu exited, code %lu", (unsigned long)ev.dwThreadId, (unsigned long)ev.u.ExitThread.dwExitCode);
            break;
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
            if (!found && code == EXCEPTION_BREAKPOINT && nmods > 40)
                log_line("breakpoint exception at %p tid %lu not one of ours", (void *)a, (unsigned long)ev.dwThreadId);
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
