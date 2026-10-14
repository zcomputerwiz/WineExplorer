# Codex handoff — 2026-10-14 Wayland L1 complete

**Current work (Codex, L2 in progress):** read WAYLAND_L2_WORKORDER.md and
WAYLAND_L2.md. Newest topics: Wine codex/wayland-l2 at 786643b, compositor
codex/wayland-l2 at 50646994. Host Wine topic now lives in
build/wayland-l2/wine; ordinary wine/ is restored to its detached production
f0108dc gitlink. Private protocol v4 reserves a native view with a live
controller handle; Wine forwards scalar atom properties and preserves Win32
ownership while mapping owned native proxies independently. PREPARED does not
mean rendered/hosted. Compositor v5 adds explicit scene commit/group status,
frame-ring cropping and native input routing, grouped move/minimize/restore and
controller/frame/native disconnect recovery. Its full suite passes 37/37 both
with and without XWayland. V6 adds epoch-tagged observed physical focus and
guarded temporary frame/native restoration. Actual key delivery and prevention
of late menu focus stealing pass in both compositor variants. V7 numbers group
configure/uncommit completion separately from unsolicited state. Native size or
decoration changes detach safely. Wine now binds v7 and consumes group/focus
acknowledgements under an explicit HOST property, retaining epoch/foreground
guards. 32/64 hosting and reservation-only probes pass, as do mixed-process L1
stacking and X11 tray/eight-dialog smoke. Main b41028b1 adds wexhost BindHost's
explicit Wayland path: owned atoms, client hit exclusion preserving the original
region, UI-thread subclass lifetime/focus-loop hooks and release/stop cleanup.
Wexhost now binds up to v7 and reports the frame capability only for v7 peers;
WEX_NATIVE_WINDOW and existing Unix-call indexes are preserved. Bind before
ShowWindow, then poll ERROR_IO_PENDING to success on the owning UI thread.
Both architectures pass the real bridge API tests, wrong-thread/lifecycle/region
checks, inventory/recovery and X11 ABI/smoke regressions.
Both architectures also pass screenshot pixel checks of native content rendered
inside the Wine frame, beyond protocol status assertions.
Read WAYLAND_L2.md for exact logs, commands, failures and correction notes.
Main 97707ed6 adds Explorer's explicit lab gate WEX_WAYLAND_NATIVE_WINDOWS=1,
binding before map and OBSERVED reconciliation/withdrawal cleanup. Both actual
64/32 Explorer shells pass native rendering/title/size changes, client clicks,
real system-menu focus restoration, two-way minimize/restore, CSD withdrawal,
SSD readoption and close cleanup. The 32-bit case also covers late native map.
Default gate-off and X11 remain unchanged. Compositor efb40ac9 fixes a reproduced
transient NONE-decoration notification during adoption; raw regression passes.
Use check-wayland-shell.sh and the separately named wayland-adoption-probe.c;
the existing L1 wayland-shell-probe.c is intact. Read WAYLAND_L2.md for logs.
Taskbar checkpoint: actual virtual-pointer task-button minimize/restore and
inactive activation, virtual-keyboard context-menu cancellation and
Minimize/Restore/Close pass in both Explorer architectures. Screenshot probes
wait for both inactive and active title repaint; immediate captures were stale,
not a taskbar defect. A proposed taskband guard was removed after the original
code passed the corrected test. No taskband production change is needed.
Compositor 50646994 adds only publisher input commands. See WAYLAND_L2.md for
the final logs and retained screenshots.
Next: owner/transient/state coverage, native resize and
move loops, then scaling/fullscreen/workspace and launch acceptance. CSD binding
is deliberately rejected, and resize/maximize UI is disabled in the lab gate.
Fullscreen/workspace, CSD, native popup, scaling and
application coverage remain acceptance work. No deployed runtime or session changes.

