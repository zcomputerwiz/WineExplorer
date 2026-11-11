#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# Deterministic fake-loader regression checks for run-wine.sh (U1-M1).
#
# A copied disposable fixture is run against a fake `wine` loader that
# emulates wineboot, reg add and the U1.0 probe. No real Wine, no builds,
# no WSL, no VM, no shared lab access. Run with Git Bash (not the WSL bash
# shim):
#
#   "C:/Program Files/Git/bin/bash.exe" tests/uwp/u1_0/test-run-wine.sh
#
# Exit code: 0 if all checks pass, 1 otherwise. The work directory is
# retained on failure for diagnostics.
set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

pass_count=0
fail_count=0
ok()  { echo "PASS: $1"; pass_count=$((pass_count + 1)); }
bad() { echo "FAIL: $1"; fail_count=$((fail_count + 1)); }

expect_rc() { # expect_rc <name> <expected> <actual>
  if [ "$2" = "$3" ]; then ok "$1 (exit $3)"; else bad "$1 (expected exit $2, got $3)"; fi
}
expect_file() { # expect_file <name> <file> <expected content>
  if [ -f "$2" ] && [ "$(cat "$2")" = "$3" ]; then
    ok "$1"
  else
    bad "$1 (got: $(cat "$2" 2>/dev/null || echo '<missing>'))"
  fi
}
expect_grep() { # expect_grep <name> <file> <pattern>
  if [ -f "$2" ] && grep -q -- "$3" "$2"; then
    ok "$1"
  else
    bad "$1 (pattern '$3' not found in $2)"
  fi
}

WORK="$(mktemp -d)"
cleanup() {
  if [ "$fail_count" -gt 0 ]; then
    echo "retained work dir for diagnostics: $WORK"
  else
    rm -rf "$WORK"
  fi
}
trap cleanup EXIT

echo "=== U1-M1 run-wine.sh fake-loader regression checks ==="

# --- Shell syntax ---
if bash -n "$SCRIPT_DIR/run-wine.sh"; then ok "syntax: run-wine.sh"; else bad "syntax: run-wine.sh"; fi
if bash -n "$SCRIPT_DIR/test-run-wine.sh"; then ok "syntax: test-run-wine.sh"; else bad "syntax: test-run-wine.sh"; fi

# --- Disposable copy of the fixture ---
FIX="$WORK/fixture"
mkdir -p "$FIX" "$WORK/prefixes" "$WORK/results"
cp -r "$SCRIPT_DIR/." "$FIX/"
for arch in x64 x86; do
  mkdir -p "$FIX/build/mingw-$arch"
  # The fake loader never executes the probe; a placeholder is enough when
  # the MinGW build outputs are not present.
  if [ ! -f "$FIX/build/mingw-$arch/probe.exe" ]; then : > "$FIX/build/mingw-$arch/probe.exe"; fi
done

