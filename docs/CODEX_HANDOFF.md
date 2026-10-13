# Codex handoff — 2026-10-13 refactor complete

Read CLAUDE.md, LICENSE.md and STOCK_CHANGES.md first. All items in
REFACTOR_WORKORDER.md are complete, including P3b extraction, P4 fresh
build/install/ISO/interactive/dev acceptance, optional P5 assessment and P6
rebase drill. Wine remains the desktop runtime; Proton remains independent.
Public source/resources only for installer builds; private assets stay in
../WineExplorer-local and ignored dev evidence. Commit each working step.

## Current source and build state

- Main: master. Wine submodule: zcomputerwiz/wine, wineexplorer/11.19,
  d0a0af068194c306bb36659b35bb2d4e0eff43c1. Project code/resources are under src.
- Assembly links owned modules into Wine. Declared desk.cpl, wlanui and
  wmplayer replacements preserve pristine stock originals in ignored build
  output. Never reset or rebase the assembled live tree; use a clean fork.
- Twelve upstream topic branches are pushed/tested; UPSTREAM.md contains
  prepared descriptions. WineHQ submissions are manual user actions.
- Rebase candidate codex/rebase-drill is pushed at
  95fa31a9a8c31725d291cd482b7e5b93822fa064, based on upstream master
  081042ede6a165a36bf9988350dd584b440d7a3d. Ninety-three retained commits
  replay unchanged in 52.798 seconds with zero conflicts; already-applied
  backports are omitted. Full build and isolated runtime smoke pass.
  Production pin stays d0a0af0681. Details/timings: REBASE.md and P6_REBASE.json.
- BUILDING.md is the current build/package guide. NEVER make install in
  ~/inst/build, even staged. Packaging requires this checkout's configured
  source and defaults to build/wine64. Historical commands below are obsolete.

## Machines and acceptance evidence

- ProtonExplorer build VM: SSH 2223, wineuser, key D:/VirtualBox_VMs/vm_key.
  Fresh accepted root /srv/protonexplorer/wex-refactor-p4-fresh; main snapshot
  965c5287, pinned public Wine submodule. Main was cloned from a verified Git
  bundle because the VM has no private-main-repository credentials; Wine was
  fetched normally. No private overlay. Full build/install and every installed
  module suite pass; all five visual comparisons match P3 except the clock.
  See refactor/P4.md for hashes, suite evidence and logs.
- Fresh public ISO there: wineexplorer.iso; host build/refactor/installer-p4.iso.
  SHA-256 ac893403a55de24da9d4ed918c83f54c6f56745f82d044e1d1213a9e56c3ba7d.
  Interactive setup/OOBE/accounts/Welcome/switch-user/desktop and read-only
  filesystem checks pass. WSL VM p4-submodule is stopped; evidence under
  /var/tmp/wineexplorer-vmtest/p4-submodule/steps. BIOS/virtio verified;
  EFI and physical installation remain separate future work.
- Rebase build root /srv/protonexplorer/wex-refactor-p6; statuses
  REBASE_BUILD_PASSED and CHECK_PASSED; p6-build.log / p6-smoke.log.
  No candidate runtime was installed on the dev VM.
- WineExplorer dev VM: SSH 2222. Tested public runtime/session packages are
  installed, LightDM restarted, Wine User desktop running 1024x768/Luna.
  Live plugin API and tray bounds pass; new DLL mappings verified. Private
  prefix art/themes remain intact. Backup root ~/build/refactor-p4-deploy
  contains installed-before, lightdm-before, xorg-before and prefix-before.
  Temporary acceptance autologin is removed from LightDM configuration and
  retained as autologin-test.conf in that backup root.
- Dev sources: ~/src/wine is now the fork Git checkout with assembly links;
  ~/src/src contains owned modules. Prior sources are backed up below
  ~/src/build/wine-source-backup.*. vm.sh sync passes, including private
  overlay and repeat checks. ~/build/wine64 is the old build: reconfigure
  and rebuild against migrated source before any future installation.
  Current runtime came from the fresh accepted public Debian packages.
- Live environment DISPLAY=:0, XAUTHORITY=/home/wineuser/.Xauthority,
  WINEPREFIX=$HOME/.wine. Log ~/.wine-session.log. The greeter has its own
  /var/lib/lightdm/.wine-logon prefix and Xauthority. Do not mix them.
- All four VirtualBox VMs remain running; XP/old install-test were not changed.
  The QEMU acceptance VM is stopped. Do not assume older unrelated test VM
  states; inspect them before use. Preserve the user's .gitignore *.tmp edit.

## Continuation and separate work

Every failure/correction and intermediate state is recorded in
refactor/CONTINUATION.md. Historical P0 maps remain hash-pinned. Public build
snapshots and ignored test drivers should not be mistaken for current master.
Use foreground persistent WSL sessions and VMTEST_DIR=/var/tmp/wineexplorer-vmtest
for installer tests; never print/commit the ignored password fixture.

Gemini owns INSTALLER_POLISH_WORKORDER.md (installer visual polish, Automatic
Updates and friendly timezone names), branch gemini/installer-polish, WSL
wexbuild. Do not overlap that assignment. Other future work includes W4/W5/W6,
explicit switch-back task retention, EFI/physical-output tests and the separate
Wayland/composition work orders. None are unfinished refactor acceptance items.

## Historical W7 handoff — 2026-10-12

The notes below preserve the earlier installer milestone. Its runtime paths,
VM states and build commands are superseded by the current handoff above.

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
  Claude's concurrent `a417c3be` empty Recycle Bin icon commit arrived after that
  snapshot; preserved on master, but not in these tested images. Include it in
  the next Wine/ISO rebuild rather than treating the blank icon as unfixed source.
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

**2026-10-13:** item 1 (installer visual polish) and the Automatic Updates and timezone-name parts of item 3 are
assigned to Gemini — `docs/INSTALLER_POLISH_WORKORDER.md`, branch `gemini/installer-polish`, environment WSL
`wexbuild`. Skip them; Gemini will not touch logonui, greeters, LightDM, wexhost, the shell or the Wine fork.

1. Installer visual polish: GUI header/backdrop, display-friendly partition
   names, installed GRUB appearance and its missing unicode.pf2 warning,
   verify new-profile Recycle Bin icon after rebuilding with `a417c3be`.
2. W4 classic login, W5 Windows Security/lock/password changes, W6 User Accounts;
   retain unprivileged greeter and D-Bus privilege boundary. Check task retention
   when switching back to a session explicitly rather than inferring it.
3. Automatic Updates policy (currently recorded only), friendly timezone names,
   EFI/physical-machine installs and display-output tests.
4. Remaining D2 checks and D3/D4 composition work, still off by default.
