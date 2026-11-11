# Debian GNU/Linux with WineExplorer Installer & Live System

This document specifies the architecture, implementation, testing procedures, and milestone verification for the Windows XP–cloned installation system for Debian 13 (Trixie, amd64) running the WineExplorer desktop shell.

---

## Current validation status (2026-10-12)

The installer continuation passed the full Wine build and scripted interactive
and zero-input installations on QEMU/KVM. Missing-answer and unavailable-target
boot cases also left the test disk unchanged. See
[INSTALLER_TEST_REPORT.md](INSTALLER_TEST_REPORT.md) for evidence, final ISO
hashes, corrections and test limits. The milestone table below records imported
claims; the new report is the current acceptance evidence.

Build only from the open-resource source tree on the build VM. Do not use the
development VM's source overlay for an ISO. Never run make install in
~/inst/build. The package script uses a configured build from the same
assembled checkout and stages it with DESTDIR.

Native text setup build dependencies include `build-essential`, `pkg-config`,
`libfdisk-dev`, `libblkid-dev` and `libncurses-dev`, in addition to the Wine and
Debian live-build toolchains.

Existing live-build state requires `--clean`; incremental chroot reuse is not
supported. The builder fails early instead of reusing package-stage markers
with a bootstrap filesystem that lacks the kernel. Packaging also requires a
configured `BUILD_DIR` from the same checkout and never falls back to copying `/usr/local` from the
build machine.

```sh
cd ~/src
bash scripts/assemble.sh
mkdir -p build/wow64
cd build/wow64
../../wine/configure --enable-archs=i386,x86_64 --prefix="$PWD/install"   # WoW64: 32-bit programs are required
make -j8
cd ../..
sudo -n env BUILD_DIR="$PWD/build/wow64" \
    WORK_DIR=/srv/protonexplorer/wineexplorer-iso-work \
    OUT_ISO=/srv/protonexplorer/wineexplorer.iso bash installer/build-iso.sh
```

Run the scripted installation inside WSL2 Debian or Linux with QEMU/KVM,
ImageMagick and Tesseract installed. It creates a disposable disk, partitions and
formats it, leaves the CD attached across restart, completes GUI setup/OOBE, and
logs on the passwordless second user. Use a dedicated VM name; the script
replaces its state directory and disk.

```sh
bash installer/tests/install-interactive.sh /path/to/wineexplorer.iso \
    /path/to/gitignored/password.txt wex-install
```

The password file must stay outside Git; Windows CR/LF is stripped by the test.
Screenshots are saved under `/tmp/vmtest/wex-install/steps/`. Use
`scripts/vmtest.py text`, `shot`, `key` and `clicktext` to investigate failures.
Do not describe a timed-out test as passing or substitute manual clicks for an
unmodified automated rerun.

Use `vmtest.py shutdown NAME` for normal ACPI shutdown before installed-disk
inspection; `stop` quits QEMU abruptly and can leave an unreplayed filesystem
journal. `installer/tests/install-unattended.sh` now performs this shutdown
before its read-only checks. `boot-safety.sh` compares logical disk contents for
negative cases, using fixtures from `prepare-safety-isos.py`.

Recent fixes include blank-disk enumeration, partition selection, license
reflow, resume-from-disk boot entry, no live-medium eject prompt, template-prefix
generation in the Linux runner, user-specific template paths on first logon,
passwordless account mode, OOBE wording and wizard default-button focus.
Setup/OOBE Win32 programs no longer use `system()` to run Linux commands through
Wine's `cmd.exe`.

The GRUB menu explicitly selects the resume entry when setup is in progress,
including on media whose initial default is unattended installation. This
prevents the added entry from shifting the numeric default to the live desktop
after the first restart.

Unattended mode requires both the kernel argument and a readable answer file.
An absent file falls back to interactive setup. If a specified `TargetDisk`
cannot be found, setup stops without falling back to another disk. An omitted
`TargetDisk` currently selects the first disk; specify it explicitly for tests
(`/dev/vda` with the QEMU virtio configuration). Validate wrong-target behavior
against unchanged disk contents, and validate a complete zero-input install
separately.

