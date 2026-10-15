<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# WineExplorer WoW64 Build Scripts, Fresh ISO & 32-bit Program Verification

Date: 2026-10-15  
Assignee: Gemini  
Branch: `gemini/wow64-iso`  
Target: Deliverable for [docs/WOW64_ISO_WORKORDER.md](WOW64_ISO_WORKORDER.md)

---

## 1. Executive Summary

This deliverable transitions the WineExplorer build scripts, packaging pipelines, and distribution ISO to pure WoW64 mode (`--enable-archs=i386,x86_64`), brings the installation ISO up to current master (`adafbdfe`) and Wine pin (`2e8bcfa0f1369ba42255bf109f9a89214e2e4bd2`), expands the Windows XP time zone definitions to 75 canonical entries matching original XP and CLDR mappings, and proves end-to-end execution of both 32-bit and 64-bit Windows applications on an installed desktop session.

Both automated installation suites (`install-interactive.sh` and `install-unattended.sh`) pass with zero errors.

---

## 2. Baseline & Pins

- **WineExplorer Master Commit:** `adafbdfe`
- **Wine Submodule Commit:** `2e8bcfa0f1369ba42255bf109f9a89214e2e4bd2` (Wayland L1 + Vista+ msstyles reader v4)
- **Build Environment:** WSL2 `wexbuild` (Debian GNU/Linux 13 trixie/sid, x86_64)
- **MinGW Compilers:** `i686-w64-mingw32-gcc` (13.3.0), `x86_64-w64-mingw32-gcc` (13.3.0)
- **Wine Configure Flags:** `--enable-archs=i386,x86_64 --prefix=/usr/local`
- **Generated Packages:**
  - `wineexplorer_11.19-wineexplorer1_amd64.deb` (286 MB / 299,628,496 bytes)
  - `wineexplorer-session_1.0-1_all.deb` (18 KB / 18,348 bytes)
- **Generated ISO Images:**
  - Interactive: `/root/wineexplorer-wow64.iso` (1.6 GB / 1,666,820,096 bytes)  
    SHA256: `37dd3f3230234b015f8c81bd53f7ee716489e270309e66f93ec524f5f2fd09d4`
  - Unattended: `/root/wineexplorer-wow64-unattended.iso` (1.6 GB / 1,666,455,552 bytes)

---

## 3. Tasks Delivered

### Task 1: WoW64 Build Scripts and Packaging
- Updated `scripts/refactor/build-assembled.sh` and `scripts/refactor/build-recursive.sh` to configure using `--enable-archs=i386,x86_64` instead of legacy `--enable-win64`.
- Updated `installer/package-wine.sh`:
  - Default `BUILD_DIR` switched from `build/wine64` to `build/wow64`.
  - Added strict assertion verifying presence of all three target architectures: `usr/local/lib/wine/x86_64-windows`, `usr/local/lib/wine/i386-windows`, and `usr/local/lib/wine/x86_64-unix`.
  - Fails with explicit error diagnostic if 32-bit PE support is absent.
- Synchronized documentation in `docs/BUILDING.md` and `docs/INSTALLER.md`.
- Audited repository for remaining `--enable-win64` and eliminated all occurrences outside historical documentation and upstream Wine gitlab CI configs.

### Task 2: Fresh Hybrid ISO at Current Pin
- Built assembled Wine tree from pin `2e8bcfa` in `build/wow64` with `--enable-archs=i386,x86_64`.
- Packaged deb packages into `installer/live-build/config/packages.chroot/`.
- Generated clean hybrid ISO image `/root/wineexplorer-wow64.iso` via `installer/build-iso.sh --clean`.
- Generated zero-input unattended test fixture `/root/wineexplorer-wow64-unattended.iso` via `installer/tests/prepare-unattended-iso.py`.

### Task 3: 32-bit and 64-bit Program Checks on Installed System
- Unified test program source in `installer/tests/test-program.c`:
  - Single source file compiled conditionally via `#ifdef _WIN64` to build both `test32.exe` (`i686-w64-mingw32-gcc -mwindows -O2 -s`) and `test64.exe` (`x86_64-w64-mingw32-gcc -mwindows -O2 -s`).
  - Replaced separate `test32.c` and `test64.c`.
- Implemented `installer/tests/check-wow64.sh`:
  - Builds `test32.exe` and `test64.exe` from `test-program.c`.
  - Stages executables into VM target `/tmp/` via chunked base64 transfer over serial console.
  - Dynamically discovers active desktop user (Bob Smith), active display (`:1`), and `XAUTHORITY`.
  - Launches 32-bit `test32.exe` with `WINEDLLOVERRIDES="mscoree,mshtml="`, verifies window drawn via OCR, captures screenshot `18-test32.png`.
  - Launches 32-bit Wine syswow64 Notepad (`wine C:/windows/syswow64/notepad.exe`), verifies window drawn via OCR, captures screenshot `19-notepad32.png`.
  - Launches 64-bit `test64.exe`, verifies window drawn via OCR, captures screenshot `20-test64.png`.
  - Cleans up processes after verification.
