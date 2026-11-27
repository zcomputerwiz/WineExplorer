<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Wayland as the default session: status

Checked on 2026-11-11 against the code (file:line evidence from a code survey), not only the docs. The owner wants
Wayland to become the default session and X11 to stay as the fallback. This file lists what stands in the way, and
what was done towards it on 2026-11-11.

## Where things are

- **The default is X11, in three places.** LightDM's `user-session=wine-explorer`
  (`installer/live-build/config/includes.chroot/etc/lightdm/lightdm.conf.d/50-wineexplorer.conf`). OOBE saves
  `wine-explorer` for every new account (`src/programs/oobe/oobe.c`, `WexUserSetSession`). logonui preselects X11 in
  its session list.
- **The Wayland session:** `wine-explorer-wayland.desktop` in `/usr/share/wayland-sessions`, with the script
  `scripts/wine-explorer-wayland-session.sh`. The compositor is `wexcomp`, a labwc fork built against wlroots 0.18
  (`>=0.18.1, <0.19.0`, `wexcomp/meson.build`).
- **Protocol versions:** `wex_window_manager_v1` is v15 (v14 until the shell keys, 2026-11-11), `wex_window_v1` v12,
  `wex_foreign_window_v1` v10. wexhost binds the manager at up to v15. Wine's `winewayland.drv` carries an older copy
  of the XML (v11) and binds at most v11.
- **Phases in the docs:** L0 done, L1 done 2026-10-14, L2 accepted 2026-10-17. `docs/WAYLAND_PLAN.md` still calls L3
  (effects) not started, but the code has blur, shadows and corners in wexcomp, so L3 is partly done. L4 has not
  started. `docs/ROADMAP.md` has no entry for Wayland as the default session.
- **Only the GDM-free path exists.** GDM appears only in wexhost's switch-user fallback and in
  `scripts/setup-default-session.sh`, which disables it. The greeter is always an X greeter
  (`wineexplorer-greeter` in `/usr/share/xgreeters`, `scripts/wex-greeter.sh`).

## The checklist, checked

| Item | Code says |
|---|---|
| Shell hotkeys (Win+L, Win+R, Alt+Tab) | Was partly done: focus moved to the Wine frame on Super or Alt+Tab only for a hosted native window; an unhosted one got nothing, and Ctrl+Esc and Alt+F4 never reached Explorer. **2026-11-11:** shared in wexhost, with an X11 and a Wayland adapter (above). No GlobalShortcuts portal for applications' own shortcuts yet. |
| Session lock | Win+L calls LightDM's `Seat.Lock` in both sessions, which shows the Welcome screen (above). **2026-11-11:** works from a Wayland session with a native program on the keyboard; logging on again returns to the same compositor (VM test `waylandshellkeys`, both firmwares). wexcomp's ext-session-lock is not used. Wine's `LockWorkStation` is a stub. |
| Greeter | An X greeter before both sessions. No Wayland greeter. **Decided 2026-11-12 (owner):** the X greeter stays; Log Off, Win+L and the Welcome screen's session list are tested from Wayland through it (`waylandlogon`, `waylandxlock`, `waylandshellkeys`). |
| Output management (Display Properties) | **Done 2026-11-11** for one output: wexhost uses wlr-output-management, which wexcomp implements (`src/dlls/wexhost/wloutput.c`). Before, it used only XRandR, which in the Wayland session had no `DISPLAY` and failed. The next Wayland session restores the saved mode. More than one output is still refused, as on X11. Display Properties' own dialogs on Wayland are not yet tested. |
| Multi-monitor and scale | Partly. Scale changes are handled per window for hosted native windows, and the taskbar's reserved edge is per output. The v14 frame clock and the frame-scheduling fallback use only the first output, and the Mica blur is one node over the whole layout. |
| Automatic adoption of native windows | The session already turns it on: the script exports `WEX_WAYLAND_NATIVE_WINDOWS=1`. Without it, Explorer reports "not managing Linux windows" (the AUTO backend is not attached). |
| CS_DROPSHADOW (D3) | Absent on Wayland. It exists only for X11 (`winex11.drv` and Explorer's DWM hook, which never starts on Wayland). wexcomp's shadow comes from the DWM frame attribute, not from the class style. |
| Physical hardware | Not run (VMs and headless only). |

## Shared design, thin adapters

Where X11 and Wayland differ only in how something reaches the session, the design is shared and each session has
a thin adapter. The window manager interface in wexhost (`wm.c` with `x11wm.c` and `waylandwm.c`) already works this
way.

- **Shell hotkeys** (2026-11-11):
  - **Shared:** `src/dlls/wexhost/shellkeys.c` holds the shell's combinations (the Windows key with anything,
    Ctrl+Esc, Alt+Tab, Alt+Shift+Tab, Alt+F4) and the replay. The replay presses the held modifiers in Wine and
    forwards every key until the combination ends; Alt+Tab holds the keyboard until Alt is released. Each key
    becomes a `WEX_WM_KEY` event, which Explorer replays into Wine with `SendInput` as before. Its keyboard hook, its
    hotkeys (Win+R, Win+E, Win+L) and DefWindowProc (Alt+F4) then handle it as for Windows programs.
  - **X11 adapter:** `x11wm.c` builds its passive key grabs from that table, and passes its XKeyEvents to the
    replay. Holding the keyboard is `XGrabKeyboard`.
  - **Wayland adapter:** `waylandwm.c` gives wexcomp the same table (`wex_window_manager_v1` v15, `set_shell_keys`).
    The compositor then behaves as an X11 passive grab. While a client that is not on the private socket has the
    keyboard, and the seat is not locked, a listed combination goes to Explorer as `shell_key` events, and so does
    every key until that key is released. `hold_keyboard` and `release_keyboard` extend this through Alt+Tab. Wine's
    own windows keep their keys. A compositor whose shell sets no table keeps the older behaviour: on Super or
    Alt+Tab, focus moves from a hosted native window to its Wine frame.
