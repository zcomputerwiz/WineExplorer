<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Vintner Boot Integration Plan

Date: 2026-11-07  
Status: written as a plan on 2026-11-07. Sections 4.1, 4.2 (behind a build option), 4.4 and 4.5 are implemented
since then, and 4.3 in part (Safe Mode through Vintner's variant); each section has its own status. GRUB is still
the default loader.  
Sources read: WineExplorer `development` at `d718f379`; Vintner Boot (`D:\Projects\vintner-boot`, sibling
repository) `local-test-env` at `cbc683b`, which is `main` plus two CI/docs commits. Line numbers refer to those
commits.

WineExplorer will eventually use Vintner Boot as its boot and recovery platform. Vintner Boot is a Debian 13
boot platform: it owns the bootloader (systemd-boot on UEFI, a static GRUB on BIOS), publishes each kernel as a
versioned image with bounded boot attempts and automatic fallback, hides the boot menu, and ships an independent
recovery image. This document lists what has to change in each project before the switch.

## 1. Summary

The projects are compatible in principle: both target Debian 13 on x86-64, both use systemd, and both are
GPL-2.0-or-later. Today they overlap on the same layer. WineExplorer's text-mode setup partitions the disk, installs
GRUB and offers Safe Mode as a GRUB menu entry. Vintner Boot removes GRUB and owns the disk layout, the bootloader,
the boot menu and the kernel command line.

Two things already work in our favour:

- Vintner's kernel and initramfs hooks do nothing unless the running kernel's command line has
  `vintner.bootstorage=` (`src/internal/publish/publish.go:314-337` in Vintner Boot). Installing Vintner
  packages into the live-build chroot or the live ISO therefore cannot break either.
- WineExplorer's Safe Mode needs one token on the command line, `wexmode=menu`, which opens the Advanced Options
  menu; the menu chooses the mode at run time (`docs/SAFE_MODE.md`). Only the way that token gets onto the
  command line has to change.

## 2. Who owns what after the switch

| Area | Today (WineExplorer) | After the switch |
|---|---|---|
| Partition table and boot partitions | textsetup | textsetup creates the layout Vintner requires (section 4.1) |
| Bootloader | textsetup: `grub-install`, `update-grub` | Vintner (systemd-boot on UEFI, static GRUB on BIOS) |
| Kernel command line | `/etc/default/grub` and `grub.d` | Vintner manifest; gen 1 written at install time |
| Boot menu | GRUB, hidden, 1 s timeout | Vintner, hidden; key press (UEFI) or Shift (BIOS) |
| Safe Mode entry | `/etc/grub.d/11_wineexplorer_advanced` (F8, `wexmode=menu`) | Vintner variant entry (needs V1) |
| Kernel fallback | none | Vintner boot counting |
| Recovery | Safe Mode in the installed system; ISO rescue shell | Safe Mode stays; Vintner recovery image added |
| Boot splash | Plymouth masked | Open decision D2 |

## 3. Prerequisites in Vintner Boot

All paths in this section are in the Vintner Boot repository.

**V1. Product boot entries (variants).** Required for Safe Mode.

- The command line is built into each UKI (signed in Secure Boot tier A).
- `firmware/uefi/loader.conf` sets `editor no` and `auto-entries no`.
- The BIOS `firmware/bios/grub.cfg` is static and never regenerated.
- Any other entry in `/EFI/Linux` or `/loader/entries` is reported as foreign (`src/internal/state/state.go`).

Vintner needs a way for a product to declare extra command-line tokens (for example a file that adds
`wexmode=safecmd`). The publisher would then build a matching entry per generation on UEFI and BIOS. The same
mechanism serves an XP-style "Enable VGA Mode" (`nomodeset`). Decided 2026-11-07: a variant boot changes no boot
count or state, so each variant is its own uncounted UKI (WO-0010 decisions D-C and D-D).

**V2. Changing the kernel command line after installation.** Each publish copies the command line of the newest
`GOOD` image (`publish.go:436-456`). Vintner does not read `/etc/kernel/cmdline` and has no command to change the
line. Desktop users will need `nomodeset`, NVIDIA options or a serial console.

**V3. Product names in the menu.**

- BIOS titles are hard-coded as `vintner os <g>` and `vintner recovery A|B` (`firmware/bios/grub.cfg:65-70`).
- `vintner_title` is documented in `firmware/bios/INTERFACE.md:30` but never used.
- Recovery UKI titles are fixed in `recovery/build.sh:73`.
- UEFI entries use `PRETTY_NAME` from the installed `/etc/os-release`, so WineExplorer controls that text.

**V4. Real Debian packages.** `debian/` is a stub:

- `vintner-core.install` ships only the Plymouth theme and `plymouthd.conf`. It does not install the `vintner`
  binary, the kernel and initramfs hooks, `vintner-bless.service`, the unit masks or `/etc/kernel/install.conf`.
  CI installs those by hand (`ci/lib/vintner-base.sh:278-294`).
- `vintner-core` is `Architecture: all` although it will carry a Go binary.
- The designed `Conflicts` with the GRUB, shim and systemd-boot packages (`docs/architecture.md:146-155`) are not
  in `debian/control`.
- `/etc/plymouth/plymouthd.conf` is probably also shipped by Debian's `plymouth` package. Recommended (WO-0010
  decision D-E): Vintner stops shipping it, and `vintner install --plymouth-theme` selects the boot screen, so
  WineExplorer passes its own theme.