# --- Fake wine loader ---
FAKE_WINE="$WORK/fake-wine"
cat > "$FAKE_WINE" <<'FAKE_EOF'
#!/bin/bash
# Fake wine loader for the run-wine.sh regression checks. It never executes
# real Windows binaries; it emulates wineboot, reg add and the U1.0 probe.
# Behavior is selected with FAKE_WINE_CTRL:
#   pass               correct two-line CRLF output, exit 0
#   wrong_hr           correct shape, wrong HRESULT, exit 0
#   fail_line          FAIL negative_activation line, exit 1
#   malformed          three lines (extra output), exit 0
#   nonzero            correct output, exit 1
#   hang               sleeps without ignoring TERM (exits 124 on SIGTERM)
#   hang_term_ignored  ignores TERM and sleeps (killed by SIGKILL, exit 137)
#   boot_fail_marker   wineboot creates syswow64 marker, then exits 1
#   boot_hang          wineboot ignores TERM and hangs
#   reg_hang           reg ignores TERM and hangs
#   alter_lock         probe tampers with the ownership lock file
#   remove_prefix      probe removes its prefix (unexpectedly missing prefix at cleanup)
set -u
case "${1:-}" in
  wineboot)
    pfx="${WINEPREFIX:?WINEPREFIX not set}"
    mkdir -p "$pfx/drive_c/windows/syswow64"
    : > "$pfx/drive_c/windows/syswow64/kernel32.dll"
    case "${FAKE_WINE_CTRL:-pass}" in
      boot_fail_marker)
        exit 1
        ;;
      boot_hang)
        trap '' TERM
        while true; do sleep 1; done
        ;;
      *)
        exit 0
        ;;
    esac
    ;;
  reg)
    case "${FAKE_WINE_CTRL:-pass}" in
      reg_hang)
        trap '' TERM
        while true; do sleep 1; done
        ;;
      *)
        exit 0
        ;;
    esac
    ;;
  *)
    out="${3:-}"
    out="${out//\\//}"
    case "$out" in
      Z:*) out="/${out#Z:/}" ;;
    esac
    case "${FAKE_WINE_CTRL:-pass}" in
      pass)
        printf 'HRESULT unpackaged 0x80040154\r\nPASS negative_activation\r\n' > "$out"
        exit 0
        ;;
      wrong_hr)
        printf 'HRESULT unpackaged 0x80004005\r\nPASS negative_activation\r\n' > "$out"
        exit 0
        ;;
      fail_line)
        printf 'HRESULT unpackaged 0x80040154\r\nFAIL negative_activation\r\n' > "$out"
        exit 1
        ;;
      malformed)
        printf 'HRESULT unpackaged 0x80040154\r\nPASS negative_activation\r\nEXTRA\r\n' > "$out"
        exit 0
        ;;
      nonzero)
        printf 'HRESULT unpackaged 0x80040154\r\nPASS negative_activation\r\n' > "$out"
        exit 1
        ;;
      hang)
        exec sleep 300
        ;;
      hang_term_ignored)
        trap '' TERM
        while true; do sleep 1; done
        ;;
      alter_lock)
        pfx="${WINEPREFIX:?WINEPREFIX not set}"
        printf 'foreign-identity-token\n' > "$pfx/.wex-u1-lock"
        printf 'HRESULT unpackaged 0x80040154\r\nPASS negative_activation\r\n' > "$out"
        exit 0
        ;;
      disappear_server)
        bindir="$(dirname "$0")"
        rm -f "$bindir/wineserver" "$bindir/server/wineserver"
        printf 'HRESULT unpackaged 0x80040154\r\nPASS negative_activation\r\n' > "$out"
        exit 0
        ;;
      remove_prefix)
        pfx="${WINEPREFIX:?WINEPREFIX not set}"
        [ -n "$pfx" ] && [ "$pfx" != "/" ] && rm -rf -- "$pfx"
        printf 'HRESULT unpackaged 0x80040154\r\nPASS negative_activation\r\n' > "$out"
        exit 0
        ;;
      *)
        echo "fake wine: unknown FAKE_WINE_CTRL '${FAKE_WINE_CTRL}'" >&2
        exit 64
        ;;
    esac
    ;;
esac
FAKE_EOF
chmod +x "$FAKE_WINE"

# --- Fake wineserver ---
FAKE_WINESERVER="$WORK/fake-wineserver"
cat > "$FAKE_WINESERVER" <<'FAKE_SRV_EOF'
#!/bin/bash
# Fake wineserver for the run-wine.sh regression checks.
# Behavior is selected with FAKE_WINESERVER_CTRL:
#   pass          exit 0 (clean shutdown / wait)
#   cleanup_hang  ignores TERM and hangs (killed by SIGKILL, exit 137)
#   cleanup_fail  exits 2 (simulated failure)
#   wait_fail_1   -k exits 0, -w exits 1 (simulated wait failure)
#   k_exit1_pass  -k exits 1, -w exits 0 (simulated no server running)
#   change_lock_w -k exits 0, -w alters ownership lock (simulated late ownership loss)
set -u
case "${FAKE_WINESERVER_CTRL:-pass}" in
  cleanup_hang)
    trap '' TERM
    while true; do sleep 1; done
    ;;
  cleanup_fail)
    echo "fake wineserver: simulated cleanup failure on $*" >&2
    exit 2
    ;;
  wait_fail_1)
    case "${1:-}" in
      -k) exit 0 ;;
      -w) exit 1 ;;
      *) exit 2 ;;
    esac
    ;;
  k_exit1_pass)
    case "${1:-}" in
      -k) exit 1 ;;
      -w) exit 0 ;;
      *) exit 2 ;;
    esac
    ;;
  change_lock_w)
    case "${1:-}" in
      -k) exit 0 ;;
      -w)
        pfx="${WINEPREFIX:?WINEPREFIX not set}"
        printf 'foreign-identity-token\n' > "$pfx/.wex-u1-lock"
        exit 0
        ;;
      *) exit 2 ;;
    esac
    ;;
  *)
    exit 0
    ;;
esac
FAKE_SRV_EOF
chmod +x "$FAKE_WINESERVER"

# --- Helpers ---
BUILD=""
RC=""
LOG=""
make_build() { # make_build <name> [layout]
  local name="$1" layout="${2:-root}"
  mkdir -p "$WORK/builds/$name"
  cp "$FAKE_WINE" "$WORK/builds/$name/wine"
  chmod +x "$WORK/builds/$name/wine"
  if [ "$layout" = "server_subdir" ]; then
    mkdir -p "$WORK/builds/$name/server"
    cp "$FAKE_WINESERVER" "$WORK/builds/$name/server/wineserver"
    chmod +x "$WORK/builds/$name/server/wineserver"
  elif [ "$layout" = "missing_server" ]; then
    : # do not create wineserver
  else
    cp "$FAKE_WINESERVER" "$WORK/builds/$name/wineserver"
    chmod +x "$WORK/builds/$name/wineserver"
  fi
  BUILD="$WORK/builds/$name"
}

