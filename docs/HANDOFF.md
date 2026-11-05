# WineExplorer handoff — current entry point

## Unified development baseline (2026-11-01)

Development test automation: [WEXDEV.md](WEXDEV.md) documents
`scripts/wexdev.sh` (sync/targeted builds, prefixes/tests, system-role services,
guest push) and its deferred TESTPLAN. Runtime acceptance is pending; do not run
it while Qwen owns the wexbuild registry-ACL fixture.

**Update (2026-11-01, later):** Wine `development` is now upstream 11.21 plus the PE section cache, its hardening and a
real `EmptyWorkingSet` (`bed5c5ea`); Edge and Chromium both run on it. See
[EDGE_DEVELOPMENT_RUN_20261101.md](EDGE_DEVELOPMENT_RUN_20261101.md) for measurements and
[PREFETCH_SERVICE.md](PREFETCH_SERVICE.md) for the planned image-cache and prefetch service. The paragraph below describes
the earlier 11.20 pin.

Use `development` in both `zcomputerwiz/WineExplorer` and `zcomputerwiz/wine` for
ongoing integration. WineExplorer combines the completed OOBE/recovery work,
Chromium token/broker reconciliation through r37, and the subsequent Chromium
diagnostics. Its Wine gitlink is `b5e05301c4a52bff777a6b9fd6bfe03c3428d035`;
`.gitmodules` also tracks Wine's `development` branch. Default/release branches
are unchanged. The older branch names in dated reports identify historical work,
not the current integration target.

The integration keeps the `wexsys` machine-root ownership model and elevation
lease APIs alongside the OOBE APIs. Native and WoW64 bridge call tables must stay
in the same order as their shared enum; rebuild both PE architectures and the
Unix bridge together. Font setup uses the selected Wine/server pair and preserves
the isolated bootstrap skip mode. Fresh-prefix Chromium and basic desktop functionality are now independently
validated, including actual front pixels with the retained v11 bridge. Assembly
and runtime-font packaging are repaired. See
[DEVELOPMENT_VALIDATION_20261101.md](DEVELOPMENT_VALIDATION_20261101.md) for measured
results and limits; older unvalidated statements below are historical.

The earlier operational handoff is [CODEX_HANDOFF.md](CODEX_HANDOFF.md), updated
2026-10-13 after the completed refactor and fresh installer acceptance. Start there for branches, VM
state, clean builds, test commands and remaining work. Current acceptance
evidence is in [refactor/P4.md](refactor/P4.md) and [INSTALLER_TEST_REPORT.md](INSTALLER_TEST_REPORT.md), the
logon/composition plan is in [WINLOGON_AND_DWM.md](WINLOGON_AND_DWM.md), and OOBE optional
components acceptance is in [OOBE_COMPONENTS_ACCEPTANCE.md](OOBE_COMPONENTS_ACCEPTANCE.md).

The October 9 notes below are historical. Their VM state, resource configuration
and commit IDs predate the licensing/history rewrite and later LightDM/compositor
work; they do not describe the current running desktop or installer.

# Historical handoff — 2026-10-09

## Current direction and state

Active checkout: `D:\DeepThought\Projects\WineExplorer`, branch `master`. Customized Wine 11.19 remains the desktop runtime and X11 window manager. Proton is an independent application runtime with its own prefixes and Steam/launcher integration. The separate ProtonExplorer project/VM is a compatibility lab, not the replacement desktop.

The live stack is **GDM → Xorg → Wine virtual desktop → Explorer shell/window manager**. Explorer hosts native Linux and separate Wine/Proton X11 windows in themed Win32 frames. Gamescope is not installed in this VM; there is no session-wide Gamescope/pass-through service. `wexhost` provides X11 management, GIO application discovery/launch, system D-Bus services and XRandR display control. Gamescope per application remains a separate option.

Current visible session: WineExplorer-VM, Luna/Bliss, 800×600. My Computer and Documents were restored after the last reload; no LibreOffice document was open at that reload. Hide inactive icons is **enabled**. Temporary tray test icons and preferences were removed. Existing per-icon preferences remain intact. Recheck open applications before any future session restart.

## Changes completed and deployed

