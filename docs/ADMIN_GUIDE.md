<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# WineExplorer Desktop Environment — Administrator Guide

This guide provides system administrators, packagers, and lab maintainers with comprehensive operational details for installing, configuring, troubleshooting, and verifying the WineExplorer Desktop Environment (WineExplorer DE).

---

## 1. Installation & Build Flows

WineExplorer provides a hybrid Debian-based distribution image built via Debian `live-build`. The source trees, packaging scripts, and unattended templates are located in `installer/`.

### 1.1 ISO / Live-Build Pipeline (`installer/build-iso.sh`)

The build orchestrator script is `installer/build-iso.sh`. It must be executed as `root` (or with `sudo`) on a Debian GNU/Linux system (typically Debian 13 "Trixie", amd64).

```sh
sudo installer/build-iso.sh [--clean] [--unattended]
```

The pipeline executes through the following stages:

1. **Compilation of `textsetup` (`installer/textsetup/`)**:
   - Compiles the native C text-mode installer binary `textsetup`.
   - Copies `textsetup` into the live chroot overlay (`$WORK_DIR/config/includes.chroot/setup/textsetup`).
2. **Packaging Wine & the Standalone Compositor**:
   - `installer/package-wine.sh`: Builds and packages the customized WoW64 Wine distribution (`wine-custom_*.deb`) including 32-bit and 64-bit binaries, PE modules, Unix libraries, and default prefix templates into `config/packages.chroot/`.
   - `installer/package-wexcomp.sh`: Compiles and packages `wexcomp` (the standalone Wayland compositor based on labwc/wlroots with optional SceneFX blur integration) into `packages.chroot/`.
3. **Configuration & Mode Staging**:
   - **Interactive Mode** (default): Uses `setupinfs/txtsetup.sif.in` processed via `tools/mklayout.py`. GRUB menu default is set to `0` (interactive setup).
   - **Unattended Mode** (`--unattended`): Copies `setupinfs/unattend.sif` into `setup/unattend.sif` and sets GRUB default to `2` for zero-interaction automated provisioning.
4. **Live-Build Execution (`lb config` & `lb build`)**:
   - Configures `live-build` with `--mode debian`, `--distribution trixie`, `--architectures amd64`, `--binary-images iso-hybrid`, `--system live`, `--archive-areas "main contrib non-free non-free-firmware"`, and `--bootloaders "grub-efi,grub-pc"`.
   - Sets ISO volume label to `WEX_INSTALL`.
   - Executes `lb build` to generate the bootable hybrid ISO (`wineexplorer.iso`).

### 1.2 System Installation (`textsetup`) & First-Boot OOBE

- **Stage 1 (Text-Mode Setup)**:
   The booted live installer launches `textsetup`, presenting an authentic blue-background text console. It handles disk partitioning, partition formatting (ext4 or NTFS), file staging from the live image to the target root, and GRUB bootloader installation (`grub-pc` for BIOS or `grub-efi-amd64` for UEFI).
- **Stage 2 (Out-Of-Box Experience / First Boot)**:
   On first boot of the installed system, systemd launches the Out-Of-Box Experience (OOBE) wizard (`wex-gui-setup.sh`), which creates administrator and standard user accounts, configures the computer name and workgroup, initializes the Default User prefix template (`/etc/skel/.wine`), and configures LightDM as the display manager (`scripts/wex-greeter.sh`).

---

## 2. Desktop Sessions

WineExplorer supports three distinct session models configured via display manager desktop entry files in `/usr/share/xsessions/` and `/usr/share/wayland-sessions/`.

### 2.1 X11 Desktop Session (Default / Production Target)

- **Session Entry File**: `/usr/share/xsessions/wine-explorer.desktop`
- **Session Script**: `/usr/local/bin/wine-explorer-session.sh`
- **Execution Stack**:
  ```
  LightDM / GDM -> Xorg Server -> Wine Virtual Desktop -> explorer.exe /desktop=Default,<res> /shell
  ```
- **Architecture**:
  - `explorer.exe` acts as both the top-level desktop shell and the Win32 window manager.
  - Native Linux applications (`/usr/share/applications/*.desktop`) and independent Wine/Proton applications are managed inside themed Win32 frames.
  - `src/dlls/wexhost/` provides the host integration bridge: X11 window lifecycle management, GIO application discovery and execution, system D-Bus integration (`DBUS_BUS_SYSTEM`), and XRandR dynamic display mode switching.
- **Display Modesetting Configuration**:
  On virtual machines (VirtualBox, QEMU/KVM) or machines where hardware cursor presentation or Glamor acceleration induces corruption:
  `/etc/X11/xorg.conf.d/20-wineexplorer-modesetting.conf`:
  ```text
  Section "Device"
      Identifier "modesetting"
      Driver "modesetting"
      Option "SWcursor" "true"
      Option "AccelMethod" "none"
  EndSection
  ```
  Software cursors avoid stale hypervisor cursor caches; `AccelMethod "none"` prevents blank or corrupted native XRender/core drawing while preserving Wine shared-memory (SHM) presentation.

### 2.2 Wayland Desktop Session (`wexcomp`)

- **Session Entry File**: `/usr/share/wayland-sessions/wine-explorer-wayland.desktop`
- **Session Script**: `scripts/wine-explorer-wayland-session.sh` (lab/opt-in)
- **Execution Stack**:
  ```
  Display Manager -> wexcomp (Wayland Compositor) -> explorer.exe /shell (winewayland.drv)
  ```
