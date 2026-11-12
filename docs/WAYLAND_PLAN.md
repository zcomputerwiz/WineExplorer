# Wayland: plan and research

Status (2026-11-11): L0 and L1 are done (`docs/WAYLAND_L0.md`, `docs/WAYLAND_L1.md`) and L2 is accepted on fresh VM
installs (`docs/WAYLAND_L2.md`, `docs/WAYLAND_SESSION.md`). An opt-in Wayland session (`wexcomp`) exists. The default
and production stack stays **LightDM → Xorg → Wine virtual desktop → Explorer** (X11 window manager, D1/D2
compositor). **Wayland is not the default** because L4 is open (hotkeys, session lock, Wayland greeter, output
management/multi-monitor), `CS_DROPSHADOW` is a known gap, restored live scaling at 150% is open, AUTO adoption of
Wayland-native windows is disabled, and no physical-hardware run exists (VMs and headless only). Revisit when those
close. The sections below are the original design and research; the phase table has current status.

## Wine's Wayland driver today (`dlls/winewayland.drv`, Wine 11.19, ~8.1k lines)

Has: OpenGL/Vulkan, color management (Vulkan color space merged upstream 2026-10), fractional scaling, viewporter,
alpha-modifier (uniform layered alpha), clipboard (`wl_data_device`, `wlr-data-control`), keyboard via xkb,
text-input-v3 (IME), cursor-shape, pointer constraints/relative pointer/pointer-warp (games), xdg-output,
xdg-toplevel-icon.

Missing for a shell:
- **No window positioning.** Win32 top-level windows are plain `xdg_toplevel`s; xdg-shell does not let a client
  place its windows, so `SetWindowPos`, topmost, the taskbar, work areas and owned-window placement cannot work.
- **Two surface roles only**: toplevel and subsurface (owned popups such as menus and tooltips become subsurfaces).
- **No virtual desktop mode** (no such code in the driver), so today's design (everything inside one Wine desktop
  window) does not carry over.
- **No privileged operations**: reading other windows (`GetDC(NULL)` capture), global hotkeys (`RegisterHotKey`,
  Win+L), focus stealing (`SetForegroundWindow`), seeing other clients' windows (taskbar).
- Keyboard FIXMEs (layout-change `WM_INPUTLANGCHANGEREQUEST`, modifier sync, some scan codes).

## How Linux shells do it

| Model | Who | Taskbar / panel | Hotkeys | Lock, capture |
|---|---|---|---|---|
| Shell **is** the compositor | GNOME (gnome-shell = Mutter + JS UI) | inside the compositor | inside the compositor | inside |
| Compositor + **privileged shell process** with private protocols | KDE (KWin + plasmashell), COSMIC | plasmashell via KDE-private protocols KWin only grants to trusted clients | KGlobalAccel over D-Bus, fed by KWin | KWin |
| Compositor + panels on **shared protocols** | wlroots family: sway, labwc, Hyprland (waybar etc.) | **wlr-layer-shell** (edge-anchored surfaces with exclusive zones = work area) + **foreign-toplevel-management** (list/activate/minimize/close) | compositor config; apps via the **GlobalShortcuts portal** | **ext-session-lock**; **screencopy / ext-image-copy-capture** or the screencast portal |

On all of them Wine apps run through XWayland by default; the Wayland driver is opt-in.