run_case() { # run_case <name> <ctrl> <prefix> [results] [srv_ctrl]
  local name="$1" ctrl="$2" prefix="$3" results="${4:-}" srv_ctrl="${5:-pass}"
  LOG="$WORK/logs/$name.log"
  mkdir -p "$WORK/logs"
  if [ -n "$results" ]; then
    FAKE_WINE_CTRL="$ctrl" FAKE_WINESERVER_CTRL="$srv_ctrl" \
      WEX_U1_WINEBOOT_TIMEOUT=2 WEX_U1_REG_TIMEOUT=2 WEX_U1_PROBE_TIMEOUT=2 \
      WEX_U1_WINESERVER_TIMEOUT=2 WEX_U1_KILL_AFTER=1 \
      bash "$FIX/run-wine.sh" "$BUILD" "$prefix" "$results" > "$LOG" 2>&1
    RC=$?
  else
    FAKE_WINE_CTRL="$ctrl" FAKE_WINESERVER_CTRL="$srv_ctrl" \
      WEX_U1_WINEBOOT_TIMEOUT=2 WEX_U1_REG_TIMEOUT=2 WEX_U1_PROBE_TIMEOUT=2 \
      WEX_U1_WINESERVER_TIMEOUT=2 WEX_U1_KILL_AFTER=1 \
      bash "$FIX/run-wine.sh" "$BUILD" "$prefix" > "$LOG" 2>&1
    RC=$?
  fi
}

# --- T1: pass ---
make_build pass
run_case pass pass "$WORK/prefixes/pass" "$WORK/results/pass"
expect_rc "pass: overall exit 0" 0 "$RC"
expect_file "pass: x64 actual" "$WORK/results/pass/actual-wine-x64.txt" "PASS unpackaged"
expect_file "pass: x86 actual" "$WORK/results/pass/actual-wine-x86.txt" "PASS unpackaged"
expect_file "pass: x64 exit status" "$WORK/results/pass/exit-x64.txt" "0"
expect_grep "pass: x64 case PASS" "$WORK/results/pass/compare-x64.txt" "unpackaged case: PASS"
expect_grep "pass: x64 HRESULT MATCH" "$WORK/results/pass/compare-x64.txt" "HRESULT: 0x80040154 (oracle: 0x80040154) MATCH"
expect_grep "pass: x64 verdict PASS" "$WORK/results/pass/compare-x64.txt" "unpackaged: PASS (expected: PASS unpackaged)"
expect_grep "pass: x86 verdict PASS" "$WORK/results/pass/compare-x86.txt" "unpackaged: PASS (expected: PASS unpackaged)"
expect_grep "pass: summary cleanup PASS" "$WORK/results/pass/summary.txt" "cleanup: PASS"
expect_grep "pass: summary overall PASS" "$WORK/results/pass/summary.txt" "overall: PASS"
if grep -q -- "overall: FAIL" "$WORK/results/pass/summary.txt"; then bad "pass: summary falsely contains overall FAIL"; else ok "pass: summary does not contain overall FAIL"; fi
if grep -q -- "cleanup: FAIL" "$WORK/results/pass/summary.txt"; then bad "pass: summary falsely contains cleanup FAIL"; else ok "pass: summary does not contain cleanup FAIL"; fi
expect_grep "pass: NOT-RUN positive" "$LOG" "positive: NOT-RUN"
expect_grep "pass: NOT-RUN missing_class" "$LOG" "missing_class: NOT-RUN"
expect_grep "pass: NOT-RUN missing_dll" "$LOG" "missing_dll: NOT-RUN"
crlf_count=$(tr -dc '\r' < "$WORK/results/pass/raw-x64.txt" | wc -c)
if [ "$crlf_count" = "2" ]; then ok "pass: raw x64 retains CRLF (2 CR)"; else bad "pass: raw x64 retains CRLF (got $crlf_count CR)"; fi
if [ ! -e "$WORK/prefixes/pass" ]; then ok "pass: prefix removed on success"; else bad "pass: prefix removed on success (still exists)"; fi
expect_file "pass: fixture oracle file untouched" "$FIX/actual-wine-x64.txt" "PASS unpackaged"