Earlier L2 bridge checkpoint: read
WAYLAND_L2.md before the historical L1/installer notes below. Main source now
contains an explicit wexhost Wayland inventory/control backend and fixed-width
32/64-bit native-info API (e5adf5bb plus recovery follow-up). Both architectures
pass real foot inventory, control, close, stale-ID, restart and transport-loss
withdrawal probes. X11 ABI and tray/eight-dialog regression checks pass. AUTO
Wayland adoption remains disabled pending safe native/frame binding. No default
session or installed runtime changes. Compositor codex/wayland-l2 is pushed at
8a5a73ed; production wexcomp remains 9a47d74f and Wine pin remains f0108dc.
Host compositor checkout: build/wayland-l2/wexcomp. Guest tested source is in
~/wexcomp-build, on codex/wayland-l2 with the tested patches applied over its
old base. Wine's host codex/wayland-l2 branch has no driver changes yet. Keep
the user's .gitignore edit untouched. Use SSH with IPQoS=none and serialize test
prefixes through check.lock; do not run backend tests concurrently.
Next: generic HWND native token, Wine independent owned proxy toplevels,
compositor native/frame groups with client-area rendering/input exclusion and
physical/logical focus separation. Then wire native launch and private test
D-Bus activation environments to compositor-supplied endpoints.

**Pin update (Claude, 2026-10-14):** Gemini's Vista+ msstyles series (`docs/UXTHEME_V4.md`) is merged; master's
Wine gitlink and `wineexplorer/11.19` are now `51c870b85b7` (your `05286f7c90` plus uxtheme only). Run
`git submodule update` before the next build. A combined WoW64 build passes; uxtheme tests pass 64/32-bit.

**Review cleanup (Codex, 2026-10-14):** current pin is f0108dc on wineexplorer/11.19,
which keeps Claude's fixes and adds complete licensing, removes unused tables,
and builds synthetic test resources from source. Full fork WoW64 build passes;
v4 (59 final checks) and system (22,252 checks) pass both architectures. Windows 11
focused 32/64 tests skip because private theme-file APIs are unavailable; no
native rendering claim. Clean four-commit upstream candidate ends at 43c139255b;
fresh upstream WoW64 build and both v4/system suites pass. Canonical
upstream/uxtheme-msstyles-v4 now has that squashed four-commit series. Original topic is saved on
archive/uxtheme-msstyles-v4-pre-cleanup. No WineHQ MR opened.
Use /srv/protonexplorer/uxtheme-v4-upstream for that clean source/build. Existing
assembled fork lab root was updated without resetting its old Git checkout;
uxtheme-review-* logs/statuses are under its checks directory. No WSL or real
desktop changes. Details and failed-command corrections: UXTHEME_V4.md.

Current work order WAYLAND_INTEGRATION_WORKORDER.md is complete. Read
WAYLAND_L1.md for acceptance, failed-command corrections and public screenshots.
The older refactor/installer notes below are historical; preserve their safety
rules, but use these current pins for new builds.

- Project master includes the five owned shell role/appbar/native-host changes
  through merge 2853f509. Wine gitlink and wineexplorer/11.19 are
  05286f7c90db81182be43ec257234b7cb3805672. This merges the accepted Wayland
  integration through ea0654efdb with concurrent visual-style work through
  70e6378514. Compositor wexcomp and codex/wex-window-handles are
  9a47d74f365c4b42310b51e81916592a40d3046f. All updates are fast-forwards.
- Every new Wine build must use --enable-archs=i386,x86_64 with both MinGW
  C++17 compilers. Full combined build passes; 32-bit Notepad and mixed 32/64
  cross-process stacking pass. No Gemini WSL distributions were used.
- The new native explorer /shell path uses the compositor's desktop and output
  modes. Standard optional xx-zones handles placement/shared coordinates;
  private wex-window v2 uses opaque compositor handles published through shared
  Win32 atoms for cross-process stacking. v1 requests remain compatible.
- Click/NoActivate, desktop-bottom, taskbar minimize/restore and reservation,
  topmost, menu, Start, pointer warp, alpha/color key, foot/XWayland focus and
  DPI placement pass. Compositor suite 26/26. Stock labwc/v1 fallback pass.
  Shipping X11, D1/D2 and native dialog bounds pass on isolated build/dev sessions.
