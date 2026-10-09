#!/bin/bash
# Run inside the existing graphical session. Each case gets an isolated profile.
set -euo pipefail
LAB="${WEX_WEBVIEW2_LAB:-$HOME/webview2-lab}"
CASE="${1:-baseline}"
[[ "$CASE" =~ ^[a-zA-Z0-9_-]+$ ]] || { echo "Invalid case name" >&2; exit 2; }
mkdir -p "$LAB/runs"
RUN=$(mktemp -d "$LAB/runs/$(date -u +%Y%m%dT%H%M%SZ)-$CASE-XXXXXX")
mkdir "$RUN/profile"
RUNTIME=$(cat "$LAB/runtime-path.txt")
export WEBVIEW2_BROWSER_EXECUTABLE_FOLDER=$(winepath -w "$RUNTIME")
export WEX_WEBVIEW2_PROFILE=$(winepath -w "$RUN/profile")
export WEX_WEBVIEW2_LOG=$(winepath -w "$RUN/host.log")
export WEX_WEBVIEW2_SECONDS="${WEX_WEBVIEW2_SECONDS:-20}"
[[ "$WEX_WEBVIEW2_SECONDS" =~ ^[0-9]+$ ]] && ((WEX_WEBVIEW2_SECONDS >= 5 && WEX_WEBVIEW2_SECONDS <= 300)) || {
    echo "WEX_WEBVIEW2_SECONDS must be between 5 and 300" >&2; exit 2;
}
export CHROME_LOG_FILE=$(winepath -w "$RUN/chromium.log")
export WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS="--enable-logging --v=1 --log-file=\"$CHROME_LOG_FILE\""
case "$CASE" in
baseline) ;;
software) WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS+=" --disable-gpu" ;;
swiftshader) WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS+=" --disable-gpu --enable-unsafe-swiftshader --use-angle=swiftshader" ;;
*) ;;
esac
WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS+=" ${WEX_WEBVIEW2_BROWSER_ARGS:-}"
export WINEDEBUG="${WEX_WEBVIEW2_WINEDEBUG:-+timestamp,+pid,+tid,err+all,warn+all,fixme+all}"
cp "$LAB/tooling.json" "$RUN/tooling.json"
printf 'DISPLAY=%s\nWINEPREFIX=%s\nCASE=%s\nARGS=%s\n' "${DISPLAY:-}" "${WINEPREFIX:-$HOME/.wine}" \
    "$CASE" "$WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS" > "$RUN/session.txt"
wine --version >> "$RUN/session.txt"
echo "Logs: $RUN"
timeout --signal=TERM --kill-after=5s "$((WEX_WEBVIEW2_SECONDS + 30))" \
    wine "$LAB/webview2-probe.exe" > "$RUN/wine.log" 2>&1 &
PROBE_PID=$!
# Actual argv may differ from requested flags. Capture only this run's runtime
# processes, selected by the unique profile directory in their command line.
python3 - "$RUN" "$PROBE_PID" "$WEX_WEBVIEW2_PROFILE" <<'PY' &
import json, pathlib, sys, time
run, pid, profile = pathlib.Path(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
seen = set()
with (run / 'processes.jsonl').open('w') as log:
    while pathlib.Path(f'/proc/{pid}').exists():
        for proc in pathlib.Path('/proc').glob('[0-9]*'):
            try:
                args = proc.joinpath('cmdline').read_bytes().replace(b'\0', b' ').decode(errors='replace')
                if 'msedgewebview2' not in args.lower() or profile not in args: continue
                identity = (proc.name, args)
                if identity not in seen:
                    seen.add(identity)
                    log.write(json.dumps({'time': time.time(), 'pid': int(proc.name), 'argv': args}) + '\n')
                    log.flush()
            except (OSError, ProcessLookupError): pass
        time.sleep(.25)
PY
MONITOR_PID=$!
RESULT=0
wait "$PROBE_PID" || RESULT=$?
wait "$MONITOR_PID" || true
printf '%s\n' "$RESULT" > "$RUN/exit-code.txt"
tail -8 "$RUN/host.log" 2>/dev/null || true
echo "Exit $RESULT; logs preserved in $RUN"
ASSESSMENT=0
python3 "$(dirname "$0")/analyze-webview2-run.py" "$RUN" || ASSESSMENT=$?
if (( RESULT == 0 )); then RESULT=$ASSESSMENT; fi
printf '%s\n' "$RESULT" > "$RUN/assessment-exit-code.txt"
exit "$RESULT"