LightDM/custom greeter integration (W7) passed fresh installed-system acceptance.
The installer polish work completed the visual parity and integration items:
- GUI setup uses Wizard97 headers, classic styling without themes, sequential billboard step indicator advancement, and an initially empty Owner Name field.
- Date and Time settings use a shared XP time zone list (`wextimezone.h`) with full Unicode CLDR Windows<->IANA mapping, date and time pickers, and DST indicator.
- Automatic Updates choice from OOBE is written to `/etc/wineexplorer/setup/automatic-updates` and applied to `/etc/apt/apt.conf.d/20auto-upgrades` via `unattended-upgrades`; `AutomaticUpdates=Yes/No` is supported in answer files.
- Text setup partition and format screens use Windows XP wording (`C: Partition4 ... on Disk 0 at Id 0 on bus 0 ... [GPT]`) with disk bus adaptation, keeping Linux device paths in logs only; the reboot countdown bar is red/brown without percentage text.
- Installed GRUB boots straight through (`GRUB_TIMEOUT_STYLE=hidden`, `GRUB_TIMEOUT=1`, `GRUB_TERMINAL=console`, Debian theme removed). F8 during that second boots the "WineExplorer Advanced Options (F8)" entry (`wexmode=menu`), which shows the XP Advanced Options menu ([SAFE_MODE.md](SAFE_MODE.md)).
- The menu names WineExplorer, not Debian (owner decision, 2026-11-10): `GRUB_DISTRIBUTOR="WineExplorer"`, so Debian's `10_linux` titles the entry "WineExplorer GNU/Linux" and its kernel submenu "Advanced options for WineExplorer GNU/Linux". `/etc/os-release` stays Debian's, because other programs read it. The UEFI install is `--removable`, so the distributor does not rename an EFI directory. Vintner Boot's titles get the name from the product (VINTNER_BOOT.md section 4.1). Tested 2026-11-10 on overlays of the BIOS and UEFI GRUB installs (`installer/tests/safe-mode.sh` installs the settings and checks grub.cfg): the menu, shown with Shift held on BIOS, lists "WineExplorer GNU/Linux", "Advanced options for WineExplorer GNU/Linux" and "WineExplorer Advanced Options (F8)".
- The desktop template properly displays the Recycle Bin icon on first logon.

The two GRUB items above apply to an ISO built with `--grub`. Since 2026-11-10 (owner decision) Setup installs
Vintner Boot by default; see [VINTNER_BOOT.md](VINTNER_BOOT.md). Its menu is hidden too, and names WineExplorer:
"WineExplorer (date)", then "WineExplorer (date): Advanced Options" and the two Recovery Console entries. To open it,
press F8 on UEFI machines, or hold Shift on BIOS machines.

---

## 1. Architecture and Stages

The installation medium is a bootable hybrid ISO image produced using Debian's `live-build` tool (`installer/build-iso.sh`). The boot environment and installed target payload share the same compressed root squashfs (`/live/filesystem.squashfs`).

### Stage 1: Text-Mode Setup (`installer/textsetup/`)
- **Console Environment**: Runs on `tty1` via `wex-textsetup.service` with `StandardOutput=tty` and kernel flags `quiet loglevel=3 systemd.show_status=false`.
- **Display**: 80×25 VGA text console cloning Windows XP: blue background (`0x0000AA`), white text, yellow highlight headers, status bar at line 24.
- **Disk Safety**:
  - The default boot entry boots the live system or prompts interactively; no disks are modified without explicit user confirmation.
  - Unattended mode is activated **only** when `wexunattend=<path>` is present on the kernel command line.
