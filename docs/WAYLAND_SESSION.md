<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Installed Wayland session

In progress, 2026-10-16. This follows `WAYLAND_SESSION_WORKORDER.md`.
Parts A and B lead to review point 1; tray integration and final regressions
follow that review. No acceptance is claimed yet.

## Part A

Linux desktop-file launches now copy the compositor's normal Wayland socket
and XWayland display into their GIO launch context. They remove both `WINE*`
and shell `WEX_*` variables. The endpoints come from the session inventory,
so launching the first native application does not require an existing window.
X11 launches retain their original display environment. Both PE architectures
and the Unix wexhost library compile in the combined WoW64 build.
The complete combined WoW64 build also passes with master's exact production
Wine pin, d92d7e44, including these changes. The launch change does not require
any property from the unmerged L2 Wine topic.

Pending: real application launches on the installed VM; CSD taskbar integration;
hosted sizing/maximize; output-scale and two-monitor checks.

## Test environment and command failures

The permitted Debian WSL distribution runs our QEMU/KVM tests. The stopped
`p4-submodule` installed disk is the read-only backing file for a new
`/var/tmp/wineexplorer-vmtest/wayland-session/disk.qcow2` overlay. Gemini's
`wexbuild` and `wexcomp` distributions are untouched. The Proton build VM
continues to supply the combined WoW64 build and compositor build.

Read-only discovery failures: `rg ... src/programs/explorer` and reading
`src/dlls/wexhost/process.c` failed because the shell is in `src/dlls/wexshell`
and desktop-file launching is in `apps.c`. Reading
`installer/scripts/package-wine.sh` likewise failed; it is
`installer/package-wine.sh`. PowerShell's literal `src/wex*` argument to rg
failed with Windows error 123; use `rg -g` instead.

The Proton build VM has neither `/dev/kvm` nor QEMU; Debian WSL has both.
`command -v live-build` and `command -v lb` found no ISO builder in Debian WSL.
Serial login on the older installed overlay returned no command output;
the image predates the current installer test marker and needs its serial
getty checked. The test password matches the prior P4 runner's saved file.
`vmtest.py shutdown wayland-session --timeout 50` reported
`guest did not shut down cleanly; disk inspection must not proceed` while
systemd was still waiting for the greeter's session-c1 scope. Do not inspect
or mount its disk until QEMU has actually exited.
QEMU subsequently exited normally; a screenshot attempted after its exit failed
with `FileNotFoundError` for `qmp.sock`, confirming that the socket was gone.