- **Session lock:** Win+L already takes one path in both sessions. Explorer asks the display manager to lock the
  seat (LightDM's `Seat.Lock` over D-Bus, through the session's `XDG_SEAT_PATH`), which shows the Welcome screen, as
  XP does with fast user switching. Nothing in it is specific to X11. On Wayland, Win+L only had to reach Explorer
  from a native window, which the shell keys do. ext-session-lock is therefore not needed. Since 2026-11-12 only the
  private socket's clients can bind it (below).

## Found on the way

- **Privileged protocols were open to every client** (found 2026-11-11, fixed 2026-11-12 after the owner's decision
  to keep them for the private socket). wexcomp restricted layer-shell, ext-session-lock and wlr-output-management
  only for sandboxed clients, so any ordinary client on `wayland-0` could bind them; only `wex_window_manager_v1` was
  limited to the private socket. WineExplorer's design says privileged operations go through the private socket.
  - Since wexcomp `1f8e5d3a` (`protocol_is_shell_only` in `src/server.c`), the protocols that make a client part of
    the shell are offered only on the private socket `wex-shell-0`: layer-shell (panels, desktop layers),
    ext-session-lock, wlr-output-management, wlr-output-power, wlr-gamma-control, wlr-foreign-toplevel-management
    and both workspace protocols.
  - Explorer and every Windows program it starts use that socket (`WAYLAND_DISPLAY=wex-shell-0`). Native programs
    get `wayland-0` (`src/dlls/wexhost/apps.c`) and no longer see these protocols, and neither do XWayland and
    sandboxed clients.
  - Nothing in the image used them on `wayland-0`. wexhost's output management (`wloutput.c`) and window list
    (`waylandwm.c`) connect from Explorer through the private socket. Wine's clipboard uses wlr-data-control, which
    stays open.
  - Also open: screen capture (grim, screen sharing through a portal), the read-only window list, idle state, the
    clipboard, input methods and virtual input. XP lets any program read the screen and the windows. Input methods
    and clipboard managers are native programs.
  - The test tools that set the output mode now use the private socket: `wlr-randr` in `check-wayland-polish.py`,
    `check-wayland-dwm.sh` and wexcomp's own test runners.
  - The private socket is the user's own, like `wayland-0` (mode 0700 runtime directory): a native program of the
    same account could still connect to it on purpose. A sandbox that is given only `wayland-0` cannot.
  - wexcomp's suite checks both sockets (`stack-client --scenario check-sockets`). wexcomp also now compares the whole
    socket name when it decides whether a client is on the private socket, not a part of the path.