# --- T2: wrong HRESULT ---
make_build wrong-hr
run_case wrong-hr wrong_hr "$WORK/prefixes/wrong-hr" "$WORK/results/wrong-hr"
expect_rc "wrong-hr: nonzero overall exit" 1 "$RC"
expect_file "wrong-hr: x64 actual FAIL" "$WORK/results/wrong-hr/actual-wine-x64.txt" "FAIL unpackaged"
expect_grep "wrong-hr: case shape PASS" "$WORK/results/wrong-hr/compare-x64.txt" "unpackaged case: PASS"
expect_grep "wrong-hr: HRESULT MISMATCH" "$WORK/results/wrong-hr/compare-x64.txt" "HRESULT: 0x80004005 (oracle: 0x80040154) MISMATCH"
expect_grep "wrong-hr: verdict FAIL" "$WORK/results/wrong-hr/compare-x64.txt" "unpackaged: FAIL (expected: PASS unpackaged)"
expect_grep "wrong-hr: summary overall FAIL" "$WORK/results/wrong-hr/summary.txt" "overall: FAIL"

# --- T3: failed check line ---
make_build fail-line
run_case fail-line fail_line "$WORK/prefixes/fail-line" "$WORK/results/fail-line"
expect_rc "fail-line: nonzero overall exit" 1 "$RC"
expect_file "fail-line: x64 actual FAIL" "$WORK/results/fail-line/actual-wine-x64.txt" "FAIL unpackaged"
expect_file "fail-line: x64 exit status" "$WORK/results/fail-line/exit-x64.txt" "1"

# --- T4: malformed (extra) output ---
make_build malformed
run_case malformed malformed "$WORK/prefixes/malformed" "$WORK/results/malformed"
expect_rc "malformed: nonzero overall exit" 1 "$RC"
expect_file "malformed: x64 actual FAIL" "$WORK/results/malformed/actual-wine-x64.txt" "FAIL unpackaged"
expect_grep "malformed: raw evidence kept" "$WORK/results/malformed/raw-x64.txt" "EXTRA"

# --- T5: nonzero probe exit ---
make_build nonzero
run_case nonzero nonzero "$WORK/prefixes/nonzero" "$WORK/results/nonzero"
expect_rc "nonzero: nonzero overall exit" 1 "$RC"
expect_file "nonzero: x64 actual FAIL" "$WORK/results/nonzero/actual-wine-x64.txt" "FAIL unpackaged"
expect_file "nonzero: x64 exit status" "$WORK/results/nonzero/exit-x64.txt" "1"

# --- T6: timeout ---
make_build timeout
run_case timeout hang "$WORK/prefixes/timeout" "$WORK/results/timeout"
expect_rc "timeout: nonzero overall exit" 1 "$RC"
expect_file "timeout: x64 exit status 124" "$WORK/results/timeout/exit-x64.txt" "124"
expect_file "timeout: x64 actual FAIL" "$WORK/results/timeout/actual-wine-x64.txt" "FAIL unpackaged"

# --- T7: foreign prefix (preexisting state) ---
make_build foreign
mkdir -p "$WORK/prefixes/foreign"
echo foreign > "$WORK/prefixes/foreign/state.txt"
run_case foreign pass "$WORK/prefixes/foreign" "$WORK/results/foreign"
expect_rc "foreign-prefix: refused" 3 "$RC"
expect_grep "foreign-prefix: refusal logged" "$LOG" "REFUSED"
expect_file "foreign-prefix: state retained" "$WORK/prefixes/foreign/state.txt" "foreign"

# --- T8: alias via .. (canonicalized before the refusal checks) ---
make_build alias-dotdot
mkdir -p "$WORK/real/foreign"
echo foreign > "$WORK/real/foreign/state.txt"
run_case alias-dotdot pass "$WORK/real/../real/foreign" "$WORK/results/alias-dotdot"
expect_rc "alias-dotdot: refused after canonicalization" 3 "$RC"
expect_file "alias-dotdot: state retained" "$WORK/real/foreign/state.txt" "foreign"

# --- T9: alias via symlink (as far as the host allows) ---
# On a host without symlink privileges Git Bash emulates the link (text file
# or real directory); the runner must refuse it either way. On Linux a real
# symlink is refused by the symlink check or, after canonicalization, by the
# system-role checks.
make_build alias-symlink
mkdir -p "$WORK/alias-target"
ln -s "$WORK/alias-target" "$WORK/prefixes/alias-symlink"
run_case alias-symlink pass "$WORK/prefixes/alias-symlink" "$WORK/results/alias-symlink"
expect_rc "alias-symlink: refused" 3 "$RC"
expect_grep "alias-symlink: refusal logged" "$LOG" "REFUSED"

# --- T10: held ownership lock ---
make_build lock
mkdir -p "$WORK/prefixes/lock"
echo "pid=99999 time=1970-01-01T00:00:00Z" > "$WORK/prefixes/lock/.wex-u1-lock"
run_case lock pass "$WORK/prefixes/lock" "$WORK/results/lock"
expect_rc "lock: refused" 3 "$RC"
expect_grep "lock: refusal mentions the lock" "$LOG" "ownership lock"
expect_file "lock: lock retained" "$WORK/prefixes/lock/.wex-u1-lock" "pid=99999 time=1970-01-01T00:00:00Z"