- **Architecture**:
  - `wexcomp`: A dedicated standalone Wayland compositor based on labwc and wlroots 0.18, extended with custom protocol extensions (`wex_window_manager_v1` and `wex_window_v1` v14).
  - **Dual-Socket Security Architecture**:
    - `LABWC_WEX_SHELL_SOCKET` (`wex-shell-0`): Private privileged socket. Only privileged shell components (Wine Explorer and `wexhost`) connect here to access window management, layer-shell placement, thumbnail creation, and corner/effect controls.
    - Standard Socket (`wayland-0`): Unprivileged application socket. Applications connecting here cannot access `wex_window_manager_v1` (attempts return `WL_DISPLAY_ERROR_INVALID_OBJECT`).
  - Win32 Graphics Driver: `winewayland.drv` selected via registry key `HKCU\Software\Wine\Drivers` (`Graphics="wayland"`).

### 2.3 Gamescope Integration (Per-Application Runtime)

Gamescope is supported as an optional per-application sandbox/compositor for games, preserving native game resolution switching, integer scaling, and AMD FSR/NIS spatial upscaling. Gamescope is not run as a session-wide display manager; it wraps individual gaming prefixes while `explorer.exe` hosts the main desktop.

---

## 3. Compositor Configuration (`wexcomp`)

When running under the Wayland session, `wexcomp` reads its configuration from `~/.config/labwc/rc.xml` or from a directory specified via the `-C` command-line switch (`wexcomp -C <dir>`).

### 3.1 XML Configuration Keys (`rc.xml`)

| XML Node Path | Value / Default | Description |
|---|---|---|
| `<core><decoration>` | `none` (default: `server`) | Disables compositor-drawn decorations so that Wine Explorer draws all window frames and title bars uniformly. |
| `<core><maxRenderTime>` | Integer ms (`0` = off) | Latency reduction late-frame latch. When set $>0$, the compositor calculates the expected VBlank time and delays output rendering until `maxRenderTime` milliseconds before VBlank, sampling the freshest client state. |
| `<keyboard><keybind key="A-Tab"><action name="None"/></keybind></keyboard>` | Action `None` | Disables labwc's built-in Alt+Tab switcher so that Wine Explorer's Win32 `alttab.c` owns task switching. |
| `<mouse><context name="Root"><mousebind button="Right" action="Press"><action name="None"/></mousebind></context></mouse>` | Action `None` | Disables the compositor's root desktop right-click menu, letting Wine Explorer's desktop view handle context menus. |

An empty menu configuration file (`menu.xml`) containing `<openbox_menu/>` is required to prevent labwc from generating default popup menus.

### 3.2 Environment Variables

- `WEX_BLUR=1`: Enables hardware-accelerated SceneFX 0.3 backdrop blur, Acrylic, and Mica effects when `wexcomp` is built with `-Dscenefx=enabled`. If unset or on unaccelerated software renderers, `wexcomp` falls back to solid/translucent tint rectangles.
- `WEX_WAYLAND_NATIVE_WINDOWS=1`: Enables native Linux Wayland application hosting inside Win32 frames with dynamic scale message synchronization.
- `WLR_RENDERER=gles2` / `WLR_RENDERER_ALLOW_SOFTWARE=1`: Forces the GLES2 renderer in headless/virtual environments using llvmpipe and Mesa surfaceless EGL.
- `LABWC_WEX_SHELL_SOCKET`: Defines the socket name for privileged shell communications (default: `wex-shell-0`).

---

## 4. Per-User Prefix Layout (Phase S0–S4 Split)

Traditional Wine provisioning duplicates an entire ~1.7 GB Windows installation (`system32`, `syswow64`, `Program Files`, and machine hives) into every user's `~/.wine` directory. WineExplorer implements a split-prefix architecture modeled after genuine Windows NT/XP multi-user systems.

### 4.1 Architecture Comparison

| Component | Traditional Wine | WineExplorer Split Model (XP Parity) |
|---|---|---|
| `C:\WINDOWS` (`system32`, `syswow64`, `Fonts`) | Duplicated in each `~/.wine` (~1.7 GB) | Shared machine root: `/var/lib/wineexplorer/machine/drive_c/windows` |
| `C:\Program Files` & `(x86)` | Duplicated in each `~/.wine` | Shared machine root: `/var/lib/wineexplorer/machine/drive_c/Program Files` |
| `C:\ProgramData` / `users\Public` | Duplicated in each `~/.wine` | Shared machine root: `/var/lib/wineexplorer/machine/drive_c/ProgramData` |
| Machine Hive (`HKLM`, `HKU\.DEFAULT`) | Isolated copy in each `~/.wine` (`system.reg`) | Shared machine hives in `/var/lib/wineexplorer/machine/*.reg` |
| User Profile (`Documents and Settings\<user>`) | `~/.wine/drive_c/users/<user>` | Real per-user directory in `~/.wine/drive_c/users/<user>` |
| User Hive (`HKCU`) | `~/.wine/user.reg` | Real per-user hive in `~/.wine/user.reg` |
| Total New User Prefix Size | ~1,700 MB | **~1 MB** |

### 4.2 Machine Root Directory Structure

The machine root resides at `/var/lib/wineexplorer/machine` (configurable via `WINE_MACHINE_ROOT`):

```
/var/lib/wineexplorer/machine/
├── drive_c/
│   ├── windows/                # system32, syswow64, Fonts, inf, Resources
│   ├── Program Files/          # Shared 64-bit applications
│   ├── Program Files (x86)/    # Shared 32-bit applications
│   ├── ProgramData/            # Shared application data
│   └── users/
│       └── Public/             # Public desktop and documents
├── system.reg                  # Shared HKEY_LOCAL_MACHINE hive
├── userdef.reg                 # Shared HKEY_USERS\.DEFAULT hive
└── .update-timestamp           # Machine upgrade sync token
```