- Wine Wayland standard-only branch upstream/winewayland-zones ends at
  01607e166c, with prepared description in UPSTREAM.md. User submits WineHQ MRs
  manually. No MR was opened. Private protocol/roles remain fork-only.
- Host restarted during testing. Both VirtualBox VMs are running: dev SSH 2222,
  Proton build SSH 2223, wineuser, key D:/VirtualBox_VMs/vm_key. Real production
  desktops were not restarted or replaced. User's .gitignore *.tmp edit is
  intentionally uncommitted.
- The final nested Wayland test session on Xvfb :30 was stopped after captures;
  its compositor and isolated Wine processes exited. Both real desktops remain
  running. Logs/screenshots are retained under the build root's checks directory.
- Accepted build root: /srv/protonexplorer/wex-wayland-l1/wow64-source/build/wine64
  on Proton VM. Assembled source was updated without resetting/rebasing its Git
  checkout: its version string still names 3bb585c4ee, while checks/source-candidate.txt
  and SHA-256 verification of all 25 changed integration/theme files identify
  05286f7c90. Do not reset this assembled tree. Build status is
  checks/wow64-current-theme-build.status CURRENT_THEME_WOW64_BUILD_PASSED.
- The packaged public runtime-wow64-public.tar.gz (host build/wayland, guest
  root above; dev ~/wex-wayland-l1/runtime-wow64) contains accepted ea0654efdb
  code before the later visual-style merge. Dev X11 acceptance used this package.
  Repackage the combined 05286f7c90 source before any deployment/ISO work; no
  globally installed runtime or ISO default was changed for L1.
- Maintained replay tools: scripts/wayland-session.sh and wayland-l1-check.sh,
  plus C/Python probes described in WAYLAND_L1.md. Lab prefixes are isolated from
  ~/.wine. Test-only session defaults to ~/wexcomp-build/build/labwc and private
  wex-shell-0 for every Wine process. Normal Linux apps use the normal socket.
  CS_DROPSHADOW pixels remain the known Wayland D3 gap; global hotkey forwarding
  is L4. Foreign Linux window frames are L2. These are separate work orders.

## Historical refactor handoff — 2026-10-13

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

**2026-10-13, merged by Claude:** `gemini/installer-polish` is in master (`930b6665`), with fixes for the XP step marker, the full Unicode License v3 notice in `src/include/wextimezone.h` and report links. The Wine fork `wineexplorer/11.19` moved to `f5016cf` (uxtheme honors `SetThemeAppProperties`: classic frame/controls when an application clears `STAP_ALLOW_NONCLIENT`/`STAP_ALLOW_CONTROLS`; listed in `STOCK_CHANGES.md`) and the master gitlink points there. Pull master and `git submodule update` before building; rebase your fork work on `f5016cf`. Open item kept for later: XP's time zone list has ~75 entries, ours 54 (pre-existing).

**2026-10-13, first priority: 32-bit Windows programs are required (user decision).** Every Wine build so far used
`--enable-win64` only, so nothing 32-bit runs (Open-Shell's 32-bit setup: `failed to load
...\syswow64\ntdll.dll`). Switch every build to WoW64, `--enable-archs=i386,x86_64` (`CLAUDE.md` rule,
`docs/BUILDING.md`): `scripts/refactor/build-assembled.sh`, `scripts/refactor/build-recursive.sh`, the dev VM
and Proton VM builds (`vm.sh` sync / their `build/wine64` configure), `installer/package-wine.sh` and the ISO
(check `syswow64` and the i386 PE DLLs land in the package), prefixes created by setup (template prefix) and
first logon. Add a 32-bit test program to the regression checks (e.g. a 32-bit build of `compose-probe`, or
`winecfg`/`notepad` from the i386 side: `wine C:\\windows\\syswow64\\notepad.exe`). Install
`g++-mingw-w64-i686` too (C++ modules). Claude will verify a WoW64 build of master in `wexbuild` and report in
this file.

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