| Commit | Change and verification |
|---|---|
| `e75e717` | Dynamic Linux Applications submenu in both Start menu styles, launched by desktop ID through GIO. Checked visibility rules, Unicode/ampersands, metadata limits, missing IDs, dynamic install/remove and direct-launch environment isolation. |
| `190b5e6` | Native client-area clicks activate and deliver keyboard input after switching from another Wine process. Correct input-queue attachment direction, X/Win32 focus synchronization and deferred Alt+Tab completion. Repeated real xterm typing and cross-runtime switching passed. |
| `551bfaf` | Native dialogs fit the monitor work area; reparented client ConfigureRequests remain intercepted. Eight native regression cases passed, including actual client bounds. LibreOffice Save was repaired to a reachable 800×570 frame; separate-profile reopening was verified. |
| `b0fb4a0` | Task buttons own local icon copies, importing X11 pixels when cross-process Wine icon handles cannot be copied. My Computer, Documents and Control Panel have distinct icons; navigation updates the same task button. |
| `a32a08b` | Start menu highlight preserves icon colors/transparency. Luna caption and Start panel regions follow theme transparency, removing white square corners. Frame probe passed maximize/restore, explicit app regions and Classic reset. Theme-only startup no longer refreshes an uninitialized desktop grid. |
| `99fcd88` | Tray reserves the theme's left content margin even without a chevron. Turning hiding off previously put icons over the dark blue transition; both icons now remain in the cyan content area. |
| `1354742` | Recorded live checks with hiding enabled: no chevron, collapsed chevron and expanded chevron all align without overlaps. Removed temporary test icon and restored hiding to enabled. |

The code above is built, installed and loaded in the visible VM. The latest guest Explorer build, installed module and prefix `windows/explorer.exe` have matching SHA-256 hashes (checked during this handoff). No new application code was changed in the documentation pass.

## Runtime, build and deployment

- VM: `WineExplorer-VM`. SSH: `wineuser@127.0.0.1`, port `2222`, host key file `D:/VirtualBox_VMs/vm_key`.
- Guest source: `~/src/wine`; build: `~/build/wine64`; installation: `/usr/local`. Guest source is copied from this repository, not a Git checkout. Commit on the Windows host.
- Live environment: `DISPLAY=:0`, `XAUTHORITY=/run/user/1000/gdm/Xauthority`, `WINEPREFIX=$HOME/.wine`.
- Session entry point: `/usr/local/bin/wine-explorer-session.sh`. It reads the Winlogon Shell value, restores the accepted display mode and starts Explorer directly on X11. With the current configuration it runs `/desktop=Default,800x600 /shell`.
- Log: `~/.wine-session.log` (previous log `.1`).
- Host helper: `scripts/vm.sh`, run from Git Bash. Read its command header before using it; sync, build, install, screenshots and cursor selection are supported.

Typical guest targeted builds:

```sh
cd ~/build/wine64
make -j8 dlls/wexhost/all programs/explorer/all
# If changing frame-region hooks:
make -j8 dlls/user32/all dlls/uxtheme/all programs/explorer/all
```

Use the `/all` targets; directory names alone do not rebuild modules. Shared-header changes can require a broader rebuild. The frame-region work changed the internal user32/uxtheme hook structure: deploy matching user32 and uxtheme builds together.

PE modules live in `/usr/local/lib/wine/x86_64-windows`; Unix libraries in `/usr/local/lib/wine/x86_64-unix`. The prefix also has builtin copies in `~/.wine/drive_c/windows/system32`, plus `~/.wine/drive_c/windows/explorer.exe`. Refresh all relevant copies. When replacing a loaded file, install to a temporary sibling then rename it atomically rather than truncating the mapped file. `scripts/vm.sh install` supplies the full-install workflow.

A GDM restart loads the new shell/libraries and **closes graphical applications**. Prior user authorization covered real-session testing and restarts, but check for new unsaved work. Explorer is the session process: exiting it logs out. Restore desired folder windows afterward. Prefer isolated tests when a live reload is unnecessary; do not kill all wineservers when using an independent test prefix.

Host screenshot command (PowerShell):

```powershell
& 'C:/Program Files/Oracle/VirtualBox/VBoxManage.exe' controlvm WineExplorer-VM screenshotpng 'D:/DeepThought/Projects/WineExplorer/build/check.png'
```

