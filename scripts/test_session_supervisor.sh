#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# The session's shell supervisor (wex_run_shell in wine-explorer-session-common.sh, Winlogon's AutoRestartShell) with a
# fake shell and a fake wineserver: no Wine needed.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIXTURE=$(mktemp -d)
trap 'rm -rf -- "$FIXTURE"' EXIT
export HOME="$FIXTURE/home"
export WINEPREFIX="$HOME/.wine"
export WEX_SKEL_PFX="$FIXTURE/skel/.wine"
mkdir -p "$HOME" "$WINEPREFIX"
. "$SCRIPT_DIR/wine-explorer-session-common.sh"
export WEX_SHELL_RESTART_DELAY=0 WEX_SHELL_SERVER_WAIT=3

fail() { echo "FAIL: $*" >&2; exit 1; }

# the fake wineserver answers -k0 like the real one: 0 when the file $FIXTURE/server exists
cat > "$FIXTURE/wineserver" <<'SH'
#!/bin/bash
[ -e "$(dirname "$0")/server" ]
SH
chmod +x "$FIXTURE/wineserver"
export WEX_WINESERVER="$FIXTURE/wineserver"
# the fake shell counts its starts and ends with the status in $FIXTURE/status (default 0)
cat > "$FIXTURE/shell" <<'SH'
#!/bin/bash
d=$(dirname "$0")
echo run >> "$d/runs"
exit "$(cat "$d/status" 2>/dev/null || echo 0)"
SH
chmod +x "$FIXTURE/shell"
runs() { if [ -e "$FIXTURE/runs" ]; then wc -l < "$FIXTURE/runs"; else echo 0; fi; }
reset() { rm -f "$FIXTURE/runs" "$FIXTURE/server" "$FIXTURE/status"; }
winlogon_reg() {   # winlogon_reg LINE: a system.reg whose Winlogon key has LINE
    printf 'WINE REGISTRY Version 2\n\n[Software\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon] 1\n%s\n' "$1" > "$WINEPREFIX/system.reg"
}

# AutoRestartShell: 1 when absent, the DWORD otherwise
rm -f "$WINEPREFIX/system.reg"
[ "$(wex_auto_restart_shell)" = 1 ] || fail 'no system.reg: AutoRestartShell is not 1'
winlogon_reg '"Shell"="explorer.exe"'
[ "$(wex_auto_restart_shell)" = 1 ] || fail 'no value: AutoRestartShell is not 1'
winlogon_reg '"AutoRestartShell"=dword:00000000'
[ "$(wex_auto_restart_shell)" = 0 ] || fail 'dword 0: AutoRestartShell is not 0'
winlogon_reg '"AutoRestartShell"=dword:00000001'
[ "$(wex_auto_restart_shell)" = 1 ] || fail 'dword 1: AutoRestartShell is not 1'
echo 'auto_restart_shell_value=PASS'

# the shell ends and the wineserver is gone (Log Off): the session ends, with the shell's status, no restart
reset; echo 3 > "$FIXTURE/status"
rc=0; wex_run_shell "$FIXTURE/shell" > "$FIXTURE/out" 2>&1 || rc=$?
[ "$rc" = 3 ] || fail "with the session: status $rc, not 3"
[ "$(runs)" = 1 ] || fail "with the session: $(runs) starts, not 1"
echo 'ends_with_the_session=PASS'

# the shell ends and the wineserver stays (a crash): started again, until the crash loop guard stops it
reset; touch "$FIXTURE/server"
rc=0; wex_run_shell "$FIXTURE/shell" > "$FIXTURE/out" 2>&1 || rc=$?
[ "$(runs)" = 4 ] || fail "crash loop: $(runs) starts, not 4 (1 + 3 restarts)"
grep -q 'not starting it again' "$FIXTURE/out" || fail 'crash loop: no log line for the guard'
echo 'restarts_with_loop_guard=PASS'

# a shell that crashes once and then ends with the session: one restart
reset; touch "$FIXTURE/server"
cat > "$FIXTURE/shell2" <<'SH'
#!/bin/bash
d=$(dirname "$0")
echo run >> "$d/runs"
[ "$(wc -l < "$d/runs")" -ge 2 ] && rm -f "$d/server"
exit 0
SH
chmod +x "$FIXTURE/shell2"
rc=0; wex_run_shell "$FIXTURE/shell2" > "$FIXTURE/out" 2>&1 || rc=$?
[ "$(runs)" = 2 ] || fail "one crash: $(runs) starts, not 2"
echo 'restarts_once=PASS'

# AutoRestartShell=0: no restart although the server stays
reset; touch "$FIXTURE/server"; winlogon_reg '"AutoRestartShell"=dword:00000000'
wex_run_shell "$FIXTURE/shell" > "$FIXTURE/out" 2>&1 || true
[ "$(runs)" = 1 ] || fail "AutoRestartShell 0: $(runs) starts, not 1"
echo 'auto_restart_off=PASS'
winlogon_reg '"AutoRestartShell"=dword:00000001'

# a TERM to the session ends the shell and the session, with no restart
reset; touch "$FIXTURE/server"
cat > "$FIXTURE/shell3" <<'SH'
#!/bin/bash
echo run >> "$(dirname "$0")/runs"
exec sleep 30
SH
chmod +x "$FIXTURE/shell3"
( wex_run_shell "$FIXTURE/shell3" > "$FIXTURE/out" 2>&1; echo $? > "$FIXTURE/rc" ) &
SUP=$!
for _ in $(seq 50); do [ "$(runs)" -ge 1 ] && break; sleep 0.1; done
sleep 0.3
kill -TERM "$SUP"
for _ in $(seq 50); do kill -0 "$SUP" 2>/dev/null || break; sleep 0.1; done
! kill -0 "$SUP" 2>/dev/null || { kill -9 "$SUP"; fail 'TERM did not end the session'; }
[ "$(runs)" = 1 ] || fail "TERM: $(runs) starts, not 1"
echo 'term_ends_the_session=PASS'
echo 'OK: session shell supervisor'