# --- T10b: prefix parent missing ---
make_build no-parent
run_case no-parent pass "$WORK/no-such-parent/prefix" "$WORK/results/no-parent"
expect_rc "no-parent: refused" 3 "$RC"
expect_grep "no-parent: refusal logged" "$LOG" "cannot create prefix"

# --- T11: usage ---
make_build usage
LOG="$WORK/logs/usage-0.log"
bash "$FIX/run-wine.sh" > "$LOG" 2>&1
RC=$?
expect_rc "usage: no args" 2 "$RC"
LOG="$WORK/logs/usage-4.log"
bash "$FIX/run-wine.sh" a b c d > "$LOG" 2>&1
RC=$?
expect_rc "usage: four args" 2 "$RC"
expect_grep "usage: exact usage printed" "$LOG" "usage: .* <wine build dir> <prefix> \[results dir\]"

# --- T12: results dir not fresh ---
make_build results-reuse
mkdir -p "$WORK/results/results-reuse"
run_case results-reuse pass "$WORK/prefixes/results-reuse" "$WORK/results/results-reuse"
expect_rc "results-reuse: refused (not fresh)" 2 "$RC"

# --- T12b: results dir parent does not exist ---
make_build results-no-parent
run_case results-no-parent pass "$WORK/prefixes/results-no-parent" "$WORK/no-such-parent/results"
expect_rc "results-no-parent: refused (parent missing)" 2 "$RC"
expect_grep "results-no-parent: error logged" "$LOG" "parent .* does not exist"

# --- T12c: results dir is a symlink ---
make_build results-symlink
mkdir -p "$WORK/results-target"
ln -s "$WORK/results-target" "$WORK/results/symlink-results"
run_case results-symlink pass "$WORK/prefixes/results-symlink" "$WORK/results/symlink-results"
expect_rc "results-symlink: refused (symlink/reparse)" 2 "$RC"

# --- T13: results dir is the caller's current directory ---
make_build results-cwd
run_case results-cwd pass "$WORK/prefixes/results-cwd" "$PWD"
expect_rc "results-cwd: refused" 2 "$RC"

# --- T14: results dir is the fixture directory (tracked oracle files) ---
make_build results-fixdir
run_case results-fixdir pass "$WORK/prefixes/results-fixdir" "$FIX"
expect_rc "results-fixdir: refused (tracked oracle files)" 2 "$RC"

# --- T15: default results dir (fresh temporary, not the cwd) ---
make_build default-results
run_case default-results pass "$WORK/prefixes/default-results"
expect_rc "default-results: overall exit 0" 0 "$RC"
results_dir=$(grep '^results dir: ' "$LOG" | head -1 | sed 's/^results dir: //')
if [ -n "$results_dir" ] && [ -f "$results_dir/summary.txt" ] && [ "$results_dir" != "$PWD" ]; then
  ok "default-results: fresh temp results dir with evidence"
else
  bad "default-results: fresh temp results dir with evidence (got: ${results_dir:-<none>})"
fi
rm -rf "$results_dir"

# --- T16: build dir under the live system-role configuration ---
BUILD="/etc/wineexplorer/build"
run_case build-etc pass "$WORK/prefixes/build-etc" "$WORK/results/build-etc"
expect_rc "build-etc: refused" 3 "$RC"
expect_grep "build-etc: refusal logged" "$LOG" "REFUSED"

# --- T17: missing probe (prerequisite) ---
make_build missing-probe
rm -f "$FIX/build/mingw-x64/probe.exe"
run_case missing-probe pass "$WORK/prefixes/missing-probe" "$WORK/results/missing-probe"
expect_rc "missing-probe: prerequisite error" 2 "$RC"
: > "$FIX/build/mingw-x64/probe.exe"

# --- T18: missing wineserver binary (prerequisite, rejected before mutation) ---
make_build missing-server missing_server
run_case missing-server pass "$WORK/prefixes/missing-server" "$WORK/results/missing-server"
expect_rc "missing-server: exit 2" 2 "$RC"
expect_grep "missing-server: error logged" "$LOG" "matching wineserver binary not found"
if [ ! -e "$WORK/prefixes/missing-server" ]; then
  ok "missing-server: prefix not created before mutation"
else
  bad "missing-server: prefix was created before mutation"
fi

