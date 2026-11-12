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