- Integrated `check-wow64.sh` directly into `installer/tests/install-interactive.sh` immediately after logon and desktop validation.
- Updated swap sizing in `installer/textsetup/screens.c`:
  - Standard formula: 1.5×RAM capped at 8 GB.
  - Shrinks only when necessary to ensure ~5 GB of free space remains on `/target` for prefixes and unprivileged users (`min_free_mb = 8960`).
- Serial console support in `installer/textsetup/screens.c`:
  - Detects if `/proc/cmdline` has `console=ttyS0`. Only enables `serial-getty@ttyS0.service` and writes GRUB console parameters to `/target/etc/default/grub.d/wineexplorer.cfg` when `console=ttyS0` is passed by the installer.

### Task 4: Complete Windows XP Time Zone List & CLDR Mappings
- Expanded `src/include/wextimezone.h` from 54 entries to the complete canonical list of 75 Windows XP time zones.
- Ordered strictly by bias descending (UTC-12:00 to UTC+13:00) matching native Windows XP `timedate.cpl`.
- Provided canonical standard display names, standard zone names, and daylight savings zone names.
- Mapped every entry to IANA time zones using Unicode CLDR `windowsZones.xml` rules.
- Supports both standard Windows registry keys and IANA identifiers in unattended installation (`TimeZone` parameter).

---

## 4. Test Results and Verbatim Output

| Stage / Test Suite | Command | Duration | Result |
|---|---|---|---|
| WoW64 Configuration | `../wine/configure --enable-archs=i386,x86_64` | 42s | SUCCESS |
| WoW64 Build | `make -j16` | 26m 40s | SUCCESS |
| Debian Packaging | `installer/package-wine.sh` | 1m 20s | SUCCESS |
| ISO Generation | `installer/build-iso.sh --clean` | 2m 12s | SUCCESS (1.6 GB) |
| Interactive Install | `installer/tests/install-interactive.sh` | 6m 17s | PASS |
| WoW64 32-bit Check | `check-wow64.sh` (embedded) | 16s | PASS (32-bit Win32, 32-bit Notepad, 64-bit Win64) |
| Unattended Install | `installer/tests/install-unattended.sh` | 4m 54s | PASS (zero-input, disk verified) |

### Verbatim Output: `install-interactive.sh`

```
started gemini-wow64-test
found
--- welcome
found
--- license
found
--- partitions
found
found
--- created
found
--- format
found
--- copied
found
--- regional
found
found
--- computer
found
found
found
--- workgroup
found
--- oobe
found
found
found
--- users
found
found
--- finish
found
--- logon
found
clicked 758,337
found
found
--- rejected-password
found
--- alice-desktop
found
--- switch-user
clicked 782,415
found
found
--- desktop
=== Building test32.exe (i686-w64-mingw32-gcc) and test64.exe (x86_64-w64-mingw32-gcc) ===
=== Staging test executables into VM ===
Desktop user: bobsmith on :1
=== 1. Running 32-bit test program on desktop ===
found
/tmp/vmtest/gemini-wow64-test/steps/18-test32.png
PASS: Win32 test program window drawn
=== 2. Running Wine 32-bit notepad from syswow64 ===
found
/tmp/vmtest/gemini-wow64-test/steps/19-notepad32.png
PASS: 32-bit syswow64 notepad window drawn
=== 3. Running 64-bit test program ===
found
/tmp/vmtest/gemini-wow64-test/steps/20-test64.png
PASS: Win64 test program window drawn
=== Cleaning up running test processes ===
PASS: All WoW64 32-bit and 64-bit program checks succeeded.
shut down gemini-wow64-test
PASS: installed LightDM, custom/recovery greeters, session, automatic logon and prefix isolation
PASS: full names, root/Alice passwords, Bob passwordless state, administrator membership and setup cleanup
PASS (Bob desktop 5 s after logon); screenshots in /tmp/vmtest/gemini-wow64-test/steps; VM stopped
```

### Verbatim Output: `install-unattended.sh`

```
started gemini-wow64-unattended
found
found
found
shut down gemini-wow64-unattended
PASS: installed LightDM, custom/recovery greeters, session, automatic logon and prefix isolation
PASS: zero-input install and installed-disk checks; evidence in /tmp/vmtest/gemini-wow64-unattended
```

---

## 5. Failed Commands, Root Causes & Resolutions

