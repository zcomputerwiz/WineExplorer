#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# Run the U1.0 probe's no-package cases on Wine and compare them with the
# Windows 11 oracle. Only the cases that need no package installation are run:
# the unpackaged direct run, which also covers the identity queries of an
# unpackaged process and the missing-class activation failure. Every
# package-activation case is marked NOT-RUN (no package activation in Wine yet).
#
# Usage: run-wine.sh <wine build dir> <prefix>
#
# The feature gate HKCU\Software\WineExplorer\Shell\UwpCompatibility is set to 1
# in the given prefix before the probe runs. The script refuses to run if the
# prefix is the wexbuild test fixture or /etc/wineexplorer is involved, and it
# never starts or kills a wineserver it did not start.
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

usage() { echo "usage: $0 <wine build dir> <prefix>" >&2; exit 2; }
[ $# -eq 2 ] || usage
WINE_BUILD_DIR="$1"
PREFIX="$2"

# Make PREFIX absolute and require it to exist.
case "$PREFIX" in
  /*) : ;;
  *) PREFIX="$PWD/$PREFIX" ;;
esac
[ -d "$PREFIX" ] || { echo "prefix does not exist: $PREFIX" >&2; exit 2; }

WINE="$WINE_BUILD_DIR/wine"
[ -x "$WINE" ] || { echo "wine loader not found or not executable: $WINE" >&2; exit 2; }

# --- Refusal: the wexbuild test fixture, or /etc/wineexplorer involved ---
refuse() { echo "REFUSED: $*" >&2; exit 3; }
# The system-role test fixture (scripts/test_sysrun_server.sh) lives under these
# paths; a prefix there belongs to it.
for bad in /var/lib/wexsys-c2 /home/wexadm /home/wexstd /home/wexcar /var/lib/wexgreeter; do
  case "$PREFIX" in
    "$bad" | "$bad"/*) refuse "prefix $PREFIX is the wexbuild test fixture (under $bad)" ;;
  esac
done
case "$PREFIX" in *"/etc/wineexplorer"*) refuse "prefix involves /etc/wineexplorer" ;; esac
case "$WINE_BUILD_DIR" in *"/etc/wineexplorer"*) refuse "wine build dir involves /etc/wineexplorer" ;; esac

# --- Wineserver tracking: never kill one we did not start ---
prefix_wineservers() {
  local pid line
  for pid in $(pgrep -x wineserver 2>/dev/null); do
    line="$(tr '\0' '\n' < "/proc/$pid/environ" 2>/dev/null | grep '^WINEPREFIX=' || true)"
    [ "$line" = "WINEPREFIX=$PREFIX" ] && echo "$pid"
  done
}
before="$(prefix_wineservers)"
cleanup_servers() {
  local pid
  for pid in $(prefix_wineservers); do
    case " $before " in
      *" $pid "*) : ;;   # already running before us; leave it
      *) kill "$pid" 2>/dev/null || true ;;
    esac
  done
}
trap cleanup_servers EXIT

export WINEPREFIX="$PREFIX"
# Headless: no Mono/Gecko install prompt (it blocks wineboot without a display, leaving syswow64 empty).
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:-mscoree,mshtml=}"
if [ ! -f "$PREFIX/drive_c/windows/syswow64/kernel32.dll" ]; then
  timeout 600 "$WINE" wineboot -i
  [ -f "$PREFIX/drive_c/windows/syswow64/kernel32.dll" ] || { echo "prefix setup failed: no syswow64/kernel32.dll" >&2; exit 2; }
fi

# --- Set the feature gate in this prefix ---
"$WINE" reg add 'HKCU\Software\WineExplorer\Shell\UwpCompatibility' /t REG_DWORD /d 1 /f

# Oracle reference for the unpackaged case (ORACLE_RESULTS.md, expected.txt).
ORACLE_UNPACKAGED_HR="0x80040154"
EXPECTED_UNPACKAGED="PASS unpackaged"
HR_RE='^HRESULT unpackaged 0x[0-9A-F]{8}$'

echo "=== U1.0 Wine run (no-package cases) ==="
echo "wine build dir: $WINE_BUILD_DIR"
echo "prefix: $PREFIX"
echo "gate: HKCU\\Software\\WineExplorer\\Shell\\UwpCompatibility = 1"
echo

for arch in x64 x86; do
  probe="$SCRIPT_DIR/build/mingw-$arch/probe.exe"
  [ -f "$probe" ] || { echo "missing $probe; run build-mingw.sh first" >&2; exit 2; }
  raw="$(mktemp)"
  out="actual-wine-$arch.txt"
  set +e
  # The probe opens its output path with _wfreopen: give it the Windows (Z:) form of the Unix temp file.
  "$WINE" "$probe" unpackaged "Z:${raw//\//\\}" unpackaged
  exit_code=$?
  set -e

  # Determine PASS/FAIL mirroring run.ps1: exactly two lines, one matching
  # HRESULT line, one "PASS negative_activation", and exit code 0.
  lines=()
  # The probe writes text mode (CRLF line ends): drop the CR before matching.
  while IFS= read -r line; do lines+=("${line%$'\r'}"); done < "$raw"
  hr_count=0
  pass_count=0
  observed_hr=""
  for line in "${lines[@]}"; do
    if [[ "$line" =~ $HR_RE ]]; then
      hr_count=$((hr_count + 1))
      observed_hr="${line##* }"
    fi
    [ "$line" = "PASS negative_activation" ] && pass_count=$((pass_count + 1))
  done
  if [ "$hr_count" -eq 1 ] && [ "$pass_count" -eq 1 ] && [ "${#lines[@]}" -eq 2 ] && [ "$exit_code" -eq 0 ]; then
    echo "PASS unpackaged" > "$out"
    result="PASS"
  else
    echo "FAIL unpackaged" > "$out"
    result="FAIL"
    echo "[$arch] probe exit $exit_code, output:" >&2
    sed 's/^/    /' "$raw" >&2
  fi
  rm -f "$raw"

  # Compare with the Windows oracle lines for the same case.
  if [ "$result" = "PASS" ]; then
    echo "[$arch] unpackaged: PASS (expected: $EXPECTED_UNPACKAGED) MATCH"
  else
    echo "[$arch] unpackaged: FAIL (expected: $EXPECTED_UNPACKAGED) MISMATCH"
  fi
  if [ -n "$observed_hr" ] && [ "$observed_hr" = "$ORACLE_UNPACKAGED_HR" ]; then
    echo "[$arch] unpackaged HRESULT: $observed_hr (oracle: $ORACLE_UNPACKAGED_HR) MATCH"
  else
    echo "[$arch] unpackaged HRESULT: ${observed_hr:-<none>} (oracle: $ORACLE_UNPACKAGED_HR) MISMATCH"
  fi
done

echo
echo "Package-activation cases (no package activation in Wine yet):"
for case in positive missing_class missing_dll; do
  echo "  $case: NOT-RUN (no package activation in Wine yet)"
done
echo
echo "WINE_RUN_DONE"
