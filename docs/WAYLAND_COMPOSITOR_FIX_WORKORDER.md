# Fix work order (Gemini): wexcomp focus and state policy

Status: done, see docs/WAYLAND_COMPOSITOR.md. Follow-up to `docs/WAYLAND_COMPOSITOR_WORKORDER.md`; the review that found this is
`docs/WAYLAND_COMPOSITOR.md` §8. Same environment and boundaries as the original work order (WSL distribution
`wexcomp`, clone `/root/wexcomp`, branch `wexcomp` of `zcomputerwiz/labwc`; in the WineExplorer repo only your report
and the status lines of the two work orders; never the Proton VM, never `git add -A`, never force-push).

## The rule to implement

A **wex window** is any view that has a `wex_window_v1` (Wine's windows, any layer). For wex windows:

1. **The compositor never changes keyboard focus or stacking by itself.** Focus and stacking change only through
   `wex_window_v1.activate()` and `place_above/place_below/place_bottom/set_layer`. Pointer events (enter, motion,
   buttons, axis) are still delivered to the surface under the pointer as usual — Wine decides from them whether to
   activate (Win32 has windows that must not take focus: `WS_EX_NOACTIVATE`, `MA_NOACTIVATE`, tooltips, the
   taskbar's popups).
2. **labwc never changes their state by itself**: no minimize, maximize, fullscreen, move, resize, snap, move to
   edge/output/workspace, and no labwc window menu — whether from keybindings, mousebindings, SSD buttons, the
   window switcher, the root menu or snapping. Requests made **by the client itself** (`xdg_toplevel.set_maximized`,
   `set_minimized`, `set_fullscreen`, `move`, `resize`, and xx-zones `set_position`) keep working — that is how Wine
   implements Win32 state changes.
3. Windows **without** `wex_window_v1` (Linux apps, XWayland, layer-shell clients) keep labwc's normal behavior.

## Where (labwc 0.8.3 + your commits)

Focus/raise paths that must skip wex windows (do not focus, do not raise):
- clicks: `src/input/cursor.c` — the `desktop_focus_view_or_surface()` calls around lines 605, 654 and 1058 (button
  press, window-switcher preview, empty-area click) and the focus-follows-mouse path around line 649;
- map: `src/view-impl-common.c:31` (`desktop_focus_view(view, true)` on map) — a newly mapped wex window waits for
  `activate()`;
- unmap/close: `src/view-impl-common.c:67` and `desktop_focus_topmost_view()` (`src/desktop.c:133`), also reached from
  `src/seat.c:756`, `src/workspaces.c:386`, `src/session-lock.c:288-290`, `src/action.c:1158`: when picking the next
  view to focus, skip wex windows (focus a non-wex view or nothing; Wine activates the next Win32 window itself);
- `src/xdg.c:939` (`desktop_focus_view(view, true)` — check what triggers it; if it is labwc acting on its own, skip
  wex windows; if it is the client's own xdg-activation request, keep it);
- `desktop_focus_view()` itself (`src/desktop.c:44`) is the natural single guard for everything above **except**
  the call made from `wex_window_v1.activate()` — e.g. split it into an internal function used by `activate()` and a
  public one that returns early for wex windows. Grep every caller after the change.

State changes that must not apply to wex windows (`src/action.c`, `run_action` / `action_is_valid` and friends):
`ICONIFY`, `TOGGLE_MAXIMIZE`, `MAXIMIZE`, `UNMAXIMIZE`, `TOGGLE_FULLSCREEN`, `MOVE`, `RESIZE`, `MOVETO`,
`MOVE_RELATIVE`, `RESIZETO`, `RESIZE_RELATIVE`, `MOVE_TO_EDGE`, `SNAP_TO_EDGE`, `GROW/SHRINK_TO_EDGE`,
`MOVE_TO_OUTPUT`, `SEND_TO_DESKTOP`, `TOGGLE_ALWAYS_ON_TOP/BOTTOM`, `TOGGLE_DECORATIONS`, `SHOW_MENU` (window
menu), `FOCUS`/`RAISE`/`LOWER`, `NEXT_WINDOW`/`PREVIOUS_WINDOW` selecting a wex window. `CLOSE` may stay (it sends
`xdg_toplevel.close`, which Wine handles as `WM_CLOSE`). Also: interactive move/resize started by labwc (mousebind
or edge drag), edge snapping and region snapping, and SSD (wex windows must have no SSD — Wine draws its frames).
Prefer one helper (`view_is_wex(view)`) and one early check per entry point over scattered layer comparisons.

## Tests (add to `tests/wex/`, run headless in `run.sh`)

Use `zwlr_virtual_pointer_v1` / `zwp_virtual_keyboard_v1` (labwc offers both) from a test client to generate input.
1. Two normal wex windows A (below) and B (above), B activated. Click A: A receives `wl_pointer.button`; **no**
   `wl_keyboard.enter` for A; pixels show B still above A. Then `activate(A)`: A gets keyboard enter and is raised.
2. A new wex window maps: no keyboard enter until `activate()`.
3. The focused wex window closes: labwc does not focus another wex window (no keyboard enter to it).
4. With a wex window activated, send labwc's default bindings for maximize (`W-a`), snap (`A-Left` etc.) and the
   window menu (`A-Space`): geometry unchanged, no menu (screenshot compare).
5. The window switcher (`A-Tab`) does not select or raise a wex window.
6. Client-initiated requests still work: the wex window calls `xdg_toplevel.set_maximized` → maximized to the usable
   area; `set_minimized` → hidden; `activate()` restores it (existing test 14).
7. A non-wex client (foot, or a plain test client without `wex_window_v1`): click-to-focus and the bindings above still
   behave as stock labwc.
8. Existing 16 tests still pass.

## Also fix (small)

- Correct `docs/WAYLAND_COMPOSITOR.md` §3.2 ("Passive Focus Policy") and §7 to match the implementation and the
  review: every Wine process connects only to `wex-shell-0`.
- Zone handles: use an opaque random token per zone instead of the output name, and make `get_zone_from_handle` with
  an unknown handle fail (send the zone `size` as invalid / `handle` empty per the protocol) instead of falling back
  to the default output. Adjust the join-by-handle test to pass the token.
- Alpha: also re-apply the alpha multiplier when a subsurface commits (or on the alpha-modifier state change), not
  only on the toplevel's commit; add a test with a subsurface at 50%.

## Acceptance

All tests above pass headless (`tests/wex/run.sh`), commits pushed to `wexcomp`, report updated with a new results
table and commit list (append; do not rewrite history), and the status line of
`docs/WAYLAND_COMPOSITOR_WORKORDER.md` and this file set to done. Rules as before: one change per commit, GPL-2.0-only
headers on labwc files, no C escapes through heredocs/sed/`python -c`.
