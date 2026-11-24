#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# vm-prune.sh [--apply] [--keep-batches N] [--keep PREFIX]... [--recent-hours H] [--dir DIR] [--iso-dir DIR]
#
# Retention for the install chain's test machines (docs/LABS.md, "Retention"). As root in a build distribution
# (wexbuild). Without --apply it only prints what it would delete (a dry run); with --apply it deletes and then runs
# fstrim so the distribution's ext4.vhdx can give the space back (docs/LABS.md, "Giving the space back to D:").
#
# A chain run is a directory of DIR named b<4 digits><suffix>-... (b1131a-base-bios, b1130c-a-defaults-uefi). Its batch
# is the four digits. Of a run that is pruned, only the disk images (*.qcow2, *.img, *.raw) are deleted: screenshots,
# journals, serial transcripts and logs stay, and a file PRUNED records when. The run's ISO in --iso-dir is deleted too.
#
# Kept:
#   - the runs of the newest N batches (--keep-batches, default 2: the last pushed and the one running);
#   - runs whose name starts with a --keep PREFIX or with a line of DIR/.keep (a failure under investigation:
#     echo b1130c-b >> DIR/.keep);
#   - runs with a file changed in the last H hours (--recent-hours, default 2: a rerun that is going or was just looked at);
#   - runs a running qemu uses, and every disk a kept disk has as its backing file;
#   - anything that is not a chain run (lab machines are listed in docs/LABS.md and never pruned by this script).
# Defaults: DIR=${VMTEST_DIR:-/var/tmp/vmtest-gpt}, --iso-dir /mnt/d/WSL/wex-iso.
set -u
APPLY=0 KEEPN=2 RECENT=2 DIR=${VMTEST_DIR:-/var/tmp/vmtest-gpt} ISODIR=/mnt/d/WSL/wex-iso
KEEP=()
while [ $# -gt 0 ]; do
    case "$1" in
        --apply) APPLY=1 ;;
        --keep-batches) KEEPN=${2:?}; shift ;;
        --keep) KEEP+=("${2:?}"); shift ;;
        --recent-hours) RECENT=${2:?}; shift ;;
        --dir) DIR=${2:?}; shift ;;
        --iso-dir) ISODIR=${2:?}; shift ;;
        *) echo "usage: vm-prune.sh [--apply] [--keep-batches N] [--keep PREFIX]... [--recent-hours H] [--dir DIR] [--iso-dir DIR]" >&2; exit 2 ;;
    esac
    shift
