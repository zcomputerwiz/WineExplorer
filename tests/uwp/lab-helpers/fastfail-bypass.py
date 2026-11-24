# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# LAB-ONLY BYPASS, never committed to Wine: patches a candidate's dlls/ntdll/unix/thread.c so that, when the environment variable
# WINE_LAB_FASTFAIL_THREAD is set, an unhandled fail-fast (c0000409) ends only the thread that raised it instead of the process.
# Used to see what Notepad draws while its input thread still fails fast (the InputSite gap). notepad-bypass-run.sh applies it,
# runs Notepad and restores the file; never leave it in a build.
#   python3 fastfail-bypass.py PATH/TO/dlls/ntdll/unix/thread.c
import sys

p = sys.argv[1]
s = open(p, encoding='utf-8').read()
if 'LAB-BYPASS fastfail' in s:
    print('already'); sys.exit(0)
old = '    NtTerminateProcess( NtCurrentProcess(), rec->ExceptionCode );\n    return STATUS_SUCCESS;\n}\n'
assert s.count(old) == 1, 'anchor'
new = ('    if (rec->ExceptionCode == 0xc0000409 && getenv( "WINE_LAB_FASTFAIL_THREAD" )) /* LAB-BYPASS fastfail */\n'
       '    {\n        ERR_(seh)("LAB-BYPASS: ending only the thread of the fail-fast\\n");\n'
       '        NtTerminateThread( NtCurrentThread(), rec->ExceptionCode );\n    }\n') + old
s = s.replace(old, new)
open(p, 'w', encoding='utf-8').write(s)
print('patched')
