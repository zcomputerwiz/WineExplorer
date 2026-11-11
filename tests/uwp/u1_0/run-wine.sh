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
# Usage: run-wine.sh <wine build dir> <prefix> [results dir]
#
#   <wine build dir>  directory containing the wine loader (and wineserver)
#   <prefix>          fresh prefix: it must not exist yet. The script creates
#                     it, takes an exclusive ownership lock in it, and removes
#                     it again on success. On any failure the prefix is
#                     retained for diagnostics.
#   [results dir]     fresh directory for the raw evidence; it must not exist
#                     yet. Default: a fresh temporary directory. Never the
#                     caller's current directory and never the fixture
#                     directory with the tracked oracle files.
#
# The feature gate HKCU\Software\WineExplorer\Shell\UwpCompatibility is set to
# 1 in the prefix before the probe runs. The build and prefix paths are
# canonicalized (symlinks and .. resolved) before the refusal checks, which
# cover the wexbuild test fixture paths and any involvement of
# /etc/wineexplorer or /usr/local/libexec/wineexplorer (the live installed
# system-role configuration and runtime). Foreign or preexisting prefix state
# is refused, including a held ownership lock. The script requires a matching
# wineserver binary (resolving server/wineserver or wineserver) before any
# mutation. Cleanup is strictly scoped to the fresh owned prefix using that
# matching binary with bounded -k and -w; no global process scans/kills are used.
# The ownership lock compares exact invocation identity. Every timeout has a
# validated positive bound and kill-after deadline so TERM-ignoring commands
# cannot hang. The results directory is created atomically. On any failure,
# cleanup failure, prefix deletion error, or lost ownership, the prefix is
# retained for diagnostics.
#
# Per-architecture raw output, exit status and comparison results are kept in
# the results directory: raw-<arch>.txt, exit-<arch>.txt,
# actual-wine-<arch>.txt, compare-<arch>.txt and summary.txt.
#
# Exit codes:
#   0  both architectures pass and match the oracle HRESULT, and prefix cleanup succeeds
#   1  a case failed (probe failure, timeout, malformed or extra output,
#      nonzero exit or oracle HRESULT mismatch) or cleanup failure
#   2  usage or prerequisite error (missing loader or probe, prefix setup
#      failure, results dir not fresh)
#   3  refused (foreign or preexisting prefix, held lock, system-role path)
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