# --- T19: wineserver binary in server/ subdirectory (normal Wine build layout) ---
make_build server-subdir server_subdir
run_case server-subdir pass "$WORK/prefixes/server-subdir" "$WORK/results/server-subdir"
expect_rc "server-subdir: overall exit 0" 0 "$RC"
expect_file "server-subdir: x64 actual" "$WORK/results/server-subdir/actual-wine-x64.txt" "PASS unpackaged"
expect_file "server-subdir: x86 actual" "$WORK/results/server-subdir/actual-wine-x86.txt" "PASS unpackaged"

# --- T20: wineboot failure even after syswow64 marker created ---
make_build boot-fail-marker
run_case boot-fail-marker boot_fail_marker "$WORK/prefixes/boot-fail-marker" "$WORK/results/boot-fail-marker"
expect_rc "boot-fail-marker: exit 2" 2 "$RC"
expect_grep "boot-fail-marker: failure logged" "$LOG" "wineboot failed"
expect_file "boot-fail-marker: marker created" "$WORK/prefixes/boot-fail-marker/drive_c/windows/syswow64/kernel32.dll" ""
if [ -d "$WORK/prefixes/boot-fail-marker" ]; then
  ok "boot-fail-marker: prefix retained for diagnostics"
else
  bad "boot-fail-marker: prefix was removed despite failure"
fi

# --- T21: wineboot timeout with TERM ignored (kill-after required) ---
make_build boot-timeout
run_case boot-timeout boot_hang "$WORK/prefixes/boot-timeout" "$WORK/results/boot-timeout"
expect_rc "boot-timeout: exit 2" 2 "$RC"
expect_grep "boot-timeout: timeout logged" "$LOG" "wineboot timed out"
if [ -d "$WORK/prefixes/boot-timeout" ]; then
  ok "boot-timeout: prefix retained for diagnostics"
else
  bad "boot-timeout: prefix was removed despite timeout"
fi

# --- T22: reg add timeout with TERM ignored (kill-after required) ---
make_build reg-timeout
run_case reg-timeout reg_hang "$WORK/prefixes/reg-timeout" "$WORK/results/reg-timeout"
expect_rc "reg-timeout: exit 2" 2 "$RC"
expect_grep "reg-timeout: timeout logged" "$LOG" "reg add timed out"
if [ -d "$WORK/prefixes/reg-timeout" ]; then
  ok "reg-timeout: prefix retained for diagnostics"
else
  bad "reg-timeout: prefix was removed despite timeout"
fi

# --- T23: probe timeout with TERM ignored (kill-after required) ---
make_build probe-timeout
run_case probe-timeout hang_term_ignored "$WORK/prefixes/probe-timeout" "$WORK/results/probe-timeout"
expect_rc "probe-timeout: nonzero overall exit" 1 "$RC"
expect_grep "probe-timeout: exit status 137" "$WORK/results/probe-timeout/exit-x64.txt" "137"
expect_file "probe-timeout: x64 actual FAIL" "$WORK/results/probe-timeout/actual-wine-x64.txt" "FAIL unpackaged"
if [ -d "$WORK/prefixes/probe-timeout" ]; then
  ok "probe-timeout: prefix retained for diagnostics"
else
  bad "probe-timeout: prefix was removed despite timeout"
fi

# --- T24: cleanup wineserver timeout with TERM ignored (kill-after required) ---
make_build cleanup-timeout
run_case cleanup-timeout pass "$WORK/prefixes/cleanup-timeout" "$WORK/results/cleanup-timeout" cleanup_hang
expect_rc "cleanup-timeout: nonzero overall exit" 1 "$RC"
expect_grep "cleanup-timeout: warning logged" "$LOG" "wineserver cleanup timed out"
expect_grep "cleanup-timeout: failure logged" "$LOG" "cleanup failed"
expect_grep "cleanup-timeout: summary cleanup FAIL" "$WORK/results/cleanup-timeout/summary.txt" "cleanup: FAIL"
expect_grep "cleanup-timeout: summary overall FAIL" "$WORK/results/cleanup-timeout/summary.txt" "overall: FAIL"
if grep -q -- "overall: PASS" "$WORK/results/cleanup-timeout/summary.txt"; then bad "cleanup-timeout: summary falsely contains overall PASS"; else ok "cleanup-timeout: summary does not contain overall PASS"; fi
if [ -d "$WORK/prefixes/cleanup-timeout" ]; then
  ok "cleanup-timeout: prefix retained for diagnostics"
else
  bad "cleanup-timeout: prefix was removed despite cleanup timeout"
fi