done
[ -d "$DIR" ] || { echo "no directory $DIR" >&2; exit 2; }
case "$KEEPN$RECENT" in ''|*[!0-9]*) echo "--keep-batches and --recent-hours take a number" >&2; exit 2 ;; esac
if [ -f "$DIR/.keep" ]; then
    while IFS= read -r l; do l=${l%%#*}; l=${l//[[:space:]]/}; [ -n "$l" ] && KEEP+=("$l"); done < "$DIR/.keep"
fi

gb() { awk -v b="$1" 'BEGIN {printf "%.1f", b / 1073741824}'; }
batch_of() { [[ "$1" =~ ^b([0-9]{4})[a-z0-9]*(-|$) ]] && echo "${BASH_REMATCH[1]}"; }

# the newest KEEPN batches present
mapfile -t BATCHES < <(for d in "$DIR"/*/; do batch_of "$(basename "$d")"; done | sort -un)
NB=${#BATCHES[@]}
KEEPB=" "
for ((i = NB - KEEPN; i < NB; i++)); do [ "$i" -ge 0 ] && KEEPB+="${BATCHES[$i]} "; done

# what running qemus use (their command lines name the disk or its directory, and the ISO)
QEMU=$(pgrep -a qemu 2>/dev/null || true)

declare -A WHY
for d in "$DIR"/*/; do
    n=$(basename "$d")
    b=$(batch_of "$n")
    if [ -z "$b" ]; then WHY[$n]="not a chain run"; continue; fi
    [[ "$KEEPB" == *" $b "* ]] && { WHY[$n]="batch $b is one of the newest $KEEPN"; continue; }
    for k in "${KEEP[@]+"${KEEP[@]}"}"; do [[ "$n" == "$k"* ]] && { WHY[$n]="kept: $k"; break; }; done
    [ -n "${WHY[$n]:-}" ] && continue
    [ -n "$(find "$d" -mmin -$((RECENT * 60)) -print -quit 2>/dev/null)" ] && { WHY[$n]="changed in the last $RECENT hours"; continue; }
    grep -qF "$DIR/$n/" <<< "$QEMU" && { WHY[$n]="a running qemu uses it"; continue; }
    WHY[$n]=PRUNE
done

# a disk that a kept disk is built on is kept, with its run (repeat until nothing changes: chains are short)
changed=1
while [ $changed = 1 ]; do
    changed=0
    for n in "${!WHY[@]}"; do
        [ "${WHY[$n]}" = PRUNE ] && continue
        while IFS= read -r q; do
            bf=$(qemu-img info -U "$q" 2>/dev/null | sed -n 's/^backing file: \([^ ]*\).*/\1/p')
            [ -n "$bf" ] || continue
            case "$bf" in /*) ;; *) bf="$(dirname "$q")/$bf" ;; esac
            [ -e "$bf" ] || { [ $changed = 0 ] && echo "WARN: $q is an overlay on $bf, which is gone: it cannot boot" >&2; continue; }
            bn=$(basename "$(dirname "$bf")")
            if [ "$(dirname "$(dirname "$bf")")" = "${DIR%/}" ] && [ "${WHY[$bn]:-}" = PRUNE ]; then
                WHY[$bn]="the base of kept $n"; changed=1
            fi
        done < <(find "$DIR/$n" -maxdepth 2 -name '*.qcow2' 2>/dev/null)
    done
done

total=0 kept=0 runs=0
echo "== chain runs in $DIR (keeping batches:$KEEPB)"
for n in $(printf '%s\n' "${!WHY[@]}" | sort); do
    sz=$(find "$DIR/$n" -maxdepth 2 \( -name '*.qcow2' -o -name '*.img' -o -name '*.raw' \) -printf '%s\n' 2>/dev/null |
         awk '{s += $1} END {print s + 0}')
    if [ "${WHY[$n]}" = PRUNE ]; then
        [ "$sz" -gt 0 ] || continue
        runs=$((runs + 1)); total=$((total + sz))
        printf 'PRUNE %7sG %s\n' "$(gb $sz)" "$n"
    else
        kept=$((kept + sz))
        printf 'keep  %7sG %s (%s)\n' "$(gb $sz)" "$n" "${WHY[$n]}"
    fi
done

isototal=0 isos=()
if [ -d "$ISODIR" ]; then
    echo "== ISOs in $ISODIR"
    for f in "$ISODIR"/b*.iso; do
        [ -e "$f" ] || continue
        n=$(basename "$f" .iso); b=$(batch_of "$n")
        why=""
        [ -z "$b" ] && why="not a batch ISO"
        [ -z "$why" ] && [[ "$KEEPB" == *" $b "* ]] && why="batch $b is one of the newest $KEEPN"
        if [ -z "$why" ]; then for k in "${KEEP[@]+"${KEEP[@]}"}"; do [[ "$n" == "$k"* || "$k" == "$n"* ]] && why="kept: $k"; done; fi
        [ -z "$why" ] && grep -qF "$f" <<< "$QEMU" && why="a running qemu uses it"
        [ -z "$why" ] && [ -n "$(find "$f" -mmin -$((RECENT * 60)) 2>/dev/null)" ] && why="changed in the last $RECENT hours"
        sz=$(stat -c %s "$f")
        if [ -z "$why" ]; then
            isos+=("$f"); isototal=$((isototal + sz))
            printf 'PRUNE %7sG %s\n' "$(gb $sz)" "$(basename "$f")"
        else
            printf 'keep  %7sG %s (%s)\n' "$(gb $sz)" "$(basename "$f")" "$why"
        fi
    done
fi

printf '== %d runs to prune: %sG of disk images; %d ISOs: %sG; kept disk images %sG\n' "$runs" \
    "$(gb $total)" "${#isos[@]}" "$(gb $isototal)" \
    "$(gb $kept)"
if [ $APPLY = 0 ]; then echo "DRY RUN: nothing deleted (--apply deletes)"; exit 0; fi

for n in "${!WHY[@]}"; do
    [ "${WHY[$n]}" = PRUNE ] || continue
    find "$DIR/$n" -maxdepth 2 \( -name '*.qcow2' -o -name '*.img' -o -name '*.raw' \) -delete
    echo "disk images deleted $(date -Is) by vm-prune.sh; logs and screenshots kept" > "$DIR/$n/PRUNED"
done
for f in "${isos[@]+"${isos[@]}"}"; do rm -f "$f"; done
echo "deleted; fstrim:"
fstrim -v / 2>&1 || echo "fstrim failed (the space stays inside ext4.vhdx until it is compacted)"