The WineExplorer ISO installs from packages, so it needs these debs.

**V5. An installer entry point that works from a chroot.** WineExplorer installs by copying the live root and
finishing inside a chroot, where `/proc/cmdline` is the live ISO's and the publish hooks do nothing.
`vintner-installer` is designed but not written (`integration/installer/README.md`). The only working reference is
`ci/lib/vintner-base.sh`: partitioning, manifest, grubenv, `core.img`, and the first publish with
`--cmdline-file`. Vintner should provide one documented command or script for "install onto `/target` from a
chroot".

**V6. Restart into recovery.** `LoaderEntryOneShot` is designed (ADR-0003) but no `vintner` command sets it.
WineExplorer would need it for a "Restart into Recovery Console" action. Vintner has only a CLI. Recommended
(WO-0010 decision D-F): Vintner offers `--json` commands and no D-Bus service, because its Go code may use only the
standard library. A privileged WineExplorer D-Bus service runs them, which keeps the `CLAUDE.md` preference for
D-Bus on WineExplorer's side.

**V7. Secure Boot beyond tier A.** Only tier A is implemented: a per-machine key enrolled in `db`, which needs the
firmware in setup mode (`docs/secure-boot.md`). Tiers B (MOK) and C (Debian-signed) are not. WineExplorer does not
boot with Secure Boot on today (unsigned GRUB installed with `--removable`), so this is not a regression. Ordinary
PCs with Secure Boot on stay unsupported until tier B or C exists.

**V8. Open Vintner bugs that would affect WineExplorer.**

- Recovery A's boot counter is never re-armed, contrary to ADR-0003:63 and `firmware/bios/INTERFACE.md:72`
  (`src/cmd/vintner/bless.go` exits early for recovery). After one fallback, recovery A stays used up.
- The side-loaded file check scans boot storage only, not the ESP (threat model T6).
- The `vintner` binary is not in the recovery initramfs, so recovery has no rollback or diagnostics commands yet
  (`recovery/BUILDING.md:94-98`).

## 4. Changes in WineExplorer

### 4.1 Partitioning in text-mode setup (`installer/textsetup/screens.c`)

Vintner requires GPT with:

- a BIOS Boot partition (type `21686148-6449-6e6f-744e-656564454649`, about 2 MiB) on BIOS machines;
- an ESP (512 MiB on a new disk; an existing ESP is reused and never resized);
- a boot storage partition (XBOOTLDR, type `bc13c2ff-59e6-4262-a352-b275fd6f7172`, FAT32, about 2 GiB, never
  encrypted, never on LVM or RAID);
- the ext4 root.

Changes:

- Always create GPT. Today BIOS machines get an MBR label (`screens.c:203`, `screens.c:648`).
- Unattended auto-partition (`screens.c:647-700`) creates the four partitions above.
- Find the ESP by its GPT type GUID. Today it is "vfat and the label contains EFI, or partition 1"
  (`screens.c:242-244`).
- Manual partitioning must create or require the ESP and boot storage. Today a UEFI install without an ESP
  continues and the `grub-install` failure is swallowed (`screens.c:876-885`, `screens.c:1620-1629`).
- Boot storage is never added to fstab; Vintner mounts it on demand.
- RAID roots: boot storage has to be a plain partition on one disk. Decide whether that is acceptable (D4).

