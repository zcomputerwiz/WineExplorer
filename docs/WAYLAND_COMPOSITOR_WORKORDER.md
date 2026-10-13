# Work order (Gemini): Wayland compositor — labwc fork "wexcomp"

Status: ready, 2026-10-13. This is the **compositor half** of `docs/WAYLAND_L1_WORKORDER.md` (its tasks 1–3). The
Wine half (tasks 4–6: Wine's Wayland driver and Explorer) belongs to Codex after its refactor reaches P4 — do not
start it. Background: `docs/WAYLAND_PLAN.md`, `docs/WAYLAND_L0.md` (spike results, screenshots).

## Working alongside Codex on the same machine — boundaries

Codex is in the middle of a large refactor in `D:\DeepThought\Projects\WineExplorer` (master) and its worktrees, and
builds on the Proton VM under `/srv/protonexplorer/wex-refactor-p3b`, `~/open` and `~/inst`.

- **Your code lives in its own clone:** `D:\DeepThought\Projects\wexcomp` (clone of
  `https://github.com/zcomputerwiz/labwc`). Do not edit anything under `WineExplorer*`, `ProtonExplorer`,
  `WineExplorer-local` or `WineExplorer-old-git`.
- **In the WineExplorer repo you may only add/edit:** `docs/WAYLAND_COMPOSITOR.md` (your report) and, at the end,
  the status line of this file. Commit only those paths (`git add <path>`; never `git add -A`/`.`), pull before
  committing, never rebase or force-push master, never touch other branches or worktrees.
- **On the Proton VM** (`ssh -i D:/VirtualBox_VMs/vm_key -p 2223 wineuser@127.0.0.1`): work only in
  `~/wexcomp` (source/build) and `/tmp/wexcomp` (runtime). Do not touch `~/open`, `~/inst`, `/srv/protonexplorer`,
  `~/l0-prefix` or `/usr/local`; do not run `make install` anywhere; do not `pkill` broad patterns (Codex runs
  Xvfb/Wine there) — kill only PIDs you started. Use display `:11` (not `:7`, `:8`, `:9`). Run builds with `nice -n 10`.
- Installing Debian build packages on the Proton VM with `sudo -n apt-get install` is fine.
- Pushing branch `wexcomp` to `zcomputerwiz/labwc` is approved. Do not open PRs upstream.

## Rules (from CLAUDE.md)

- Commit after each working step, one change per commit, clear messages.
- License: the compositor is labwc's, **GPL-2.0-only**; keep labwc's headers on its files. New files: GPL-2.0-only
  header (same form labwc uses); a new protocol XML: MIT (the convention for Wayland protocols). No Apache-2.0 /
  GPL-3.0 code copied in.
- No Microsoft material anywhere.
- Never pass C escapes (`\\`, `\n`) through shell heredocs, `sed` or `python -c` — write files with an editor tool
  or a script file, and grep the touched lines afterwards.

## Base

- Branch `wexcomp` from upstream tag **`0.8.3`** (`8cf708ee77ceb4926b76c0bf110963be704857a6`; fetch tags from
  `labwc/labwc` — the fork's master equals upstream master). This matches Debian 13: **wlroots 0.18.2**
  (`libwlroots-0.18-dev` 0.18.2-3), which is what we build against. Keep the binary name `labwc` for now.
- Build deps (Debian 13): `meson ninja-build pkg-config libwlroots-0.18-dev wayland-protocols libwayland-dev
  libxkbcommon-dev libinput-dev libxml2-dev libpango1.0-dev libcairo2-dev libglib2.0-dev libpng-dev librsvg2-dev
  scdoc` (check `meson setup` output and add what it asks for).

## Tasks

### 1. xx-zones-v1 (standard, experimental; merged in wayland-protocols 2026-02)
- Copy `staging`/`experimental` `xx-zones-v1.xml` from wayland-protocols into the fork (record the
  wayland-protocols commit in the commit message); generate with wayland-scanner in meson.
- `xx_zone_manager_v1`: `get_zone(output?)`, `get_zone_from_handle(handle)`, `get_zone_item(xdg_toplevel)`.
- One zone per output, covering the output's layout box (later: minus exclusive zones); send `size`, `handle`,
  `done`. Zones created from the same handle are the same zone (every Wine process of a session will join one zone
  by handle).
- `xx_zone_v1.add_item/remove_item`; `item_entered`/`item_left`.
- `xx_zone_item_v1.set_position(x, y)`: place the toplevel at zone coordinates and **skip labwc's placement policy**
  for it; reply `position`; `position_failed` only if the window would be entirely outside the zone; send
  `frame_extents` (0 for client-side-decorated windows, labwc's SSD sizes otherwise).
- Keep positions across output changes the way labwc keeps window positions.

### 2. wex-window-v1 (private; stacking, layers, activation)
Write `protocols/wex-window-v1.xml` (MIT). Interfaces (names may be refined, document them):
- `wex_window_manager_v1.get_window(xdg_toplevel) → wex_window_v1`
- `wex_window_v1`:
  - `place_above(wex_window_v1 sibling)`, `place_below(sibling)`, `place_bottom()` — relative stacking among windows
    of the same layer;
  - `set_layer(uint layer)` with enum `normal`, `topmost` (above normal windows), `desktop` (below everything, not
    in window cycling), `shell` (taskbar: above topmost, not in window cycling);
  - `set_exclusive_edge(uint edge, uint size)` for `shell` windows: reserve that edge like layer-shell's exclusive
    zone, so maximized windows and the zone's usable area shrink;
  - `activate()` — give keyboard focus and raise within its layer, no xdg-activation token needed;
  - events: `stacking_changed` is not needed; keep it request-only for v1.
- **Trust:** expose this global only on a second, privileged socket: the compositor opens `wex-shell-0` next to
  `wayland-0` in `$XDG_RUNTIME_DIR` (or the name given by an environment variable / labwc config option), and binds
  `wex_window_manager_v1` only for clients that connected through it. Document the mechanism.
- Interaction with labwc: windows of these layers ignore labwc's raise-on-focus where it would break Win32 z-order;
  `desktop` and `shell` windows get no SSD, are skipped by Alt+Tab/window switcher and by labwc's own placement.

### 3. Standard globals missing in L0
- `wp_alpha_modifier_v1`: wlroots 0.18 has `wlr_alpha_modifier_v1`; wire it to the scene so a surface's uniform alpha
  multiplies its buffer (Win32 `LWA_ALPHA`).
- `wp_pointer_warp_v1`: not in wlroots 0.18; small protocol (`warp_pointer(surface, pointer, x, y, serial)`); accept
  it for the focused surface, move the cursor, emit pointer motion. Vendor its XML from wayland-protocols.
- Optional: `xdg_toplevel_icon_v1` (labwc may already have it in later versions; skip if it means a big backport).

### 4. Test clients and automation (no Wine needed)
- Add `tests/wex/` (or `contrib/wex-tests/`) with small C clients (wayland-scanner generated, linked against
  libwayland-client, shm buffers):
  - `zones-client`: creates N toplevels at given positions through xx-zones, prints `position` events;
  - `stack-client`: creates windows in layers `desktop`, `normal`, `topmost`, `shell` (bottom edge, exclusive 30 px),
    reorders with `place_above/below`, activates one;
  - `alpha-client`: one window at 50% `alpha_modifier`;
  - `warp-client`: warps the pointer, prints the motion it receives.
- A script `tests/wex/run.sh`: starts the built compositor headless (`WLR_BACKENDS=headless WLR_RENDERER=pixman
  WLR_HEADLESS_OUTPUTS=1`, output 1280x800) or nested in `Xvfb :11` (`WLR_BACKENDS=x11`), runs each client, takes a
  screenshot with `grim` (screencopy; `sudo apt-get install grim`), and checks pixels/positions with a small Python
  script (exact window rectangles and colors). Exit status non-zero on failure.

## Acceptance

Write `docs/WAYLAND_COMPOSITOR.md` in the WineExplorer repo (report: what was built, commit list, how to run, results
table, screenshots in `docs/wayland-compositor/` — small PNGs only):
- `zones-client`: every window at its requested zone position (exact pixels); a second process joining by handle
  shares the zone; `position_failed` for an off-zone request; labwc's centering not applied.
- `stack-client`: desktop below everything; shell window at the bottom edge above topmost and normal; topmost above
  normal; `place_above/below` reorder as asked; clicking a normal window does not raise it above topmost/shell;
  maximizing a normal window stops at the shell window's exclusive edge; `desktop`/`shell` absent from Alt+Tab.
- `wex_window_manager_v1` is not available on `wayland-0`, only on the privileged socket.
- `alpha-client` renders at 50%; `warp-client` receives motion at the warped position.
- No regressions: stock clients (foot, an XWayland app such as xmessage) still behave as in L0 (`docs/WAYLAND_L0.md`,
  `s4-mixed.png`); `scripts/wayland-spike-env.sh` run against your binary still shows Wine notepad and menus working.
- `tests/wex/run.sh` passes headless.
- Branch `wexcomp` pushed to `zcomputerwiz/labwc`; the report lists every commit.

Then update this file's status line ("done, see docs/WAYLAND_COMPOSITOR.md") and leave the Wine-side tasks to Codex.
