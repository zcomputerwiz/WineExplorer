#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Both-ABI regression run (windows.storage, kernel32 version, rpcrt4 server) of a built candidate in a fresh
# owned prefix. usage: guest-regress.sh CANDIDATE_NAME RUN_NAME
set -euo pipefail
root=/home/debian/uwp-u1-20261111
candidate="$root/build/candidates/$1"
run="$candidate/$2"
build="$candidate/build"
test "$(id -un)" = debian
test "$(cat "$root/owner")" = codex-uwp-u1-20261111
test "$(cat "$candidate/phase")" = BUILT
test "$(cat "$candidate/exit")" = 0
mkdir -- "$run"
echo RUNNING > "$run/phase"
timeout --kill-after=15s 1800s make -C "$build" -j"${JOBS:-4}" dlls/windows.storage/tests/all dlls/kernel32/tests/all \
    dlls/rpcrt4/tests/all > "$run/build.log" 2>&1
export WINEPREFIX="$run/prefix" WINESERVER="$build/server/wineserver"
export WINEDEBUG=-all WINEDLLOVERRIDES=mscoree,mshtml= WINETEST_INTERACTIVE=0 WINETEST_COLOR=0
mkdir -- "$WINEPREFIX"
cleanup() {
  result=$?; trap - EXIT; set +e
  timeout --kill-after=5s 30s "$WINESERVER" -k > "$run/server-k.log" 2>&1; killed=$?
  timeout --kill-after=5s 30s "$WINESERVER" -w > "$run/server-w.log" 2>&1; waited=$?
  printf 'kill=%s wait=%s\n' "$killed" "$waited" > "$run/cleanup"
  if (( (killed != 0 && killed != 1) || waited != 0 )); then result=1; fi
  printf '%s\n' "$result" > "$run/exit"
  if (( result == 0 )); then echo PASSED > "$run/phase"; else echo FAILED > "$run/phase"; fi
  exit "$result"
}
trap cleanup EXIT
cd "$run"
timeout --kill-after=10s 180s "$build/wine" wineboot -i > boot.log 2>&1
overall=0
for architecture in x86_64 i386; do
  for target in storage version server; do
    case "$target" in
      storage) exe="$build/dlls/windows.storage/tests/$architecture-windows/windows.storage_test.exe";;
      version) exe="$build/dlls/kernel32/tests/$architecture-windows/kernel32_test.exe";;
      server)  exe="$build/dlls/rpcrt4/tests/$architecture-windows/rpcrt4_test.exe";;
    esac
    set +e
    timeout --kill-after=10s 300s "$build/wine" "$exe" "$target" > "$architecture-$target.log" 2>&1
    status=$?
    set -e
    printf '%s\n' "$status" > "$architecture-$target-exit"
    printf '%s %s exit=%s %s\n' "$architecture" "$target" "$status" "$(tail -n 1 "$architecture-$target.log" | tr -d '\r')" >> summary
    if (( status != 0 )); then overall=1; fi
  done
done
exit "$overall"
