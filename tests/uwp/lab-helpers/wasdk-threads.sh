#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: where do the threads of the Windows App SDK Notepad sit after 25 s? The real process is the one with the most threads; gdb gives rip
# and the top of the stack of every thread, /proc/PID/maps resolves the addresses to module and offset (the PE files are mapped).
# usage: CAND=u13 wasdk-threads.sh   output: wasdk-run/peek4.txt (+ .maps, .regs)
# Limit: the native DLLs of the package are mapped from a Wine memfd, so their frames show as memfd:wine-mapping; PE files of Wine itself resolve.
# the workspace of the lab guest (override with UWP_WS)
UWP_WS=${UWP_WS:-/home/debian/uwp-u1-20261111}
root=${UWP_WS}
build=$root/build/candidates/${CAND:-u13}/build
run=$root/build/candidates/${CAND:-u13}/wasdk-run
cd "$run"
export WINEPREFIX="$run/prefix" WINESERVER="$build/server/wineserver" DISPLAY=${DISPLAY:-:99} WINEDEBUG=-all
"$build/server/wineserver" -k 2>/dev/null; sleep 1
( timeout --kill-after=10s 90s "$build/wine" $root/claude/applaunch.exe 'Microsoft.WindowsNotepad_8wekyb3d8bbwe!App' > peek4.log 2>&1 & )
sleep 25
best=0; pid=
for p in $(pgrep -f 'Notepad.exe'); do
  [ -d /proc/$p/task ] || continue
  n=$(ls /proc/$p/task | wc -l)
  [ "$n" -gt "$best" ] && { best=$n; pid=$p; }
done
echo "pid $pid threads $best" > peek4.txt
rm -f peek4.maps; cat /proc/$pid/maps > peek4.maps
for t in /proc/$pid/task/*; do echo "$(basename $t) $(cat $t/comm) wchan=$(cat $t/wchan 2>/dev/null) cpu=$(awk '{print $14+$15}' $t/stat)"; done >> peek4.txt
cat > peek4.gdb <<'EOF'
set pagination off
handle SIGUSR1 nostop noprint pass
handle SIGUSR2 nostop noprint pass
handle SIGALRM nostop noprint pass
handle SIGSEGV nostop noprint pass
handle SIGCHLD nostop noprint pass
thread apply all -s info registers rip rsp
thread apply all -s x/400gx $rsp
EOF
timeout 60 gdb -batch -nx -p $pid -x peek4.gdb > peek4.regs 2>&1
python3 - <<'PY'
import re
maps = []
for l in open('peek4.maps'):
    m = re.match(r'([0-9a-f]+)-([0-9a-f]+) (\S+) ([0-9a-f]+) \S+ \S+\s*(.*)', l)
    if m: maps.append((int(m.group(1), 16), int(m.group(2), 16), m.group(5).strip(), int(m.group(4), 16)))
base = {}
for s, e, f, off in maps:
    if f and f not in base: base[f] = s
def where(a):
    for s, e, f, off in maps:
        if s <= a < e:
            return '%s+%x' % (f.split('/')[-1] if f else 'anon', (a - base[f]) if f in base else a - s)
    return None
cur = None
stacks = {}
order = []
for l in open('peek4.regs', errors='replace'):
    m = re.match(r'Thread (\d+) .*LWP (\d+)', l)
    if m:
        cur = m.group(2)
        if cur not in stacks: stacks[cur] = []; order.append(cur)
        continue
    m = re.match(r'0x[0-9a-f]+[^:]*:\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)', l)
    if m and cur:
        for g in m.groups():
            w = where(int(g, 16))
            if w and not w.startswith(('libc', 'anon', 'ld-')) and '.so' not in w:
                stacks[cur].append(w)
for t in order:
    print('thread', t, ' <- '.join(stacks[t][:14]))
PY
echo ---
cat peek4.txt
"$build/server/wineserver" -k
