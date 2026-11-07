<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Vintner Boot Integration Plan

Date: 2026-11-07  
Status: plan only; nothing below is implemented in either project.  
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
- WineExplorer's Safe Mode only looks for `wexmode=safecmd` in `/proc/cmdline`
  (`scripts/wex-safemode-setup.sh:12-14`, `scripts/wex-safemode-pam-check.sh:7`). Only the way that token gets
  onto the command line has to change.

## 2. Who owns what after the switch

| Area | Today (WineExplorer) | After the switch |
|---|---|---|
| Partition table and boot partitions | textsetup | textsetup creates the layout Vintner requires (section 4.1) |
| Bootloader | textsetup: `grub-install`, `update-grub` | Vintner (systemd-boot on UEFI, static GRUB on BIOS) |
| Kernel command line | `/etc/default/grub` and `grub.d` | Vintner manifest; gen 1 written at install time |
| Boot menu | GRUB, hidden, 1 s timeout | Vintner, hidden; key press (UEFI) or Shift (BIOS) |
| Safe Mode entry | `/etc/grub.d/11_wineexplorer_safemode` | Vintner variant entry (needs V1) |
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

### 4.3 Safe Mode

Goal (owner, 2026-11-07): starting up and recovering should feel familiar to people who know Windows. The Vintner
side of this is recorded in Vintner Boot's `docs/work-orders/WO-0010-product-readiness.md` ("Decisions" and items
4.1-4.8). The WineExplorer side, recommended here:

- Remove the GRUB generator once Vintner variants exist: drop
  `installer/live-build/config/includes.chroot/etc/grub.d/11_wineexplorer_safemode` from the live-build include
  and from the session package (`installer/package-wine.sh:260-262`).
- Declare one Vintner variant, "Advanced Options", with the token `wexmode=menu`. A variant boot changes no boot
  count or state (owner decision): a good image stays good, a bad image stays bad.
- **Advanced Options Menu.** A `wexmode=menu` boot shows an XP-style text menu early in boot, before the display
  manager, on `tty1`. It offers the XP F8 list, with what each item does here:

  | Item | Here |
  |---|---|
  | Safe Mode | `wexmode=safe` for this boot: the desktop with the minimum of services (new) |
  | Safe Mode with Networking | `wexmode=safenet` (new) |
  | Safe Mode with Command Prompt | `wexmode=safecmd`, as today (`scripts/wex-safemode-setup.sh`) |
  | Enable Boot Logging | Persistent, verbose journal for this boot, plus a boot log in the Windows directory, as XP writes one |
  | Enable VGA Mode | Keep the GPU's KMS driver from loading for this boot (a `modprobe` blacklist under `/run`), with an X fallback that does not force `modesetting`; see section 6. If that proves unreliable, declare a second variant with `nomodeset` |
  | Last Known Good Configuration | `vintner next-boot previous-good`, then restart |
  | Directory Services Restore Mode | Grayed out (does not apply) |
  | Debugging Mode | Kernel log level raised for this boot |
  | Disable automatic restart on system failure | `kernel.panic=0` for this boot |
  | Start WineExplorer Normally | Continue this boot in normal mode |
  | Reboot | Restart |
  | Return to OS Choices Menu | `vintner next-boot menu`, then restart |

  `wex-safemode.service`, `wex-safemode-setup.sh` and the PAM check keep working on the `wexmode=` token. They
  gain the `safe` and `safenet` modes.
- **"WineExplorer did not start successfully".** XP shows this screen after a failed start. When
  `vintner status --json` reports failed attempts of the booted image, an ordinary boot shows the same choices
  early, with a 30-second countdown that defaults to "Start WineExplorer Normally". A Safe Mode choice records the
  mode for one boot and restarts into the Advanced Options variant (`vintner next-boot variant`), which goes
  straight to that mode. The boot that showed the screen was counted, so finishing it in Safe Mode would mark the
  image good.
- Press F8 at start to reach the menu (UEFI; BIOS uses Shift unless Vintner's WO-0010 item 4.8 finds a way).

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

### 4.5 Live ISO menu

"Continue Setup from the hard disk" (`installer/live-build/config/hooks/live/9000-grub-menu.binary:27-39`) runs
`configfile /boot/grub/grub.cfg` from the installed root. That file will not exist. Chain-load the disk's ESP
loader instead, or remove the entry. The ISO's own boot (live-build GRUB) is unaffected.

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

- `docs/INSTALLER.md` describes GRUB on the installed system and still says `GRUB_TIMEOUT=0` (line 93); the code
  uses `1` since `ccbf98ba`.
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
  (backup)", passed to `vintner install --recovery-title`. One known difference from XP: Vintner's recovery asks
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
