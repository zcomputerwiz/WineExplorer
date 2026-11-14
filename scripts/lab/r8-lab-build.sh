#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# Lab build for the R8 standard-user chain (E1-S18e), in wexbuild as root. Never for a shipped image.
# Copies the already built WoW64 tree and rebuilds, in the copy only:
#   * the server with the S6 transport, R8, S10 token and S13 profile lab switches,
#   * wexhost's Unix side with the S6 transport,
#   * the registry/profile client DLLs with the S13 switch (both ABIs),
#   * wexelevate.exe and consent.exe with WEXELEVATE_E1S18, kept as *-e1s18.exe next to the normal ones.
# Usage: bash scripts/lab/r8-lab-build.sh [MAIN_BUILD [LAB_ROOT]]
set -e
MAIN=${1:-/root/WE-prefetch/build/wow64}
LABROOT=${2:-/root/owned-r8}
LAB=$LABROOT/build/wow64
LOG=${R8_LAB_LOG:-/tmp/r8-lab-build.log}
WINE_SRC=$(dirname "$(dirname "$MAIN")")/wine
SERVER_FLAGS='-DWEX_E1S6_TRANSPORT_TEST -DWEX_R8_SERVER_TEST -DWEX_E1S10_TOKEN_TEST -DWEX_E1S13_HKU_TEST'
SERVER_OBJECTS=(server/named_pipe.o server/sysrun.o server/accounts.o server/registry.o server/process.o
                server/request.o server/handle.o server/token.o server/thread.o server/e1s_profile.o
                server/wex_profile_storage.o server/file.o server/fd.o)

echo "=== 1. prepare lab tree ==="
rm -rf "$LAB" && mkdir -p "$LABROOT/build" && cp -a "$MAIN" "$LAB"
ln -sfn "$WINE_SRC" "$LABROOT/wine"
: > "$LOG"

echo "=== 2. lab server ==="
cd "$LAB"
rm -f "${SERVER_OBJECTS[@]}"
make -j1 CFLAGS="-g -O2 $SERVER_FLAGS" "${SERVER_OBJECTS[@]}" >> "$LOG" 2>&1
make -j16 server/wineserver >> "$LOG" 2>&1
echo "lab wineserver ok"

echo "=== 3. lab wexhost (S6 transport) ==="
rm -f dlls/wexhost/e1s6.o
make -j1 CFLAGS="-g -O2 -DWEX_E1S6_TRANSPORT_TEST" dlls/wexhost/e1s6.o >> "$LOG" 2>&1
make -j16 dlls/wexhost/wexhost.so >> "$LOG" 2>&1
echo "lab wexhost ok"

echo "=== 4. lab profile client DLLs (S13, both ABIs) ==="
PROFILE_OBJECTS=() PROFILE_DLLS=()
for arch in x86_64 i386; do
    PROFILE_OBJECTS+=("dlls/ntdll/$arch-windows/reg.o" "dlls/kernelbase/$arch-windows/registry.o"
                      "dlls/userenv/$arch-windows/userenv_main.o" "dlls/shell32/$arch-windows/shellpath.o")
    PROFILE_DLLS+=("dlls/ntdll/$arch-windows/ntdll.dll" "dlls/kernelbase/$arch-windows/kernelbase.dll"
                   "dlls/userenv/$arch-windows/userenv.dll" "dlls/shell32/$arch-windows/shell32.dll")
done
rm -f "${PROFILE_OBJECTS[@]}"
make -j1 x86_64_CFLAGS='-g -O2 -DWEX_E1S13_HKU_TEST' i386_CFLAGS='-g -O2 -DWEX_E1S13_HKU_TEST' \
    "${PROFILE_OBJECTS[@]}" >> "$LOG" 2>&1
make -j16 "${PROFILE_DLLS[@]}" >> "$LOG" 2>&1
echo "lab profile DLLs ok"

echo "=== 5. lab wexelevate and consent ==="
rm -f programs/wexelevate/*-windows/main.o programs/wexelevate/*-windows/wexelevate.exe
rm -f programs/consent/*-windows/consent.o programs/consent/*-windows/consent.exe
# WEX_E1_TEST_HOOKS: the hooks that must never ship even when WEXELEVATE_E1S18 does (the dialog's
# ElevationTest values that type the password); docs/E1_PRODUCTION_PLAN.md
make -j16 x86_64_CFLAGS="-g -O2 -DWEXELEVATE_E1S18 -DWEX_E1_TEST_HOOKS" i386_CFLAGS="-g -O2 -DWEXELEVATE_E1S18 -DWEX_E1_TEST_HOOKS" \
    programs/wexelevate/all programs/consent/all >> "$LOG" 2>&1
# Keep the E1S18 variants under their own names (only the R8 end-to-end case installs them) and rebuild the
# normal broker and consent, which every other lab case expects.
for a in x86_64 i386; do
    cp programs/wexelevate/$a-windows/wexelevate.exe programs/wexelevate/$a-windows/wexelevate-e1s18.exe
    cp programs/consent/$a-windows/consent.exe programs/consent/$a-windows/consent-e1s18.exe
done
rm -f programs/wexelevate/*-windows/main.o programs/wexelevate/*-windows/wexelevate.exe
rm -f programs/consent/*-windows/consent.o programs/consent/*-windows/consent.exe
make -j16 programs/wexelevate/all programs/consent/all >> "$LOG" 2>&1
echo "lab wexelevate and consent ok"

echo "LAB_BUILD_COMPLETE"
