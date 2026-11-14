#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# The Safe Mode test chain on an installed image, with the stages and both firmwares run side by side.
# Run as root in the VM test distro (WSL Debian; docs/TOOLING.md), from any checkout: the checkout's scripts/ and
# installer/ are copied first, so edits made while it runs do not reach it (bash reads a script as it goes).
#
#   run-chain.sh [-j LANES] [--fws "bios uefi"] [--iso FILE.iso] NAME [STAGE...]
#
#   NAME      the installed test disks are NAME-bios and NAME-uefi (VMTEST_DIR, default /var/tmp/vmtest-gpt)
#   --iso     install them first from FILE.iso (built with --unattended or not: the test's unattend.sif is put in),
#             both firmwares at once, through Vintner Boot (install-unattended.sh, check-vintner.py)
#   STAGE     defaults accounts classic rundialog wayland startrec (all when none is named), or NAME=PHASE,PHASE
#             for other phases of safe-mode.sh. Each stage on each firmware is one job on its own overlay
#             (NAME-STAGE-FW), so jobs do not share a disk.
#   -j        jobs at once (default 3). Each VM has 4 GB and 2 CPUs; WSL's memory (15 GB on the test machine) is the
#             limit, not the CPUs.
#
# Environment passed on to safe-mode.sh: WEX_TEST_PROBES, WEX_TEST_WINE, WEX_SAFEMODE_FILES (default installed).
# Results: /var/tmp/wex-chain/NAME/ (one log per job, the failure screenshots, summary.txt); the exit status is 0
# when every job passed.
set -u
usage() { sed -n '7,22p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }

LANES=3
FWS="bios uefi"
ISO=
while [ $# -gt 0 ]; do
    case "$1" in
        -j) LANES=${2:?}; shift 2 ;;
        --fws) FWS=${2:?}; shift 2 ;;
        --iso) ISO=${2:?}; shift 2 ;;
        -h|--help) usage ;;
        -*) echo "unknown option $1"; usage ;;
        *) break ;;
    esac
done
NAME=${1:-}
[ -n "$NAME" ] || usage
[[ "$NAME" =~ ^[a-zA-Z0-9_-]+$ ]] || { echo "invalid NAME"; exit 2; }
shift
[ "$(id -u)" = 0 ] || { echo "run as root"; exit 2; }
[[ "$LANES" =~ ^[1-9][0-9]*$ ]] || { echo "invalid -j"; exit 2; }

phases_of() {   # the safe-mode.sh phases of a stage; nothing for the defaults
    case "$1" in
        defaults) ;;
        accounts|classic|rundialog|startrec) echo "$1" ;;
        wayland) echo waylanddisplay waylandsafe waylandcmd ;;
        *=*) echo "${1#*=}" | tr ',' ' ' ;;
        *) return 1 ;;
    esac
}
stage_name() { echo "${1%%=*}"; }