- **Partition Editor (`screens.c`)**:
  - Built with native `libfdisk` and `libblkid` (no fragile `sfdisk` shell wrappers).
  - Detects all disks (excluding live boot medium and loop/zram devices) and presents partitions and unpartitioned free regions in Windows XP layout (`Disk 0 (<model>, <size> MB) on <bus>`).
  - Key bindings:
    - **C**: Displays "Create partition" screen with minimum and maximum size prompts, creating the partition in the selected free region.
    - **D**: Displays XP partition deletion confirmation screen requiring **L** to confirm deletion.
    - **ENTER**: Selects an existing partition or creates a partition filling the free space.
  - **Partition layout** (the layout Vintner Boot needs, [VINTNER_BOOT.md](VINTNER_BOOT.md) section 4.1): every disk
    Setup prepares is GPT, on BIOS and UEFI machines alike. A blank disk, or an MBR disk with no partitions, gets a
    new GPT label. An MBR disk that has partitions is refused with a message (delete them first), as Windows Setup
    refuses an MBR disk on UEFI. Setup gives the disk three system partitions, in this order, before the first
    partition the user creates or selects:
    - BIOS boot, 2 MB, no file system: GRUB's core image on BIOS machines;
    - EFI system, 512 MB, FAT32;
    - Boot storage (XBOOTLDR type `BC13C2FF-59E6-4262-A352-B275FD6F7172`), 2048 MB, FAT32 labelled `BOOTSTORAGE`,
      with GPT attribute 63 ("no-auto") so `systemd-gpt-auto-generator` never mounts it. It is never listed in
      `/etc/fstab`: Vintner Boot mounts it by PARTUUID when it needs it.

    A system partition the disk already has (found by its GPT type, for example the EFI system partition of an
    installed Windows) is reused and never formatted unless it has no FAT file system. A notice lists the
    partitions Setup will add before anything is created; ESC goes back. System partitions are listed without a
    drive letter (`Partition2 (EFI System) [vfat]`) and cannot be selected as the install target; C: is the first
    partition that is not a system partition. Partitions start on 1 MB boundaries. A partition Setup has only
    planned (not yet written) shows `[New (Raw)]`, never the file system of a deleted partition that started at the
    same place, and Setup clears old file system signatures (`wipefs`) from the system partitions it creates
    before formatting them; the BIOS boot partition gets no file system.
  - Unattended mode always writes a new GPT label on the target disk: the three system partitions and the root in
    the rest of the disk.
- **Filesystem Formatting**:
  - "Format the partition using the ext4 file system (Quick)"
  - "Format the partition using the ext4 file system"
  - "Leave the current file system intact" (enabled only when the selected partition is already formatted as ext4; preserves existing files).
  - Nothing is written to disk until the user confirms the format screen.
- **Copy Engine**:
  - Driven by `/run/live/medium/setup/layout.inf` (generated at build time by `installer/tools/mklayout.py`).
  - Uses in-process copying via `copy_file_range` (falling back to buffered `read`/`write`), preserving ownership, modes, extended attributes, and timestamps. Symlinks and device nodes are recreated from file list metadata.
  - Byte-accurate progress bar: `bytes copied / total bytes` updated on every buffer chunk, with real-time file status line.
  - Error handling: Displays Windows XP retry/skip/cancel modal screen if a file read or write fails.
- **Target Sanitization & Swap**:
  - Automatically purges live-boot artifacts (`live-boot`, `live-config`, `/etc/sudoers.d/live`, `/etc/sudoers.d/wineuser`, live `wineuser` home, `/setup`, `/etc/live`, `wex-textsetup.service`).
  - Creates a dedicated swap file (`/swapfile`) sized at 1.5× RAM (capped at 8 GB) with `/etc/fstab` entry.
  - Installs the start-up files: Vintner Boot (`installer/textsetup/install-vintner.sh`,
    [VINTNER_BOOT.md](VINTNER_BOOT.md) section 4.2), the default since 2026-11-10. The disk then starts on BIOS and
    UEFI alike. If this step fails, Setup stops with "Setup cannot continue" and names `/var/log/vintner-install.log`
    on the new installation; it never falls back to GRUB.
  - On an ISO built with `build-iso.sh --grub`, Setup installs GRUB instead, with a generated `device.map`:
    `grub-install --target=i386-pc /dev/sdX` on BIOS machines (its core image goes into the BIOS boot partition),
    `--target=x86_64-efi --removable` into the EFI system partition on UEFI machines.
  - Sets `/etc/wineexplorer/setup/stage` to `gui` and triggers a 15-second red countdown bar before ejecting media and rebooting.