- **xterm under XWayland closed by itself in the Wayland session** (found and fixed 2026-11-11). An xterm started in
  the session exited with status 0 within a second of appearing, while other X programs (`xev`) stayed open. An
  strace in the VM showed xterm itself sending SIGHUP to its shell. The cause:
  - wexcomp sent `_NET_WM_PING` to every new X window (`handle_new_surface` in `src/xwayland.c`, inherited from
    labwc). wlroots 0.18 sends it without checking the window's `WM_PROTOCOLS`, and xterm lists only
    `WM_DELETE_WINDOW` there.
  - xterm binds every `WM_PROTOCOLS` client message to its `DeleteWindow()` action, without looking at which protocol
    it is (xterm 398, `charproc.c` and `main.c`), and so hung up its shell.
  - The ping arrives while xterm starts. When xterm handled it before forking its shell, the hangup had nothing to
    signal, which is why earlier Wayland sessions could run xterm (`docs/WAYLAND_SESSION.md`).

  Upstream labwc removed the ping on 2025-12-01 (`d20d5675`, "don't send _NET_WM_PING to new windows"): nothing
  handled its timeout. wexcomp's branch now carries that commit (`034d722f`). The VM test's `waylandshellkeys` phase
  checks that xterm is still open after 10 seconds and takes the shell keys and Win+L.
