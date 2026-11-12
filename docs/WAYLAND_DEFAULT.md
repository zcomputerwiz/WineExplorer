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
- **Protocol versions:** `wex_window_manager_v1` is v14, `wex_window_v1` v12, `wex_foreign_window_v1` v10.
  wexhost binds the manager at up to v14. Wine's `winewayland.drv` carries an older copy of the XML (v11) and binds
  at most v11.
- **Only the GDM-free path exists.** GDM appears only in wexhost's switch-user fallback and in
  `scripts/setup-default-session.sh`, which disables it. The greeter is always an X greeter
  (`wineexplorer-greeter` in `/usr/share/xgreeters`, `scripts/wex-greeter.sh`).

## The checklist, checked

| Item | Code says |
|---|---|
| Shell hotkeys (Win+L, Win+R, Alt+Tab) | Partly. wexcomp binds no keys (`scripts/wayland/rc.xml` turns labwc's defaults off). It moves focus to the Wine frame on Super or Alt+Tab only when the focused native window is hosted in one, and there Explorer's `RegisterHotKey` and keyboard hook see the key. A native window that is not hosted gets nothing: the private protocol has no key event, and `winewayland.drv` has no `RegisterHotKey`. No GlobalShortcuts portal. |
| Session lock | Partly. wexcomp implements ext-session-lock-v1, but nothing in WineExplorer uses it. Win+L calls LightDM's `Seat.Lock`, which switches to the X greeter. Wine's `LockWorkStation` is a stub. |
| Greeter | An X greeter before both sessions. No Wayland greeter. |
| Output management (Display Properties) | **Done 2026-11-11** for one output: wexhost uses wlr-output-management, which wexcomp implements (`src/dlls/wexhost/wloutput.c`). Before, it used only XRandR, which in the Wayland session had no `DISPLAY` and failed. The next Wayland session restores the saved mode. More than one output is still refused, as on X11. Display Properties' own dialogs on Wayland are not yet tested. |
| Multi-monitor and scale | Partly. Scale changes are handled per window for hosted native windows, and the taskbar's reserved edge is per output. The v14 frame clock and the frame-scheduling fallback use only the first output, and the Mica blur is one node over the whole layout. |
| Automatic adoption of native windows | The session already turns it on: the script exports `WEX_WAYLAND_NATIVE_WINDOWS=1`. Without it, Explorer reports "not managing Linux windows" (the AUTO backend is not attached). |
| CS_DROPSHADOW (D3) | Absent on Wayland. It exists only for X11 (`winex11.drv` and Explorer's DWM hook, which never starts on Wayland). wexcomp's shadow comes from the DWM frame attribute, not from the class style. |
| Physical hardware | Not run (VMs and headless only). |

## Found on the way

- **Privileged protocols are open to every client.** wexcomp restricts layer-shell, ext-session-lock and
  wlr-output-management only for sandboxed clients, so any ordinary client on `wayland-0` can bind them. Only
  `wex_window_manager_v1` is limited to the private socket. Most wlroots compositors do the same, but WineExplorer's
  design says privileged operations go through the private socket. Restricting these needs a decision on what may
  still use them (wexhost's output management connects from Explorer's process through the private socket).
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
2. Decide the greeter. Recommended: keep the X greeter, which already starts both sessions, and test Switch User
   and Log Off from a Wayland session through it.
3. Hotkeys for windows not hosted in a Wine frame. This needs a key event, or a compositor-side binding that
   forwards to Explorer over the private protocol.
4. Lock: logonui on ext-session-lock, or keep LightDM's `Seat.Lock` and test it from Wayland.
5. Restrict the privileged protocols to the private socket (decision above).
6. The 150% scaling items, multi-output frame clock and Mica, CS_DROPSHADOW.
7. Physical Intel, AMD and Nvidia runs.
8. Then flip the default:
   - `user-session=wine-explorer-wayland`, and OOBE saves Wayland;
   - logonui preselects the account's own session;
   - Safe Mode keeps X11 as its default.
   - X11 fallback when wexcomp cannot start: the Wayland session saves X11 as the account's choice and ends, so the
     next logon is X11. This is not built yet.