Status (2026-11-07, branch `claude/vintner-boot-integration`): implemented, GRUB still the loader. Every disk Setup
prepares is GPT: BIOS boot (2 MiB), ESP (512 MiB, FAT32; an existing one is reused and never resized), boot storage
(2 GiB, FAT32, GPT attribute 63 "no-auto"), then the ext4 root. MBR disks that have partitions are refused. Details
and screens: [INSTALLER.md](INSTALLER.md), Partition Editor. Test: `installer/tests/check-disk-layout.py`, called by
`install-unattended.sh` (`WEX_TEST_FIRMWARE=uefi` for OVMF) and `install-interactive.sh`.

Verified 2026-11-07 on the 2026-10-17 ISO repacked with the new `textsetup` (QEMU/KVM, 20 GiB virtio disk):
`install-unattended.sh` passes on SeaBIOS and on OVMF (zero-input install to the desktop, layout, attribute 63,
fstab; GRUB core image in the BIOS boot partition on BIOS, `x86_64-efi --removable` on UEFI). Scripted interactive
cases, checked by reading the disk back: a blank disk (C, notice, list, format screen); an MBR disk with an ext4
partition (ENTER and C refused, nothing written until D frees the disk; the freed disk gets the notice, and the old
ext4 signature is gone from the new BIOS boot partition); a GPT disk with a 100 MiB ESP holding a file (ENTER on
the ESP refused, the notice lists only BIOS boot and boot storage, the ESP keeps its FAT UUID and its file).
`install-interactive.sh` was not run end to end: its OOBE steps are newer than that ISO.

Agreed with the Vintner Boot side (WO-0010 phase 1, 2026-11-07):

- Vintner relies only on the boot storage type GUID for the layout, and at run time on
  `vintner.bootstorage=PARTUUID=<p3>`, which must equal the manifest's `boot_storage.partuuid`. The PARTLABEL and
  the FAT label are cosmetic. The sizes match Vintner's architecture section 5 (2 GiB default, about 1.5 GiB
  measured need; the ESP holds only the loader).
- On BIOS, Vintner's core.img finds boot storage by its FAT file system UUID (`search --fs-uuid`), written at
  install time: boot storage must never be reformatted after the Vintner install.
- Attribute 63 makes `systemd-gpt-auto-generator` skip boot storage whatever `/boot` holds, which matters once
  systemd-boot reports `LoaderDevicePartUUID`. Vintner's check is row A5 of `ci/integration/uefi-bless/run.sh`;
  Vintner adds the bit to its layout contract (WO-0010 3.1).

### 4.2 Bootloader step (`installer/textsetup/screens.c:1562-1630`)

- Remove `device.map`, `sfdisk -A`/`--part-type`, `grub-install` and `update-grub`.
- After purging `live-boot` and `live-config`, run `update-initramfs -u` in the target. Setup never does this
  today, so the first Vintner image would otherwise carry the live ISO's initramfs.
- Run the Vintner install sequence (V5). Vintner's order is: deploy and verify the loader and recovery, remove the
  stock loader packages, verify again, install the Vintner packages (`docs/architecture.md:130`). Decide what setup
  does when it fails (D5).
- The installed system is a copy of the live root, so `grub-pc`, `grub-pc-bin`, `grub-efi-amd64-bin`,
  `grub-common` and `grub2-common` from `installer/live-build/config/package-lists/wineexplorer.list.chroot:74-78`
  land on disk. `grub-pc` conflicts with Vintner and must be purged from the target.
- Generation 1's command line is set here and inherited by every later kernel until V2 exists:
  `root=UUID=<root> ro vintner.bootstorage=PARTUUID=<boot storage>` plus our options. The serial console for test
  VMs (`screens.c:1567-1572`) has to be decided at this point too.
- Delete `/etc/default/grub.d/wineexplorer.cfg` (both copies: the live-build include and the string written at
  `screens.c:1574-1585`) once GRUB is gone.

