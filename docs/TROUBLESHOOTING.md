# Troubleshooting (VM / session)

## 1. New Wine clients hang after `pkill wineserver`
**Symptom:** after killing the wineserver, any `wine ...` started from ssh prints nothing and never returns
(`strace` shows the process in `read()` on an internal wait pipe); `explorer` may be stuck too.
**Cause:** `pkill wineserver` kills the server but leaves its child processes alive (`services.exe`, `rpcss.exe`,
`winedevice.exe`, `explorer.exe`, `wineboot.exe`, ...). A new server and the orphans fight over the same endpoints
and named objects.
**Fix:** `scripts/vm.sh clean` (runs `scripts/vm-clean-wine.sh` on the VM): `wineserver -k`, then kill every process whose
command line contains `windows\system32` and any wineserver. `scripts/vm.sh restart-shell` does that and restarts GDM.
Never use `pkill -f <pattern>` from an ssh one-liner that itself contains the pattern: it kills its own shell
(exit 255); put the commands in a script file on the VM.

## 2. Cold start: the first session after a server restart dies
**Symptom:** GDM autologin shows a black screen (or "Wine configuration is being updated") and then drops back to the
login screen; `wine-session.log` ends at "Starting direct". The second attempt works.
**Cause:** the old session script ran `wine reg query ...` to read `Winlogon\Shell` *before* starting explorer. That
first Wine client starts wineserver, `wineboot --init` (prefix update) and a bare `explorer.exe /desktop` as side
effects, so our `explorer.exe /desktop=Default,WxH` found a desktop manager already running and exited, which ended
the session. Leftover processes from section 1 made it worse.
**Fix:** the session script (acting as Winlogon) now reads the `Shell` value straight out of `$WINEPREFIX/system.reg`
(no Wine process) and its first Wine process is `explorer.exe /desktop=Default,WxH /shell`. `/shell` is a new
explorer switch that turns the Windows-style shell on for that desktop. Verified: clean + restart gives a working
desktop on the first try.
If it ever recurs: `scripts/vm.sh clean`, then `scripts/vm.sh restart-shell`, and read `~/.wine-session.log`.

### 2b. First start after `make install` (prefix update) ends the session
After a fresh install Wine updates the prefix on the next start (`.update-timestamp` older than `ntdll.so`).
Run as a side effect of starting explorer, the update's helper (`wine: failed to start syswow64undll32.exe: c0000135`,
this build has no 32-bit part) took the session down. The session script now runs `wineboot.exe -u` up front when the
prefix is stale, kills that wineserver, and then starts explorer cleanly. The log is `~/.wine-session.log`
(previous run: `.1`) and only has errors (`WINEDEBUG=-all,err+all`; set `WINEDEBUG` to override).

## 3. A stale VM clock stops rebuilds
The guest clock once ran months ahead (VirtualBox time sync is disabled on purpose). Files created with future
timestamps (`*.res`, objects) are "newer" than their sources, so `make` skips them. Fix: set the clock
(`sudo date -u -s ...`) and `find ~/build ~/src -newermt now -exec touch {} +`. `vm.sh sync` extracts with `tar -m`
so synced sources always get the current time.

## 4. Volume/PipeWire only works with a seat
With GDM stopped there is no logged-in seat and PipeWire falls back to the "Dummy Output" sink; volume calls then
succeed but change nothing visible. Test audio inside the graphical session.

## 5. polkit prompts need the right login session
polkit decides by the *caller's* login session. A Wine process started from an ssh command belongs to the ssh
session (no agent) and is refused; start test programs from the shell (tray, Start menu) so they run in the
graphical session where explorer's agent is registered.

## 6. Editing from Windows
The tool environment strips backslashes in some heredocs and Python writes CRLF. Use the editor tools for strings with
`\n` or `\\`, and run `scripts/fix-eol.sh` before syncing (`vm.sh sync` does it).
