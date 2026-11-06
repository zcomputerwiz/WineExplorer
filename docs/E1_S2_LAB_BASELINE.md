<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# E1-S2: disposable lab baseline

Lab baseline for the E1-S standard-user work ([E1-S work order](E1_S_STANDARD_USERS.md),
section 7 row 2 and section 8). Collected on the disposable QEMU guest (VM B) on
2026-11-06. This records the pinned starting state, the synthetic fixture accounts, the
current elevation controls, the isolated test PAM service, and the snapshot/restore
procedure. **No snapshot was taken.** No host or personal credentials entered the guest.
Throwaway fixture passwords live only in a root-only guest file and are never printed here.

All lab scripts and raw output are under `D:/DeepThought/ChromiumLab/e1s-baseline/`
(local lab infrastructure, not a portable project path).

## 1. Pinned guest baseline

- Guest: Debian GNU/Linux 13 (trixie), kernel `6.12.115+deb13.1-amd64` (host name
  `wex-g4-virgl`).
- Runtime root: `/opt/wex-rt2/build/wow64` (WoW64 build). Machine root:
  `/var/lib/wineexplorer/machine`, mode `drwxrwsr-x root:wexsys` (setgid `wexsys`);
  the per-user prefix shares the machine `system.reg`/`userdef.reg` by symlink.
- Key runtime file hashes (SHA-256), pinned for later comparison:

| File (under the runtime root unless noted) | SHA-256 |
| --- | --- |
| `server/wineserver` | `6614caa9fd888d9763ce6e88854bd4ecba7ac06c3fa163ea3d4ec53dfba08457` |
| `loader/wine` | `f2a547901802c13f971f9c4a61eba084f69062eea32f7eaa756def01688ff926` |
| `dlls/wexhost/wexhost.so` | `d7aeaf7dc16c29e4841bb733c0810c1de5bfb3f674e01562bd21f12591954128` |
| `dlls/wexhost/x86_64-windows/wexhost.dll` | `6889ff30d6a18be3035365ea2cb616562fae821e0211598813e114415e3139bd` |
| `dlls/wexhost/i386-windows/wexhost.dll` | `d8d99b8441c064f79b8c4f5449e9864e7132e48c5e0e3b2debf8e5d0ed1a1f69` |
| `dlls/ntdll/x86_64-windows/ntdll.dll` | `cf8ec32622feb46189ab4d165bf6bc729f4112e16fe287bd08c68db18f1e8097` |
| `dlls/ntdll/i386-windows/ntdll.dll` | `ce2a76121148c8ef016b48737f60e5f0fe7e82bb4ed15d5535b415d55a8994c6` |
| machine prefix `system32/wexelevate_test.exe` (test broker) | `611770bc34d321acc7fc562549b41a08f6d0cb9f82be511993fc5eae18904911` |

- Pre-existing users: `debian` (1000, in `sudo`), `wexcache` (987, service),
  `wextest` (1001), `wexadm` (1002, in `sudo`, the filtered-administrator lab identity).
- Pre-existing groups: `sudo` (`debian`,`wexadm`), `wexcache` (987), `wexsys` (989),
  `wextest` (1001), `wexadm` (1002).

## 2. Fixture accounts (created)

Synthetic, disposable accounts for the E1-S lab. Each has a random throwaway password
written only to `/root/e1s-fixtures-passwords` (mode `600 root:root`, one `name=pass`
line each). The passwords are never printed in this document or the lab output.

| Account | UID | Role | Notes |
| --- | --- | --- | --- |
| `bob` | 1003 | standard | owns the interactive prefix `/home/bob/.wine` (`700 bob:bob`) |
| `alice` | 1004 | administrator | member of `sudo` |
| `carol` | 1005 | standard | see discrepancy note below |
| `eve` | 1006 | standard | attacker-ish; authenticates but is not an administrator |

Creation: `create-fixtures.sh` (idempotent; `useradd -m`, `chpasswd`, `usermod -aG sudo`
for `alice`). Removal: `cleanup-fixtures.sh` (see section 6).

> Discrepancy: section 8 of the work order names *Alice and Carol* as mapped
> administrators, but this work order's fixture list makes only `alice` an administrator
> and `carol` a standard user. This baseline follows the work order (`carol` standard).
> If the test plan needs a second administrator, add `carol` to `sudo`
> (`usermod -aG sudo carol`) before the token/profile packages.

## 3. Current elevation controls (run)

Run as the filtered administrator `wexadm` through the lab elevation runner
(`/tmp/gsr/guest-elev-run.sh`), targeting a harmless no-op console command. The test
broker's approval is the file `/tmp/wexelev/approve.all`.

| Control | Setup | Result |
| --- | --- | --- |
| Approve (click consent) | approval file present | **success**: `runas started`, target `exit code 0` |
| Cancel / deny | approval file removed | **denied**: `runas failed: error 1223` (`ERROR_CANCELLED`) |
| Standard user | `bob` (standard token), approval present | **denied**: `runas failed: error 50` (`ERROR_NOT_SUPPORTED`) |

