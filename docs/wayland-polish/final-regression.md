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
| Selected Wayland install and part-B actions | Pending |
| Wayland native tray | Pending |
| Item 1: four real SSD programs, 100%/150% borders and client placement | Pending |
| Item 2: Qt Exit dialog taskbar switch, five-second bound at 150% | Pending |
| Item 3: maximized/restored foot live scale, input, work area, taskbar | Pending |
| Broader part-B native programs and 32-bit GUI installer | Pending |

Signed-in Steam close-to-tray/reopen/menu stays **manual, not claimed**.
The instructions are in `WAYLAND_SESSION.md`. No account sign-in is submitted
by the test runner.