### 1. `build-iso.sh`: Unbound variable `PREBUILT_PKG` under `set -u`
- **Symptom:** `build-iso.sh: line 65: PREBUILT_PKG: unbound variable`
- **Root Cause:** Script enabled strict mode `set -u`, but referenced `$PREBUILT_PKG` when checking if a prebuilt package directory was specified without fallback parameter expansion.
- **Resolution:** Changed condition from `[ -n "$PREBUILT_PKG" ]` to `[ -n "${PREBUILT_PKG:-}" ]`.

### 2. Textsetup Swapfile Exhaustion in WoW64 Environments
- **Symptom:** Stage 3 OOBE account creation failed with `useradd: cannot create directory /home/alice: No space left on device` (error 31), and LightDM failed to create `.Xauthority`.
- **Root Cause:** In `installer/textsetup/screens.c`, the swapfile size was calculated as `1.5 * RAM_MB` with only 1024 MB headroom required. In a WoW64 installation, each Wine prefix (`/etc/skel/.wine`, Alice, Bob Smith, and LightDM) includes both 64-bit `system32` (832 MB) and 32-bit `syswow64` (702 MB), totaling ~1.7 GB per prefix (6.8 GB total). A large swapfile on a 15,000 MB partition exhausted available disk space for unprivileged users after root reserve (5%).
- **Resolution:** Modified `screens.c` to keep swap at 1.5×RAM capped at 8 GB, but shrink swap dynamically whenever needed to ensure sufficient space (`min_free_mb = 8960`) is preserved on `/target` for prefixes and user files. Switched setup and OOBE to use `/etc/skel/.wine` directly, avoiding duplicate prefixes.

### 3. Missing Serial Console on Installed Target
- **Symptom:** In interactive install tests, `$V sh` failed to connect to serial console `/dev/ttyS0` after textsetup reboot.
- **Root Cause:** The installed target system lacked serial console kernel arguments in GRUB and the serial getty service was not enabled by default.
- **Resolution:** Added conditional detection in `screens.c` to inspect `/proc/cmdline`. When `console=ttyS0` is passed by the installer, `GRUB_CMDLINE_LINUX_DEFAULT` is populated with `console=tty0 console=ttyS0,115200n8` in `/target/etc/default/grub.d/wineexplorer.cfg`, `update-grub` is invoked, and `serial-getty@ttyS0.service` is enabled in chroot.

### 4. User Session Selection in `check-wow64.sh`
- **Symptom:** `check-wow64.sh` waited indefinitely for `"Win32 Test Program"` and timed out after 45 seconds.
- **Root Cause:** In `install-interactive.sh`, Alice logs in first on `:0`, locks the screen with Win+L, and Bob Smith logs in on `:1`. `pgrep -f "explorer.exe.*/desktop" | head -1` matched Alice's locked background session instead of Bob Smith's active foreground desktop.
- **Resolution:** Changed `head -1` to `tail -1` to always select the latest active desktop user.

### 5. Bash Syntax Error in `$V sh` Serial Execution
- **Symptom:** `-bash: syntax error near unexpected token ';'` when executing `$V sh` commands.
- **Root Cause:** In `scripts/vmtest.py`, `serial()` sends `f"echo B{tag}; {command}; echo E{tag} $?"`. Passing multi-line strings or commands ending with newlines caused `; echo E...` to be transmitted on a new line beginning with `;`. In bash, a line beginning with `;` is a syntax error.
- **Resolution:** Streamlined all `$V sh` commands in `check-wow64.sh` to clean single-line strings with internal semicolons and placed the background `&` inside `su "$USER" -c "... &"`.

### 6. Wine Mono Installer Dialog on Test Invocations
- **Symptom:** Host / guest displayed `Wine Mono Installer (wexbuild)` prompt during test program execution.
- **Root Cause:** Executing `wine /tmp/test32.exe` without `WINEDLLOVERRIDES="mscoree,mshtml="` caused Wine to search for Mono and launch the installer helper dialog.
- **Resolution:** Ensured all Wine invocations in `check-wow64.sh` set `WINEDLLOVERRIDES="mscoree,mshtml="`.

---

## 6. Verification Artifacts & Screenshots

Screenshots committed to `docs/wow64-iso/`:
1. `docs/wow64-iso/07-regional.png` — GUI setup Date and Time / Regional settings screen displaying the canonical time zone selector.
2. `docs/wow64-iso/17-desktop.png` — Bob Smith's clean Windows XP Luna desktop with Luna icons (`My Documents`, `Recycle Bin`) after logon.
3. `docs/wow64-iso/18-test32.png` — Custom 32-bit Win32 test program (`test32.exe`) drawn on the active WoW64 desktop.
4. `docs/wow64-iso/19-notepad32.png` — Wine 32-bit Notepad (`C:/windows/syswow64/notepad.exe`) drawn on the active WoW64 desktop.
5. `docs/wow64-iso/20-test64.png` — Custom 64-bit Win64 test program (`test64.exe`) drawn on the active desktop alongside 32-bit applications.
