# Codex handoff — 2026-10-12 continuation complete

Read `CLAUDE.md` and `LICENSE.md` first. The project remains customized Wine as
its desktop/window manager, with independent Proton application runtimes. This
repository contains open resources; local reference/overlay material belongs in
the separate local-only repository and must not enter installer builds.

## Completed work

The installer-port/master merge compiled successfully in a full Wine build.
Both clean ISO builds completed from the open source tree. The interactive
script then passed from a blank disk through GUI setup, OOBE, Enter on Finish,
and Bob Smith's passwordless desktop (8 seconds after logon). Read-only account
checks verified full names, root/Alice passwords against the private fixture,
Alice's administrator membership, Bob's passwordless state and setup cleanup.

The zero-input install also passed through the installed desktop and normal
ACPI shutdown, followed by read-only checks of hostname, account, prefix,
cleanup and installed license texts. Missing-answer and unavailable-target boot
cases were reverified against the final menu, with unchanged logical disk bytes.
See [INSTALLER_TEST_REPORT.md](INSTALLER_TEST_REPORT.md) for hashes, exact
failures/corrections and evidence. Imported older milestone claims are not the
current test report.

Fixes in this continuation:

- Package GPL/LGPL texts and the license summary in both installer packages.
- Add GPL notices to native text setup without replacing existing notices;
  repair/test the header helper's GNU-name and wrapped-comment detection.
- Require `--clean` with existing live-build state. Incremental reuse had
  retained completed stage markers with a bootstrap root lacking the kernel.
- Require a configured Wine build for packaging; remove installed-runtime copy
  fallback.
- Select the resume menu entry whenever setup is in progress. Unattended default
  index 2 otherwise shifted to the live desktop after the added resume entry.
- Stabilize OCR checkpoints and screenshot sequencing. Network radio focus
  caused a missed label; desktop OCR read Computer as Gomputer. The corrected
  full interactive test passed without manual advancement.
- Add `vmtest.py shutdown` (ACPI), negative boot/disk comparisons, zero-input
  installation and read-only account verification. Abrupt QEMU quit plus a
  no-journal-replay mount initially produced a false cleanup failure.

## Branches and worktrees

The validated installer work is merged into master (`1e5de3da`) and published
through `origin/master`. `installer-port` remains in `../WineExplorer-inst`; its
files were ported onto the clean rewritten history. Do not import/push the old
external installer history. `d2-effects` was already merged; its old worktree
was not removed in this continuation.

Master's preexisting uncommitted `*.tmp` ignore addition and `.claude/` directory
are user/other-agent work and must be preserved. The merge must not commit them
as part of the installer changes. Check Git state before starting new work.

## Machines and artifacts

- Proton build VM: SSH port 2223, wineuser, key `D:/VirtualBox_VMs/vm_key`.
  Sources `~/inst`, build `~/inst/build`. Never run an unstaged `make install`
  there. Use `installer/package-wine.sh` to install only into `DESTDIR` staging.
- Dev VM WineExplorer-VM: Claude reported LightDM/logonui, Composition=0. This
  continuation did not install over or restart that desktop.
- Existing XP and VirtualBox install-test VMs were not powered off or modified.
- WSL2 Debian: QEMU/KVM tests, ImageMagick, Tesseract, qemu-nbd, qemu-io, xorriso.
  New test VMs `wex-install-codex`, `wex-unattended-codex`,
  `wex-missing-answer-codex`, `wex-wrong-target-codex` are all stopped. Disks and
  evidence remain under `/tmp/vmtest/`; test NBD mounts were disconnected.
- Final local images: `D:/VirtualBox_VMs/iso/wineexplorer.iso` and
  `wineexplorer-unattended.iso`. Clean baseline filesystems were retained while
  their menus were corrected through `installer/tools/update-iso-menu.py`.
  Future clean builds use the corrected source hook directly. Final hashes
  are in INSTALLER_TEST_REPORT.md.
- Guest `~/inst/wineexplorer*.iso` are the pre-menu-correction clean baselines;
  the final corrected candidates are on the Windows host. Do not substitute the
  guest baselines for the final candidates.
- Private test password remains in gitignored
  `build/install-test/password.txt`. Logs/screenshots and detailed command
  failure notes are in the same ignored directory. Do not commit credentials.

## Build and repeat tests

Sync using Git archives into `~/inst`; do not use the development overlay's sync
path. Targets are `make -j8` for full Wine or `dlls/<name>/all` for targeted work.

```sh
cd ~/inst
sudo -n env BUILD_DIR="$HOME/inst/build" WORK_DIR=/home/wineuser/iso-build \
    OUT_ISO="$HOME/inst/wineexplorer.iso" bash installer/build-iso.sh --clean
# For unattended: prepare an explicit target answer file, then add --unattended.
```

In WSL, call scripts with Bash (they need not be executable on the Windows mount):

```sh
bash installer/tests/install-interactive.sh ISO PASSWORD_FILE wex-install-codex
bash installer/tests/install-unattended.sh UNATTENDED_ISO wex-unattended-codex
python3 installer/tests/prepare-safety-isos.py INTERACTIVE_ISO OUTPUT_DIR
bash installer/tests/boot-safety.sh MISSING_ANSWER_ISO 'Welcome to Setup' wex-missing-answer-codex
bash installer/tests/boot-safety.sh WRONG_TARGET_ISO 'Setup cannot find the disk' wex-wrong-target-codex
```

These scripts replace the named disposable disk. `install-unattended.sh` expects
the example hostname/account (WEX-PC/wineuser) and verifies the installed disk,
not merely a desktop screenshot. Account checks use
`check-interactive-accounts.py MOUNTED_ROOT PASSWORD_FILE`, run as root against a
read-only mounted installed filesystem. Shut down normally before mounting;
`vmtest.py stop` quits abruptly. `vmtest.py start NAME` without `--iso` can boot
an existing installed disk. Never attach its disk through NBD while QEMU runs.

Known command pitfalls from this continuation are recorded in the ignored
failure notes and test report. The wexhost ABI header is `wexhost_unix.h`, not
`unixlib.h`. Never pass C escapes through shell heredocs/sed/python -c; use an
editor or saved script. Maintain GPL-compatible notices and list stock-Wine
changes in STOCK_CHANGES.md.

## Next work

1. W7: integrate LightDM/custom greeter into the installed distribution; it
   currently uses GDM even though the development VM uses the new greeter.
2. Installer presentation: GUI header/backdrop, display-friendly partition
   labels, installed GRUB appearance, new-profile Recycle Bin icon.
3. Apply Automatic Updates policy (currently only recorded), improve timezone
   labels, and exercise EFI/physical-machine installs separately. These new
   runs cover QEMU/KVM BIOS and virtio disks only.
4. Continue W4-W7 and D3-D4 / remaining D2 checks in WINLOGON_AND_DWM.md.

Commit after each working step. Preserve the independent-Proton desktop design
and the open-resource boundary when building or publishing.