# --- T25: cleanup wineserver failure ---
make_build cleanup-fail
run_case cleanup-fail pass "$WORK/prefixes/cleanup-fail" "$WORK/results/cleanup-fail" cleanup_fail
expect_rc "cleanup-fail: nonzero overall exit" 1 "$RC"
expect_grep "cleanup-fail: failure logged" "$LOG" "cleanup failed"
expect_grep "cleanup-fail: summary cleanup FAIL" "$WORK/results/cleanup-fail/summary.txt" "cleanup: FAIL"
expect_grep "cleanup-fail: summary overall FAIL" "$WORK/results/cleanup-fail/summary.txt" "overall: FAIL"
if grep -q -- "overall: PASS" "$WORK/results/cleanup-fail/summary.txt"; then bad "cleanup-fail: summary falsely contains overall PASS"; else ok "cleanup-fail: summary does not contain overall PASS"; fi
if [ -d "$WORK/prefixes/cleanup-fail" ]; then
  ok "cleanup-fail: prefix retained for diagnostics"
else
  bad "cleanup-fail: prefix was removed despite cleanup failure"
fi

# --- T26: changed ownership lock retained (exact token comparison) ---
make_build lock-changed
run_case lock-changed alter_lock "$WORK/prefixes/lock-changed" "$WORK/results/lock-changed"
expect_rc "lock-changed: nonzero overall exit" 1 "$RC"
expect_grep "lock-changed: warning logged" "$LOG" "ownership lock in .* is not ours"
expect_file "lock-changed: tampered lock retained" "$WORK/prefixes/lock-changed/.wex-u1-lock" "foreign-identity-token"
expect_grep "lock-changed: summary cleanup FAIL" "$WORK/results/lock-changed/summary.txt" "cleanup: FAIL"
expect_grep "lock-changed: summary overall FAIL" "$WORK/results/lock-changed/summary.txt" "overall: FAIL"
if grep -q -- "overall: PASS" "$WORK/results/lock-changed/summary.txt"; then bad "lock-changed: summary falsely contains overall PASS"; else ok "lock-changed: summary does not contain overall PASS"; fi
if [ -d "$WORK/prefixes/lock-changed" ]; then
  ok "lock-changed: prefix retained"
else
  bad "lock-changed: prefix was removed despite lost ownership"
fi

# --- T27: invalid timeout bound (positive integer validation) ---
make_build invalid-timeout
LOG="$WORK/logs/invalid-timeout.log"
WEX_U1_PROBE_TIMEOUT="invalid" bash "$FIX/run-wine.sh" "$BUILD" "$WORK/prefixes/invalid-timeout" "$WORK/results/invalid-timeout" > "$LOG" 2>&1
RC=$?
expect_rc "invalid-timeout: exit 2" 2 "$RC"
expect_grep "invalid-timeout: error logged" "$LOG" "must be a positive integer"

# --- T28: zero timeout bound (must be positive) ---
make_build zero-timeout
LOG="$WORK/logs/zero-timeout.log"
WEX_U1_KILL_AFTER="0" bash "$FIX/run-wine.sh" "$BUILD" "$WORK/prefixes/zero-timeout" "$WORK/results/zero-timeout" > "$LOG" 2>&1
RC=$?
expect_rc "zero-timeout: exit 2" 2 "$RC"
expect_grep "zero-timeout: error logged" "$LOG" "must be a positive integer"

# --- T29: cleanup wineserver wait failure (-k exit 0, -w exit 1) ---
make_build wait-fail-1
run_case wait-fail-1 pass "$WORK/prefixes/wait-fail-1" "$WORK/results/wait-fail-1" wait_fail_1
expect_rc "wait-fail-1: nonzero overall exit" 1 "$RC"
expect_grep "wait-fail-1: wait failure logged" "$LOG" "cleanup wait failed"
expect_grep "wait-fail-1: cleanup failure logged" "$LOG" "cleanup failed"
expect_grep "wait-fail-1: summary cleanup FAIL" "$WORK/results/wait-fail-1/summary.txt" "cleanup: FAIL"
expect_grep "wait-fail-1: summary overall FAIL" "$WORK/results/wait-fail-1/summary.txt" "overall: FAIL"
if grep -q -- "overall: PASS" "$WORK/results/wait-fail-1/summary.txt"; then bad "wait-fail-1: summary falsely contains overall PASS"; else ok "wait-fail-1: summary does not contain overall PASS"; fi
if [ -d "$WORK/prefixes/wait-fail-1" ]; then
  ok "wait-fail-1: prefix retained for diagnostics"
else
  bad "wait-fail-1: prefix was removed despite wait failure"
fi
if [ -f "$WORK/prefixes/wait-fail-1/.wex-u1-lock" ]; then
  ok "wait-fail-1: lock retained"
else
  bad "wait-fail-1: lock was removed despite wait failure"
fi

