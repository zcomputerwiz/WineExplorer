<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Fresh-ISO polish regression, 2026-10-17

Status: running; not yet ready for the final review.

The clean image contains owned source **de35f152**, Wine **ed5fe96**, and
compositor **acc46fe6**. No hot-staged runtime modules are used for these
acceptance installations. The combined i386/x86_64 Wine build and compositor
build pass. [Raw Wine build](logs/pinned-wow64-build.log),
[raw clean ISO build](logs/fresh-iso-build.log).

- Image: `/srv/protonexplorer/wex-wayland-l1/wineexplorer-polish.iso`.
- Copied to Debian WSL: `/var/tmp/wineexplorer-vmtest/polish-20261017.iso`.
- Size: **1,676,251,136 bytes**.
- SHA256 on both copies:
  `3b1d8967532d9f7d1cde89e30a9f59db838e9791e5844a53e77c712c697367f0`.
- New blank test disks: `polish-20261017-x11` and `polish-20261017-wayland`.
  The older acceptance disks remain preserved.

| Check | Result and evidence |
|---|---|
| Default X11 install, account/prefix isolation, 32/64-bit Windows programs and theme listing | PASS: [raw install log](logs/fresh-x11-install.log) |
| X11 volumeicon/XEmbed and nm-applet/SNI, including menus, signals and removal | PASS: [raw tray log](logs/fresh-x11-tray.log) |
| Selected Wayland install, 32/64-bit programs, isolation and part-B foot/Notepad actions | PASS: [raw install/action log](logs/fresh-wayland-install.log); Bob's desktop in 8 s |
| Wayland native tray | PASS: [raw tray log](logs/fresh-wayland-tray.log) |
| Item 1: four real SSD programs, 100%/150% borders and client placement | PASS, all eight cases: [raw frame log](logs/fresh-frames.log) |
| Item 2: Qt Exit dialog taskbar switch, five-second bound at 150% | PASS, **0.333 s**: [raw dialog log](logs/fresh-qt-dialog.log) |
| Item 3: maximized/restored foot live scale, input, work area, taskbar | PASS, six states: [raw scale log](logs/fresh-scale.log) |
| Broader part-B native programs and 32-bit GUI installer | Pending |

Fresh-image screenshots: [Preferences at 150%](screenshots/fresh-volumeicon-150.png),
[Qt Exit buttons after switching back](screenshots/fresh-qt-dialog-after-switch.png),
[maximized foot at 150%](screenshots/fresh-scale-maximized-1-150.png),
[restored foot after returning to 100%](screenshots/fresh-scale-restored-2-100.png),
[32-bit Notepad](screenshots/fresh-wayland-notepad32.png).

Signed-in Steam close-to-tray/reopen/menu stays **manual, not claimed**.
The instructions are in `WAYLAND_SESSION.md`. No account sign-in is submitted
by the test runner.

The [outer wrapper log](logs/fresh-wrapper.log) ends in a Bash parse error,
after every child check passed and both guests shut down normally. I edited
the running wrapper to add broader application checks; Bash's existing file
offset then resumed inside changed text (`line 38: unexpected EOF while
looking for matching '"'`). The current file passes `bash -n`. Additional
checks run from a frozen copy in `/var/tmp`, avoiding edits to a running
script. This is a harness failure, not a passing wrapper exit; individual
logs above establish the completed checks.

An initial screenshot copy reused the earlier staged evidence filenames.
Those eight originals were restored from Git; fresh screenshots use the
`fresh-` prefix. No staged evidence was replaced.
