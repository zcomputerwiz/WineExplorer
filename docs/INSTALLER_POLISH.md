# Installer Polish Verification Report

**Date:** 2026-10-13  
**Branch:** `gemini/installer-polish`  
**Environment:** WSL2 `wexbuild` (Debian 13.5)  
**Status:** Complete

---

## 1. Overview and Completed Tasks

All six tasks defined in [`docs/INSTALLER_POLISH_WORKORDER.md`](INSTALLER_POLISH_WORKORDER.md) have been implemented, cleanly committed, and end-to-end verified in QEMU/KVM test environments.

### Task 1: GUI Setup Visual Parity (`src/programs/setup`)
- **Wizard97 Headers**: Configured property sheet pages with `PSH_WIZARD97 | PSH_HEADER` and `PSP_USEHEADERTITLE | PSP_USEHEADERSUBTITLE` using standard title and subtitle resources matching Windows XP setup dialogs.
- **Classic Styling**: Added `SetThemeAppProperties(0)` in `WinMain` and forced classic window themes on the wizard dialog via `SetWindowTheme(hwndDlg, L"", L"")` in `PropSheetCallback`, ensuring the classic window frame and control appearance without modern theme bleed.
- **Billboard Step Advancement**: Updated billboard step matching (`i == g_ctx.install_step`) to advance sequentially through:
  - Step 0: "Collecting information"
  - Step 1: "Dynamic update"
  - Step 2: "Preparing installation"
  - Step 3: "Installing Debian & Wine"
  - Step 4: "Finalizing installation"
- **Empty Initial Name**: Initialized owner name buffer to empty string and disabled the "Next" button on `IDD_PAGE_USERINFO` until the user enters text in `IDC_OWNER_NAME` (handling `EN_CHANGE` and `PSN_SETACTIVE`).
- **Commit**: `6bf11981` — `setup: implement Wizard97 headers, classic styling, and billboard step advancement`

### Task 2: Date and Time Page XP Time Zones (`src/programs/setup`, `src/dlls/timedate.cpl`)
- **Shared Header**: Created [`src/include/wextimezone.h`](../src/include/wextimezone.h) under the Unicode License v3 (CLDR release 45 / typeVersion 2021a) and LGPL 2.1+, containing:
  - All 54 Windows XP time zones (`g_xp_timezones`).
  - Full Unicode CLDR mapping between Windows zone names and IANA zone identifiers (`g_wex_tz_iana_map`).
  - Resolution helpers `wex_iana_for_key()`, `wex_key_for_iana()`, and `wex_resolve_timezone()`.
- **Setup Wizard Integration**: Populated `IDC_TZ_COMBO` with the XP time zone display strings, bound `IDC_DATEPICKER` and `IDC_TIMEPICKER` (`SysDateTimePick32`) initialized to local time, and linked the daylight saving changes checkbox to the selected zone's DST capabilities (`has_dst`), disabled/grayed out since Linux systemd `timedate1` manages zone rules automatically.
- **Unattended Time Zone Resolution**: Supported both Windows zone keys (e.g., `Eastern Standard Time`) and IANA strings (e.g., `America/New_York`) in `unattend.sif` via `wex_resolve_timezone()`.
- **CPL Sharing**: Removed duplicate timezone and IANA fallback tables in `src/dlls/timedate.cpl/timezone.c` to consume the shared header.
- **Commit**: `323b5436` — `setup, timedate.cpl: share XP timezone list and complete CLDR mapping`

### Task 3: Automatic Updates Applied (`src/programs/oobe`, `installer/scripts/wex-oobe.sh`)
- **OOBE Staging**: In `src/programs/oobe/oobe.c`, recorded the user's choice in the staging directory at `/etc/wineexplorer/setup/automatic-updates` (`on` or `off`).
- **Unattended SIF Parsing**: Added support for `AutomaticUpdates = Yes/No` under both `[Data]` and `[Unattended]` sections of `unattend.sif`.
- **Host Application**: In `installer/scripts/wex-oobe.sh`, read `/etc/wineexplorer/setup/automatic-updates` as root upon OOBE completion and wrote `/etc/apt/apt.conf.d/20auto-upgrades`:
  - `on` → `APT::Periodic::Update-Package-Lists "1"; APT::Periodic::Unattended-Upgrade "1";`
  - `off` → `APT::Periodic::Update-Package-Lists "0"; APT::Periodic::Unattended-Upgrade "0";`
- **Package List**: Added `unattended-upgrades` to `installer/live-build/config/package-lists/wineexplorer.list.chroot`.
- **Documentation**: Documented `AutomaticUpdates = Yes` in `installer/setupinfs/unattend.sif.example`.
- **Commit**: `a683f03e` — `oobe, installer: apply automatic updates choice via unattended-upgrades`

### Task 4: Text Setup Wording & Countdown Bar (`installer/textsetup`)
- **XP Disk & Partition Wording**: Updated `installer/textsetup/screens.c` to format disks and partitions matching Windows XP text setup:
  - Partition list disk header: `%lu MB Disk %d at Id %d on bus 0 on %s [%s]`
  - Partition list entry: `C: Partition1 [New (Raw)] 15000 MB (15000 MB free)`
  - Format screen header: `"Setup will install Debian GNU/Linux with WineExplorer on partition C: Partition1 [New (Raw)] 15000 MB ( 15000 MB free) on 20480 MB Disk 0 at Id 0 on bus 0 on virtio [MBR]."`
  - Linux device paths (e.g., `/dev/vda1`, `/dev/vda`) are kept strictly in logs.
- **Reboot Countdown Bar**: Updated `installer/textsetup/ui.c` to suppress percentage text when drawing with `PAIR_REBOOT`, presenting a clean solid red/brown countdown bar without percentage text.
- **Commit**: `1bea4d2b` — `textsetup: update partition display wording and restart countdown bar`

