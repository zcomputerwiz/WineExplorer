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

The installed overlay now runs Bob's actual LightDM Wayland session, selected
through the Welcome screen's new session list. X11 is the initial choice.
Gedit keeps its header bar and has one taskbar button; physical typing and
taskbar minimize work. Foot physical edge grow/shrink and corner grow work,
followed by successful typing. This uses Wine L2 topic 0607ece plus the pending
captured-motion fix, compositor eca5e619, and the current owned source. These
are incremental checks, not the complete part A acceptance.

The native-input failure was configuration: suppressing labwc defaults also
removed client click-to-focus. Explicit Client Focus/Raise bindings restore
normal native activation without enabling labwc's shell shortcuts. Foot's
outward sizing failure was Wine clamping captured motion to the old frame;
the pending Wine fix skips that clamp only during explicit compositor capture.
The existing window-control tool's z-order readback now includes geometry and
hosting/focus properties to diagnose this real foot failure; no synthetic
client cases were added.

Production Wine remains d92d7e44. Sizing controls are guarded at that pin;
maximize remains disabled in both builds pending its integration. Remaining:
Start launch acceptance, CSD restore/maximize, hosted maximize, the other real
program classes, output-scale and two-monitor checks.

## Part B

The separate Wayland launcher shares an environment helper with X11, retains
PAM's runtime directory, and starts the private-socket shell without a virtual
desktop. Its Wine driver list permits both backends, so the next X11 login is
not left with a Wayland-only prefix. It lets Wine perform its normal prefix
update rather than forcing a full reinstall on every login.

The new `wexcomp` submodule pins eca5e619. `installer/package-wexcomp.sh` builds
that source in its own Meson directory, stages only the compositor executable,
records its source revision and license notices, and calculates Debian shared
library dependencies. Its initial combined source build and package generation
passed on the Proton build VM. ISO integration and a fresh install test remain
pending; the overlay uses staged packages/files for development.

Screenshots currently in the ignored `build/wayland-session` directory:
`greeter-choice.png`, `bob-wayland.png`, `input.png` (gedit header bar/taskbar),
`foot-resized.png`. Review point 1 requires final evidence from the new ISO,
not just these development captures.

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

The first NBD mount ran before `/dev/nbd7p1` appeared and failed with
`Can't lookup blockdev`; dependent discovery commands also failed because it
was unmounted. The NBD device was disconnected, then reconnected with
`udevadm settle` and `blockdev --rereadpt`. The overlay's serial getty was
enabled offline. Its disk and ext4 partition were expanded to 40 GiB while
stopped (`sfdisk`, `e2fsck -pf`, `resize2fs`); the backing disk is unchanged.
The subsequent serial session works. A redundant poweroff command raced guest
shutdown and returned `BrokenPipeError`; no disk was mounted while running.

The first package command failed because its explicit TMPDIR did not exist;
creating `/srv/protonexplorer/tmp` fixed it. A staging tar named `COPYING`, but
the compositor's license is `LICENSE`; the production package uses `LICENSE`
and retains libsfdo's notice when linked from the Meson fallback.
`dpkg-shlibdeps` initially warned that the executable was outside its staged
package; it now analyzes the staged executable. A first HTTP server background
launch did not persist; the active foreground server serves the ignored staging
directory on Debian WSL loopback port 18765 (guest address 10.0.2.2).

The first foot launch inherited `/root` as its working directory and failed;
setting HOME and `--working-directory=/home/bobsmith` fixed it. Gedit warns about
missing optional Peas typelibs under `--no-install-recommends`; its editor still
runs. The test VM has no sound device, producing ALSA warnings. The compositor
reports missing accelerated EGL for its standard VGA; its fallback draws the
desktop. Missing xdg_toplevel_icon_manager_v1 is logged by Wine.

One foot typing wait failed before the Client focus bindings were restored;
the same real typing check passes afterward. A config reload recentered the
taskbar at y=370; a fresh session puts it back at y=770. Output-change behavior
still needs examination in the required scale/multi-output checks.

`make dlls/winewayland.drv/winewayland.drv.so` failed because the Unix target is
`winewayland.so`; rebuilding the correct target passed. The following scp of
the nonexistent output also failed and was repeated after the successful build.
`bash -n scripts/vmtest.py` was an interpreter mistake and reported Python syntax
as a shell error; `python3 -m py_compile` passes. Other read-only path mistakes:
`src/programs/wexgreeter`, `src/programs/welcomelogon`,
`src/dlls/wexshell/taskbar.c`, `src/dlls/wexshell/traywnd.c`, and
`wexcomp/src/session.c` do not exist. The actual files are `programs/logonui`,
`wexshell/systray.c`, and `wexcomp/src/config/session.c`. A repeated literal
`winewayland.drv/*.c` rg argument produced Windows error 123; `-g '*.c'` works.

State: Debian WSL and our `wayland-session` QEMU overlay are running. Bob is in
Wayland, with foot open. NBD7 is disconnected. The loopback HTTP staging server
is running. The Proton build VM's assembled source contains current owned edits
and the Wine L2 topic plus captured-motion fix; its compositor packaging uses a
separate clean source checkout. No source or guest session in Gemini's two WSL
distributions was used.