- **X terminals one column short in a UTF-8 locale** (found and fixed 2026-11-12). In UTF-8 mode xterm with a scroll
  bar and a bitmap font asks for its size again after mapping, while the size hints it set first still give a base
  one pixel smaller (without the scroll bar's border). wexcomp, as labwc, snapped a client's configure request to the
  size hints (`view_adjust_size` in `handle_request_configure`), so an 80-column xterm became 79. Since wexcomp
  `1f8e5d3a`, a size an X client asks for itself is taken as an X server without a window manager takes it
  (`view_adjust_requested_size`: only the minimum applies); interactive resizes still snap to the increments.
  - Headless check (wexbuild), the recovery console's xterm options at 640x480: 79 columns in C.UTF-8 before, 80
    after; 80 in C either way. wexcomp's suite now checks it (`tests/wex/run.sh`, needs xterm and xfonts-base).
  - The recovery console keeps its scroll bar without a border (`XTerm*vt100.scrollBarBorder: 0`, added before the
    fix); with the fix it is no longer needed for the width.
- **Logging off from Wayland could log the account straight back on** (found and fixed 2026-11-12). Once in eleven
  log offs from the Wayland session on UEFI (the VM test's `waylandlogon`), the Welcome screen did not come: the
  automatic logon's desktop came back. Log Off ends the session through logind (`TerminateSession`, wexhost), which
  stops every process in it at once. The cause:
  - wlroots writes the X display's lock file (`/tmp/.X0-lock`) with the compositor's pid and gives the display's
    listening sockets to XWayland, and removes the lock as the compositor ends, without waiting for XWayland.
  - LightDM, starting the greeter's X server, takes a display as free when its lock is gone or names a dead process.
    When the compositor had ended before XWayland, LightDM chose `:0`, which XWayland still held. The X server could
    not listen ("Cannot establish any listening sockets"), LightDM stopped the seat ("greeter display server failed
    to start") and exited with 1, and systemd's restart of `lightdm.service` ran the automatic logon again. Without an
    automatic logon the Welcome screen came back after the restart.

  wexcomp now ends XWayland (`SIGTERM` and `SIGCONT`, at most a second) before the lock goes (`0d524d63`, branch
  `claude/safe-mode-output-mode`). The VM test's `waylandxlock` phase makes the race certain: XWayland held stopped,
  the compositor alone ended. With the earlier wexcomp that run gave the failure above (the X server's error and
  LightDM's exit in its logs); with this one LightDM keeps running and shows the Welcome screen.
- **The socket variable.** The session script exported `LABWC_WEX_SHELL_SOCKET`, which nothing reads. wexcomp reads
  `WEX_SHELL_SOCKET`. Both meant the default, `wex-shell-0`, so it worked. Fixed on 2026-11-11: the script sets
  `WEX_SHELL_SOCKET` and uses it.
- **Automatic logon forced X11.** `autologin-session=wine-explorer` started X11 at every automatic logon, even for an
  account that had chosen Wayland. Removed on 2026-11-11 (the VM test's `waylandauto` phase).
- **Safe Mode changed the account's choice.** LightDM saves the session of every greeter logon, so a Safe Mode logon
  through another session, or through the GTK fallback (X11), replaced the account's choice. Since 2026-11-11 a
  snapshot before LightDM starts and a restore at the start of the Safe Mode session keep it (`docs/SAFE_MODE.md`
  section 3).

## Order of work towards the switch

1. Done on 2026-11-11: the account's choice is kept through Safe Mode and used by automatic logon; Display
   Properties changes the real output on Wayland.
2. Decided 2026-11-12 (owner): keep the X greeter, which already starts both sessions. Log Off, Win+L and the
   Welcome screen's logon into Wayland are tested through it.
3. Done on 2026-11-11: the shell's keys reach Explorer from any native window, hosted or not (shared
   `shellkeys.c`, `wex_window_manager_v1` v15), including X programs through XWayland. Applications' own global
   shortcuts (a GlobalShortcuts portal) are not covered.
4. Done on 2026-11-11: the lock stays on LightDM's `Seat.Lock`, tested from Wayland. Since 2026-11-12 the Welcome
   screen's session list follows the account clicked (below), so it no longer shows X11 for a Wayland account.
5. Done on 2026-11-12: the shell-only protocols are offered on the private socket only (wexcomp `1f8e5d3a`, above).
6. The 150% scaling items, multi-output frame clock and Mica, CS_DROPSHADOW.
   - **Restored live scaling at 150%: reproduced headless on 2026-11-26** (`scripts/wexbuild-root/wayland-scale-cycle.sh`:
     headless wexcomp with server-side decoration, `explorer /shell` with `WEX_WAYLAND_NATIVE_WINDOWS=1`, a real restored
     `foot` hosted in a Wine frame, 100% -> 150% -> 100% by `wlr-randr --scale`, the window found in a `grim` screenshot by
     its background colour; needs the `foot` package). It fails in every run, with a different wrong state each time, all
     steady for 12 s (not transient). Foot at 100%: (520,313)-(759,481). Seen after a scale change: at 150% the window
     at (780,470), 1.5 times its place (the compositor kept the logical position; stays 395x245), or at (521,314) (the
     physical place kept); and back at 100%: (780,470) (not restored), (347,209)-(490,312) (moved and shrunk by 1/1.5),
     or correctly (520,313). Of 6 traced runs (3 cycles each) 4 failed somewhere and runs differ, so it is a race.
     **What the traces show (temporary logging in `native_event_to_wine`, the shell's scale-message handler and the
     compositor's `view_adjust_for_layout_change`, not kept):**
     - The two outcomes at 150% are two behaviours. *Logical place kept*: the compositor leaves the native view at its
       logical position (520,313), so it appears at physical (780,470). *Physical place kept*: within 7 ms of the scale
       change, before the shell's scale message is handled, the native view and the Wine frame are already at logical
       (347,209) (= physical (520,313) / 1.5) and the native size is 160x101 logical. The compositor's trace shows the
       view's pending geometry already changed when `view_adjust_for_layout_change` runs (that function skips its stock
       clamping for hosted views, as designed), so the mover is the Wine side's coordinated layout update (the
       driver's `WM_WAYLAND_WEX_UPDATE` after the scale event), not the compositor.
     - Failures happen on the way back to 100%: the native view stays at logical (347,209), 144x104, and nothing moves
       it back, so the Wine frame then follows the native rectangle to physical (347,209) once the shell's two-second
       `geometry_pending` guard expires. In passing cycles the same coordinated update moves it to (520,313) again. So
       the open question is why the driver's update at the return to 100% sometimes does not reach the compositor.
     - A trial (not shipped: the shell's scale-message handler also calling `WexWmConfigure` with the frame's physical
       geometry) did not fix it: 16 of 30 checks still failed over 5 runs. The earlier trials (observation guard, active
       frame authority) had the same result, so the shell side is probably not where the update is lost.
     - **Driver trace** (temporary logging in `wayland_wex_update_hosting` and `wex_configured`, not kept): at every scale
       change the driver sends one `configure_foreign` for the new logical client rectangle (for foot at 150%: 160x113
       logical, serial N) and the group stays pending (`wex_group_pending`) while the Wine window changes size
       several times; every later `update_hosting` returns early on that flag. The compositor answers the request about
       two seconds later (its transaction timeout) with group status 2 (the hosted group is dropped), and the driver
       re-adopts it at once (status 1, new serial). This happens at every scale change **in passing runs too** (11 to 16
       status-2 answers per 3-cycle run in all four runs), so the timeout alone does not separate pass from fail. What
       differs is which geometry wins at the re-adoption: the Wine frame's physical placement (pass) or the native
       client's logical rectangle from the other scale (fail: the frame is moved to physical (347,209) and resized to
       about 144x104 once the shell's two-second `geometry_pending` guard has expired).
     - Reading: the transaction needs the native client to commit exactly the requested size, and foot (a cell grid with
       its own size hints) does not, so the transaction always ends in the timeout and a detach/re-adopt, and the result
       depends on a race in the re-adoption. This matches the lead in `docs/wayland-polish/item3-notes.md` (floating
       clients may choose another size; `reconfigure_commit` requires the exact one). A fix is a design change, not a
       one-line one: either the transaction accepts the size the native client chose within its hints (and the frame
       follows it), or the re-adoption takes Wine's physical placement as authoritative and converts it to the new
       scale. Neither is done; each must pass `wayland-scale-cycle.sh` ten times in a row before it ships.
     - **Results of the attempts (2026-11-26, 3 cycles = 6 checks per run):** baseline about half of the checks fail
       (18 of 36 over six traced runs). Driver retry on `not_ready` (3 retries): 11 of 30 fail, no better. **Accepting the
       committed size in `reconfigure_commit`** (wexcomp `claude/scale-accept-committed-size` 5140bae7: the native
       client's committed size becomes the client rectangle, the frame follows, at most twice): 11 of 48 fail over 8
       runs, 4 of 8 runs pass completely; the compositor suite stays 63/63. That removes the transaction timeouts but
       not the item. Also following a native client's *own* later resize (keeping the group instead of dropping it in
       `native_committed`) was worse: 19 of 36 fail, with new modes (window at the bottom right edge), because the frame
       and the terminal chase each other's snapped sizes.
     - **What remains (traced):** after the transaction completes, foot re-snaps by itself (160x101 -> 144x104, or
       126x104 on the next cycle), `native_committed` sees a size that differs from the group's client rectangle and drops
       the group; the driver's re-adoption request carries Wine's physical rectangle, which no longer matches, so it is
       answered `not_ready` twice; then the Wine frame follows the native window (still at the other scale's logical
       position) and the group re-forms there. The native window's *position* was never restored. What is needed is a
       re-adoption that moves the native view to the frame's client origin (Wine's placement authoritative) before the
       commit, and then takes whatever size the client has, in one step: an adopt operation for a group that has no scene
       (today `configure_foreign` without a scene only tries `commit_foreign`, which requires matching dimensions and does
       not move anything). Not done.
     - **Where the failures come from, and two trials (2026-11-27, `wayland-scale-cycle.sh`, 3 cycles = 6 checks per run):**
       trace (compositor logging, not kept) of the hosted `foot`: at every scale change `foot` commits a size of its own
       (cell grid, fractional scale), `native_committed` drops the group, Wine's frame follows the new native size, and the
       group re-forms; the sizes drift by a cell or two per cycle, harmless. The position failures are a flip between two
       models: after the compositor keeps the frame's *physical* place (logical 344,189 at 150%), the frame is moved to
       logical 517,293 (the old logical place, which is physical 776,440), 1.5 times the original. Trial 1 (wexcomp
       `claude/scale-drop-keeps-place` b00f4f06: a dropped group leaves the native window where it was shown, not at its
       pre-group coordinates): baseline 10 of 18 checks fail over 3 runs, with it 11 of 36 over 6 runs (2 of 6 runs fully
       clean). Trial 2 (not shipped: the shell's scale-message handler also treats a frame with the native host property
       as owning its physical placement when the group is not active, so it does not apply the stale native snapshot at
       the new scale): worse, 18 of 36 over 6 runs, with a 16 px vertical drift (the client offset differs between the
       scales). So trial 1 halves the failures and is kept as a branch; trial 2 is not. Still not shippable.
     - **First attempt at that adopt operation (not shipped):** on `configure_foreign` for a group dropped before
       (a `group_dropped` flag on the frame), compose the pair at the native client's current size, which puts the native
       view at the frame's client origin, then run the layout transaction to the requested size. It was worse than the
       accept-committed-size change alone: 24 of 48 checks fail over 8 runs (against 11 of 48), and many runs end at 100%
       with the native window at the 150% position and size (780,470)-(1175,716).
     - **Trace of those adopt runs (compositor positions and scales at scene creation, view moves and transactions):**
       the box is not computed at a stale scale; the two directions use different coordinate models and the factor 1.5
       compounds. (1) At 100% -> 150% the group is already dropped when the first `configure_foreign` arrives (`dropped 1`,
       no scene), so the native client never receives a resize request for the new scale. The frame keeps its logical
       position (517,293), which is physical (776,440): the compositor's logical position is mirrored back into Wine's
       physical placement ("logical place kept"). In some runs the native client also keeps its logical size (267x165),
       which is 400x247 physical, and the group composes at that size, so the Wine frame grows by 1.5 to follow it.
       (2) At 150% -> 100% Wine's physical placement (776,440 and 401x247) is kept ("physical place kept"), which at scale
       1 is logical (776,440), 1.5 times the original; the transaction then asks the native client for 401x247 and the
       window ends at the 150% rectangle. So one direction keeps the logical place and the other the physical one, and
       what Wine remembers is whatever the first direction left. A single owner of placement across a scale change is
       needed: either Wine's physical rectangle is saved at the scale message and restored (position and size, the native
       size following within its own constraints) in both directions, or the logical one is. Neither exists; the
       earlier observation-guard and active-frame trials changed only the shell side of this.
   - **The rule that replaces the attempts above (decided 2026-11-26): one owner of placement across an output scale
     change, the shell.** Windows' own model for a Per-Monitor-V2 window is the template: the window keeps its top left and
     its device-independent size, so its physical size follows the scale ratio (the suggested rectangle of
     `WM_DPICHANGED`). The shell (`wex_scale_changed` in `wexshell/linux_windows.c`, triggered by a hosted frame's or the
     taskbar's scale message, once per change) converts every normal top-level window's size by the ratio around its top
     left, in both directions, hosted frames and plain Wine windows alike. The compositor and the driver follow what the
     shell sets; nobody else writes placement during a scale change. Minimized, maximized and fullscreen windows keep their
     own policy. The session starts at 100% and the driver posts no message for the initial scale, so the shell assumes
     120 (120ths) at the start. In the compositor, `view_adjust_for_layout_change` no longer applies stock labwc placement
     (moving floating views in proportion to the layout, a second writer) to Wine windows or the native views their frames
     reserve (wexcomp `claude/scale-owner-follow`, on top of the accept-committed-size change).
   - **Result of that rule (headless, `wayland-scale-cycle.sh`, 3 cycles = 6 checks per run per window):** the plain Wine
     window (`WITH_FOOT=0`) passes ten runs in a row, 0 of 60 checks failing: top left kept at both scales, size 1.5 times
     at 150% and the original at 100%; the compositor suite stays 63/63. Hosted foot is **not** fixed: about 19 of 48
     checks still fail (the group is dropped when the terminal re-snaps by itself, and nothing re-forms it at the frame's
     position; the re-adoption attempt, wexcomp `claude/scale-adopt-wip`, did not help: 23 of 48). Hosting of native
     windows is opt-in (`WEX_WAYLAND_NATIVE_WINDOWS=1`); the default Wayland session does not use it. Open: the hosted
     case, and A's interactive-resize case with the stall time measured.
   - **Hosted case fixed (2026-11-27), from a traced diagnosis.** (The two trials of 2026-11-27 above ran against the
     wexbuild tree's shell, an older copy without `wex_scale_changed`; their numbers do not describe this rule.) The
     compositor (every geometry snapshot it sends, every frame move) and the shell (every conversion of a native event,
     every reconcile, every scale message) logged on one realtime clock, merged per run. What it showed, in the ~40 ms
     after `wlr-randr` changes the scale, before the hosted frame's own scale message reaches the shell:
     1. the driver sends the group's layout request at once, computed from the unconverted Win32 rectangle at the new
        scale (240 physical -> 160 logical), and `foot` answers it;
     2. `foot` also commits at the new scale with a size of its own, and the compositor drops the group (`native_committed`,
        and `frame_committed` because Wine's first frame buffer at the new scale still has the old physical size); the
        dropped native keeps the other scale's logical place, and the re-adoption cannot move it;
     3. the shell, its hold not yet set (only the frame's own scale message set it, and cleared it when the group was not
        active), follows each transient native rectangle as the placement.
     So three writers, and which answer arrived last decided the result: the "race". Fixes, one per writer:
     - shell (`wexshell/linux_windows.c`): the conversion and the hold start at the first sign of the new scale (an
       observation that carries it, or any scale message), for every hosted frame whatever its group state; the hold
       ends only on an answer near the converted size, not on a stale answer at the right top left; the conversion scales
       the client area and keeps Wine's frame metrics physical, and starts from the window's intended device-independent
       size (a property), so a client that snaps to whole text rows does not lose a row per change;
     - driver (Wine `claude/wayland-scale-driver-hold`, `wayland_wex_update_hosting`): no layout request for a scale
       change alone; it follows the shell's converted rectangle (maximized and fullscreen keep the driver's conversion);
     - compositor (wexcomp `claude/scale-hosted-keep`): a hosted group is kept, not dropped, while the output scale differs
       from the scene's, and the scale-change transaction takes the frame's margins from the last frame size that matched
       the client box, not from a transitional buffer (that gave negative margins and a transaction that always timed out).
     After each fix the runs became more alike; with all of them every run is identical to the pixel. Result: ten runs in a
     row, hosted foot and the plain window, 60 of 60 checks each (top left kept, size 1.5 times at 150% and back at 100%,
     no drift over three cycles).
   - **Interactive resize of a hosted window with accept-committed-size ("A"), stall measured (2026-11-27):**
     `scripts/wexbuild-root/wayland-hosted-resize.sh` (headless wexcomp 1120df36, the shell and driver above, a real
     foot hosted in a Wine frame; a virtual pointer, `scripts/wex-vpointer.c`, drags the frame's bottom-right corner; the
     stall is the time from wexhost's "accepted" to "complete" for each resize transaction). With "show window contents
     while dragging" on: ten runs of 8 steps 400 ms apart, every transaction completes, median stall 16 ms (one frame),
     the slowest single step 77 ms; ten runs of 16 steps 50 ms apart, every transaction completes, stalls 14-31 ms. Every
     run: the top left stays, the frame grows with the drag, and at the end foot fills the frame's client area at its own
     cell-grid size. Outline drag ("show window contents while dragging" off): each step supersedes the unpublished one
     and the last completes at the release, as on Windows. **Note:** that setting is off in a fresh WineExplorer prefix
     (Wine's default; nothing sets `HKCU\Control Panel\Desktop\DragFullWindows`), while XP has it on, so every window
     resizes as an outline today; a parity item for the defaults, not for this compositor work.
7. Physical Intel, AMD and Nvidia runs.
8. Then flip the default:
   - `user-session=wine-explorer-wayland`, and OOBE saves Wayland;
   - logonui preselects the account's own session. **Done 2026-11-12:** `WexUsersList` gives each account's saved
     session (AccountsService's `XSession`), and clicking a tile sets the list to it, as LightDM's own greeters do.
     Before, the list showed X11 for everyone, so a Wayland account logging on from the Welcome screen got X11, and
     LightDM saved X11 as its choice. An account with none saved gets the first entry, X11, until the default flips.
   - Safe Mode keeps X11 as its default.
   - X11 fallback when wexcomp cannot start. **Done 2026-11-12:** when the compositor exits or never opens the shell's
     socket, the Wayland session runs `wex-session-choice --fallback wine-explorer` (as the account: AccountsService
     and `~/.dmrc`) and ends, so the next logon is X11. In Safe Mode the fallback saves nothing, as Safe Mode logons
     never change the choice.