Status (2026-11-09): implemented behind a build option, GRUB still the default (D1: Safe Mode needs Vintner's
variants first).
- `installer/build-iso.sh --vintner=DIR` puts `vintner-core` and `vintner-platform` from DIR on the ISO as files
  under `/setup/vintner`, together with Setup's step `installer/textsetup/install-vintner.sh`. It also adds
  `systemd-boot-efi`, `systemd-ukify` and `sbsigntool` (vintner-core's dependencies) to the live system. Neither
  Vintner package is installed in the live system, and an ISO built without the option has nothing of Vintner.
- When `/setup/install-vintner.sh` exists, text-mode Setup runs it in place of the GRUB step. It runs after the
  live packages are purged, with the live system's `/dev`, `/proc`, `/sys` and `/run` bound into the target. The
  step follows Vintner Boot's `ci/install` row:
  1. install vintner-core in the target and unpack vintner-platform beside it;
  2. write `/etc/kernel/cmdline` (`quiet`) and run `update-initramfs -u -k all`;
  3. mount boot storage, the ESP (also on BIOS machines) and efivarfs (UEFI only);
  4. run `vintner install --disk D --firmware both --root-partuuid --boot-storage-partuuid --esp-partuuid
     --payload-dir ... --update-initramfs`. The root needs `--root-partuuid` because its type is the generic Linux
     one;
  5. once `grub-pc` is gone, install vintner-platform and delete `/etc/default/grub.d/wineexplorer.cfg`.
  The log is `/var/log/vintner-install.log` on the new installation.
- `--firmware both` is always used, because every disk Setup prepares has a BIOS boot partition and an ESP, so the
  disk starts on either firmware. When Setup itself started through BIOS there is no efivarfs. `vintner install`
  then writes no NVRAM entry and relies on `\EFI\BOOT\BOOTX64.EFI`.
- If the step fails, Setup shows "Setup cannot continue" with the log's path and quits (D5). There is no GRUB
  fallback.
- The recovery images keep Vintner's own command line (it already has `console=ttyS0,115200`). Since 2026-11-10
  Setup passes `--recovery-title "WineExplorer Recovery Console"` (D6; Vintner Boot #82), so the menus show
  "WineExplorer Recovery Console" and "WineExplorer Recovery Console (backup)".
- Test: `WEX_TEST_LOADER=vintner installer/tests/install-unattended.sh` checks the installed disk with
  `installer/tests/check-vintner.py`:
  - vintner-core and vintner-platform are installed, and no stock loader is;
  - Setup's step ended well and GRUB has no configuration;
  - the manifest holds generation 1, GOOD, for both firmwares, with the root and boot storage on its command line;
  - the ESP holds Vintner's loader, also as `\EFI\BOOT\BOOTX64.EFI`.

Verified 2026-11-09 with Vintner Boot `main` at `79a0f61` (#78). The ISO was built in wexbuild from this branch, and
the installs ran in QEMU/KVM in WSL Debian.
- Zero-input install (`install-unattended.sh`, with the CD left in) passed on SeaBIOS and on OVMF. The installed
  disk reached the desktop and generation 1 was blessed GOOD. `vintner install` purged `grub-pc`,
  `grub-efi-amd64-signed` and `shim-signed`, which the target had from the live system.
- On OVMF it created the NVRAM entry `Boot0008 "Vintner Boot"`. On SeaBIOS it reported "no UEFI variables here" and
  wrote only the removable path: the first boot test of that case.
- Started again from the disk alone, with no CD:
  - SeaBIOS booted `/vintner/bios/os-1/vmlinuz` through Vintner's GRUB;
  - OVMF's systemd-boot selected `vintner-os-1.efi` (the test machines' OVMF keeps no variables across a power-off,
    so this came through `BOOTX64.EFI`);
  - on both, `vintner-bless.service`, `wex-startup-check.service` and `boot-complete.target` were active, Wine's
    32-bit and 64-bit Notepad opened on the desktop, and generation 1 stayed GOOD.

Still open: F8 on a Vintner install (section 4.3; Safe Mode through Vintner's variant works since 2026-11-10), and
the Plymouth decision (D2).
Not tested: the interactive install (`install-interactive.sh`) on a Vintner ISO, an existing ESP that holds
another system, and the "Setup cannot continue" screen (no run has failed the step).

Status (2026-11-08): not started, and now unblocked. `vintner install` is on Vintner Boot's `main` (PRs #71-#73,
merge `0753aec`), as reported by the Vintner session:
- Usage: `vintner install --disk D --firmware uefi|bios|both [--target / --esp --boot-storage --payload-dir]
  [--check] [--resume]`.
- It publishes generation 1, takes over `BOOTX64.EFI` and the BIOS boot code, and then removes the stock loader.
- Its documentation is Vintner Boot's `docs/accepted-platforms.md` and `docs/architecture.md` section 5.
- Its `ci/install` row boots the result on SeaBIOS and OVMF.
- Since PR #76 (merge `851963d`) it also creates the UEFI NVRAM boot entry. The health report flags a missing or
  demoted entry, and `vintner boot-entry` recreates it. The bless point is still `boot-complete.target`, so
  `wex-startup-check.service` keeps its ordering.

Read those before building this step; the 2026-11-07 notes below predate the command.

Status (2026-11-07): not started; it waits for `vintner install`. Per the Vintner Boot side:

- There is no `vintner install` and no Debian package yet (WO-0010 3.2 and phase 2). The install sequence lives in
  Vintner's CI (`ci/lib/vintner-base.sh`), which 3.2 replaces. Callable today on `main`: `vintner publish`, `bless`,
  `reconcile`, `manifest`, `secureboot`, and `recovery/build.sh`.
- Build against `origin/main` for the CLI and `schema/manifest.md`; the install entry point comes with phase 3. Ask
  the Vintner side to pull a command forward if 4.2 needs it earlier.
- The plan above matches 3.2: Setup writes `/etc/kernel/cmdline` and runs the sequence inside the target chroot.
  `vintner install --esp` writes `esp.partuuid` into the manifest itself; Setup does not.
- `vintner publish` refuses an image whose command line lacks `vintner.bootstorage=PARTUUID=`.
- BIOS needs nothing from Setup beyond the GPT disk, the BIOS boot partition and boot storage's FAT UUID; Vintner
  builds core.img from its own module list.

### 4.3 Safe Mode

Goal (owner, 2026-11-07): starting up and recovering should feel familiar to people who know Windows. The Vintner
side of this is recorded in Vintner Boot's `docs/work-orders/WO-0010-product-readiness.md` ("Decisions" and items
4.1-4.8).

Status (2026-11-08): the WineExplorer side works with GRUB and is described in `docs/SAFE_MODE.md`. One
`wexmode=menu` boot shows the XP Advanced Options menu on `tty1` before `basic.target`. The menu chooses the mode
at run time and keeps it in `/run`; a systemd generator selects the services. Nothing on disk changes. The GRUB
entry `11_wineexplorer_advanced` carries `--hotkey=f8`, and during GRUB's hidden timeout a menu hotkey boots its
entry directly, so F8 works on BIOS and UEFI. It was tested on SeaBIOS and OVMF. Since then (2026-11-08) Safe Mode
logs on through the Welcome screen, a permanent VM test (`installer/tests/safe-mode.sh`) passes all seven phases on
both firmwares, and the remaining WineExplorer work is planned in `docs/SAFE_MODE_WORKORDER.md`. Its D1 (the
start-up failure screen) and D3 (Last Known Good) are the items that meet Vintner.

Vintner's variants (V1, the Vintner session, 2026-11-09): next in its queue, after its Secure Boot row (#80). The
contract, from WO-0010 4.3:

- A product declares a variant in `/etc/vintner/variants.d/<id>.conf`: a title and extra command-line tokens,
  validated as in 4.2. Vintner appends `vintner.variant=<id>` and the product's tokens (here `wexmode=menu`) to that
  variant's command line, so `wex-bootmode` keeps reading `wexmode=` and needs no change. Wine learns the mode from
  `/run/wineexplorer/bootmode`, which the menu writes; a variant that carries `wexmode=safe` directly needs
  `wex-bootmode-record.service` (`docs/SAFE_MODE_WORKORDER.md` B0).
- UEFI: one extra uncounted UKI per retained generation and variant, never the default. BIOS: one more command line
  per variant in `slot.cfg`, listed in the Shift menu.
- Choosing a variant from the running system is `vintner next-boot variant <id>`: that variant of the generation
  that would boot next, for one boot, with no state change. It comes with 4.5/V6, not V1, so with V1 alone the
  variants can only be chosen in the loader's menu. The Vintner session gives the exact syntax when it starts V6.

Status (2026-11-10): V1 is on Vintner Boot's `main` (PR #81). WineExplorer declares the variant in
`installer/vintner/advanced.conf`, which the session package installs as the conffile
`/etc/vintner/variants.d/advanced.conf`: the title "Advanced Options" (until 2026-11-10 "Advanced Options for
WineExplorer"; Vintner puts the generation's title, which names WineExplorer, in front of it) and the token
`wexmode=menu`.
An ISO built with `--vintner` and the Vintner packages from `8b6eac4` installed with no input on SeaBIOS and on OVMF.
`installer/tests/check-vintner.py` found the variant in the manifest, in `slot.cfg`, and as
`vintner_variant-os-1-advanced.efi` in `EFI/Linux` on boot storage (152 MB, as large as the generation's UKI). Both
loader menus list it second:

- BIOS (Vintner's GRUB, Shift held): `vintner os 1`, `vintner os 1: Advanced Options for WineExplorer`,
  `vintner recovery A`, `vintner recovery B`.
- UEFI (systemd-boot, a key held): "Debian GNU/Linux 13 (trixie), 6.12.116+deb13-amd64", the same followed by
  "(Advanced Options for WineExplorer)", the two recovery images with their build, and "Reboot Into Firmware
  Interface". These titles come from the UKI's `os-release`. The Vintner session will give both firmwares one title
  source, the product name and the generation, as part of its 4.1.

Since Vintner Boot #82 (4.1, `014aaad`, 2026-11-10) both loaders show the same titles: the installed `PRETTY_NAME`
and the publish date, the variant as that name followed by `: ` and its title, and the recovery images under the
product's `--recovery-title`. An ISO built with those packages (`wex-v2`) installed with no input on SeaBIOS and
OVMF, and both disks carry, in `slot.cfg` and in the UKIs' `.osrel`:

- `Debian GNU/Linux 13 (trixie) (2026-11-10)`;
- `Debian GNU/Linux 13 (trixie) (2026-11-10): Advanced Options for WineExplorer`;
- `WineExplorer Recovery Console` and `WineExplorer Recovery Console (backup)`.

The ordinary entry names Debian because WineExplorer leaves Debian's `/etc/os-release` as it is. The owner decided
on 2026-11-10 that the menu names WineExplorer. `/etc/os-release` stays Debian's, because other programs read it.
Instead, the product declares the name to Vintner, which uses it in place of `PRETTY_NAME` in both loaders:
`WineExplorer (2026-11-10)` and `WineExplorer (2026-11-10): Advanced Options`. The Vintner session is asked for that
setting (a conffile the product ships, like `variants.d`, or an install option). GRUB, the default until then, names
WineExplorer through `GRUB_DISTRIBUTOR` (`docs/INSTALLER.md`).

The Safe Mode VM test through the variant (`WEX_TEST_LOADER=vintner`) passed all seven phases on OVMF. On SeaBIOS
one `safecmd` start showed a black 1280x800 console instead of the menu for three minutes (the variant was
highlighted and chosen; the journal, cut off by the forced power-off, ends with the menu's unit started); the other
six passed, and a second run passed all seven. That is one start in 22 through the variant that day. The VM test now
writes the journal to disk (Alt+SysRq+S) before stopping a failed VM, and presses Down to see whether the menu
runs unseen. One suspect: vintner-core puts its Plymouth theme into the initramfs (`ShowDelay=0`), while the menu
quits Plymouth itself (WineExplorer masks Plymouth's quit units, decision D2 open).

With `WEX_TEST_LOADER=vintner`, the Safe Mode VM test chose the variant in those menus and passed all eight of its
phases on both firmwares (`docs/SAFE_MODE.md` section 6). grub2-common stays installed on a Vintner install, and
its `update-grub` writes a `/boot/grub/grub.cfg` that Vintner's GRUB never reads; the Vintner session lists
diverting it for 4.x.

Left for Vintner:

- Remove the GRUB generator once Vintner variants exist: drop
  `installer/live-build/config/includes.chroot/etc/grub.d/11_wineexplorer_advanced` from the live-build include
  and from the session package.
- Done (2026-11-10, above): one Vintner variant, "Advanced Options", with the token `wexmode=menu`. A variant boot
  changes no boot count or state (owner decision): a good image stays good, a bad image stays bad.
- Done (2026-11-10): Last Known Good Configuration puts back the machine registry's control set from the last
  start that counted, on either loader (`docs/SAFE_MODE.md` section 4). With Vintner it also restarts into the
  previous good generation (`vintner next-boot previous-good`) when `vintner status --json` says the booted
  generation is newer than the newest `GOOD` one. Return to OS Choices Menu restarts into the loader's menu
  (`vintner next-boot menu`), which lists the generations, the variant and the Recovery Console. Both were tested
  on both firmwares (Vintner Boot `2ece4a9`).
- Enable VGA Mode stays grayed until there is an X fallback that does not force `modesetting` (section 6). Then
  keep the GPU's KMS driver from loading for the boot (a `modprobe` blacklist under `/run`), or declare a second
  variant with `nomodeset`.
- **"WineExplorer did not start successfully".** XP shows this screen after a failed start. When
  `vintner status --json` reports failed attempts of the booted image, an ordinary boot shows the same choices
  early, with a 30-second countdown that defaults to "Start WineExplorer Normally". A Safe Mode choice records the
  mode for one boot and restarts into the Advanced Options variant (`vintner next-boot variant`), which goes
  straight to that mode. The boot that showed the screen was counted, so finishing it in Safe Mode would mark the
  image good.
- F8 at start reaches the menu. On Vintner's static BIOS GRUB the same `--hotkey=f8` on the variant's entry gives
  F8 during the hidden timeout (WO-0010 item 4.8); systemd-boot on UEFI needs its own key handling.

### 4.4 Boot health check

Vintner marks a boot good once `boot-complete.target` is reached (`integration/systemd-units/vintner-bless.service`
in Vintner Boot). Without a WineExplorer check, a boot where LightDM or Xorg fails still counts as good. If we add
one (`Before=boot-complete.target`, `RequiredBy=boot-complete.target`):

- The setup stages must count as healthy. LightDM is deliberately held back while
  `/etc/wineexplorer/setup/stage` exists, and setup reboots partway through, so a strict check could use up the
  tries and fall back to recovery during installation.
- Safe Mode boots from the Advanced Options variant are not counted at all (owner decision), so they need no rule.
- Give the unit a short `Description=`; Vintner's CI found systemd cuts status lines at 80 columns on serial.

Recommended rule (D3), from Windows: XP updates its last known good configuration after a successful logon. The
check passes when the logon screen is up: the greeter (`logonui.exe`) is running, or a setup stage screen is
showing.

Implemented for GRUB (2026-11-08, `docs/SAFE_MODE_WORKORDER.md` D1) as `wex-startup-check.service`
(`scripts/wex-startup-check.sh`). It is `Type=oneshot`, `WantedBy=multi-user.target`, and
`Before=`/`RequiredBy=boot-complete.target`, with the description "Start-up check".
- It passes when the `logonui.exe` program itself (not Explorer's command line naming it) or the GTK greeter runs
  for `lightdm`, when a graphical user session exists
  (automatic logon), or when a setup stage unit is active. It then clears GRUB's `wex_boot_pending`.
- Starts through the Advanced Options entry and Safe Mode choices do not count, and the check returns at once for
  them.
- With Vintner, the same unit gates the bless; only the GRUB variable goes away.

### 4.5 Live ISO menu

"Continue Setup from the hard disk" (`installer/live-build/config/hooks/live/9000-grub-menu.binary:27-39`) runs
`configfile /boot/grub/grub.cfg` from the installed root. That file will not exist. Chain-load the disk's ESP
loader instead, or remove the entry. The ISO's own boot (live-build GRUB) is unaffected.

Status (2026-11-09): done. The entry still loads the installed `grub.cfg` when it exists (GRUB installs). Otherwise
it chain-loads Vintner's loader: on UEFI `\EFI\vintner\systemd-bootx64.efi`, found on the disk's ESP, and on BIOS
the disk's own boot code (`chainloader +1` on the disk that holds the setup stage file). Both Vintner installs
above went through it: the CD stayed in for GUI-mode setup.

### 4.6 sysdm.cpl "Startup and Recovery"

The button currently reports that systemd manages startup (`src/dlls/sysdm.cpl/advanced.c:43-46`). With
Vintner's `--json` commands (WO-0010 items 4.2, 4.5, 4.7) the dialog can work as on XP:

- "Default operating system": the one WineExplorer entry, read-only.
- "Time to display list of operating systems": `vintner menu-timeout`.
- "Time to display recovery options when needed": the countdown of the "did not start successfully" screen
  (section 4.3).
- "Edit": opens the kernel command line in Notepad, as XP opens `boot.ini`. Saving applies it with
  `vintner cmdline set`. A bad change then falls back to the previous generation by itself.
- "System failure": `kernel.panic` and the crash log settings.

The `vintner` commands need root. Following `CLAUDE.md` (D-Bus over spawning tools), sysdm.cpl talks to a
privileged WineExplorer D-Bus service that runs them; Vintner offers no D-Bus interface of its own.

### 4.7 Documentation

- `docs/INSTALLER.md` describes GRUB and its F8 entry on the installed system (line 93).
- `docs/ADMIN_GUIDE.md:40` names `grub-efi-amd64`, which is not installed.

Update both when the switch lands.

## 5. Decisions

Recorded 2026-11-07. The owner decided that the installer partitions and that a Safe Mode boot changes no boot
count or state. For the rest the owner set the goal of a Windows-familiar experience; the entries marked
"recommended" serve that goal and await the owner's confirmation.

- **Partitioning (decided).** textsetup partitions to Vintner's layout (section 4.1); `vintner install` only
  checks it and deploys.
- **Safe Mode boots (decided).** A Safe Mode boot changes no boot count or state (section 4.3).
- **D1. Safe Mode before Vintner variants (recommended): wait.** Windows users expect F8 and Safe Mode. Vintner
  becomes the default boot path only once its variants (WO-0010 item 4.3) exist.
- **D2. Plymouth (recommended): turn it on.** Use an original WineExplorer boot screen: a black background, the
  WineExplorer logo and a moving progress bar, as XP's start-up looks. The art must be original: no XP boot screen,
  no Windows logo and nothing traced from them (`CLAUDE.md`, `docs/ASSETS.md`). Until that art exists, use
  Vintner's `vintner-bgrt` (the PC maker's firmware logo, as Windows 8 and later show). Unmask Plymouth in
  `installer/live-build/config/hooks/normal/0500-enable-services.hook.chroot:48` and `screens.c:1631`; setup and
  OOBE already run `plymouth --quit` before starting their own Xorg.
- **D3. Health check (recommended): pass at the logon screen** (section 4.4).
- **D4. RAID roots (recommended): allow them.** XP installed onto RAID volumes. Put the ESP and boot storage on the
  first disk and say on the partition screen that the start-up partitions are not mirrored.
- **D5. Install failure (recommended): stop with an XP-style "Setup cannot continue" screen** that names the log
  file. No GRUB fallback code stays behind; running Setup again starts over, as on XP.
- **D6. Recovery naming (recommended): "WineExplorer Recovery Console"** and "WineExplorer Recovery Console
  (backup)", passed to `vintner install --recovery-title` (Setup does so since 2026-11-10). One known difference from XP: Vintner's recovery asks
  for no password (it is a BusyBox shell with the root mounted read-only), while XP's Recovery Console asked for the
  Administrator password. Document that anyone at the keyboard gets a root shell.

## 6. Smaller points

- Vintner supports x86-64 only. WoW64 only affects user space, so this is fine.
- Vintner only publishes kernels owned by a dpkg package (`publish.go:481-484`). setup copies the dpkg database
  with `/boot`, so this holds.
- WineExplorer ships no DKMS modules today. A proprietary NVIDIA driver (`nvidia-detect` is already installed)
  needs Vintner's Secure Boot tier B, with the machine key enrolled as a MOK and used by DKMS for signing (WO-0010
  decision D-G and item 5.3). On tier A it cannot load with Secure Boot on.
- The forced `modesetting` X configuration (`scripts/20-wineexplorer-modesetting.conf`, installed for all
  hardware by `installer/package-wine.sh:257`) leaves X without a fallback when no KMS driver loads. Enable VGA
  Mode (section 4.3) needs one.
- `unattended-upgrades` (enabled by OOBE) will install kernels unattended; Vintner publishes each one through its
  hook. Vintner keeps retained kernels with `APT::NeverAutoRemove`.
- `console=ttyS0` makes Plymouth fall back to its text theme; Vintner's CI adds `plymouth.ignore-serial-consoles`.

## 7. Suggested order

1. Vintner: V4 (packages) and V5 (chroot install entry point).
2. WineExplorer: section 4.1 partitioning (GPT everywhere, boot storage). This is useful on its own and can land
   while GRUB is still the loader.
3. WineExplorer: section 4.2 bootloader step behind a build option, keeping GRUB as the default.
4. Vintner: V1 (variants), V3 (names), V8 (recovery A re-arm).
5. WineExplorer: section 4.3 Safe Mode port, section 4.4 health check, section 4.5 ISO menu.
6. Vintner: V2, V6, V7 as needed; then make Vintner the default and remove the GRUB path.

Each WineExplorer step needs an installer VM run on BIOS and UEFI, including a 32-bit program check, before it is
committed as working.