usage() { echo "usage: $0 <wine build dir> <prefix> [results dir]" >&2; exit 2; }
if [ $# -lt 2 ] || [ $# -gt 3 ]; then usage; fi
WINE_BUILD_DIR="$1"
PREFIX="$2"
RESULTS_ARG="${3:-}"

# Bounds (seconds) for the Wine commands; overridable for tests.
WINEBOOT_TIMEOUT="${WEX_U1_WINEBOOT_TIMEOUT:-600}"
REG_TIMEOUT="${WEX_U1_REG_TIMEOUT:-120}"
PROBE_TIMEOUT="${WEX_U1_PROBE_TIMEOUT:-300}"
WINESERVER_TIMEOUT="${WEX_U1_WINESERVER_TIMEOUT:-10}"
KILL_AFTER="${WEX_U1_KILL_AFTER:-10}"

die2() { echo "ERROR: $*" >&2; exit 2; }
refuse() { echo "REFUSED: $*" >&2; exit 3; }

validate_timeout() {
  local name="$1" val="$2"
  if [[ ! "$val" =~ ^[1-9][0-9]*$ ]]; then
    die2 "timeout $name must be a positive integer, got '$val'"
  fi
}
validate_timeout "WEX_U1_WINEBOOT_TIMEOUT" "$WINEBOOT_TIMEOUT"
validate_timeout "WEX_U1_REG_TIMEOUT" "$REG_TIMEOUT"
validate_timeout "WEX_U1_PROBE_TIMEOUT" "$PROBE_TIMEOUT"
validate_timeout "WEX_U1_WINESERVER_TIMEOUT" "$WINESERVER_TIMEOUT"
validate_timeout "WEX_U1_KILL_AFTER" "$KILL_AFTER"

command -v realpath >/dev/null 2>&1 || die2 "realpath is required"
command -v timeout >/dev/null 2>&1 || die2 "timeout is required"

# --- Canonicalize before any refusal check (resolves symlinks and ..) ---
WINE_BUILD_DIR="$(realpath -m -- "$WINE_BUILD_DIR")"
PREFIX="$(realpath -m -- "$PREFIX")"

# --- Refusal: system-role and wexbuild fixture paths ---
# The system-role test fixture (scripts/test_sysrun_server.sh) lives under
# these paths; a prefix or build there belongs to it. /etc/wineexplorer and
# /usr/local/libexec/wineexplorer are the live installed system-role
# configuration and runtime.
for bad in /var/lib/wexsys-c2 /home/wexadm /home/wexstd /home/wexcar /var/lib/wexgreeter; do
  case "$PREFIX" in
    "$bad" | "$bad"/*) refuse "prefix $PREFIX is the wexbuild test fixture (under $bad)" ;;
  esac
  case "$WINE_BUILD_DIR" in
    "$bad" | "$bad"/*) refuse "wine build dir $WINE_BUILD_DIR is the wexbuild test fixture (under $bad)" ;;
  esac
done
case "$PREFIX" in *"/etc/wineexplorer"*) refuse "prefix involves /etc/wineexplorer" ;; esac
case "$WINE_BUILD_DIR" in *"/etc/wineexplorer"*) refuse "wine build dir involves /etc/wineexplorer" ;; esac
case "$PREFIX" in *"/usr/local/libexec/wineexplorer"*) refuse "prefix involves the installed system-role runtime" ;; esac
case "$WINE_BUILD_DIR" in *"/usr/local/libexec/wineexplorer"*) refuse "wine build dir involves the installed system-role runtime" ;; esac

# --- Wine loader, matching wineserver, and probes (required before mutation) ---
WINE="$WINE_BUILD_DIR/wine"
[ -x "$WINE" ] || die2 "wine loader not found or not executable: $WINE"

WINE_SERVER=""
if [ -x "$WINE_BUILD_DIR/server/wineserver" ]; then
  WINE_SERVER="$(realpath -m -- "$WINE_BUILD_DIR/server/wineserver")"
elif [ -x "$WINE_BUILD_DIR/wineserver" ]; then
  WINE_SERVER="$(realpath -m -- "$WINE_BUILD_DIR/wineserver")"
fi
[ -n "$WINE_SERVER" ] && [ -x "$WINE_SERVER" ] || die2 "matching wineserver binary not found or not executable in $WINE_BUILD_DIR (checked server/wineserver and wineserver)"

for arch in x64 x86; do
  probe="$SCRIPT_DIR/build/mingw-$arch/probe.exe"
  [ -f "$probe" ] || die2 "missing $probe; run build-mingw.sh first"
done

# --- Results directory: fresh atomic mkdir, never cwd or tracked oracle files ---
if [ -n "$RESULTS_ARG" ]; then
  if [ -L "$RESULTS_ARG" ]; then
    die2 "results dir $RESULTS_ARG is a symlink/reparse; a fresh real directory is required"
  fi
  RESULTS="$(realpath -m -- "$RESULTS_ARG")"
  if [ -L "$RESULTS" ]; then
    die2 "results dir $RESULTS is a symlink/reparse; a fresh real directory is required"
  fi
  if [ "$RESULTS" = "$PWD" ]; then
    die2 "results dir must not be the current directory"
  fi
  case "$RESULTS" in
    "$SCRIPT_DIR" | "$SCRIPT_DIR"/*) die2 "results dir must not be the fixture directory (tracked oracle files)" ;;
    "$PREFIX" | "$PREFIX"/*) die2 "results dir must not be inside the prefix" ;;
  esac
  RESULTS_PARENT="$(dirname "$RESULTS")"
  [ -d "$RESULTS_PARENT" ] || die2 "results dir parent $RESULTS_PARENT does not exist"
  if [ -L "$RESULTS_PARENT" ]; then
    die2 "results dir parent $RESULTS_PARENT is a symlink/reparse; ownership unprovable"
  fi
  # Atomic mkdir without -p: fails if directory already exists
  mkdir -- "$RESULTS" 2>/dev/null || die2 "results dir $RESULTS already exists or cannot be created as a fresh directory"
else
  RESULTS="$(mktemp -d)"
  RESULTS="$(realpath -m -- "$RESULTS")"
fi

# --- Fresh, empty, explicitly supplied prefix with exclusive ownership lock ---
LOCK_NAME=".wex-u1-lock"
if [ -L "$2" ] || [ -L "$PREFIX" ]; then
  refuse "prefix $PREFIX is a symlink; a fresh real directory is required"
fi
PREFIX_PARENT="$(dirname "$PREFIX")"
[ -d "$PREFIX_PARENT" ] || refuse "cannot create prefix $PREFIX (parent missing or foreign state appeared)"
if [ -L "$PREFIX_PARENT" ]; then
  refuse "prefix parent $PREFIX_PARENT is a symlink; ownership unprovable"
fi
if [ -e "$PREFIX" ]; then
  if [ -f "$PREFIX/$LOCK_NAME" ]; then
    refuse "prefix $PREFIX holds an ownership lock ($LOCK_NAME) from another or a crashed run; state retained, not removed"
  fi
  refuse "prefix $PREFIX already exists; a fresh empty prefix is required (preexisting state retained)"
fi
if ! mkdir -- "$PREFIX"; then
  refuse "cannot create prefix $PREFIX (parent missing or foreign state appeared)"
fi

# Invocation identity token for exact comparison (not pid-prefix grep)
LOCK_TOKEN="wex-u1-invoc:pid=$$:time=$(date -u '+%Y-%m-%dT%H:%M:%SZ'):rand=${RANDOM:-0}"
if ! ( set -o noclobber; printf '%s\n' "$LOCK_TOKEN" > "$PREFIX/$LOCK_NAME" ); then
  refuse "cannot take the exclusive ownership lock in $PREFIX"
fi

# --- Scoped wineserver cleanup: only fresh owned prefix using matching binary ---
is_lock_ours() {
  [ -f "$PREFIX/$LOCK_NAME" ] || return 1
  local current
  current="$(cat "$PREFIX/$LOCK_NAME" 2>/dev/null || true)"
  [ "$current" = "$LOCK_TOKEN" ]
}

cleanup_server() {
  [ -d "$PREFIX" ] || return 0
  if [ -z "$WINE_SERVER" ] || [ ! -x "$WINE_SERVER" ]; then
    echo "warning: matching wineserver binary missing or not executable during cleanup: ${WINE_SERVER:-<unset>}" >&2
    return 1
  fi

  local s_rc=0
  local w_rc=0
  set +e
  WINEPREFIX="$PREFIX" timeout -k "$KILL_AFTER" "$WINESERVER_TIMEOUT" "$WINE_SERVER" -k 2>/dev/null
  s_rc=$?
  WINEPREFIX="$PREFIX" timeout -k "$KILL_AFTER" "$WINESERVER_TIMEOUT" "$WINE_SERVER" -w 2>/dev/null
  w_rc=$?
  set -e

  if [ "$s_rc" -eq 124 ] || [ "$s_rc" -eq 137 ] || [ "$w_rc" -eq 124 ] || [ "$w_rc" -eq 137 ]; then
    echo "warning: wineserver cleanup timed out for prefix $PREFIX" >&2
    return 1
  fi
  if [ "$s_rc" -gt 1 ] || [ "$w_rc" -ne 0 ]; then
    echo "warning: wineserver cleanup wait failed (kill exit $s_rc, wait exit $w_rc) for prefix $PREFIX" >&2
    return 1
  fi
  return 0
}

cleanup() {
  local rc=$?
  trap - EXIT

  local cleanup_ok=true

  # Only clean up wineserver if the prefix exists and the lock is provably ours
  if [ -d "$PREFIX" ] && is_lock_ours; then
    if ! cleanup_server; then
      cleanup_ok=false
      echo "cleanup failed: wineserver did not terminate cleanly for $PREFIX" >&2
    fi
  fi

  # Coherent ownership and existence verification after bounded server cleanup.
  # Late ownership loss (including lock change during server -k/-w), unexpectedly
  # missing prefix, or prefix deletion error is a cleanup failure.
  # Remove prefix ONLY on overall success and provable ownership.
  # Never delete a changed lock or foreign prefix; retain any remaining state.
  if [ ! -d "$PREFIX" ]; then
    cleanup_ok=false
    echo "cleanup failed: prefix $PREFIX is missing" >&2
  elif ! is_lock_ours; then
    cleanup_ok=false
    echo "ownership lock in $PREFIX is not ours; retaining prefix" >&2
  elif [ "$rc" -eq 0 ] && [ "$cleanup_ok" = "true" ]; then
    if rm -rf -- "$PREFIX"; then
      :
    else
      echo "cleanup failed: failed to remove prefix $PREFIX" >&2
      cleanup_ok=false
      echo "retained prefix for diagnostics: $PREFIX" >&2
    fi
  else
    echo "retained prefix for diagnostics: $PREFIX" >&2
  fi

  # Cleanup failure, unexpectedly missing prefix, or lost/changed ownership forces failure return
  if [ "$cleanup_ok" != "true" ]; then
    if [ "$rc" -eq 0 ]; then
      rc=1
    fi
  fi

  # Update machine-readable summary.txt to reflect final cleanup and overall status.
  # Never report overall: PASS after cleanup failure, prefix deletion error, or lost ownership.
  if [ -n "${RESULTS:-}" ] && [ -d "$RESULTS" ] && [ -f "$RESULTS/summary.txt" ]; then
    local clean_str="PASS"
    if [ "$cleanup_ok" != "true" ]; then
      clean_str="FAIL"
    fi
    local overall_str="PASS"
    if [ "$rc" -ne 0 ] || [ "$cleanup_ok" != "true" ]; then
      overall_str="FAIL"
    fi

    local tmp_sum="$RESULTS/summary.tmp.$$"
    local sum_line
    while IFS= read -r sum_line || [ -n "$sum_line" ]; do
      case "$sum_line" in
        cleanup:*|overall:*) ;;
        *) printf '%s\n' "$sum_line" ;;
      esac
    done < "$RESULTS/summary.txt" > "$tmp_sum"
    printf 'cleanup: %s\noverall: %s\n' "$clean_str" "$overall_str" >> "$tmp_sum"
    mv -f "$tmp_sum" "$RESULTS/summary.txt"
  fi

  exit "$rc"
}
trap cleanup EXIT

export WINEPREFIX="$PREFIX"
# Headless: no Mono/Gecko install prompt (it blocks wineboot without a display, leaving syswow64 empty).
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:-mscoree,mshtml=}"

# Oracle reference for the unpackaged case (ORACLE_RESULTS.md, expected.txt).
ORACLE_UNPACKAGED_HR="0x80040154"
EXPECTED_UNPACKAGED="PASS unpackaged"
HR_RE='^HRESULT unpackaged 0x[0-9A-F]{8}$'

echo "=== U1.0 Wine run (no-package cases) ==="
echo "wine build dir: $WINE_BUILD_DIR"
echo "prefix: $PREFIX"
echo "results dir: $RESULTS"
echo "gate: HKCU\\Software\\WineExplorer\\Shell\\UwpCompatibility = 1"
echo

# --- Boot the fresh prefix (bounded, kill-after) ---
boot_rc=0
if [ ! -f "$PREFIX/drive_c/windows/syswow64/kernel32.dll" ]; then
  set +e
  timeout -k "$KILL_AFTER" "$WINEBOOT_TIMEOUT" "$WINE" wineboot -i
  boot_rc=$?
  set -e
  if [ "$boot_rc" -ne 0 ]; then
    if [ "$boot_rc" -eq 124 ] || [ "$boot_rc" -eq 137 ]; then
      echo "wineboot timed out after ${WINEBOOT_TIMEOUT}s" >&2
    else
      echo "wineboot failed with exit $boot_rc" >&2
    fi
    die2 "prefix setup failed: wineboot failed (exit $boot_rc)"
  fi
fi
[ -f "$PREFIX/drive_c/windows/syswow64/kernel32.dll" ] || die2 "prefix setup failed: no syswow64/kernel32.dll"

# --- Set the feature gate in this prefix (bounded, kill-after) ---
set +e
timeout -k "$KILL_AFTER" "$REG_TIMEOUT" "$WINE" reg add 'HKCU\Software\WineExplorer\Shell\UwpCompatibility' /t REG_DWORD /d 1 /f
reg_rc=$?
set -e
if [ "$reg_rc" -ne 0 ]; then
  if [ "$reg_rc" -eq 124 ] || [ "$reg_rc" -eq 137 ]; then
    echo "feature gate reg add timed out after ${REG_TIMEOUT}s" >&2
  else
    echo "feature gate reg add failed with exit $reg_rc" >&2
  fi
  exit 2
fi

OVERALL=0

for arch in x64 x86; do
  probe="$SCRIPT_DIR/build/mingw-$arch/probe.exe"
  raw="$RESULTS/raw-$arch.txt"
  set +e
  # The probe opens its output path with _wfreopen: give it the Windows (Z:) form of the Unix path.
  timeout -k "$KILL_AFTER" "$PROBE_TIMEOUT" "$WINE" "$probe" unpackaged "Z:${raw//\//\\}" unpackaged
  exit_code=$?
  set -e
  if [ ! -f "$raw" ]; then : > "$raw"; fi
  echo "$exit_code" > "$RESULTS/exit-$arch.txt"

  # Determine PASS/FAIL mirroring run.ps1: exactly two lines, one matching
  # HRESULT line, one "PASS negative_activation", and exit code 0.
  lines=()
  # The probe writes text mode (CRLF line ends): drop the CR before matching.
  # Strip into a scalar first: some bash builds (Git Bash) do not expand
  # $'\r' in the pattern of an array assignment.
  while IFS= read -r line || [ -n "$line" ]; do
    line="${line%$'\r'}"
    lines+=("$line")
  done < "$raw"
  hr_count=0
  pass_count=0
  observed_hr=""
  for line in "${lines[@]}"; do
    if [[ "$line" =~ $HR_RE ]]; then
      hr_count=$((hr_count + 1))
      observed_hr="${line##* }"
    fi
    if [ "$line" = "PASS negative_activation" ]; then
      pass_count=$((pass_count + 1))
    fi
  done
  if [ "$hr_count" -eq 1 ] && [ "$pass_count" -eq 1 ] && [ "${#lines[@]}" -eq 2 ] && [ "$exit_code" -eq 0 ]; then
    case_ok="PASS"
  else
    case_ok="FAIL"
    echo "[$arch] probe exit $exit_code, output:" >&2
    sed 's/^/    /' "$raw" >&2
  fi

  # Compare with the Windows oracle lines for the same case.
  hr_match="MISMATCH"
  if [ -n "$observed_hr" ] && [ "$observed_hr" = "$ORACLE_UNPACKAGED_HR" ]; then
    hr_match="MATCH"
  fi
  # The per-arch verdict requires both the case shape and the oracle HRESULT.
  if [ "$case_ok" = "PASS" ] && [ "$hr_match" = "MATCH" ]; then
    result="PASS"
  else
    result="FAIL"
  fi
  echo "$result unpackaged" > "$RESULTS/actual-wine-$arch.txt"
  case_line="[$arch] unpackaged case: $case_ok (shape and exit code)"
  hr_line="[$arch] unpackaged HRESULT: ${observed_hr:-<none>} (oracle: $ORACLE_UNPACKAGED_HR) $hr_match"
  verdict_line="[$arch] unpackaged: $result (expected: $EXPECTED_UNPACKAGED)"
  echo "$case_line"
  echo "$hr_line"
  echo "$verdict_line"
  printf '%s\n%s\n%s\n' "$case_line" "$hr_line" "$verdict_line" > "$RESULTS/compare-$arch.txt"

  if [ "$result" = "FAIL" ]; then
    OVERALL=1
  fi
done

echo
echo "Package-activation cases (no package activation in Wine yet):"
for c in positive missing_class missing_dll; do
  echo "  $c: NOT-RUN (no package activation in Wine yet)"
done
echo

{
  echo "wine build dir: $WINE_BUILD_DIR"
  echo "prefix: $PREFIX"
  echo "results dir: $RESULTS"
  for arch in x64 x86; do
    echo "actual-wine-$arch: $(cat "$RESULTS/actual-wine-$arch.txt")"
    echo "exit-$arch: $(cat "$RESULTS/exit-$arch.txt")"
  done
  echo "cleanup: pending"
  if [ "$OVERALL" -eq 0 ]; then echo "overall: PASS"; else echo "overall: FAIL"; fi
} > "$RESULTS/summary.txt"
echo "WINE_RUN_DONE"
exit "$OVERALL"
