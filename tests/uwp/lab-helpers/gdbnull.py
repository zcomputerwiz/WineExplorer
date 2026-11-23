# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
import gdb

gdb.execute('handle SIGUSR1 nostop noprint pass')
gdb.execute('handle SIGUSR2 nostop noprint pass')
gdb.execute('handle SIGALRM nostop noprint pass')
gdb.execute('handle SIGCHLD nostop noprint pass')
gdb.execute('handle SIGSEGV stop noprint pass')
gdb.execute('handle SIGILL stop noprint pass')

n = 0
while True:
    try:
        gdb.execute('continue', to_string=True)
    except gdb.error as e:
        print('continue failed: %s' % e)
        break
    n += 1
    try:
        addr = int(gdb.parse_and_eval('$_siginfo._sifields._sigfault.si_addr'))
        sig = int(gdb.parse_and_eval('$_siginfo.si_signo'))
        pc = int(gdb.parse_and_eval('$pc'))
    except gdb.error:
        break
    if sig == 11 and addr == 0 and pc == 0:
        print('NULLCALL after %d stops' % n)
        gdb.execute('info threads')
        gdb.execute('info registers rip rsp')
        gdb.execute('x/64gx $rsp')
        break
    if n > 5000:
        break
try:
    gdb.execute('detach')
except gdb.error:
    pass