# --- T30: wineserver binary disappearing after probe (missing during cleanup) ---
make_build server-disappear
run_case server-disappear disappear_server "$WORK/prefixes/server-disappear" "$WORK/results/server-disappear"
expect_rc "server-disappear: nonzero overall exit" 1 "$RC"
expect_grep "server-disappear: missing server logged" "$LOG" "wineserver binary missing or not executable during cleanup"
expect_grep "server-disappear: cleanup failure logged" "$LOG" "cleanup failed"
expect_grep "server-disappear: summary cleanup FAIL" "$WORK/results/server-disappear/summary.txt" "cleanup: FAIL"
expect_grep "server-disappear: summary overall FAIL" "$WORK/results/server-disappear/summary.txt" "overall: FAIL"
if grep -q -- "overall: PASS" "$WORK/results/server-disappear/summary.txt"; then bad "server-disappear: summary falsely contains overall PASS"; else ok "server-disappear: summary does not contain overall PASS"; fi
if [ -d "$WORK/prefixes/server-disappear" ]; then
  ok "server-disappear: prefix retained for diagnostics"
else
  bad "server-disappear: prefix was removed despite missing wineserver"
fi
if [ -f "$WORK/prefixes/server-disappear/.wex-u1-lock" ]; then
  ok "server-disappear: lock retained"
else
  bad "server-disappear: lock was removed despite missing wineserver"
fi

# --- T31: wineserver -k exit 1 (no server lock owner), -w exit 0 passes ---
make_build k-exit1-pass
run_case k-exit1-pass pass "$WORK/prefixes/k-exit1-pass" "$WORK/results/k-exit1-pass" k_exit1_pass
expect_rc "k-exit1-pass: overall exit 0" 0 "$RC"
expect_grep "k-exit1-pass: summary cleanup PASS" "$WORK/results/k-exit1-pass/summary.txt" "cleanup: PASS"
expect_grep "k-exit1-pass: summary overall PASS" "$WORK/results/k-exit1-pass/summary.txt" "overall: PASS"
if grep -q -- "overall: FAIL" "$WORK/results/k-exit1-pass/summary.txt"; then bad "k-exit1-pass: summary falsely contains overall FAIL"; else ok "k-exit1-pass: summary does not contain overall FAIL"; fi
if [ ! -e "$WORK/prefixes/k-exit1-pass" ]; then
  ok "k-exit1-pass: prefix removed on success"
else
  bad "k-exit1-pass: prefix retained despite clean exit"
fi

# --- T32: ownership lock changed during wineserver -w (late ownership loss) ---
make_build lock-changed-w
run_case lock-changed-w pass "$WORK/prefixes/lock-changed-w" "$WORK/results/lock-changed-w" change_lock_w
expect_rc "lock-changed-w: nonzero overall exit" 1 "$RC"
expect_grep "lock-changed-w: warning logged" "$LOG" "ownership lock in .* is not ours"
expect_file "lock-changed-w: tampered lock retained" "$WORK/prefixes/lock-changed-w/.wex-u1-lock" "foreign-identity-token"
expect_grep "lock-changed-w: summary cleanup FAIL" "$WORK/results/lock-changed-w/summary.txt" "cleanup: FAIL"
expect_grep "lock-changed-w: summary overall FAIL" "$WORK/results/lock-changed-w/summary.txt" "overall: FAIL"
if grep -q -- "overall: PASS" "$WORK/results/lock-changed-w/summary.txt"; then bad "lock-changed-w: summary falsely contains overall PASS"; else ok "lock-changed-w: summary does not contain overall PASS"; fi
if [ -d "$WORK/prefixes/lock-changed-w" ]; then
  ok "lock-changed-w: prefix retained"
else
  bad "lock-changed-w: prefix was removed despite lost ownership"
fi

# --- T33: prefix removed before cleanup (unexpectedly missing prefix) ---
make_build prefix-removed
run_case prefix-removed remove_prefix "$WORK/prefixes/prefix-removed" "$WORK/results/prefix-removed"
expect_rc "prefix-removed: nonzero overall exit" 1 "$RC"
expect_grep "prefix-removed: missing prefix logged" "$LOG" "cleanup failed: prefix .* is missing"
expect_grep "prefix-removed: summary cleanup FAIL" "$WORK/results/prefix-removed/summary.txt" "cleanup: FAIL"
expect_grep "prefix-removed: summary overall FAIL" "$WORK/results/prefix-removed/summary.txt" "overall: FAIL"
if grep -q -- "overall: PASS" "$WORK/results/prefix-removed/summary.txt"; then bad "prefix-removed: summary falsely contains overall PASS"; else ok "prefix-removed: summary does not contain overall PASS"; fi
if [ ! -e "$WORK/prefixes/prefix-removed" ]; then
  ok "prefix-removed: prefix remains absent"
else
  bad "prefix-removed: prefix unexpectedly present"
fi

echo
echo "checks passed: $pass_count, failed: $fail_count"
if [ "$fail_count" -gt 0 ]; then
  echo "RESULT: FAIL"
  exit 1
fi
echo "RESULT: PASS"
exit 0
