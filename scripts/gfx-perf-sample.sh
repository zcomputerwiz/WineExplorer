#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# CPU sampler for the graphics path, run beside scripts/gfx-perf-probe.c in the guest:
#   gfx-perf-sample.sh SECONDS OUTFILE
# Once a second it writes "t=<s> total=<busy%> name=<cpu% of one core> ..." for the busiest processes
# (compositor, Xorg, Wine and its shell included), read from /proc. Not GPU load.
SECS=${1:-30}
OUT=${2:-/tmp/gfx-perf-sample.log}
HZ=$(getconf CLK_TCK)
: > "$OUT"
snap() {   # pid name ticks
    for d in /proc/[0-9]*; do
        [ -r "$d/stat" ] || continue
        read -r line < "$d/stat" 2>/dev/null || continue
        rest=${line##*) }
        set -- $rest
        # fields after the command: state is $1, utime $12, stime $13
        echo "${d#/proc/} $(cat "$d/comm" 2>/dev/null | tr ' ' _) $(( ${12:-0} + ${13:-0} ))"
    done
}
snap > /tmp/gfxs.a
t0=$(date +%s)
i=0
while [ "$i" -lt "$SECS" ]; do
    sleep 1
    snap > /tmp/gfxs.b
    # join the two snapshots on pid
    awk -v hz="$HZ" -v t=$(( $(date +%s) - t0 )) '
        NR == FNR { a[$1] = $3; next }
        ($1 in a) { d = $3 - a[$1]; if (d > 0) { cpu[$2] += d; total += d } }
        END {
            line = sprintf("t=%d total=%.0f", t, 100 * total / hz)
            for (n in cpu) if (100 * cpu[n] / hz >= 1) line = line sprintf(" %s=%.0f", n, 100 * cpu[n] / hz)
            print line
        }' /tmp/gfxs.a /tmp/gfxs.b >> "$OUT"
    mv /tmp/gfxs.b /tmp/gfxs.a
    i=$((i + 1))
done
rm -f /tmp/gfxs.a /tmp/gfxs.b
echo SAMPLEDONE >> "$OUT"
