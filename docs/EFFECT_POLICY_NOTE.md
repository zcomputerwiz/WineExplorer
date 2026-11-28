<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Effect policy: what is left of review item 2 (design note, 2026-11-28)

Answers `GRAPHICS_ARCHITECTURE_REVIEW.md` item 2 ("Resolve effect policy before backend dispatch") for the Orchestrator's
decision. No code has been written for this note. **Conclusion: no protocol bump is needed (no window v13).** The shell
already resolves the policy and the wire format already carries everything the review asks for. What is left is three
small checks and one parity item.

## What the review asks for, and where it stands

| Review asks | State on the topic branches (WE `claude/material-colours` 576b2d41, wexcomp `claude/material-defaults` 30a04931) |
| --- | --- |
| The shell effect request carries resolved material, tint, shadow and region | **Already so.** `WEX_WINDOW_EFFECT` (`wexhost.h`: `kind`, `tint`, `tinted`, `radius`, `region`, `has_region`, `dark`, `shadow`, `materials`) is built in one place, `pending_effect()` in `dwm_bridge.c`, and handed to `WexWmApplyEffects`. Each backend maps it: X11 writes the `_WEX_DWM_BACKDROP` property, Wayland sends `wex_window_manager_v1.set_effects` (since 13: `backdrop`, `flags` shadow/dark/tint/region, `tint`, region). |
| Material defaults decided before dispatch | **Done in 576b2d41.** `wex_material_default_tint( kind, dark, &tint )` is applied in `pending_effect()` when the app gives no tint, and the result is sent as an explicit tint (`tinted = TRUE`). Mica 0xcc202020 / 0x80f3f3f3, Acrylic 0xb3202020 / 0xb3f3f3f3, Blur and Gradient none. X11 on-screen result: all rows within one level of the table (`docs/WAYLAND_DEFAULT.md`, 2026-11-28). |
| Explicit zero-alpha tint distinct from no tint | **Already representable.** The `tint` flag on the wire means "use the tint argument as given", so flag set with alpha 0 is an explicit transparent tint, and flag clear is "the compositor's own default". The shell now always sets the flag for Mica and Acrylic, so a compositor default is no longer reached from the shell. Both compositors honour a flagged tint as given (wexcomp `compute_backdrop_color`; the X11 painter's Acrylic and, since 576b2d41, Mica). **Not yet checked on screen:** a flagged tint with alpha 0 (an app's ACCENT_POLICY colour with alpha 0). |
| Thumbnails and Peek: one semantic record, marshalled inside each adapter | **Already so at the shell boundary.** The shell passes `WEX_WINDOW_THUMB` (HWNDs, rectangles in Wine pixels) to `WexWmApplyThumbnails`, and `WexWmPeekWindow( target, picture )` takes HWNDs; no HWND crosses Unixlib. Inside wexhost the X11 path builds `struct compose_thumb` (window ids) and the Wayland path the packed 104-byte `WEX_WM_THUMB` (shared handles) after resolving identity. The pointer-versus-packed split the review saw is the adapters' own marshalling, which is where it should end. |
| Keep blending, clipping, sampling and native object access in the backend | Unchanged. |
| Preserve window generation and scale checks, composition-disabled behaviour, WoW64 layout | Unchanged; nothing in this note touches them. |

## What is left

1. **Compositor fallback defaults.** `wex-window.c` `compute_backdrop_color` (wexcomp) and `compose.c` (X11) still have
   their own default tints for a client that sets effects without the tint flag. The shell no longer reaches them, but
   another privileged client could. Rather than delete them, keep them and add a check that they equal the shared table:
   X11 by the existing `x11-material-parity.sh` run with the flag cleared, wexcomp by the same probe with
   `wlr`-side screenshots. This is a test, not a protocol change. **Needs your decision:** keep the fallbacks (my
   proposal) or remove them and make the tint flag mandatory for materials (a protocol clarification in the XML text, no
   version change).
2. **Explicit zero-alpha tint, end to end.** Add an Acrylic and a Mica row to `dwm-material-parity-probe.c` whose tint is
   set with alpha 0 (ACCENT_POLICY), and require the screen to equal the blur alone on X11 and on Wayland. If either
   backend draws a default instead, that is a bug of its own (most likely the Wayland client-side `scene_node_invisible`
   path, which was patched for blur rects in 30a04931).
3. **Mica Alt has no table row.** It shows Mica's tint on X11 (printed in the parity run). On Windows it is a different
   material (`DWM_MEASUREMENTS.md`: tabbed is darker than Mica in light mode and nearly black in dark mode). A parity item
   for the table, with its own Windows measurement; unrelated to the protocol.
4. **Wayland on-screen parity** of the same six windows (`wayland-class-shadow`-style headless run with `grim`): the topic
   reports headless coverage of the Mica per output case only. The probe and script are written for X11 and need a
   Wayland variant; the expected values are the same table.

## Why not v13

A bump would add a record the wire already has: `set_effects` carries material, flags, tint and region, and its
`tint` flag already separates "explicit" from "default". The remaining work is verification, and a protocol bump would also
have to be carried by the Wine copy of the XML (a v11 subset; review item 4), which is the larger problem. Item 4 (a script
that diffs Wine's subset against the shell's XML) should come first if the protocol is touched at all.

## Order

Items 1 to 4 are independent and small. Proposed as one batch after the scaling batch: probe rows (2), Wayland variant of
the parity script (4), fallback check (1). Item 3 waits for a Windows measurement.
