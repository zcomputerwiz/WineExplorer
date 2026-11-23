# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: generates exception-tramp.c from the clean dlls/ntdll/exception.c of a candidate: ud2 trampolines (log the counters of an object and the
# caller, restore the bytes, single-step, patch again) at the entries and inline sites in sites.txt (see sites.py) of the INSTALLED lab copy of
# Microsoft.UI.Input.dll (patch that copy with mkpatch3.py); at a failing Begin, and at the window procedure of Microsoft.UI.Windowing.Core
# (RVA 0x33e70) for WM_NCHITTEST, it also logs the stack as LABSTK lines (mapstk.py). Run in a directory with exception-clean.c and sites.txt.
src = open('exception-clean.c', newline='').read()
anchor = '''NTSTATUS WINAPI dispatch_exception( EXCEPTION_RECORD *rec, CONTEXT *context )
{
    NTSTATUS status;
    DWORD i;
'''
assert anchor in src, 'anchor'
sites = [l.split() for l in open('sites.txt').read().split('\n') if l.strip()]
generated = ''.join('            { 0x%s, { 0x89, 0x%s }, "I%s", %d },\n' % (r, '43' if reg == 'rbx' else '47', r, 2 if reg == 'rbx' else 1) for r, reg in sites)
open('rvas.txt', 'w').write(' '.join(r for r, reg in sites))
block = r'''
#ifdef __x86_64__
    /* LAB: ud2 trampolines over function entries of Microsoft.UI.Input.dll (installed lab copy): log, restore the bytes, single-step, patch again */
    {
        /* reg: 0 the object is in rcx, 1 in rdi, 2 in rbx (the inlined begins) */
        static const struct { ULONG_PTR rva; BYTE orig[2]; const char *name; int reg; } labfn[] =
        {
            { 0x2b020, { 0x40, 0x53 }, "Begin", 0 }, { 0x2b080, { 0x40, 0x53 }, "End80", 0 },
            { 0x2b210, { 0x48, 0x83 }, "EndA", 0 },  { 0x2b2b0, { 0x40, 0x53 }, "EndB", 0 },
            { 0x2a44a, { 0x89, 0x47 }, "I2a44a", 1 }, { 0x2aa87, { 0x89, 0x47 }, "I2aa87", 1 },
            { 0x2ab3f, { 0x89, 0x47 }, "I2ab3f", 1 }, { 0x2acc5, { 0x89, 0x43 }, "I2acc5", 2 },
            { 0x2b68d, { 0x89, 0x47 }, "I2b68d", 1 }, { 0x2b04e, { 0x89, 0x43 }, "I2b04e", 2 },
            { 0x2b08e, { 0x44, 0x8b }, "AfterDec", 0 },
            { 0x33e70, { 0x48, 0x89 }, "WndProc", 0 },
@GENERATED@
        };
        static ULONG_PTR lab_pending[64];
        unsigned int tid = HandleToULong( NtCurrentTeb()->ClientId.UniqueThread ) & 63;

        if (rec->ExceptionCode == 0xc000001d)
        {
            void *base = NULL;
            ULONG_PTR rip = context->Rip;
            unsigned int k;

            if (RtlPcToFileHeader( (void *)rip, &base ) && base)
                for (k = 0; k < ARRAY_SIZE(labfn); k++)
                {
                    if (rip - (ULONG_PTR)base != labfn[k].rva) continue;
                    {
                        const BYTE *obj = (const BYTE *)(labfn[k].reg == 0 ? context->Rcx : labfn[k].reg == 1 ? context->Rdi : context->Rbx);
                        const ULONG_PTR *sp = (const ULONG_PTR *)context->Rsp;
                        void *cbase = NULL;
                        void *addr = (void *)rip;
                        SIZE_T size = 2;
                        ULONG old;
                        ULONG_PTR ret = 0;
                        int v40 = -1, v44 = -1, v48 = -1, v58 = -1, v5c = -1;

                        __TRY
                        {
                            v40 = *(const int *)(obj + 0x40); v44 = *(const int *)(obj + 0x44); v48 = *(const int *)(obj + 0x48);
                            v58 = *(const int *)(obj + 0x58); v5c = *(const int *)(obj + 0x5c);
                            ret = sp[0];
                        }
                        __EXCEPT_PAGE_FAULT
                        {
                        }
                        __ENDTRY
                        RtlPcToFileHeader( (void *)ret, &cbase );
                        {
                            const BYTE *obj2 = (const BYTE *)context->Rdx;
                            int w40 = -1;

                            __TRY { w40 = *(const int *)(obj2 + 0x40); }
                            __EXCEPT_PAGE_FAULT { }
                            __ENDTRY
                            ERR( "LABTRAMP %s tid %04lx obj %p +40=%d +44=%d +48=%d +58=%#x +5c=%#x rdx %p rdx+40=%d caller %p (module %p +%lx)\n",
                                 labfn[k].name, (unsigned long)HandleToULong( NtCurrentTeb()->ClientId.UniqueThread ), obj, v40, v44, v48,
                                 v58, v5c, obj2, w40, (void *)ret, cbase, cbase ? (unsigned long)(ret - (ULONG_PTR)cbase) : 0 );
                        }
                        if ((k == 0 && v40 != v44 + v48) || (labfn[k].rva == 0x33e70 && context->Rdx == 0x84))
                        {
                            unsigned int n;
                            for (n = 0; n < 600; n++)
                            {
                                void *mb = NULL;
                                if (sp[n] > 0x10000 && RtlPcToFileHeader( (void *)sp[n], &mb ) && mb)
                                    ERR( "LABSTK %u module %p +%lx\n", n, mb, (unsigned long)(sp[n] - (ULONG_PTR)mb) );
                            }
                        }
                        if (NtProtectVirtualMemory( NtCurrentProcess(), &addr, &size, PAGE_EXECUTE_READWRITE, &old )) break;
                        memcpy( (void *)rip, labfn[k].orig, 2 );
                        addr = (void *)rip; size = 2;
                        NtProtectVirtualMemory( NtCurrentProcess(), &addr, &size, old, &old );
                        NtFlushInstructionCache( NtCurrentProcess(), (void *)rip, 2 );
                        lab_pending[tid] = rip;
                        context->EFlags |= 0x100;
                        return NtContinue( context, FALSE );
                    }
                }
        }
        else if (rec->ExceptionCode == EXCEPTION_SINGLE_STEP && lab_pending[tid])
        {
            void *addr = (void *)lab_pending[tid];
            SIZE_T size = 2;
            ULONG old;

            if (!NtProtectVirtualMemory( NtCurrentProcess(), &addr, &size, PAGE_EXECUTE_READWRITE, &old ))
            {
                ((BYTE *)lab_pending[tid])[0] = 0x0f;
                ((BYTE *)lab_pending[tid])[1] = 0x0b;
                addr = (void *)lab_pending[tid]; size = 2;
                NtProtectVirtualMemory( NtCurrentProcess(), &addr, &size, old, &old );
                NtFlushInstructionCache( NtCurrentProcess(), (void *)lab_pending[tid], 2 );
            }
            lab_pending[tid] = 0;
            context->EFlags &= ~0x100;
            return NtContinue( context, FALSE );
        }
    }
#endif
'''
block = block.replace('@GENERATED@\n', generated)
open('exception-tramp.c', 'w', newline='').write(src.replace(anchor, anchor + block, 1))
print('ok')
