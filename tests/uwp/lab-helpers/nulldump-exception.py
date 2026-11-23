# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: temporary patch of dlls/ntdll/exception.c (dispatch_exception) that prints the stack words pointing into modules at a null call
p = 'C:/Users/Owner/AppData/Local/Temp/claude/f10dd599a76cdd8a/cc6dd05f-7e73-49f2-be6b-aa4119777252/scratchpad/exception.c'
s = open(p, newline='', encoding='utf-8').read()
a = '''            for (i = 0; i < 96; i++) TRACE( "LAB stack[%03lu] %016lx\\n", (unsigned long)i, sp[i] );'''
assert a in s
b = '''            for (i = 0; i < 256; i++)
            {
                void *base = NULL;
                if (sp[i] > 0x10000 && RtlPcToFileHeader( (void *)sp[i], &base ) && base)
                    TRACE( "LAB code[%03lu] %p+%lx\\n", (unsigned long)i, base, (unsigned long)(sp[i] - (ULONG_PTR)base) );
            }'''
s = s.replace(a, b)
open(p, 'w', newline='', encoding='utf-8').write(s)
print('ok')