---

### Stage 2: GUI-Mode Setup (`wine/programs/setup/`)
- **Execution Context**: Runs on the first boot from the installed disk before the display manager starts (`wex-setup.service`).
- **Runner**: Executes as a dedicated unprivileged system account (`wexsetup`) inside an isolated Wine prefix on Xorg `vt7` at 1024×768 resolution.
- **Billboard Slideshow**: Native Win32 billboard featuring the classic Windows XP 5-step progress indicator on the left (Collecting information, Dynamic Update, Preparing installation, Installing Debian, Finalizing installation), rotating feature panels on the right, and remaining time estimation computed from remaining steps.
- **Interactive Wizard Pages**:
  1. *Regional and Language Options*: Locale selection, keyboard layout.
  2. *Personalize Your Software*: Registered Owner, Organization.
  3. *Computer Name and Administrator Password*: Hostname validation (alphanumeric and dashes, max 15 chars), root password input and verification. Since 2026-11-09 (`docs/SAFE_MODE_WORKORDER.md` C4), the same password also goes to the `administrator` account, XP's built-in Administrator. The package creates that account locked, and `wex-gui-setup.sh` copies root's hash to it. Root keeps getting the password as before, and a blank password leaves both locked.
  4. *Date and Time Settings*: Time zone combo, NTP synchronization checkbox.
  5. *Networking Settings*: Typical (DHCP) vs Custom settings.
  6. *Workgroup or Computer Domain*: Workgroup name (`WORKGROUP`).
- **Unattended Mode**: Reads answer file (`unattend.sif`) via `GetPrivateProfileStringW`, applies configured settings directly, and skips interactive wizard dialogs while maintaining full billboard visual status.
- **Privilege & System Integration**:
  - Uses system D-Bus with non-interactive polkit authorization (`interactive = FALSE`):
    - Hostname: `org.freedesktop.hostname1.SetStaticHostname` (`WexHostnameSet`).
    - Timezone: `org.freedesktop.timedate1.SetTimezone` (`WexTimedateSetTimezone`).
    - Locale: `org.freedesktop.locale1.SetLocale` (`WexLocaleSet`).
    - Root Password: SHA-512 crypt hash passed to AccountsService User0.
  - Polkit authorization is guarded by `/etc/polkit-1/rules.d/49-wexsetup.rules` requiring presence of `/etc/wineexplorer/setup/stage`.
- **User Template Prefix**: Builds clean `/etc/skel/.wine` template prefix via `wineboot -u`, ensuring instant first logon for created users.
- **Transition**: Writes `oobe` to `/etc/wineexplorer/setup/stage` and reboots into Stage 3.

---

### Stage 3: Out-of-Box Experience (`wine/programs/oobe/`)
- **Execution Context**: Runs on second boot (`wex-oobe.service`) under `wexsetup` on Xorg `vt7`.
- **Wizard Sequence**:
  1. *Welcome*: Windows XP Luna Blue background greeting.
  2. *Automatic Updates*: Records the choice; applying system update policy is not implemented.
  3. *Internet Connectivity*: Checks network interface status via NetworkManager D-Bus.
  4. *User Account Creation*: Five user name fields, individual password entry dialogs.
  5. *Thank You*: Final transition summary.
