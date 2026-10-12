# Installer verification — 2026-10-12

This records the verified continuation on `installer-port`, separately from the
imported milestone claims in INSTALLER.md. Earlier verified scenarios are below;
the latest W7 status is recorded separately.

## LightDM continuation (W7)

The final image passed fresh blank-disk acceptance on QEMU/KVM (BIOS, virtio).
Its clean-build squashfs was retained byte-for-byte while layout.inf and
txtsetup.sif were regenerated from the immutable payload. The new build hook
does this directly on future clean builds.

| Final W7 check | Result |
|---|---|
| Native directory regression tests | Pass, two tests; empty home, UID/GID, mode/setgid and traversal rejection |
| Complete native directory preflight | Pass, all 5,015 payload directories |
| Corrected binary layout hook | Pass; immutable squashfs mounted read-only and tables generated |
| Repaired ISO payload comparison | Pass, extracted final squashfs compared byte-for-byte with clean build |
| Live CD | Pass, automatic Wine desktop and normal ACPI shutdown |
| Fresh interactive install | Pass, GUI/OOBE, custom Welcome screen, wrong Alice password rejection, correct password login, Win+L, passwordless Bob desktop in 5 seconds |
| Interactive installed disk | Pass, root/Alice password hashes, full names, administrator membership, Bob passwordless state, both users' session logs, greeter/home ownership and setup cleanup |
| Zero-input install | Pass, automatic installed Wine desktop, normal shutdown and installed-disk hostname/account/prefix/config/ownership/cleanup/license checks |

Final host images:

- `D:/VirtualBox_VMs/iso/wineexplorer-w7.iso`:
  `bad479fea7f44b0a6913577860b7f9a397b8f5ce9b92874cb50a82899b8bdce2`.
- `D:/VirtualBox_VMs/iso/wineexplorer-w7-unattended-test.iso` (derived test media
  with an external CD answer file explicitly selecting `/dev/vda`):
  `3b714a5ccf9110f654b76fc8295da75b2c9b21de10787081ab46a80ed506514a`.

Final logs are ignored `build/install-test/w7-{interactive,unattended,live}.log`;
screenshots are in `/var/tmp/wineexplorer-vmtest/` and copied to the host evidence
directory. All three W7 test VMs end stopped. Earlier failed images/logs are
diagnostics, not current acceptance artifacts.

These tests do not exercise EFI/physical hardware, task retention after switching
back to the first session, or an actual GTK recovery-greeter login.
Claude's concurrent `a417c3be` empty Recycle Bin resource fix landed after the
payload's `45fb8c6a` source snapshot. It is preserved on master but is not part of
these tested images; include and verify it on the next Wine/ISO rebuild.

Already verified in this continuation:

- Native directory-copy tests preserve an empty service account home, numeric
  UID/GID, permissions and setgid; traversal outside the destination is rejected.
- Live CD automatically reaches the Wine desktop and shuts down normally after
  replacing setup/display-manager conflicts with a setup-stage condition.
- A stopped installed multi-user disk repaired to the correct LightDM home
  ownership passed wrong/correct Alice authentication, Win+L, Bob's passwordless
  desktop (5 seconds), normal shutdown and read-only account/config/prefix checks.
  This was diagnostic confirmation before the final blank-disk rerun passed.

Corrections discovered:

- The installer copied file metadata but ignored WinntDirectories. Empty
  `/var/lib/lightdm` became root-owned; the greeter failed to write X authority.
  Native setup now creates and preserves every directory in the layout.
- Enabled setup services' Conflicts could cancel display-manager startup even
  when their conditions later skipped them. Gate LightDM with the setup-stage
  file and remove those conflicts; remove stale duplicate include units.