- **Permissions & Security**: Owned by `root:sudo` with mode `2775` (setgid directory). A POSIX default ACL (`setfacl -d -m g:sudo:rwX`) ensures files created by administrators remain group-writable.
- **Machine Updates**: Executed once as `root` via `scripts/setup-machine-root.sh` or the Debian package `postinst` script.

### 4.3 Per-User Prefix Layout (`~/.wine`)

When a user logs in, their prefix is created from `/etc/skel/.wine` (~1 MB):
- `dosdevices/c:` points to `../drive_c`
- `dosdevices/z:` points to `/` (Linux root)
- `drive_c/users/<username>/`: Real directory holding personal documents and appdata.
- Symlinks: `drive_c/windows`, `drive_c/Program Files`, `drive_c/Program Files (x86)`, `drive_c/ProgramData`, `drive_c/users/Public`, `system.reg`, and `userdef.reg` all point to their respective targets in `/var/lib/wineexplorer/machine/`.

### 4.4 Enforcement & Copy-On-Write Semantics

1. **Standard (Limited) Users**:
   - Machine files are read-only (`EACCES`). Limited users cannot modify system binaries or install software for all users.
   - Writes to `HKLM` succeed in memory for the duration of the session but are never committed to `system.reg` on disk (`save_branch()` safely skips read-only hives without breaking symlinks).
2. **Administrators (Members of `sudo` group)**:
   - Administrators can write to machine folders and install applications for all users.
   - On save, `wineserver` acquires an exclusive `flock()` on the machine lock file, compares hive timestamps, and performs an atomic merge-on-save of changed keys. External deletions and modifications are reconciled without resurrecting tombstoned keys.
3. **Fake DLL Copy-On-Write (`ntdll`)**:
   - When an installer or updater attempts to overwrite a symlinked built-in DLL pointing into `/usr/local/lib/wine`, Wine's `ntdll` detects the write open and breaks the symlink into a private regular file, preventing modifications from polluting the host package tree.

### 4.5 Migration of Existing Prefixes

To migrate an existing monolithic 1.7 GB prefix to the split-prefix layout, run:
```sh
/usr/local/bin/wex-migrate-prefix.sh [prefix_path]
```
The script creates a backup in `~/.wine/pre-split-backup-<timestamp>`, replaces monolithic system trees with symlinks into `/var/lib/wineexplorer/machine`, preserves user documents and `user.reg`, and shrinks the prefix to ~1 MB.

---

## 5. DWM Registry Feature Gates

Desktop Window Manager (DWM) features, composition behavior, and visual styles are controlled via registry values located in `HKCU\Software\WineExplorer\Shell` (and related project subkeys).

### 5.1 Comprehensive Gate Table

| Registry Value | Type | Default | Effect / Mechanism | Target Visual Era |
|---|---|---|---|---|
| `DwmService` | `REG_DWORD` | `0` (off) | Controls whether `explorer.exe` launches the DWM broker (`C:\windows\system32\dwm.exe`) on startup and whether `wexshell` starts the DWM bridge thread to synchronize attributes with the compositor. Required along with `Composition=1`. | Windows Vista / 7 / 10 / 11 DWM Infrastructure |
| `Composition` | `REG_DWORD` | `0` (off) | Primary desktop composition gate. When set to `1` (and `DwmService=1`), enables DWM composition (`DwmIsCompositionEnabled` returns `TRUE`), starts the DWM bridge thread, initializes the frame clock, and under X11 acquires Composite redirection via `WexCompositionSetEnabled`. When `0`, composition is reported disabled. | Windows Vista through 11 Composition |
| `DwmAlpha` | `REG_DWORD` | `0` (off) | Controls glass translucency / alpha channel presentation. When non-zero, allows translucent glass alpha rendering across window surfaces and enables margins extended into the client area via `DwmExtendFrameIntoClientArea`. | Windows Vista / Windows 7 Aero Glass |
| `DwmFrameEffects` | `REG_DWORD` | `0` (off) | Non-client frame glass gate. Read by `uxtheme`, `win32u`, `user32`, and `dwmapi`. When non-zero, captioned windows render non-client titlebars and borders with glass effects even with zero margins, applying rounded outer corners. | Windows Vista / 7 / 10 / 11 Non-Client Frames |
| `DwmMaterials` | `REG_DWORD` | `0` (off) | Advanced backdrop materials gate (Blur, Acrylic, Mica, and Tint Gradients). Read once at bridge thread start. When `0`, backdrops are omitted to conserve performance on unaccelerated systems. Automatically forced off if High Contrast mode is active (`SPI_GETHIGHCONTRAST`) or if `EnableTransparency=0`. | Windows 10 (Acrylic) & Windows 11 (Mica) |
| `DwmFrameStyle` | `REG_DWORD` | `0` (Modern) | Selects frame visual style: `0` (or omitted) selects Windows 10/11 flat thin-bordered frames with optional soft shadows; `1` (or theme INI `FrameStyle=aero` / `1`) selects Windows Vista/7 Aero glass frames with wide blurred borders. Read dynamically on theme changes. | Era Switch: `0` = Win10/11 Modern; `1` = Vista/7 Aero |
| `DwmFrameClock` | `REG_DWORD` | `1` (on) | Rollback switch for DWM frame clock. When `1`, creates shared memory mapping `__wine_dwm_clock_<desk>` and pulses synchronization event `__wine_dwm_clock_event_<desk>` to drive `DwmFlush` and `DwmGetCompositionTimingInfo`. When `0`, disables frame clock publishing. | Windows Vista through 11 Presentation Timing |
| `TaskbarThumbnails` | `REG_DWORD` | `0` (off) | Enables live window thumbnail hover popups above taskbar buttons. Requires active composition. Queries live window surfaces or iconic bitmaps. | Windows Vista / 7 Superbar Previews |
| `AltTabThumbnails` | `REG_DWORD` | `0` (off) | Enables live window thumbnail previews inside the Alt+Tab task switcher instead of classic XP icons. Requires active composition. | Windows Vista / 7 Flip Switcher |
| `TaskbarPeek` | `REG_DWORD` | `0` (off) | Enables Aero Peek when hovering over taskbar thumbnail previews. Temporarily hides all other application windows, leaving desktop and hovered window content visible. | Windows 7 Aero Peek |
| `ManageLinuxWindows` | `REG_DWORD` | `1` (on) | Controls whether Wine Explorer's shell manages and decorates native Linux X11/Wayland applications in themed Win32 frames with taskbar integration. | WineExplorer Hybrid DE Integration |

