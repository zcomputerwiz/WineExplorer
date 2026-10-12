# Installer verification — 2026-10-12

This records the current continuation on `installer-port`, separately from the
imported milestone claims in INSTALLER.md. Runtime checks still pending below
must pass before merging/publishing the installer.

## Build and source checks

| Check | Result |
|---|---|
| Full Wine build after master/compositor merge | Pass, `make -j8` in `~/inst/build` on the Proton build VM |
| Native text setup | Pass; existing format-truncation warnings remain |
| Open-resource manifest | All 102 source asset hashes match the clean host checkout |
| Package license documents | Both `.deb` archives contain GPL/LGPL texts and license summary under `/usr/share/doc/<package>/` |
| License-header helper | Three regression tests pass, including GNU product names and wrapped/existing notices |
| Clean ISO rebuild | In progress |

The initial four-job build was stopped with SIGTERM and resumed with eight jobs
after the older packaging run finished. No unstaged `make install` was used;
Wine is installed only into the package's `DESTDIR`. Source synchronization used
Git archives from the clean installer branch, followed by committed fixes. The
development VM's local resource overlay was not used.

Guest logs: `~/inst/codex-make.log`, `~/inst/codex-textsetup.log`,
`~/inst/codex-iso.log` and `~/inst/codex-rebuild.log`. These are local build
artifacts, not tracked files. The older build logs and ISO are not evidence for
the new image.

## Runtime acceptance

| Scenario | Result |
|---|---|
| Interactive blank-disk install through Bob Smith's desktop | Pending |
| Missing answer file falls back to interactive setup, disk unchanged | Pending |
| Specified unavailable target disk stops setup, disk unchanged | Pending |
| Zero-input install plus installed-disk cleanup/license checks | Pending |

`installer/tests/install-interactive.sh` checks partition creation, format,
copy/restart, GUI wizard, OOBE, Enter on Finish and passwordless-user logon.
Passwords are read from a gitignored file and are not included in this report.

`installer/tests/boot-safety.sh ISO EXPECTED_TEXT [VM]` seeds a disposable disk,
boots the default entry, waits for the expected screen and compares logical disk
contents with its baseline using `qemu-img compare`. Prepare separate ISO
variants for absent-answer and unavailable-target cases; reaching a screen alone
does not prove disk safety.

`installer/tests/install-unattended.sh ISO [VM]` expects an ISO built with
`--unattended` using the example account/hostname (`wineuser`, `WEX-PC`). It sends
no keyboard or mouse input, waits for copy and desktop, then stops the VM and
mounts its root read-only to verify setup cleanup, user prefix and license texts.
Only an unused NBD device is selected, and cleanup disconnects only a device
successfully attached by this test.

All test scripts replace the named disposable disk. Use dedicated names, such
as `wex-install-codex`, `wex-safety-codex` and `wex-unattended-codex`. Evidence is
under `/tmp/vmtest/<name>/`; the interactive log is also captured in the host's
gitignored `build/install-test/codex-interactive.log`.
