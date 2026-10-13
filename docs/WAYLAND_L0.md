# Wayland L0 spike: results

2026-10-12, Proton VM. labwc 0.8.3 (Debian 13) nested in `Xvfb :9` (`WLR_BACKENDS=x11 WLR_RENDERER=pixman`), our Wine
11.19 build (`~/open`, master) forced onto `winewayland.drv` (`HKCU\Software\Wine\Drivers` `Graphics`=`wayland`,
`DISPLAY` unset), prefix `~/l0-prefix`. Harness: `scripts/wayland-spike-env.sh`. Plan: `docs/WAYLAND_PLAN.md`.
Screenshots: `docs/wayland-l0/`.

## Summary

Everything **renders and takes input** under Wayland — Win32 frames, menus, layered windows, our desktop, taskbar,
tray. What fails is exactly what the plan predicted, and nothing else showed up: **Wine cannot place or stack its
top-level windows**, so every window lands where the compositor puts it (centered), topmost is not honored, and the
"virtual desktop" does not contain its windows. A positioning/stacking protocol is the one hard requirement; the
other gaps are covered by protocols labwc already has or are small.

## Results

| Check | Result | Screenshot |
|---|---|---|
| Notepad, winecfg as plain windows | **Pass**: draw correctly with Wine's own (client-side) frames; labwc gives Wine no server-side decoration | `s1-winecfg.png` |
| Window placement | **Fail**: labwc centers every top-level; requested positions are ignored (the probe asks for 6 different positions, all land centered on top of each other) | `s1b-probe.png` |
| Menus (owned popups) | **Pass**: File menu opens at the right place (subsurface of its owner), by mouse and by keyboard | `s1b-menu-key.png` |
| Keyboard | **Pass** once a window has focus (Alt+F, access keys) | `s1b-menu-key.png` |
| Per-pixel alpha (alpha ramp), color-key hole | **Pass** | `s1b-probe.png` |
| Uniform layered alpha (50% blue) | **Fail**: drawn opaque; labwc lacks `wp_alpha_modifier_v1`, which the driver uses for it | `s1b-probe.png` |
| `CS_DROPSHADOW` shadow | **None** (expected: D2 shadows live in our X11 compositor) | `s1b-probe.png` |
| `explorer /desktop=Shell,1024x768 /shell` | **Partial**: desktop with icons, taskbar, tray and clock appear — but the desktop is one Wayland window and the windows "inside" it (the taskbar included) are separate Wayland windows placed by labwc: taskbar centered mid-screen, probe windows centered over the desktop | `s2a-vdesk.png`, `s2c-probe.png` |
| Desktop at output size (`/desktop=Shell,1280x800`) | **Fail**: `NtUserChangeDisplaySettings` returns -2, explorer cannot set the size; labwc stretches the 1024x768 desktop (blurry) | `s2c-startmenu.png` |
| Stacking / topmost | **Fail**: clicking the desktop raises it over the taskbar | `s2c-startmenu.png` |
| `explorer /shell` without a virtual desktop | **Fail**: `/shell` is taken as a folder path (`Failed to create PIDL for Z:\shell`); no shell | — |
| Linux Wayland app (foot) and X11 app via XWayland (xmessage) next to Wine | **Pass**: labwc starts XWayland on demand (rootless `:1`), decorates both, stacks and focuses all three kinds of window correctly | `s4-mixed.png` |

Logged driver errors: only `xdg_toplevel_icon_manager_v1` missing (no window icons in the compositor's own UI).

## What labwc 0.8.3 offers (45 globals)

Has everything the plan's standard-protocol part needs: `zwlr_layer_shell_v1` 4, `zwlr_foreign_toplevel_manager_v1` 3,
`ext_foreign_toplevel_list_v1`, `ext_session_lock_manager_v1`, `zwlr_screencopy_manager_v1` 3 (no
`ext_image_copy_capture` yet), `zwlr_output_manager_v1` 4, `xdg_activation_v1`, `zxdg_decoration_manager_v1`,
`wp_security_context_manager_v1` (lets a compositor tell trusted clients apart — the hook for granting `wex-window-v1`
only to our Wine), `ext_workspace_manager_v1`, virtual pointer/keyboard (test automation), text-input/input-method.

Missing that Wine's driver would use: `wp_alpha_modifier_v1` (uniform layered alpha), `wp_pointer_warp_v1`
(`SetCursorPos`), `xdg_toplevel_icon_v1` (window icons).

## What `wex-window-v1` must contain (from these results)

1. **Absolute position** of a top-level surface in output (or desktop) coordinates, applied on map and on every
   move — covers `SetWindowPos`, default placement, owned dialogs, the taskbar.
2. **Stacking**: place above/below a given surface, and a topmost band (taskbar, `WS_EX_TOPMOST`), so z-order follows
   the Win32 z-order.
3. **Activation** without the xdg-activation token dance for windows Wine activates itself (`SetForegroundWindow`
   rules stay Wine's).
4. **Desktop size = output size**: either the driver stops pretending to change modes in a non-virtual desktop, or the
   compositor accepts the desktop surface's size; and explorer's `/shell` must work without `/desktop=` (today it
   parses `/shell` as a path when no virtual desktop is given).
5. Not needed in the protocol: menus/tooltips already work as subsurfaces; Linux app windows already work through the
   compositor (frames for them are L2 work via foreign-toplevel).

Plus, outside the private protocol: implement `wp_alpha_modifier_v1` and `wp_pointer_warp_v1` in our compositor
(both standard; wlroots has the former), and use `ext_session_lock`, layer-shell and screencopy as planned.

## Estimate for L1

The rendering, input and mixed-client parts work today, so L1 is smaller than feared: a wlroots compositor from
tinywl/labwc with `wex-window-v1` (positions, stacking, topmost band, activation), the alpha-modifier and
pointer-warp globals, and the Wine driver commit that uses `wex-window-v1` when present (the driver already tracks
each window's Win32 rect; today it only cannot send it). Explorer: make `/shell` work without a virtual desktop.
Rough size: 2–4 weeks for a usable L1, most of it the compositor; the Wine driver part is a few hundred lines.