**Our fit is the KDE model**: our own compositor (KWin's role) plus Explorer as the trusted shell process
(plasmashell's role). GNOME's model is impossible (our shell is a Win32 program in Wine).

## Design

1. **Standard protocols for everything a Linux shell also needs** — implemented by the compositor (labwc already has
   them) and used by Wine's Wayland driver for our shell windows:
   - taskbar and desktop windows → wlr-layer-shell (exclusive zone gives the work area);
   - taskbar button list → foreign-toplevel-management (or ext-foreign-toplevel-list + a control protocol);
   - Win+L / lock → ext-session-lock (logonui as the lock surface, or LightDM's greeter);
   - D2 capture / `GetDC(NULL)` → ext-image-copy-capture;
   - display settings (desk.cpl) → wlr-output-management;
   - shell hotkeys → compositor bindings forwarded to Explorer, apps via the GlobalShortcuts portal.
   The driver support for these roles could plausibly go upstream.
2. **One small private protocol** (`wex-window-v1`) for the only Wine-specific need — Win32 programs place and stack
   their own windows: absolute position, z-order (incl. topmost), activation, owned-window placement. Advertised
   only to Wine clients the compositor trusts. Lives in our Wine fork branch (see `REFACTOR_WORKORDER.md`); propose
   upstream only if a standard protocol with that scope appears.
3. **Linux apps**: Wayland apps directly, X11 apps (Steam) through the compositor's XWayland. Explorer frames them by
   placing its Win32 frame windows around their surfaces (compositor stacks the app surface inside the frame);
   wexhost's X11 window manager becomes a Wayland client of foreign-toplevel + `wex-window-v1`.
4. **Effects**: D1/D2 (shadows, AnimateWindow fades, alpha) move into the compositor, where they are native.

### Compositor base

- **wlroots** (MIT): scene graph, XWayland, layer-shell, foreign-toplevel, session-lock, screen capture, output
  management, pixman renderer (works in our VMs without 3D). Its `tinywl` example (~1k lines) is the skeleton.
- **labwc** (GPL-2.0-only; compatible with the GPL-2.0-or-later whole): lightweight stacking compositor on wlroots,
  Openbox-like — the main donor for window-management behavior, XWayland handling and protocol plumbing.
- **Weston** (MIT): parts only (shell plugins, color code). WSLg (Weston + RDP RAIL) is not useful to us: it remotes
  windows across a VM boundary we do not have, copies frames, and its RDP layer (FreeRDP) is Apache-2.0.
- **Cost**: wlroots breaks its API every release — pin a version, budget updates.

### Borrow before writing (prior art, by need)

| Need | Take from | License | How |
|---|---|---|---|
| The compositor itself | **labwc** | GPL-2.0-only | **Fork it** rather than grow tinywl: stacking WM, XWayland, layer-shell, foreign-toplevel, session-lock, screencopy, output management, keybindings, menus are done; rebasing on labwc releases also absorbs wlroots API churn. The compositor is its own program, so it simply stays GPL-2.0-only. |
| Win32 window **positions** | **xx-zones-v1** (wayland-protocols, experimental, merged Feb 2026; KWin has a plugin, `kwin-zones`) | MIT (protocol) | Standard per-client coordinate space with `set_position` and position feedback. Implement it in our compositor and in Wine's Wayland driver — the driver part is then **upstreamable**, not a fork-only commit. |
| Stacking, topmost band, roles (desktop, taskbar) | KDE **plasma-shell** / plasma-window-management protocols (plasma-wayland-protocols) | LGPL-2.1+ (XML) | Design reference for the part xx-zones lacks (it has no stacking); our `wex-window-v1` shrinks to stacking + topmost + roles. |
| D2 effects: shadows, rounded corners, blur, fades | **scenefx** (drop-in `wlr_scene` replacement used by SwayFX); **Wayfire** plugins (animate, fade) | MIT | Shadows/fades without writing a renderer. |
| Taskbar client (wexhost side) | **waybar** / **sfwbar** taskbar modules (wlr-foreign-toplevel client) | MIT / GPL-3.0 (sfwbar: reference only) | Client code for listing/activating/minimizing windows. |
| Tray for Linux apps (Steam) — also useful on X11 now | **waybar** tray (StatusNotifierItem host), **snixembed** (SNI ↔ XEmbed bridge) | MIT / ISC | SNI host; snixembed lets old XEmbed-only apps show up too. |
| Lock screen | **swaylock** (`ext-session-lock` client) | MIT | Pattern for logonui as the lock surface. |
| Greeter on Wayland | **cage** (kiosk compositor) running the Wine logonui; **greetd** as an alternative to LightDM | MIT / GPL-3.0 (greetd: as a separate program only) | Logon screen without the full shell. |
| Display settings backend (desk.cpl) | **wlr-randr**, **kanshi** (`zwlr_output_manager` clients) | MIT | Mode/position/scale apply-and-test logic. |
| Screen capture for `GetDC(NULL)` / D2 capture | **grim** (screencopy client) | MIT | Capture path for the driver hook. |
| Global shortcuts for apps | **xdg-desktop-portal-wlr** / **-hyprland** GlobalShortcuts | MIT / BSD-3 | Portal side; shell hotkeys stay compositor keybindings forwarded to Explorer. |
| Test automation | wlroots **headless** backend + virtual pointer/keyboard (`wlrctl`, `ydotool`) | MIT / AGPL-3.0 (ydotool: tool only) | CI for the compositor without a display, like `scripts/vmtest.py` for X11. |

## Phases

| Phase | Work | Size |
|---|---|---|
| **L0 spike** | Measure the gaps (work order below) | days |
| L1 | Compositor skeleton (tinywl/labwc) + layer-shell roles in Wine's driver + `wex-window-v1` + the Wine driver commit for it | Completed 2026-10-14; opt-in session, acceptance in WAYLAND_L1.md |
| L2 | Linux apps: foreign toplevels, Explorer frames, XWayland | Accepted 2026-10-17 on VMs (WAYLAND_L2.md); polish items open |
| L3 | D1/D2 effects and capture in the compositor | Partly: blur/shadows/corners in wexcomp (WAYLAND_BLUR.md); `CS_DROPSHADOW` open |
| L4 | Hotkeys, session lock, Wayland greeter (LightDM), output management, multi-monitor | Not started (greeter may stay X11) |

## L0 work order (spike; no product code)

Goal: replace guesses above with measurements. Environment: the Proton VM (or a QEMU VM via `scripts/vmtest.py`),
Debian 13 packages `labwc` and `foot`, our Wine 11.19 build with the Wayland driver enabled
(`HKCU\Software\Wine\Drivers` `Graphics`=`wayland`, or `DISPLAY` unset with `WAYLAND_DISPLAY` set).

1. Start labwc (nested in X with `WLR_BACKENDS=x11`, or on a VT). Run `wine notepad`, `winecfg`, a dialog with
   owned windows, a program with menus and tooltips: note placement, stacking, focus, popups, keyboard layout.
2. Run `wine explorer /shell` with the Wayland driver: record what the shell does (it expects a virtual desktop):
   taskbar position, desktop window, Start menu popup, tray, work area, Win+L, Alt+Tab.
3. Check what labwc offers: `wayland-info` (layer-shell, foreign-toplevel, session-lock, image-copy-capture,
   output-management versions).
4. Run a Linux X11 app (XWayland) and a Wayland app (`foot`) next to Wine windows: stacking and focus between them.
5. Write `docs/WAYLAND_L0.md`: each item pass/fail with screenshots, which standard protocol would cover each fail,
   and what `wex-window-v1` must contain. Estimate L1 from that.