So today: a filtered administrator can elevate (approve) or be cancelled (1223), and a
standard user cannot elevate at all — the broker returns `ERROR_NOT_SUPPORTED` (50),
consistent with `identify_caller()` accepting only a limited (filtered) token. Full logs:
`control-approve-full.txt`, `control-deny-full.txt`, `control-stduser-full.txt`.

## 4. Isolated test PAM service (specified and verified)

For the later PAM conversation worker (E1-S7), the lab uses a standalone PAM service that
accepts **only** the fixture accounts' throwaway passwords. It is **not** enabled
system-wide: no stock `pam.d` service includes or references the service name.

- Service file: `/etc/pam.d/wex-e1s-test` (reference copy `pam-wex-e1s-test.ref`).
- Allow-list: `/etc/wex-e1s-fixtures` (mode `640 root:root`; one account per line:
  `bob`, `alice`, `carol`, `eve`; reference copy `pam-wex-e1s-fixtures.ref`).
- Modules (all present in the guest under `/lib/x86_64-linux-gnu/security/`):
  `pam_listfile.so` (restrict to the allow-list, `onerr=fail`), `pam_unix.so` (verify the
  throwaway password against `/etc/shadow`), `pam_deny.so` (fail closed), `pam_permit.so`
  (prime success). The guest has 46 PAM modules; only these four are used.

Verified with a minimal PAM test client (`pamtest`, compiled on the guest):

| Case | Result |
| --- | --- |
| `bob`/`alice`/`carol`/`eve` with their throwaway password | `AUTHENTICATE: SUCCESS` |
| `bob` with a wrong password | `AUTHENTICATE: FAIL` (Authentication failure) |
| `root`, `debian`, `wexadm` (not in the allow-list) | `AUTHENTICATE: FAIL` |
| unknown user | `AUTHENTICATE: FAIL` |

Output: `pam-test-output.txt`. A passing test module proves orchestration only, not the
production distro stack (per section 8).

## 5. Snapshot/restore procedure (specified, not executed)

The guest disk is a qcow2 overlay on the lab host (`/var/tmp/wex-vmb/vmb-overlay.qcow2`,
created by the VM B launcher over a base image). The disk is **94% full**
(71G/79G, ~5.2G free), so free space before snapshotting.

**Free space first** (remove only confirmed-superseded, owned artifacts; coordinate with
their owner per `TOOLING.md`):

- `/home/debian/wex-g4` (~28G): old `wine-verify-r6` replay dirs, `wine-control-channel`,
  and other superseded lab runs.
- `/home/debian/development-validation-20261101` (~22G): old edge/chromium/chrome install
  fixtures and per-run runtimes.
- Older lab prefixes: `edge-net-20261105` (~1.9G), `chrome-stub-20261105` (~1.7G),
  `edge-realigned` (~1.3G).
- Regenerable caches/logs: `apt-get clean` (`/var/cache/apt`, ~454M) and
  `journalctl --vacuum-size=50M` (`/var/log/journal`, ~177M).
- Old tarballs: `wine-4315be8-wow64.tgz` (~538M), `private-consumers.tgz` (~449M).

Removing the two large directories (~50G) brings usage from ~94% to ~30%.

**Snapshot (before any E1-S lab change):** stop the VM, then either take an internal
qcow2 snapshot (`qemu-img snapshot -s e1s-baseline <overlay>`) or copy the overlay to a
dated backup. Take it **before** creating fixture accounts / PAM artifacts so the baseline
is clean (run `cleanup-fixtures.sh` first if they already exist).

**Restore (after each lab run):** stop the VM, then apply the internal snapshot
(`qemu-img snapshot -a e1s-baseline <overlay>`) or delete the overlay and let the launcher
recreate it from the base (coarse: discards all changes since the base).

No snapshot was taken for this work package; the procedure is for the later lab packages
(S9/S11/S16/S20).

## 6. Cleanup

`cleanup-fixtures.sh` (run as root on the guest) removes every E1-S2 change: the four
fixture users and their home/`.wine` dirs, the root-only password file, and the isolated
PAM service plus allow-list. It is idempotent and prints no passwords. It optionally
purges the `gcc`/`libpam-dev` packages that were installed only to verify the PAM service
(commented out by default).

## 7. Guest changes made by this work package

- Created users `bob`, `alice`, `carol`, `eve` (and home dirs); `alice` added to `sudo`.
- Created `/root/e1s-fixtures-passwords` (`600 root:root`) with the throwaway passwords.
- Created `/home/bob/.wine` owned by `bob` (the interactive prefix).
- Created `/etc/pam.d/wex-e1s-test` and `/etc/wex-e1s-fixtures` (isolated, not enabled
  system-wide).
- Installed `gcc` and `libpam-dev` (to compile the PAM test client).
- Wrote lab logs under `/tmp/gsr-out/e1s-*.txt` and a bob-flavored runner
  `/tmp/gsr/guest-elev-run-bob.sh`.

Reverting all of the above: run `cleanup-fixtures.sh` (and optionally purge
`gcc`/`libpam-dev`). The elevation controls left the approval file
`/tmp/wexelev/approve.all` present (restored after the deny run).
