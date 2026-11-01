<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# G4 owned VirGL launcher session procedure

Prepared for review only; **not executed and no VM was booted**. This replaces
the prior Ctrl-C-on-exec-session shutdown attempt. It preserves the launcher's
900-second bound and QMP `system_powerdown` cleanup while separating launch,
consumer operation, and verified stop.

## Start and retain the host session

Choose a never-used safe run name. In a persistent PowerShell/exec session run:

```powershell
pwsh -NoProfile -File D:/DeepThought/Projects/WineExplorer/scripts/g4-virgl-launcher-session.ps1 -RunName recovery-r1-fixed-timeline-20261022
```

The controller uses `Start-Process wsl.exe -WindowStyle Hidden`, with distinct
stdout/stderr files and a per-run Windows session JSON under
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/`. The JSON records WSL PID,
Windows start time, exact arguments, run directory, log paths and final WSL
exit code. The controller remains alive in the owned execution session and
waits for WSL; do not interrupt that execution or close its host session.

In WSL, `launch.py` creates the unique output directory and
`launcher-identity.json` before starting QEMU. That record has the Linux
launcher PID, `/proc` start ticks, exact command line and launcher path, exact
output/identity paths, plus QEMU and Xvfb PID/start-tick/cmdline identities once
started. The launcher writes final `results.json` atomically from its existing
`finally`, including stop reason, QMP graceful-powerdown outcome, QEMU exit
status and child cleanup results.

## Stop only the recorded launcher

After the bounded consumer has exited, use a separate WSL command (not Ctrl-C
on the controller or launcher exec session):

```powershell
wsl -d Debian --exec python3 -B /mnt/d/DeepThought/Projects/WineExplorer/scripts/stop_g4_virgl_launcher.py --identity /mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/recovery-r1-fixed-timeline-20261022/launcher-identity.json --timeout 60
```

The stopper checks the recorded run directory and identity-file path, exact
launcher PID/start ticks/cmdline/path, and both child PID/start ticks before it
sends SIGINT to that launcher PID. It then waits up to 60 seconds for the
atomic final result, requires `stop_reason=signal`,
`graceful_powerdown=true`, and both child cleanup flags, and confirms the
recorded QEMU/Xvfb identities have exited. Any mismatch or timeout is a hard
failure; it does not signal another PID or fall back to termination. After the
stopper succeeds, wait for the retained PowerShell controller to exit and read
its session JSON for the WSL process exit code. Preserve both Windows logs,
session JSON, Linux identity and final results JSON in the D: run folder.

## Validation and remaining review

The session and stop helpers are prepared for static/unit review. They have not
been used to start or stop a VM. The next launch remains blocked on parent
review of these scripts and the renderer-wait change. A successful stopper is
required evidence; process absence or `qemu-img check` alone is not sufficient.