### Task 5: Installed System Boot Configuration
- **Silent Instant Boot**: Configured installed GRUB via `installer/live-build/config/includes.chroot/etc/default/grub.d/wineexplorer.cfg` and `installer/textsetup/screens.c` prior to running `update-grub`:
  - `GRUB_TIMEOUT_STYLE=hidden`
  - `GRUB_TIMEOUT=0`
  - `GRUB_TERMINAL=console` (eliminating `unicode.pf2` font requirement and warning)
  - `GRUB_THEME=""` and `GRUB_BACKGROUND=""`
- **Theme Removal**: Removed `/etc/default/grub.d/desktop-base.cfg` in both `0500-enable-services.hook.chroot` and `screens.c` before target bootloader generation.
- **Commit**: `0ceb6a25` — `installer: configure silent instant boot in installed GRUB`

### Task 6: Documentation, Test Anchors, and Recycle Bin Verification
- **Documentation**: Updated [`docs/INSTALLER.md`](INSTALLER.md) with details on the polish changes, new answer-file keys, and instructions for running tests in `wexbuild`.
- **Test Anchor Update**: Updated line 47 of `installer/tests/install-interactive.sh` to type `"Wine User"` before pressing Enter on the Name page (required because Name now starts empty and disables Next until text is entered).
- **Recycle Bin Verification**: Confirmed Recycle Bin desktop icon appears on first logon of the user (tested with Bob's passwordless session in `install-interactive.sh` step 17).
- **Commit**: `cff29321` — `docs, tests: update installer documentation and interactive test name input`

---

## 2. Test Results

### 1. Hybrid ISO Clean Build
- Command: `WORK_DIR=/root/iso-work/work OUT_ISO=/root/iso-work/wineexplorer.iso installer/build-iso.sh --clean`
- Result: Produced 1.4 GB hybrid ISO at `/root/iso-work/wineexplorer.iso` in ~11 minutes.

### 2. End-to-End Interactive Install Test
- Command: `installer/tests/install-interactive.sh /root/iso-work/wineexplorer.iso /root/install-test-password.txt gemini-polish-test`
- Result: **PASS** (completed in ~6.5 minutes).
  - Text-mode partition creation (15000 MB), ext4 formatting, file copying.
  - GUI-mode setup wizard: Regional, Owner Name ("Wine User"), Computer name/passwords, Time zone, Networking, Workgroup.
  - Stage 3 OOBE: Welcome, Automatic Updates, Connectivity, User accounts (Alice with password, Bob Smith passwordless).
  - Logon and session verification: LightDM custom greeter authentication, incorrect password rejection, Alice desktop logon, Win+L user switch, Bob Smith first desktop logon with Recycle Bin and desktop icons in 6 seconds.
  - Screenshots recorded to [`docs/installer-polish/`](installer-polish/).

### 3. Unattended Install with AutomaticUpdates=No
- Answer File: Created answer file containing:
  ```ini
  [Data]
  AutomaticUpdates = No
  [Unattended]
  AutomaticUpdates = No
  TargetDisk = /dev/vda
  Repartition = Yes
  ```
- Generated Test ISO: Created `/root/iso-work/unattended-no-updates.iso` using `prepare-unattended-iso.py`.
- Execution: Ran `install-unattended.sh`. Result: **PASS**.
- Direct Target Disk Inspection via `qemu-nbd`:
  Attached installed root partition (`/dev/nbd0p1`) and inspected `/etc/apt/apt.conf.d/20auto-upgrades`:
  ```
  APT::Periodic::Update-Package-Lists "0";
  APT::Periodic::Unattended-Upgrade "0";
  ```
  Confirmed that automatic updates policy is properly applied to `0` when disabled.

---

## 3. Screenshots

All 17 step screenshots from the interactive test run are archived in [`docs/installer-polish/`](installer-polish/):
- `01-welcome.png`: Text setup welcome screen.
- `02-license.png`: EULA screen.
- `03-partitions.png`: Partition editor with XP bus and disk index formatting.
- `04-created.png`: Newly created 15000 MB partition (`C: Partition1 [New (Raw)] 15000 MB`).
- `05-format.png`: Format screen with XP wording (`Setup will install Debian GNU/Linux with WineExplorer on partition C: Partition1...`).
- `06-copied.png`: Target file copy completion.
- `07-regional.png`: GUI setup with Wizard97 header and classic styling.
- `08-computer.png`: Computer name and administrator password page.
- `09-workgroup.png`: Workgroup configuration page.
- `10-oobe.png`: OOBE welcome screen.
- `11-users.png`: User provisioning page.
- `12-finish.png`: OOBE completion.
- `13-logon.png`: Dual-user custom Welcome screen.
- `14-rejected-password.png`: Incorrect password challenge.
- `15-alice-desktop.png`: Alice desktop session.
- `16-switch-user.png`: Lock screen return to Welcome screen.
- `17-desktop.png`: Bob Smith desktop session displaying Recycle Bin icon.

---

## 4. Commits on `gemini/installer-polish`

1. `6bf11981` — `setup: implement Wizard97 headers, classic styling, and billboard step advancement`
2. `323b5436` — `setup, timedate.cpl: share XP timezone list and complete CLDR mapping`
3. `a683f03e` — `oobe, installer: apply automatic updates choice via unattended-upgrades`
4. `1bea4d2b` — `textsetup: update partition display wording and restart countdown bar`
5. `0ceb6a25` — `installer: configure silent instant boot in installed GRUB`
6. `cff29321` — `docs, tests: update installer documentation and interactive test name input`

No remaining items. Ready for merge into `origin/master`.