- Debian 13 AccountsService already writes LightDM's main configuration through
  WexUserSetSession. Removed a redundant OOBE username handoff (its CRT text-mode
  CRLF was also rejected by the shell's username validation). The checker now
  accounts for LightDM's legacy SeatDefaults section.
- Tesseract omitted white text on the blue Welcome screen. An explicit 60%
  threshold fallback recognizes names and small antialiased password labels.
- `/tmp` is a 32 GiB RAM filesystem in WSL. Accumulated disks filled it and caused
  QMP screenshot/ImageMagick failures. Preserve old stopped disks on persistent
  storage, and use VMTEST_DIR=/var/tmp/wineexplorer-vmtest for further runs;
  acceptance scripts now honor this existing vmtest setting.
- The original layout was scanned from the mutable build chroot after squashfs
  compression. It contained 122 directories absent from the payload (first
  `/packages`), so the new strict directory pass correctly stopped. Generate
  tables from mounted squashfs instead. The final ISO replaces only those two
  tables; its extracted payload passed a byte-for-byte comparison. Xorriso was
  initially absent on the build VM; it is now installed there as well as in WSL.

Initial failed-run logs, diagnostic screenshots and the repaired-disk auth
evidence are under the ignored `build/install-test/` directory. An NBD inspection
initially raced partition node creation; the diagnostic helper was corrected to
wait and disconnect on failure. No unrelated VM was stopped or changed.

## Build and source checks

| Check | Result |
|---|---|
| Full Wine build after master/compositor merge | Pass, `make -j8` in `~/inst/build` on the Proton build VM |
| Native text setup | Pass; existing format-truncation warnings remain |
| Open-resource manifest | All 102 source asset hashes match the clean host checkout |
| Package license documents | Both `.deb` archives contain GPL/LGPL texts and license summary under `/usr/share/doc/<package>/` |
| License-header helper | Three regression tests pass, including GNU product names and wrapped/existing notices |
| Clean interactive ISO rebuild | Pass; transferred SHA-256 `e418950ac100cd7e658bd1b14327bb2b706eafd9a9a6915121c7223e9c9651dc` |
| Clean unattended ISO rebuild | Pass; transferred SHA-256 `e9ef7b985a4fd492d962dcf76debf3c85b0da295b3204afc4b9d74391665102c` |
| Workspace/build-directory guards | Pass; reused live-build state and invalid build directory are rejected before staging |

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
| Interactive blank-disk install through Bob Smith's desktop | Pass; corrected script rerun from blank disk without manual intervention, desktop in 8 seconds after logon |
| Interactive installed-account checks | Pass; full names, root/Alice password hashes against fixture, Alice in sudo, Bob passwordless, setup account/state removed |
| Missing answer file falls back to interactive setup, disk unchanged | Pass; welcome screen reached, `qemu-img compare`: Images are identical |
| Specified unavailable target disk stops setup, disk unchanged | Pass; target error screen reached, `qemu-img compare`: Images are identical |
| Zero-input install plus installed-disk cleanup/license checks | Pass; no key/mouse input, normal ACPI shutdown, installed hostname/user/prefix, cleanup and license documents verified |

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

## Failures and corrections in this continuation

- First interactive run: `timeout waiting for 'Typical settings'`, followed by
  `FAIL: no network page`. The screenshot showed the network page; OCR omitted
  the focused radio label. The test now waits for static `Networking software`
  text; the corrected full rerun passed without manual advancement.
- Incremental unattended build: `cp: cannot stat 'chroot/boot/vmlinuz-*': No such
  file or directory`. The reused filesystem was a bootstrap chroot with no
  kernel/package payload while completed package-stage markers remained. The
  rebuild now uses `--unattended --clean`; the builder rejects existing chroot
  or `.build` state unless `--clean` is supplied. The unreachable incremental
  update block was removed.
- The header helper originally mistook `GNU/Linux` text for an existing notice.
  Its first stricter expression then missed licenses wrapped across C comment
  lines. Comment-prefix normalization and three regression checks corrected
  both cases. Tests exposed unclosed file warnings; file reads/writes now use
  context managers, and tests pass with Python warnings treated as errors.

- Second interactive run reached Bob's desktop, but `timeout waiting for 'My
  Computer'` followed by `FAIL: no desktop for Bob Smith` occurred because OCR
  read `My Gomputer`. Both scripts now require `My Documents` and `Recycle Bin`.
  The third full interactive run passed all checkpoints with no manual input.
  Screenshot numbering also now increments in the parent shell rather than a
  command-substitution subshell.
- The first unattended restart selected `Start WineExplorer from CD`: adding a
  resume entry shifted default index 2 to the live entry. The menu now selects
  index 0 inside the setup-in-progress condition. Both images' menus were
  corrected with `installer/tools/update-iso-menu.py`, retaining their compiled
  filesystem and boot metadata. Fresh negative fixtures and the complete
  zero-input install passed after the correction.
- The first offline cleanup check reported `FAIL: temporary setup account
  remains`. The test had quit QEMU abruptly and mounted the filesystem with
  journal replay disabled. After recovery, original OOBE logs showed account
  provisioning and cleanup had completed; the apparent leftover state was an
  unreplayed journal. `vmtest.py shutdown` now requests normal ACPI shutdown and
  waits for QEMU to exit. A fresh full install passed the disk checks with this
  corrected procedure. Hard `stop` remains appropriate for disposable boot
  safety cases; do not use it to inspect a newly installed filesystem.

Safety fixtures are reproducible with
`installer/tests/prepare-safety-isos.py INTERACTIVE_ISO OUTPUT_DIR` (requires
`xorriso`). It preserves the boot metadata, selects the unattended entry, and
adds an unavailable-target answer file only to the negative fixture. Run
`boot-safety.sh` against each variant. Observed evidence is in
`/tmp/vmtest/wex-missing-answer-codex/` and
`/tmp/vmtest/wex-wrong-target-codex/`; screenshot copies are also in the host's
gitignored `build/install-test/`.

The account check used `installer/tests/check-interactive-accounts.py ROOT
PASSWORD_FILE` against a read-only mounted installed root after stopping the
test VM. It compares hashes through libcrypt without printing the password or
shadow entries. The interactive VM is now stopped; its disk/evidence remain in
`/tmp/vmtest/wex-install-codex/`, and the desktop screenshot is copied to host
`build/install-test/interactive-desktop.png`.

## Final local artifacts

The clean build hashes above identify the pre-menu-correction baselines. Final
candidate paths and hashes after the source-equivalent menu correction:

| ISO | SHA-256 |
|---|---|
| `D:/VirtualBox_VMs/iso/wineexplorer.iso` | `497c2a06fcc4d84bc5031dbfa1970ebad2fc2a8b53b3cc981118ed5c1a824560` |
| `D:/VirtualBox_VMs/iso/wineexplorer-unattended.iso` | `1ab8d25427562cf872681ad7983b076b5eaa0aa944e06181bac5e0e5a6108077` |

Both negative cases were rerun against fixtures derived from the final menu.
Host evidence/logs live in gitignored `build/install-test/`, including
`codex-interactive.log`, `codex-unattended.log`, `missing-answer.log`,
`wrong-target.log` and desktop/safety screenshots. All new QEMU test VMs are
stopped, their disks remain in `/tmp/vmtest/`, and test-owned NBD mounts were
released. No development desktop was installed over or restarted.

These runs cover QEMU/KVM BIOS boot and a virtio disk. They do not revalidate the
imported EFI claims or a physical-machine install. Remaining GUI/branding and
LightDM integration work is listed in INSTALLER.md and WINLOGON_AND_DWM.md.
