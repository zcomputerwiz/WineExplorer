# Codex handoff — 2026-10-12 W7 complete

Read `CLAUDE.md` and `LICENSE.md` first. Customized Wine remains the desktop/window
manager; Proton application runtimes remain independent. Use only the tracked
open-resource tree for builds. Local reference/overlay material stays in
`../WineExplorer-local` and must not enter this repository or installer images.
Commit after each working step.

## Completed in this continuation

W7 installs LightDM and the unprivileged Wine `logonui.exe` Welcome screen,
with GTK greeter retained for recovery. The greeter prepares its own
`/var/lib/lightdm/.wine-logon` prefix on first use. AccountsService handles
accounts, session choice and automatic login through the existing D-Bus API.
Debian 13 writes automatic login into LightDM's main configuration using the
legacy `[SeatDefaults]` section; do not add a duplicate username handoff.

Fresh QEMU/KVM BIOS/virtio tests passed:

- Live CD automatically reaches Wine desktop and shuts down normally.
- Interactive install from blank disk through GUI setup/OOBE to Welcome screen.
- Wrong Alice password rejected; correct password starts Alice's own session.
- Win+L returns to Welcome; passwordless Bob starts his own desktop in 5 seconds.
- Normal shutdown followed by read-only checks of root/Alice passwords against
  the private fixture, full names, administrator membership, Bob passwordless
  state, both user session logs, LightDM home/prefix ownership and setup cleanup.
- Zero-input install to automatic Wine desktop, normal shutdown, and read-only
  hostname/account/prefix/LightDM/ownership/cleanup/license checks.
- Two native directory-copy regression tests and full 5,015-directory preflight.

See [INSTALLER_TEST_REPORT.md](INSTALLER_TEST_REPORT.md) for final SHA-256 values,
exact acceptance evidence and limits. No EFI/physical-machine verification,
switch-back task-retention test or GTK recovery-login test was added.

## Fixes and pitfalls

- Setup units' display-manager Conflicts canceled startup even when their
  conditions skipped them. Gate LightDM on absence of the setup-stage file and
  remove those conflicts. Canonical units are owned by the session package;
  stale include copies were removed.
- Native file copying preserved file ownership but ignored directory metadata.
  Empty LightDM home became root-owned and X authority writes failed. New
  `textsetup/copydirs.c` restores every WinntDirectories entry, UID/GID and mode.
- Original layout scanned a mutable build chroot after compression, including
  122 directories absent from squashfs (first `/packages`). The binary hook now
  mounts immutable squashfs read-only and scans it instead. Final clean payload
  was retained; `tools/update-iso-layout.py` replaced just layout.inf/txtsetup.sif.
  Extracted final squashfs was compared byte-for-byte with the clean build.
- Tesseract missed white Welcome-screen text. The VM helper retries misses with
  a 60% contrast threshold; tiny password text uses the stable password keyword.
  Do not run concurrent OCR/shot commands against one VM's shared scratch files.
- WSL `/tmp` is a 32 GiB RAM filesystem. Accumulated test disks filled it and
  caused QMP/ImageMagick failures. Scripts now honor `VMTEST_DIR`; use persistent
  `/var/tmp/wineexplorer-vmtest` for tests.
- Detached WSL launchers exited before child tests started. Keep test commands
  foreground in persistent exec sessions. SSH/nohup build scripts work on the VM.
- NBD partition nodes can appear after udevadm settle. Wait for the node, install
  cleanup traps immediately after attaching, and inspect only after normal
  shutdown. Abrupt stop plus ro,noload can hide unreplayed journal changes.

Every command failure/correction and intermediate state is in the ignored
`build/install-test/continuation-notes.txt`; earlier failed logs/images are
preserved as diagnostics. Never pass C escapes through shell heredocs/sed/python
-c; edit source with a patch or saved file. The full-directory preflight caught
layout drift before its repair; the fresh final reruns passed without a hotfix.

## Branches and machines

