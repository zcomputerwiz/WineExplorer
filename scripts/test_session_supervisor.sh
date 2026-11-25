#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# The session's shell supervisor (wex_run_shell in wine-explorer-session-common.sh, Winlogon's AutoRestartShell) with a
# fake shell: no Wine needed. The session ends on purpose when wineboot has left the marker file (programs/wineboot).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FIXTURE=$(mktemp -d)
trap 'rm -rf -- "$FIXTURE"' EXIT
export HOME="$FIXTURE/home"
export WINEPREFIX="$HOME/.wine"
export WEX_SKEL_PFX="$FIXTURE/skel/.wine"
mkdir -p "$HOME" "$WINEPREFIX/drive_c/windows/temp"
. "$SCRIPT_DIR/wine-explorer-session-common.sh"
export WEX_SHELL_RESTART_DELAY=0 WEX_SHELL_WAIT=0.1
MARKER="$(wex_session_ending_marker)"

fail() { echo "FAIL: $*" >&2; exit 1; }

# a fake pgrep: another shell runs while the file $FIXTURE/other exists
cat > "$FIXTURE/pgrep" <<'SH'
#!/bin/bash
[ -e "$(dirname "$0")/other" ]
SH
chmod +x "$FIXTURE/pgrep"
export WEX_SHELL_PGREP="$FIXTURE/pgrep"
# the fake shell counts its starts and ends with the status in $FIXTURE/status (default 0)
cat > "$FIXTURE/shell" <<'SH'
#!/bin/bash
d=$(dirname "$0")
echo run >> "$d/runs"
exit "$(cat "$d/status" 2>/dev/null || echo 0)"
SH
chmod +x "$FIXTURE/shell"
runs() { if [ -e "$FIXTURE/runs" ]; then wc -l < "$FIXTURE/runs"; else echo 0; fi; }
reset() { rm -f "$FIXTURE/runs" "$FIXTURE/other" "$FIXTURE/status" "$MARKER" "$FIXTURE/out"; }
winlogon_reg() {   # winlogon_reg LINE: a system.reg whose Winlogon key has LINE
    printf 'WINE REGISTRY Version 2\n\n[Software\\\\Microsoft\\\\Windows NT\\\\CurrentVersion\\\\Winlogon] 1\n%s\n' "$1" > "$WINEPREFIX/system.reg"
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

# the shell ends with the marker (Log Off): the session ends, with the shell's status, no restart
reset; echo 3 > "$FIXTURE/status"
cat > "$FIXTURE/shell_logoff" <<SH
#!/bin/bash
d=\$(dirname "\$0")
echo run >> "\$d/runs"
touch "$MARKER"
exit 3
SH
chmod +x "$FIXTURE/shell_logoff"
rc=0; wex_run_shell "$FIXTURE/shell_logoff" > "$FIXTURE/out" 2>&1 || rc=$?
[ "$rc" = 3 ] || fail "with the session: status $rc, not 3"
[ "$(runs)" = 1 ] || fail "with the session: $(runs) starts, not 1"
echo 'ends_with_the_session=PASS'

# a stale marker from an earlier session is removed at the start: a crash is then restarted
reset; touch "$MARKER"
cat > "$FIXTURE/shell_once" <<SH
#!/bin/bash
d=\$(dirname "\$0")
echo run >> "\$d/runs"
[ "\$(wc -l < "\$d/runs")" -ge 2 ] && touch "$MARKER"
exit 0
SH
chmod +x "$FIXTURE/shell_once"
rc=0; wex_run_shell "$FIXTURE/shell_once" > "$FIXTURE/out" 2>&1 || rc=$?
[ "$(runs)" = 2 ] || fail "one crash after a stale marker: $(runs) starts, not 2"
echo 'restarts_once=PASS'

# the shell ends and no marker comes (a crash, status 0 as after a kill -9): started again, until the loop guard
reset
rc=0; wex_run_shell "$FIXTURE/shell" > "$FIXTURE/out" 2>&1 || rc=$?
[ "$(runs)" = 4 ] || fail "crash loop: $(runs) starts, not 4 (1 + 3 restarts)"
grep -q 'not starting it again' "$FIXTURE/out" || fail 'crash loop: no log line for the guard'
echo 'restarts_with_loop_guard=PASS'

# another shell of the account runs in the gap: no restart while it runs, one after it ends
reset; touch "$FIXTURE/other"
( sleep 1; rm -f "$FIXTURE/other"; touch "$MARKER.later" ) &
cat > "$FIXTURE/shell_gap" <<SH
#!/bin/bash
d=\$(dirname "\$0")
echo run >> "\$d/runs"
[ "\$(wc -l < "\$d/runs")" -ge 2 ] && touch "$MARKER"
exit 0
SH
chmod +x "$FIXTURE/shell_gap"
start=$SECONDS
rc=0; wex_run_shell "$FIXTURE/shell_gap" > "$FIXTURE/out" 2>&1 || rc=$?
wait
[ "$(runs)" = 2 ] || fail "other shell: $(runs) starts, not 2"
[ $((SECONDS - start)) -ge 1 ] || fail 'other shell: restarted before the other shell ended'
rm -f "$MARKER.later"
echo 'waits_for_another_shell=PASS'

# AutoRestartShell=0: no restart
reset; winlogon_reg '"AutoRestartShell"=dword:00000000'
wex_run_shell "$FIXTURE/shell" > "$FIXTURE/out" 2>&1 || true
[ "$(runs)" = 1 ] || fail "AutoRestartShell 0: $(runs) starts, not 1"
echo 'auto_restart_off=PASS'
winlogon_reg '"AutoRestartShell"=dword:00000001'

# a TERM to the session ends the shell and the session, with no restart
reset
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