- **User Provisioning**:
  - Accounts created via AccountsService `CreateUser` (`WexUserCreate`).
  - First user is provisioned with `AccountType=1` (administrator, automatically assigned to `sudo` group).
  - The user name fields refuse Administrator, in any case, because the package makes that account (since 2026-11-09, `docs/SAFE_MODE_WORKORDER.md` C4).
  - Copies template prefix from `/etc/skel/.wine` to `/home/<user>/.wine` with proper user ownership.
- **Display Manager & Autologin**:
  - If exactly one user was created and that user has no password: `WexUserSetSession` asks AccountsService to enable automatic login. In Debian 13 this writes LightDM's main configuration (the legacy `[SeatDefaults]` section).
  - If multiple users or passwords exist: displays the Wine `logonui.exe` Welcome screen, running as the unprivileged `lightdm` account with its own prefix, `/var/lib/lightdm/.wine` (until 2026-11-08 `.wine-logon`, which the greeter moves on its next start). GTK greeter remains installed for recovery.
- **Cleanup**:
  - Terminates running Wine instances and user processes.
  - Deletes temporary user `wexsetup` (`userdel -r -f wexsetup`).
  - Removes `/etc/wineexplorer/setup` staging directory.
  - Deletes `/etc/polkit-1/rules.d/49-wexsetup.rules`.
  - Sets `/etc/wineexplorer` directory permissions to `755 root:root`.
  - Sets `/var/log/wineexplorer-*.log` permissions to `640 root:adm`.
  - Disables setup systemd units and starts LightDM.

---

## 2. Unattended Installation

An unattended response file (`unattend.sif`) can automate the complete installation process from boot to desktop:

```ini
; Unattended installation answer file for WineExplorer
[Data]
AutomaticUpdates = Yes

[Unattended]
UnattendMode = FullUnattended
OemSkipEula = Yes
AutomaticUpdates = Yes
TargetDisk = /dev/sda
Repartition = Yes

[GuiUnattended]
TimeZone = "UTC"
AdminPassword = ""
OEMSkipRegional = 1

[UserData]
FullName = "Administrator"
OrgName = "WineExplorer Organization"
ComputerName = "WEX-PC"

[Networking]
InstallDefaultComponents = Yes

[WexOobe]
User1 = "wineuser"
User1Admin = 1
```

- Build with `--unattended --clean`: `sudo ./installer/build-iso.sh --unattended --clean` (sets GRUB default entry to `Install (unattended)`).
- The ISO carries Vintner Boot's `vintner-core` and `vintner-platform` packages as files under `/setup/vintner`,
  together with Setup's step, and their dependencies (`systemd-boot-efi`, `systemd-ukify`, `sbsigntool`) are in the
  live system.
  - By default `build-iso.sh` builds the packages from the `vintner` submodule (Vintner Boot, pinned like `wine`
    and `wexcomp`; `git submodule update --init vintner`). It uses that project's `ci/package/build-debs.sh`, which
    needs Debian 13 and the build dependencies in `vintner/debian/control`: debhelper and golang-go, plus git,
    dpkg-dev, fakeroot, curl, cpio, kmod, xz-utils, binutils, grub-pc-bin, systemd-boot-efi and python3-pefile.
    The build downloads the recovery image's Debian packages.
  - `--vintner=DIR` takes packages already built in DIR instead.
  - `--grub` puts nothing of Vintner on the ISO, and Setup installs GRUB, the default until 2026-11-10.
- Boots directly into Stage 1, partitions `TargetDisk`, formats, copies files, reboots into Stage 2 (applies hostname, timezone, locale), transitions to Stage 3 (provisions `wineuser` as administrator), configures LightDM autologin, cleans up temporary setup state, and launches into the WineExplorer desktop session without a single keystroke.

### Running Automated Install Tests in WSL2 (`wexbuild`)
From the repository root inside `wexbuild`:
```bash
# 1. Package Wine into the live-build staging area
./installer/package-wine.sh

# 2. Build the live hybrid ISO
WORK_DIR=/root/iso-work/work OUT_ISO=/root/iso-work/wineexplorer.iso ./installer/build-iso.sh --clean

# 3. Run interactive end-to-end installation test in QEMU/KVM
./installer/tests/install-interactive.sh /root/iso-work/wineexplorer.iso /root/install-test-password.txt wex-test-vm
```

