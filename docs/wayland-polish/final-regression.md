<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Fresh-ISO polish regression, 2026-10-17

Status: ready for the work order's end review, with restored-window scaling
time-boxed and still open. This is not an all-items-pass claim.

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
| Item 3: maximized/restored foot live scale, input, work area, taskbar | Maximized passes; restored remains open after a repeat grew off-screen: [failure](logs/fresh-scale-order-race.log). Two unsuccessful follow-up candidates were removed; see below. |
| GTK CSD input and taskbar minimize/restore, one proxy | PASS: [initial application log](logs/fresh-programs-initial-ocr.log) |
| Qt Wayland and XWayland xterm input and taskbar minimize/restore, one proxy each | PASS: [terminal log](logs/fresh-programs-qt-xterm.log) |
| Firefox CSD local navigation/input and taskbar minimize/restore, winecfg, Control Panel | PASS: [browser and shell log](logs/fresh-programs-firefox-shell.log) |
| Normal Steam launch | PASS: [raw Steam log](logs/fresh-steam.log); no sign-in submitted |
| 32-bit GUI installer and installed File Manager | PASS: [install log](logs/fresh-installer32.log), [window/file verification](logs/fresh-installer32-verify.log), [screenshot](screenshots/fresh-installer32-file-manager.png) |

Fresh-image screenshots: [Preferences at 150%](screenshots/fresh-volumeicon-150.png),
[Qt Exit buttons after switching back](screenshots/fresh-qt-dialog-after-switch.png),
[maximized foot at 150%](screenshots/fresh-scale-maximized-1-150.png),
[restored foot after returning to 100%](screenshots/fresh-scale-restored-2-100.png),
[32-bit Notepad](screenshots/fresh-wayland-notepad32.png).

Signed-in Steam close-to-tray/reopen/menu stays **manual, not claimed**.
The instructions are in `WAYLAND_SESSION.md`. No account sign-in is submitted
by the test runner.

The restored-scale failure and both removed trials are analysed in
[item 3 notes](item3-notes.md), including the comparison with Sway, labwc and
foot's actual resize/scale code. The research provides a concrete transaction
reconciliation lead, not a confirmed root cause or another passing claim.
Both trial DLLs were replaced with the original image's DLLs:
[verified hashes and normal shutdown](logs/fresh-runtime-restored.log).
All QEMU test guests are stopped; native test packages remain on the fresh
Wayland disk. The build VM remains available on SSH 2223.

The [outer wrapper log](logs/fresh-wrapper.log) ends in a Bash parse error,
after every child check passed and both guests shut down normally. I edited
the running wrapper to add broader application checks; Bash's existing file
offset then resumed inside changed text (`line 38: unexpected EOF while
looking for matching '"'`). The current file passes `bash -n`. Additional
checks run from a frozen copy in `/var/tmp`, avoiding edits to a running
script. This is a harness failure, not a passing wrapper exit; individual
logs above establish the completed checks.

The broader application runner also needed measurement corrections:
whole-screen OCR omitted correctly rendered terminal text, even after moving
the pointer away and simplifying the marker. Cropping the actual terminal
text and using block OCR reads both the command and its output. The
[second OCR failure](logs/fresh-programs-fullscreen-ocr.log) and
[visible text screenshot](screenshots/fresh-qt-terminal-ocr.png) are retained.
The first log records GTK passing before the Qt OCR failure; the terminal
log records Qt/xterm passing before Firefox's changing title stopped its
match. Firefox had minimized correctly (style includes WS_MINIMIZE, hosted
group ACTIVE); the readback fixture's `%ls` output truncates its title at the
em dash. Matching the stable page-title prefix fixes that harness assumption.
Only the failed/uncompleted classes were resumed; GTK was not repeated.
These failures did not justify product/rendering changes.

The fresh guest has no `objdump`; the initial installer preparation stopped
before launching anything ([log](logs/fresh-installer32-missing-tool.log)).
PE i386 inspection now uses the host's MinGW tools, and the guest verifies
the previously recorded SHA256. The GUI install completed and the installed
File Manager opened, but full-screen OCR missed its small Computer label.
The continuation verifies the actual top-level 7-Zip window and installed
file, records its screenshot, and closes it. No reinstall was needed.

After the host restart, the Proton build VM was powered off (SSH port 2223
refused connections), QEMU sockets were stale (serial connection refused),
and the staging HTTP server was absent (guest curl error 7). The build VM
and existing QEMU disk were restarted. An attempted inline `bash -c` HTTP
startup did not leave a server/log; a foreground `python3 -m http.server`
managed by the command tool works. Its listener is WSL loopback port 18765.

An initial screenshot copy reused the earlier staged evidence filenames.
Those eight originals were restored from Git; fresh screenshots use the
`fresh-` prefix. No staged evidence was replaced.

During final source resync, `tar -xf` preserved the archive's commit
timestamps, approximately 10,100 seconds ahead of the build VM clock after
the restart. Make repeatedly regenerated its Makefile because the imported
`Makefile.in` files appeared to be in the future. Re-extracting the identical
archive with `tar -xm -f` normalized only its timestamps; compilation then
progressed. The restored `linux_windows.c` SHA256 is
`e8ec50075f9e36b8d36a38300323dd16780f00159b99e9299d310790f417e255`
on both host and build VM. A documentation patch used context from the wrong
file and failed atomically; the corrected patch changed this report.

Final verification after resync: combined Wine `make -j4` with
`--enable-archs=i386,x86_64` and compositor `ninja` both pass at pinned Wine
ed5fe96 / compositor acc46fe6. Both architecture DLLs are present. This
rebuild contains the original committed implementation, with neither trial:
[raw final build](logs/final-pinned-build.log). The test runner compiles with
`python3 -m py_compile`; `git diff --check` passes. The ISO above remains the
fresh-install evidence; no replacement image is implied by this final build.

The first push was rejected as non-fast-forward: other contributors had
advanced master with open-font packaging, Open Luna resources and new work
orders during this checkpoint. `origin/master` was merged normally without
conflicts or force-push. These incoming changes do not change the polish
runtime C code or either submodule pin. The fresh ISO evidence above predates
those font/art changes; it does not claim to test their updated packaging.
The merged checkpoint 52975515 was resynced and both builds passed again
before pushing: [merged-master build log](logs/final-merged-pinned-build.log).