### 5.2 Related Shell & Applet Registry Keys

| Registry Key & Value | Type | Default | Description |
|---|---|---|---|
| `HKCU\Software\WineExplorer\TrayIcons\<exe>`: `IconBehavior` | `REG_DWORD` | `0` | Per-icon notification tray behavior: `0` = Hide when inactive, `1` = Always hide, `2` = Always show. |
| `HKCU\Software\WineExplorer\Taskbar`: `Rows` | `REG_DWORD` | `1` | Number of task button rows when taskbar is unlocked and dragged vertically. |
| `HKCU\Software\WineExplorer\StartMenu`: `Skin` | `REG_SZ` | `""` | Active Start menu skin name (Open-Shell skin format). |
| `HKCU\Software\WineExplorer\StartMenu`: `SkinVariation` | `REG_SZ` | `""` | Sub-variation or color option for the active Start menu skin. |
| `HKCU\Software\WineExplorer\StartMenu`: `SkinOptions` | `REG_SZ` | `""` | Feature option flags for the active Start menu skin. |
| `HKCU\Software\WineExplorer\Search`: `Character` | `REG_DWORD` | `1` | Enables animated Search Companion assistant character. |
| `HKCU\Software\WineExplorer\Search`: `CharacterFile` | `REG_SZ` | `rover.acs` | Agent character file name loaded from `C:\windows\srchasst\chars\`. |
| `HKLM\Software\WineExplorer\ElevationTest`: `SecureDesktopTest` | `REG_DWORD` | `0` | Test gate forcing UAC elevation prompts to execute on the isolated secure desktop. |

---

## 6. Troubleshooting & Diagnostics

### 6.1 Debug Logging Channels (`WINEDEBUG`)

WineExplorer components use dedicated Wine debug channels for tracing and troubleshooting. Prefix session invocations with `WINEDEBUG`:

```sh
WINEDEBUG=+wexshell,+wexhost wine explorer /shell
```

- `+wexshell`: Traces shell components (taskbar button allocation, start menu view, system tray events, Alt+Tab window switching, desktop grid, DWM bridge polling).
- `+wexhost`: Traces host platform integration (X11 foreign window reparenting, Wayland layer-shell negotiation, GIO application launch, D-Bus system bus calls).
- `+explorer`: Traces `programs/explorer/` desktop window creation and `dwmhost.c` broker supervision.
- `+dwmapi`: Traces `dlls/dwmapi/` client calls, channel IPC opcodes, margin extension, and blur requests.
- `+dcomp`: Traces `dlls/dcomp/` DirectComposition visual tree commits, animation objects, and swapchain binding.
- `+uxtheme`: Traces `dlls/uxtheme/` non-client frame rendering, visual style theme parsing, and frame region clipping.
- `+win32u`: Traces `dlls/win32u/` low-level window management, default window procedure frame hits, and input queues.
- `+winewayland` / `+x11drv`: Traces graphics display driver communication with Wayland or X11 servers.

### 6.2 Log Files & Locations

- **X11 Session Log**: `~/.wine-session.log` (rotated to `~/.wine-session.log.1` on restart). Captures `wine-explorer-session.sh` output, X server diagnostics, and Wine console logs.
- **Wayland Compositor Log**: `~/.local/state/wineexplorer-wayland/session.XXXXXX/compositor.log`. Captures `wexcomp` wlroots output, seat initialization, and scene graph errors.
- **Wayland Shell Log**: `~/.local/state/wineexplorer-wayland/session.XXXXXX/shell.log`. Captures Explorer shell stdout/stderr under Wayland.

### 6.3 Diagnostic Probes (`scripts/check-*`)

The repository includes specialized standalone diagnostic probes in `scripts/`:

| Probe Script | Purpose & Usage |
|---|---|
| `scripts/check-first-thread-alignment.py` | Validates that the first thread's data block in Wine is aligned to the signal stack size. Prevents Chromium profiler threads from hanging during suspension. |
| `scripts/check-runtime-fonts.py` | Asserts that all 6 required TrueType system fonts (Tahoma, Arial, Courier New, Times New Roman, Marlett, Symbol) exist in the prefix. Prevents DirectWrite/Skia font fallback stack exhaustion. |
| `scripts/check-token-security.sh` | Tests token security enforcement, elevation leases, and AppContainer integrity labels. |
| `scripts/check-wayland-dwm.sh` | Probes `wexcomp` Wayland DWM protocol extensions (`wex_window_manager_v1` v14), testing corner radius, effects, and frame clocks. |
| `scripts/check-wayland-backend.sh` | Verifies Wayland socket creation, privileged socket isolation, and basic window mapping. |
| `scripts/check-wayland-association.py` | Asserts correct mapping between Wayland surface IDs and Win32 HWNDs. |
| `scripts/check-wayland-shell.sh` | Tests Wayland layer-shell protocol surfaces (taskbar and desktop layer placement). |
| `scripts/check-wayland-stacking.py` | Tests Wayland Z-order stacking order and transient window parenting. |
| `scripts/check-wayland-stock.sh` | Compares stock Wine Wayland driver behavior against project-extended driver. |
| `scripts/check-wm-backend.sh` | Probes window manager backend selection between X11 and Wayland. |
| `scripts/check-dcomp-surface-screen.sh` | Verifies DirectComposition surface screen capture and swapchain presentation. |

### 6.4 Common Failure Modes & Remedies

1. **Virtual Machine Blank Screens or Stale Mouse Cursors**:
   - *Cause*: VirtualBox / QEMU graphics drivers mishandling hardware cursors or Glamor 2D acceleration.
   - *Fix*: Ensure `/etc/X11/xorg.conf.d/20-wineexplorer-modesetting.conf` specifies `Driver "modesetting"`, `Option "SWcursor" "true"`, and `Option "AccelMethod" "none"`.
2. **Chromium / Electron Application Stack Overflow on Launch**:
   - *Cause*: Tahoma or core TrueType fonts missing from `C:\windows\Fonts`, triggering infinite recursive fallback in DirectWrite.
   - *Fix*: Run `python3 scripts/check-runtime-fonts.py` and ensure metric-compatible open fonts are installed in the machine root.
3. **Chromium Profiler Hang on Multi-Core Hosts**:
   - *Cause*: First thread data block unaligned with POSIX signal stack boundaries.
   - *Fix*: Verify Wine build includes the thread data alignment patch (`scripts/check-first-thread-alignment.py`).
4. **DWM Broker Crash Loop Backoff**:
   - *Cause*: If `dwm.exe` crashes $>3$ times within one minute, `explorer.exe` enters a backoff state and ceases restarts to prevent CPU thrashing.
   - *Fix*: Inspect `WINEDEBUG=+dwmapi,+explorer` logs to diagnose the crash cause. Reset the broker by toggling `HKCU\Software\WineExplorer\Shell\DwmService` off and on.
5. **Wayland Privileged Protocol Rejection (`WL_DISPLAY_ERROR_INVALID_OBJECT`)**:
   - *Cause*: An unprivileged client attempting to bind `wex_window_manager_v1` on `wayland-0`.
   - *Fix*: Privileged shell operations must connect exclusively via `LABWC_WEX_SHELL_SOCKET` (`wex-shell-0`).
6. **SceneFX Backdrop Blur Fallback to Flat Tint**:
   - *Cause*: Running without `WEX_BLUR=1`, or running on unaccelerated Pixman software renderers without GLES2/EGL.
   - *Fix*: Ensure `WEX_BLUR=1` is exported and Mesa surfaceless EGL patch `0001-fx_renderer-surfaceless-egl.patch` is present.
7. **Permission Denied on Multi-User Application Installation**:
   - *Cause*: Standard users attempting to write to `/var/lib/wineexplorer/machine`.
   - *Fix*: Run installers as a user belonging to the `sudo` group, or execute administrative installations using `pkexec` / elevation leases.

---

## 7. How to Verify (Test Script Inventory)

The repository provides comprehensive test suites in `installer/tests/` and `scripts/`. Below is a one-line summary of every verification script:

### 7.1 Installer Test Suite (`installer/tests/`)

- `boot-safety.sh`: Verifies bootloader configuration and partition safety rules during installation.
- `check-interactive-accounts.py`: Asserts user accounts, sudo group membership, and split-prefix symlink integrity on an interactive install.
- `check-lightdm.py`: Validates LightDM configuration, autologin, and greeter session setup.
- `check-native-tray.py`: Validates StatusNotifierItem and XEmbed native tray integration in live sessions.
- `check-wayland-polish.py`: Verifies Wayland visual appearance including corner rounding, shadows, and window borders.
- `check-wayland-session.py`: Verifies Wayland lab session startup, socket creation, and window management under wexcomp.
- `check-wow64.sh`: Validates multi-user WoW64 execution, 32/64-bit binaries, and admin vs limited user permissions.
- `collect_conformance_logs.py`: Gathers test logs from Wine conformance test runs during installation testing.
- `collect_s3_conformance_logs.py`: Gathers registry test logs specifically for Phase S3 shared machine hive verification.
- `cow_test.c`: C test probe checking copy-on-write behavior when writing to symlinked Wine fake DLLs.
- `install-interactive.sh`: Automated QEMU test harness executing interactive ISO installation with user prompts.
- `install-unattended.sh`: Automated QEMU test harness executing zero-input unattended ISO installation from unattend.sif.
- `inventory_real_files.py`: Audits files in `system32`/`syswow64` to verify which files are symlinks vs real copies.
- `prepare-safety-isos.py`: Generates test ISO variants with modified partition headers for boot safety testing.
- `prepare-unattended-iso.py`: Injects `unattend.sif` into the installation ISO for automated zero-input testing.
- `run_cow_tests.sh`: Runner script executing the copy-on-write regression suite across user and admin accounts.
- `run_startskin_acceptance.sh`: Acceptance test runner for Start menu skinning and theme variation rendering.
- `run_startskin_fuzz.sh`: Fuzzing test runner testing Start menu skin parser robustness against malformed skin definitions.
- `select-wayland.py`: Helper script configuring LightDM or session scripts to launch the Wayland compositor session.
- `session-run.py`: Automated runner that boots a VM or container session and waits for desktop readiness.
- `test-program.c`: Minimal Win32 GUI test program used to verify binary execution across WoW64 architectures.
- `test_copydirs.py`: Tests recursive directory copy logic in installer staging scripts.
- `test_cow_cases.c`: Test program covering edge cases of copy-on-write file opens, overwrites, and unlinking.
- `test_s1.c`: Phase S1 test program validating symlink traversal to Wine library directories.
- `test_s2_acceptance.sh`: Phase S2 acceptance suite verifying shared machine root folders and read-only user access.
- `test_s3_acceptance.sh`: Phase S3 acceptance suite verifying shared `HKLM` registry hive, merge-on-save, and isolation.
- `test_s4_acceptance.sh`: Phase S4 acceptance suite verifying full end-to-end split-prefix installation and migration.
- `test_startskin_acceptance.c`: C test suite checking Start menu skin rendering, metrics, and bitmaps.
- `test_startskin_fuzz.c`: Fuzz test binary feeding corrupted `.skin` files to the skin parser to prevent crashes.

### 7.2 Scripts Test & Probe Suite (`scripts/`)

- `check-compose-placement.py`: Checks compositor window placement and geometry under X11/Wayland.
- `check-dcomp-surface-screen.sh`: DComp surface presentation and screen capture verification.
- `check-first-thread-alignment.py`: Validates first thread data block signal stack alignment to prevent Chromium profiler hang.
- `check-font-targets.py`: Audits target font directories and replacement mappings for UI font parity.
- `check-g3-correctness.py`: Correctness test suite for G3 graphics pipeline and swapchain presentation.
- `check-g4-chromium-headful.py`: Validates headful Chromium browser launch and rendering under the G4 graphics bridge.
- `check-g4-chromium-native-exit.py`: Tests clean native exit and shutdown of Chromium processes under Wine.
- `check-g4-composition.py`: Verifies G4 composition pipeline across DirectX and Wayland surface boundaries.
- `check-g4-edge-headful.py`: Validates headful Microsoft Edge browser launch and window presentation.
- `check-g4-edge-install.py`: Tests Microsoft Edge installation integrity and binary verification.
- `check-g4-foreign-present.py`: Tests cross-process presentation of foreign producer frames into host UI windows.
- `check-g4-native-egl.py`: Checks EGL context creation, extensions, and surface binding for native acceleration.
- `check-g4-native-exit-calibration.py`: Calibrates timeout thresholds for native process termination.
- `check-g4-pacing.py`: Validates frame pacing, submission-to-presentation latency, and frame drops.
- `check-g4-stack-calibration.py`: Calibrates stack sizes and signal handling for multi-threaded browser engines.
- `check-g4-webview2.py`: Validates WebView2 loader and Evergreen runtime initialization.
- `check-oobe-captures.py`: Audits visual screenshots captured during OOBE setup wizard execution.
- `check-runtime-fonts.py`: Validates presence of 6 required TrueType fonts (Tahoma, etc.) to prevent DirectWrite recursion.
- `check-token-security.py` / `check-token-security.sh`: Tests token privilege filtering, AppContainer labels, and UAC elevation leases.
- `check-wayland-association.py` / `check-wayland-association.sh`: Validates association between Wayland surfaces and Win32 HWNDs.
- `check-wayland-backend.sh`: Probes Wayland compositor backend communication and privileged socket availability.
- `check-wayland-clicks.py`: Tests mouse click dispatch and hit-testing across Wayland client and frame boundaries.
- `check-wayland-dwm.sh`: Verifies Wayland DWM protocol extensions (`wex_window_v1`, corner radius, effects).
- `check-wayland-positions.py`: Validates multi-monitor window positioning and work-area clamping on Wayland.
- `check-wayland-shell.py` / `check-wayland-shell.sh`: Tests Wayland shell surface roles (layer-shell, desktop, taskbar).
- `check-wayland-stacking.py`: Tests window Z-order stacking, transient relationships, and topmost window ordering on Wayland.
- `check-wayland-stock.sh`: Compares stock Wine Wayland driver behavior against project-extended driver.
- `check-wex-render-surface.py`: Verifies render surface allocation and pixel formats.
- `check-wm-backend.sh`: Probes window manager backend dispatch between X11 and Wayland.
- `dwm-p2-lifecycle-test.sh`: Tests DWM broker lifecycle, startup, termination, and crash recovery.
- `dwm-p3-attr-test.sh`: Tests DWM attribute query and mutation protocol requests.
- `probe-edge-capture.sh`: Captures framebuffer output of Microsoft Edge during test runs.
- `probe-edge-sandbox.sh`: Probes sandbox token restrictions and integrity levels for Edge processes.
- `probe-egl-capabilities.py`: Queries EGL device extensions, formats, and surfaceless platform support.
- `probe-g4-virgl-host.py`: Probes host GPU acceleration and VirGL 3D capability for virtual machines.
- `run-webview2-lab.sh`: Automated test runner executing the WebView2 compatibility lab test suite.
- `secure_desktop_probe.sh`: Tests switching to the isolated Winlogon secure desktop for UAC elevation prompts.
- `test-edge-local.sh`: Executes local installation and startup test of Microsoft Edge.
- `test-edge-stock-sandbox.sh`: Evaluates Edge sandbox operation against unmodified stock Wine behavior.
- `test-runner-sentinel.sh`: Process watchdog sentinel monitoring test runner execution and preventing hangs.
- `test-token-runner-isolation.py`: Tests process token isolation between concurrent test runner instances.
- `test_assembly_registration.py`: Verifies Win32 SxS assembly registration and COM manifest resolution.
- `test_b3_machine_root.sh`: Validates Phase B3 machine root directory layout, ownership, and permissions.
- `test_b4_explorer_fresh.sh`: Tests explorer startup in a completely fresh, uninitialized Wine prefix.
- `test_component_recovery.py`: Tests optional component recovery and rollback on failure.
- `test_component_review.py`: Audits staged optional components and their license metadata.
- `test_component_runtime_artifacts.py`: Verifies generated runtime artifacts for installed Windows components.
- `test_component_terms.py`: Validates license agreement display and acceptance during component installation.
- `test_fluent_acceptance.py`: Acceptance test verifying Fluent UI icon pack rendering and theme consistency.
- `test_fluent_t1_cpl.py`: Tests Fluent UI icon integration inside Control Panel applets.
- `test_g4_capacity.py` / `test_g4_capacity_maintenance.py`: Tests resource allocation capacity and texture memory limits under G4.
- `test_g4_chromium_cache.py`: Verifies Chromium disk and shader cache persistence across sessions.
- `test_g4_chromium_headful.py`: Validates Chromium visual rendering and GUI interaction.
- `test_g4_chromium_module_adapter.py`: Tests module loader adaptation for Chromium shared libraries.
- `test_g4_chromium_native_adapter.py`: Validates native X11/Wayland event loop integration for Chromium.
- `test_g4_chromium_native_exit.py`: Verifies clean termination without orphaned renderer child processes.
- `test_g4_chromium_observation_tolerance.py`: Tests frame timing jitter tolerance during automated browser observation.
- `test_g4_chromium_userenv_adapter.py`: Validates Userenv profile path resolution for Chromium user data.
- `test_g4_chromium_userenv_r4_adapter.py` / `test_g4_chromium_userenv_r6_adapter.py`: Revision-specific Userenv profile adaptation tests.
- `test_g4_chromium_wait_snapshot.py`: Takes synchronization snapshots during Chromium startup phases.
- `test_g4_combined_r6_deploy.py`: Validates combined R6 graphics and broker deployment.
- `test_g4_compositor_scope.py`: Verifies compositor surface tracking scope and damage boundaries.
- `test_g4_consumer_harness.py`: Test harness driving browser consumer workloads under G4.
- `test_g4_diagnostic_inventory.py`: Inventories active graphics contexts, surfaces, and buffer allocations.
- `test_g4_edge_headful.py`: Headful browser functional test for Microsoft Edge.
- `test_g4_edge_http_fixture.py`: Local HTTP test fixture serving test pages for browser validation.
- `test_g4_edge_install.py`: Tests installation and registration of Microsoft Edge.
- `test_g4_edge_owned_threads.py`: Audits thread ownership and lifetime in Edge browser processes.
- `test_g4_guest_readiness.py`: Tests VM guest readiness before initiating graphical test passes.
- `test_g4_idle_diagnostic.py`: Validates zero CPU/GPU waste and zero damage emission during idle intervals.
- `test_g4_native_calibration_refresh.py`: Refreshes timing calibration for native window event dispatch.
- `test_g4_native_exit.py` / `test_g4_native_exit_calibration.py`: Tests clean exit signaling and teardown.
- `test_g4_native_identity_isolation.py`: Verifies security isolation of window identity handles across processes.
- `test_g4_native_loader_r4.py`: Tests dynamic loading of native graphics libraries under R4.
- `test_g4_observation_control.py`: Controls automated visual inspection and frame capture.
- `test_g4_pacing_probe.cpp`: C++ test probe verifying GPU command pacing and presentation queues.
- `test_g4_prefix_ownership.py` / `test_g4_prefix_ownership_integration.py`: Validates prefix file permissions and ownership rules.
- `test_g4_presentation_timing.py`: Tests presentation timing synchronization between DirectX and compositor.
- `test_g4_qmp_shutdown.py`: Tests QEMU guest shutdown via QMP monitor during test runs.
- `test_g4_r6_host_adapter.py`: Tests R6 host adapter layer for external display rendering.
- `test_g4_ready_boot_integration.py`: Verifies boot-time prefetch and warm-start optimization.
- `test_g4_rejected_identity.py`: Tests security rejection of unauthorized cross-process window identity spoofing.
- `test_g4_remote_failure.py`: Simulates remote compositor or broker failure to test client recovery.
- `test_g4_renderer_unblock.py`: Tests unblocking hung GPU renderer processes.
- `test_g4_runtime_unblock.py`: Verifies unblocking blocked Win32 runtime calls.
- `test_g4_stack_calibration.py` / `test_g4_stack_commands.py`: Stack frame depth verification and debugging helper.
- `test_g4_theme_harness.py`: Tests browser non-client frame theming under various visual styles.
- `test_g4_userenv_sid_cleanup.py`: Verifies cleanup of per-user SID directories upon profile removal.
- `test_g4_userenv_view.py`: Tests Userenv profile view mapping in split prefixes.
- `test_g4_virgl_budget.py` / `test_g4_virgl_launcher.py`: Validates VirGL memory budget enforcement and launcher.
- `test_g4_visible_webgl.py`: Tests WebGL 1.0 and 2.0 hardware-accelerated rendering inside browser frames.
- `test_g4_wexcomp_harness.py`: Test harness exercising wexcomp compositor features under G4.
- `test_gate_e_synthetic.py`: Synthetic test suite for Gate E graphics pipeline validation.
- `test_graphics_frame_pixels.py`: Validates exact pixel values across rendered graphics frames.
- `test_graphics_harness.py`: General graphics driver and swapchain test harness.
- `test_graphics_private_loopback.py`: Tests private IPC loopback channel for graphics commands.
- `test_graphics_probes.py`: Python orchestrator running compiled C++ graphics probes.
- `test_graphics_resources.py`: Monitors GPU memory, textures, and buffer resource lifecycle.
- `test_graphics_x_socket_isolation.py`: Verifies X11 socket permission and security isolation.
- `test_live_oobe.sh`: End-to-end interactive test script for live Out-Of-Box Experience setup.
- `test_oobe_bridge.py`: Tests communication between OOBE wizard UI and host user management backend.
- `test_oobe_capture_audit.py`: Audits screenshots taken during OOBE execution for visual regressions.
- `test_oobe_review.py` / `test_oobe_review_layout.py`: Tests OOBE user interface layout and DPI scaling.
- `test_runtime_repairs.py`: Verifies runtime error detection and automatic repair mechanisms.
- `test_safemode.py`: Validates Safe Mode boot option and minimal service initialization.
- `test_sysrun_server.sh`: Tests sysrun system-level service daemon operation.
- `test_webview2_analysis.py`: Performs static and dynamic analysis of WebView2 runtime initialization logs.
- `test_wex_sysrun.sh`: Tests sysrun elevation lease API and privilege granting.
- `wayland-l1-check.sh`: Shell script running L1 Wayland protocol and window management checks.

Compiled C/C++ diagnostic probe binaries located in `scripts/`:
- `appcontainer-label-probe.c`: Validates mandatory integrity labels on scratch directories and files.
- `browser-module-probe.c`: Probes browser DLL loading and COM class registration.
- `compose-probe.c`: Probes X11 Composite and Damage extension interactions.
- `cursor-probe.c`: Tests hardware and software mouse cursor shape changes.
- `display-output-probe.c`: Probes XRandR vs DRM modesetting and display topology.
- `dwm-backdrop-color-probe.c`: Measures backdrop color blending under active composition.
- `dwm-blur-visual-probe.c`: Validates backdrop blur rendering under SceneFX and fallback tinting.
- `dwm-corner-shadow-probe.c`: Measures corner radius clipping and shadow geometry.
- `dwm-corner-visual-probe.c`: Validates anti-aliased corner pixel values.
- `dwm-dcomp-alpha-visual-probe.cpp`: Tests DirectComposition alpha channel presentation.
- `dwm-frame-metrics-probe.c`: Validates window frame border widths and titlebar height across themes.
- `dwm-frame-visual-probe.c`: Verifies visual appearance of window non-client frames.
- `dwm-glass-visual-probe.c`: Checks translucent glass rendering in client margins.
- `dwm-glassframe-visual-probe.c`: Tests Aero glass frame drawing on captioned windows.
- `dwm-hwnd-reuse-visual-probe.c`: Tests HWND recycling to ensure stale DWM attributes are purged.
- `dwm-iconic-visual-probe.c`: Tests iconic thumbnail and live preview bitmap registration and display.
- `dwm-mica-visual-probe.c`: Validates Mica material backdrop isolation against desktop wallpaper.
- `dwm-thumbnail-visual-probe.c`: Checks live thumbnail downscaling and positioning.
- `dwm-timing-iconic-probe.c`: Tests DWM timing synchronization and iconic peek latency.
- `dwmapi-probe.c`: Verifies dwmapi export functions and error code returns.
- `dwrite-font-probe.c`: Probes DirectWrite font enumeration and fallback chains.
- `linux-apps-probe.c`: Tests Linux application enumeration and launch via GIO.
- `linux-dialog-probe.c`: Tests native X11 dialog positioning and work-area clamping.
- `network-module-probe.c`: Tests NetworkManager backend and Wi-Fi scanning.
- `oobe_review_layout_probe.c`: Probes OOBE wizard page layout and control positions.
- `power-module-probe.c`: Probes sysfs battery and AC power supply interfaces.
- `search-module-probe.c`: Tests Search Companion COM interfaces and character animation.
- `secure_desktop_probe.c`: Tests UAC consent prompt on the isolated secure desktop.
- `shell-plugin-probe.c`: Validates explorer shell plugin hook loading and version negotiation.
- `sizing-loop-probe.c`: Detects window resize oscillation loops between Win32 and X11/Wayland.
- `suspend-context-probe.c`: Validates thread suspension and register context sampling.
- `sysrun_roles_probe.c` / `sysrun_server_probe.c`: Tests role assignment and client requests on sysrun daemon.
- `theme-gallery-probe.c`: Renders visual style samples across all themes for regression testing.
- `theme-module-probe.c`: Tests `.theme` file parsing and uxtheme color/metric application.
- `theme-parts-probe.c`: Probes `.msstyles` part and state bitmap extraction.
- `theme-preview-probe.c`: Tests monitor preview rendering control in Display Properties.
- `themed-frame-probe.c`: Tests Luna/Aero/Classic non-client window frame region shaping.
- `tray-layout-probe.c`: Checks notification area layout bounds and chevron positioning.
- `tray-service-probe.c`: Tests tray notification message routing and icon callbacks.
- `tray_balloon_probe.cpp`: Tests tray balloon tooltip creation, timeout, and dismissal.
- `wayland-adoption-probe.c`: Tests Wayland window adoption into Win32 shell management.
- `wayland-association-probe.c`: Verifies mapping between Wayland surface IDs and HWNDs.
- `wayland-pointer-move.c`: Injects virtual pointer motion events via `zwlr_virtual_pointer_v1`.
- `wayland-position-probe.c`: Tests window placement across multi-output Wayland layouts.
- `wayland-shell-probe.c`: Tests Wayland shell surface role binding and configure events.
- `wayland-stacking-probe.c`: Tests Wayland window Z-order layering and transient parentage.
- `wayland-stock-probe.c`: Probes stock Wayland driver behavior against project modifications.
- `wayland-window-control.c`: Tests programmatic minimize, maximize, and restore on Wayland.
- `webview2-probe.c`: Probes WebView2 COM interfaces and edge loader initialization.
- `wex-wayland-backend-probe.c`: Tests direct connection to `wexcomp` compositor protocol interfaces.
- `wex-wayland-binding-probe.c`: Tests protocol interface binding permissions and version negotiation.
- `wex-wayland-state-probe.c`: Inspects compositor view state and property lists.
- `wex-wm-backend-probe.c`: Tests window manager backend switching and capability reporting.
- `wexelevate_probe.c`: Tests UAC elevation request dispatch and consent broker invocation.
- `x11_grab_probe.c`: Tests X11 pointer and keyboard grab handling during modal UI operations.