---

## 3. Milestones and Acceptance Evidence

This is the imported milestone report. Its **DONE** labels have not yet been
revalidated against the current merged branch by the automated install test.

| Milestone | Description | Status | Evidence Location | Verification Summary |
|---|---|---|---|---|
| **F1** | Repository & Build Cleanliness | **DONE** | Repository git log & `.gitignore` | History rewritten; binaries and deb packages removed from git; `.gitignore` enforced; `setup.exe` and `oobe.exe` built in Wine build system (`wine/programs/setup`, `wine/programs/oobe`). |
| **F2** | Disk Safety on Boot Timeout | **DONE** | `installer/test-evidence/F2/` | Booted ISO without keys; GRUB timeout booted Live session; hard disk VDI remained byte-for-byte identical (`sha256sum` matched). |
| **F3** | Partition Preservation & Editor | **DONE** | `installer/test-evidence/F3/` | Pre-partitioned disk with test file; partition list displayed all partitions and free spaces; delete required `L` confirmation; "Leave intact" preserved file across install. |
| **F4** | Progress Bar & Quiet Console | **DONE** | `installer/test-evidence/F4/` | Copy progress driven by `layout.inf` byte count; updated smoothly; quiet console with zero kernel or systemd message bleed. Screenshots at 25%, 50%, 75%. |
| **F5** | Clean Target State & Bootloader | **DONE** | `installer/test-evidence/F5/` | Target disk verified clean: no `live-*` packages, no `wineuser`, no live sudoers, `/swapfile` created and active, GRUB MBR installed. |
| **F6** | Interactive Stage 2 GUI Wizard | **DONE** | `installer/test-evidence/F6/` | All 6 wizard pages displayed over billboard; computer name, timezone, locale, and admin password applied via D-Bus; input sanitized against shell injection. |
| **F7** | Interactive Stage 3 OOBE | **DONE** | `installer/test-evidence/F7/` | Two user accounts created, first user in `sudo`; password prompts honored; GDM greeter displayed (autologin suppressed for multi-user); both users logged into WineExplorer desktop. |
| **F8** | Unattended Mode End-to-End | **DONE** | `installer/test-evidence/F8/` | Zero-touch install from ISO boot to desktop; answer file settings applied with return code 0; `wexsetup` and setup rules cleanly purged; logs secured to 640 `root:adm`. |
| **F9** | Documentation & Verification | **DONE** | `docs/INSTALLER.md` | Comprehensive documentation matching code, tests, and security architecture. |

---

## 4. Current Limitations & Missing Features

1. **Text-Mode Repair Subsystem**: The "To repair Debian GNU/Linux with WineExplorer... press R" option is disabled/grayed out in Stage 1 until an automated recovery console and prefix verification tool is implemented.
2. **Offline PackageKit Repository (`/pool`)**: Dynamic component installation (e.g. LibreOffice, Samba) from an offline optical pool repository during Stage 2 is stubbed; all default system packages are currently pre-installed in the base squashfs.
3. **Active Directory / Domain Join**: Stage 2 and OOBE currently support Workgroup membership (`[Identification] JoinWorkgroup`); Windows Domain joining via SSSD/Realmd is not yet implemented.
# Refactor acceptance — 2026-10-13

The installer now packages the same checkout's assembled Wine submodule
and owned src modules. Fresh full build/install, public ISO build and
interactive QEMU installation pass; see [refactor/P4.md](refactor/P4.md)
for source pins, ISO hash and exact evidence. Default build directory is
build/wow64. Packaging rejects a different configured source and the legacy
~/inst/build; never run make install there, including staged installation.
Current build instructions are [BUILDING.md](BUILDING.md).
