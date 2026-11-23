# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: adds C++ throw stacks (LABCXX lines, see cxxbefore.py) to exception-tramp.c made by ntdll-tramp-patch.py
src = open('exception-tramp.c', newline='').read()
anchor = '''        else if (rec->ExceptionCode == EXCEPTION_SINGLE_STEP && lab_pending[tid])'''
assert anchor in src
block = r'''        else if (rec->ExceptionCode == 0xe06d7363)
        {
            const ULONG_PTR *sp = (const ULONG_PTR *)context->Rsp;
            unsigned int n, shown = 0;

            ERR( "LABCXX throw tid %04lx\n", (unsigned long)HandleToULong( NtCurrentTeb()->ClientId.UniqueThread ) );
            __TRY
            {
                for (n = 0; n < 300 && shown < 24; n++)
                {
                    void *mb = NULL;
                    if (sp[n] > 0x10000 && RtlPcToFileHeader( (void *)sp[n], &mb ) && mb)
                    {
                        ERR( "LABCXX %u module %p +%lx\n", n, mb, (unsigned long)(sp[n] - (ULONG_PTR)mb) );
                        shown++;
                    }
                }
            }
            __EXCEPT_PAGE_FAULT
            {
            }
            __ENDTRY
        }
'''
open('exception-tramp.c', 'w', newline='').write(src.replace(anchor, block + anchor, 1))
print('ok')