STAGES=("$@")
# the longest first, so the short ones fill the lanes around it
[ ${#STAGES[@]} -gt 0 ] || STAGES=(defaults startrec wayland classic accounts rundialog)
for s in "${STAGES[@]}"; do
    phases_of "$s" >/dev/null || { echo "unknown stage $s"; exit 2; }
done

export VMTEST_DIR=${VMTEST_DIR:-/var/tmp/vmtest-gpt}
export WEX_SAFEMODE_FILES=${WEX_SAFEMODE_FILES:-installed}
HERE=$(cd "$(dirname "$0")" && pwd)
CHECKOUT=$(cd "$HERE/../.." && pwd)
OUT=/var/tmp/wex-chain/$NAME
SRC=$OUT/src
if [ "${WEX_CHAIN_COPY:-}" != "$SRC" ]; then
    # Copy the checkout's tests, then run this script from the copy: bash reads a script as it runs, so an edit of
    # the checkout's run-chain.sh during a run broke it at its end (2026-11-14, the last jobs lost with it).
    mkdir -p "$OUT" "$VMTEST_DIR"
    rm -f "$OUT"/*.log "$OUT"/*.png "$OUT"/*.rc "$OUT/summary.txt"
    rm -rf "$SRC" && mkdir -p "$SRC"
    tar -C "$CHECKOUT" --exclude='*.vdi' --exclude='*.iso' --exclude=installer/live-build/config/packages.chroot \
        --exclude=installer/test-evidence --exclude=__pycache__ -cf - scripts installer | tar -C "$SRC" -xf - || exit 1
    [ -f "$SRC/installer/tests/safe-mode.sh" ] || { echo "no copy of the tests"; exit 1; }
    ARGS=(-j "$LANES" --fws "$FWS")
    [ -n "$ISO" ] && ARGS+=(--iso "$ISO")
    WEX_CHAIN_COPY=$SRC exec bash "$SRC/installer/tests/run-chain.sh" "${ARGS[@]}" "$NAME" "${STAGES[@]}"
fi

total=$(awk '/^MemTotal:/ { print int($2 / 1048576) }' /proc/meminfo)
if [ $((LANES * 4 + 2)) -gt "$total" ]; then
    echo "warning: $LANES VMs of 4 GB at once on ${total} GB; consider -j $(( (total - 2) / 4 ))"
fi

log() { echo "$(date +%T) $*" | tee -a "$OUT/summary.txt"; }
overall=0
start_all=$(date +%s)

if [ -n "$ISO" ]; then   # the installs, both firmwares at once
    [ -f "$ISO" ] || { echo "no $ISO"; exit 2; }
    W=$OUT/iso
    mkdir -p "$W"
    sed 's|^; TargetDisk = /dev/sda .*$|TargetDisk = /dev/vda|' "$SRC/installer/setupinfs/unattend.sif.example" \
        > "$W/unattend-test.sif"
    rm -f "$W/unattended.iso"
    python3 "$SRC/installer/tests/prepare-unattended-iso.py" "$ISO" "$W/unattend-test.sif" "$W/unattended.iso" \
        > "$OUT/prepare-iso.log" 2>&1 || { log "FAIL: the unattended ISO (prepare-iso.log)"; exit 1; }
    log "install $NAME on $FWS"
    pids=()
    for fw in $FWS; do
        ( WEX_TEST_LOADER=vintner WEX_TEST_FIRMWARE=$fw bash "$SRC/installer/tests/install-unattended.sh" \
              "$W/unattended.iso" "$NAME-$fw" > "$OUT/install-$fw.log" 2>&1
          echo $? > "$OUT/install-$fw.rc" ) &
        pids+=($!)
        sleep 30   # the two setups' first minutes (the ISO read) apart
    done
    wait "${pids[@]}"
    rm -rf "$W"
    for fw in $FWS; do
        if [ "$(cat "$OUT/install-$fw.rc")" = 0 ] && grep -q '^PASS: zero-input install' "$OUT/install-$fw.log"; then
            log "PASS install $fw"
        else
            log "FAIL install $fw: $(grep -m1 '^FAIL' "$OUT/install-$fw.log")"
            overall=1
        fi
    done
    [ "$overall" = 0 ] || { log "the install failed; no stages run"; exit 1; }
fi

for fw in $FWS; do
    [ -f "$VMTEST_DIR/$NAME-$fw/disk.qcow2" ] || { log "FAIL: no installed disk $NAME-$fw"; exit 1; }
done

run_job() {   # run_job STAGE FW
    local stage=$1 fw=$2 name test rc t0
    name=$(stage_name "$stage")
    test=$NAME-$name-$fw
    t0=$(date +%s)
    # shellcheck disable=SC2046   # the phases are words
    WEX_TEST_FIRMWARE=$fw bash "$SRC/installer/tests/safe-mode.sh" "$NAME-$fw" "$test" $(phases_of "$stage") \
        > "$OUT/$name-$fw.log" 2>&1
    rc=$?
    for f in "$VMTEST_DIR/$test"/safe-mode/*-fail.png; do
        [ -f "$f" ] && cp "$f" "$OUT/$name-$fw-$(basename "$f")"
    done
    echo "$rc $(( ($(date +%s) - t0) / 60 ))" > "$OUT/$name-$fw.rc"
    # WSL keeps the page cache (the job's disk reads) from Windows until it is dropped; clean pages only
    sync
    echo 1 > /proc/sys/vm/drop_caches
}

log "stages on $FWS, $LANES at once: ${STAGES[*]}"
for stage in "${STAGES[@]}"; do
    for fw in $FWS; do
        while [ "$(jobs -rp | wc -l)" -ge "$LANES" ]; do wait -n; done
        log "start $(stage_name "$stage") $fw"
        run_job "$stage" "$fw" &
        sleep 20   # QEMU starts and the overlays' provisioning boots apart
    done
done
wait

for stage in "${STAGES[@]}"; do
    name=$(stage_name "$stage")
    for fw in $FWS; do
        read -r rc minutes < "$OUT/$name-$fw.rc" 2>/dev/null || { rc=x minutes='?'; }
        if [ "$rc" = 0 ]; then
            log "PASS $name $fw ($minutes min)"
        else
            log "FAIL $name $fw ($minutes min): $(grep '^FAIL' "$OUT/$name-$fw.log" | head -n 3 | tr '\n' ' ')"
            overall=1
        fi
    done
done
log "done in $(( ($(date +%s) - start_all) / 60 )) min, overall=$overall; logs in $OUT"
rm -rf "$SRC"
exit "$overall"