- Continue `master`. The validated installer port was already merged; old
  `../WineExplorer-inst` and `../WineExplorer-d1` worktrees remain, untouched.
- Preserve preexisting uncommitted `.gitignore` `*.tmp` addition and `.claude/`.
- Proton build VM: SSH port 2223, wineuser, key `D:/VirtualBox_VMs/vm_key`.
  Sources `~/inst`; Wine build `~/inst/build`. NEVER run unstaged `make install`
  there. Packaging uses `make install DESTDIR=...`. Final payload came from a
  clean build through `45fb8c6a`; later product changes are binary-table tooling.
  Xorriso is now installed on this VM too. Logs `~/inst/w7-iso-build.log`,
  `w7-layout-repair.log`, `w7-hook-test.log`; initial failed repair log retained.
- Guest `~/inst/wineexplorer-w7.iso` is the pre-layout-fix clean baseline.
  Guest `~/inst/wineexplorer-w7-layout.iso` is the final corrected image.
- Host final images: `D:/VirtualBox_VMs/iso/wineexplorer-w7.iso` and
  `wineexplorer-w7-unattended-test.iso`. The latter is derived test media with an
  external CD answer file explicitly targeting `/dev/vda`; not a separate
  `--unattended` clean build. Earlier host images remain, under diagnostic names.
- WSL final VMs `wex-w7-live`, `wex-w7-interactive`, `wex-w7-unattended` are stopped,
  state/disks/evidence in `/var/tmp/wineexplorer-vmtest/`. No NBD mounts remain.
- Older stopped disks `wex-install-codex` and `wex-unattended-codex` were preserved
  under `/var/lib/wineexplorer-vmtest-archive/<name>/disk.qcow2`; original
  `/tmp/vmtest/<name>/disk.qcow2` paths are symlinks. Old failed W7 state also stays
  under `/tmp/vmtest/`. Claude's older `inst` QEMU VM remains running, untouched.
- Dev WineExplorer-VM (SSH 2222), XP VM and old VirtualBox install-test VM were
  not restarted, installed over or powered off. Dev has local reference overlay;
  never use its source/runtime as installer input.

## Test and build commands

From WSL Debian as root, set `VMTEST_DIR=/var/tmp/wineexplorer-vmtest` for every
vmtest command, including screenshots and booting an existing disk. Run:

```
export VMTEST_DIR=/var/tmp/wineexplorer-vmtest
bash installer/tests/install-interactive.sh ISO build/install-test/password.txt VM_NAME
bash installer/tests/install-unattended.sh UNATTENDED_TEST_ISO VM_NAME
python3 -B -W error installer/tests/test_copydirs.py
```

These scripts replace the named disposable disk and end stopped. The private
password fixture is gitignored; never print or commit it. Final host logs and
screenshots are under `build/install-test/`, including `w7-welcome.png`.

For clean ISO builds on the build VM:

```
cd ~/inst
sudo -n env BUILD_DIR="$HOME/inst/build" WORK_DIR=/home/wineuser/iso-build \
  OUT_ISO="$HOME/inst/wineexplorer.iso" bash installer/build-iso.sh --clean
```

Sync tracked clean sources with Git archives, not the dev overlay's vm.sh sync.
Use `make -C build -j8 programs/NAME/all` for targeted Wine programs; a bare
subdirectory target does not rebuild them. Existing live-build state requires
`--clean`; do not reuse an incomplete root with completed stage markers.

## Next work

1. Installer visual polish: GUI header/backdrop, display-friendly partition
   names, installed GRUB appearance and its missing unicode.pf2 warning,
   new-profile Recycle Bin icon.
2. W4 classic login, W5 Windows Security/lock/password changes, W6 User Accounts;
   retain unprivileged greeter and D-Bus privilege boundary. Check task retention
   when switching back to a session explicitly rather than inferring it.
3. Automatic Updates policy (currently recorded only), friendly timezone names,
   EFI/physical-machine installs and display-output tests.
4. Remaining D2 checks and D3/D4 composition work, still off by default.