Windows XP Pro is the visual/behavioral reference VM. Reference source/assets live in ignored `references/`; generated screenshots, binaries and test scripts in ignored `build/` are not guaranteed handoff artifacts. Maintained probes are under `scripts/`.

## Verification tools and limits

| Probe / check | Purpose and environment |
|---|---|
| `scripts/linux-apps-probe.c` | Enumerate or launch a desktop ID through the bridge. Compile with MinGW; run through Wine. |
| `scripts/linux-dialog-probe.c` | Eight native dialog placement/client geometry cases. Compile with `cc -Wall -Wextra ... -lX11`; use an isolated 800×600 Xvfb session with a 30-pixel taskbar. |
| `scripts/themed-frame-probe.c` | Luna frame region, app-defined regions, maximize/restore and Classic reset. MinGW with `-lgdi32`; requires an English My Computer window. It moves/resizes the test window, so use an isolated session. |
| `scripts/tray-layout-probe.c` | Read-only visible tray child bounds versus icons, clock and task band. MinGW `-mwindows`; writes `Z:\tmp\tray-layout-probe.txt`. Bounds alone do not validate the cyan themed boundary: also inspect screenshots. |
| `scripts/display-output-probe.c` | XRandR versus read-only DRM scanout. See DISPLAY_OUTPUT_TESTING.md for build/run and supported topology. |
| `scripts/check-first-thread-alignment.py`, `scripts/suspend-context-probe.c` | Run after every Wine rebase: the first thread's data block must be aligned to the signal stack size, and a sampler must be able to suspend the main thread and read its context (Chromium's profiler hung otherwise). Build the probe with both MinGW compilers, run it through Wine, 32-bit included. See CHROMIUM_FIRST_THREAD_DATA_ALIGNMENT_20261101.md. |
| `scripts/check-runtime-fonts.py`, `scripts/dwrite-font-probe.c` | Run on every packaged Wine runtime: all six TrueType fonts (Tahoma above all) must be present, or Chromium's font initialization recurses until its stack is gone. The probe shows what DirectWrite and Skia see; build it with both MinGW compilers and run it through Wine (`-v` lists every family). See CHROMIUM_MISSING_WINE_FONTS_20261101.md. |
| `scripts/appcontainer-label-probe.c` | Sets and reads a mandatory label on a scratch directory and file the way Chromium's AppContainer profile code does and prints the Win32 error of every call, so Wine can be compared with a native Windows host. Run as a standard user; build 64- and 32-bit. See CHROMIUM_APPCONTAINER_LABEL_20261101.md. |

Isolated GUI tests used Xvfb `:6` and `~/launcher-check/prefix`. Apply the theme in that prefix, stop **its** wineserver, then start a fresh `/shell` desktop: applying a theme first can create a plain desktop that otherwise gets reused without a taskbar. The server must match the Wine build/runtime.

Latest tray evidence: host `build/tray-hide-off-fixed.png`, `tray-on-chevron-collapsed.png`, `tray-on-chevron-expanded.png`, `tray-on-final.png`. Two visible icons collapsed and three expanded passed actual child-window bounds checks; screenshots confirmed the theme content area. Settings key: `HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Taskbar`, `HideInactiveIcons=1`. Per-icon preferences: `HKCU\Software\WineExplorer\TrayIcons`.

Keep-on-top behavior was verified earlier with Notepad activated, maximized and sized over the taskbar, including auto-hide/reveal. See SYSTEM_TRAY.md. Physical-output testing verified VirtualBox guest DRM scanout changes, Keep/No/timeout and restoration to 800×600; it did not test a host physical monitor, Hyper-V or VMware.

## VM graphics caveats

`/etc/X11/xorg.conf.d/20-wineexplorer-modesetting.conf` currently sets `Driver "modesetting"`, `SWcursor "true"` and `AccelMethod "none"`. The software cursor avoids stale VirtualBox hardware cursor shapes. Glamor was subsequently disabled because native Linux Xrender/core drawing was blank or corrupt, while Wine's SHM drawing worked. Earlier notes describing accelerated glamor are historical. This is a working compatibility configuration, not a measured performance improvement or a universal hypervisor recommendation.

Guest cursor images changed correctly during the original fault; a driver/hypervisor stale presentation cache is plausible but its exact cause was not proven. Hardware cursors and acceleration need independent retesting after graphics-stack updates.

## Remaining work / next pickup

1. Linux Applications menu icons/categories and pinned/MFU integration. Current menu is a flat, dynamically refreshed list capped at 512 entries.
2. Linux tray icons (StatusNotifierItem) and notifications; see DBUS_PLAN.md.
3. Launch and verify a real Steam/Proton application using its independent runtime/prefix. Native Linux hosting is verified; this continuation did not exercise a Proton application in WineExplorer-VM.
4. Multi-monitor work areas/struts, foreign-window grouping and shell-crash recovery. Native Wayland window management and a session-wide Gamescope bridge are not implemented.
5. Accelerated/per-hypervisor graphics and physical-output tests need suitable hardware. The VM currently reports CPU Vulkan; nested Gamescope output resizing is not evidence of changing physical scanout.
6. WebView2 remains a separate lab. A positive baseline-Wine test on ProtonExplorer-VM used a per-app Windows 7 override for `msedgewebview2.exe` plus SwiftShader. Do not infer accelerated rendering or shell integration from it; see WEBVIEW2.md for exact provenance and remaining tests.
7. Chromium fresh-prefix navigation, WebGL1/2 fixture and front presentation now pass on the combined development build, alongside the desktop/Start menu/Notepad/File Explorer smoke checks. The retained v11 Wine driver bridge fixes the otherwise white browser surface. See DEVELOPMENT_VALIDATION_20261101.md. Lowbox/MIC/broker enforcement and full installer/login acceptance remain separate from this functional result.

## Documentation map

- [ADMIN_GUIDE.md](ADMIN_GUIDE.md): system administration, installation/live-build, session models, DWM registry gates, and troubleshooting.
- [USER_GUIDE.md](USER_GUIDE.md): end-user XP-style shell, Start menu variants, themes, where files live, and verified keyboard shortcuts.
- [ARCHITECTURE.md](ARCHITECTURE.md): current desktop/runtime ownership.
- [LINUX_WINDOWS.md](LINUX_WINDOWS.md): native hosts, focus, launch bridge and dialog constraints.
- [START_MENU.md](START_MENU.md) / [THEMES.md](THEMES.md): shell settings, theme packages, menu icon and region behavior.
- [SYSTEM_TRAY.md](SYSTEM_TRAY.md): hiding, chevrons, network activity, boundaries and topmost checks.
- [DISPLAY.md](DISPLAY.md) / [DISPLAY_OUTPUT_TESTING.md](DISPLAY_OUTPUT_TESTING.md): graphical mode changes and output validation.
- [STOCK_CHANGES.md](STOCK_CHANGES.md): changes outside the Explorer/wexhost modules.
- [TROUBLESHOOTING.md](TROUBLESHOOTING.md): cursor and VM integration notes.
- [CHROMIUM_FIRST_THREAD_DATA_ALIGNMENT_20261101.md](CHROMIUM_FIRST_THREAD_DATA_ALIGNMENT_20261101.md) / [CHROMIUM_MISSING_WINE_FONTS_20261101.md](CHROMIUM_MISSING_WINE_FONTS_20261101.md) / [CHROMIUM_APPCONTAINER_LABEL_20261101.md](CHROMIUM_APPCONTAINER_LABEL_20261101.md): why Chromium stopped working on the new Wine base, the three failures found, the repairs, validation and open items.
- [HANDOFF_2026-10-07.md](HANDOFF_2026-10-07.md): archived earlier handoff; contains superseded statements. Current code and topical docs take precedence.

Commit work regularly at coherent boundaries. The desktop runtime decision above supersedes the old full-Proton migration plan.

## Fonts/runtimes repair branch (2026-10-17)

`codex/fonts-runtimes` rebuilds the useful parts of Gemini's proposal from
reviewed master. See [FONTS_RUNTIMES.md](FONTS_RUNTIMES.md) for exact evidence,
operational constraints and remaining merge gates. `wex-graphics vulkan
on|off|status` is per-user and applies at next login; it never rewrites the
shared DLLs. Windows Components authenticates through pkexec and requires the
selected vendor's actual terms; missing terms disable installation. Do not
confuse passing source-build smoke tests with fresh-ISO acceptance.
