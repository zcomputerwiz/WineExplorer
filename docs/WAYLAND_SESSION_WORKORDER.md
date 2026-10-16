<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work order (Codex): a usable Wayland session, plus tray icons

Status: ready, 2026-10-16. Assignee: Codex. Supersedes the open-ended remainder of `WAYLAND_L2_WORKORDER.md`.

## Goal

On a system installed from our ISO, a user picks **"WineExplorer (Wayland)"** at the logon screen and can use it for
a normal day. Wine programs (32- and 64-bit) and Linux programs share one taskbar, Alt+Tab, the Start menu and the tray.
They move, resize, maximize, minimize and close like XP windows. X11 stays the default session and keeps working.

This is one large task with **two review points** (end of part B and end of part D). Do not stop for review between
them. Commit and push verified steps to master as you go (build master at its pinned Wine before every push).

## Test budget: real programs, not hypothetical clients

L2's transport is reviewed and solid. From here on:
- **Acceptance is real programs** on an installed VM (`installer/tests/` and `scripts/vmtest.py`). Use these: Firefox,
  a GTK app (gedit or GNOME Text Editor), a Qt app (KWrite or qterminal), foot, an XWayland X11 app (xterm), the
  Steam client (XWayland), Wine notepad (32- and 64-bit), winecfg, our Control Panel, and a 32-bit Windows installer
  (any small freeware installer).
- **One check per behaviour per program class.** Write a protocol probe only to reproduce a bug a real program showed.
- **No new tests for synthetic clients or protocol corner cases** that no listed program exhibits. If you find such
  a case, write one line in `WAYLAND_L2.md` ("known, not pursued") and move on.
- **No new private protocol versions or generic Wine hooks** unless a listed program needs one. Say which program,
  and how, in the commit.
- Time-box any single investigation to about half a day. If it does not converge, record it and continue.

## A. Finish L2 for real programs (enable the controls)

- Enable sizing and maximize for hosted native windows in the Wayland session, with the work-area maximize and
  restore behaviour of `WAYLAND_L2_WORKORDER.md`. The optional forward no-op early end from the last review is part of
  this if foot or GTK hit the timeout in normal use.
- CSD clients (GTK with header bars, Firefox) keep their own decorations: one taskbar button, correct activation and
  minimize/restore. No Wine frame.
- Launching: Start menu entries for Linux programs (desktop files) start them in the session with the normal Wayland
  socket and XWayland display, stripped of shell Wine variables.
- 100% and 150% output scale on one monitor; two monitors at 100% (move a window across, maximize on each).

## B. Install the Wayland session

- Package the compositor (`wexcomp`, our labwc fork at its production commit) and its runtime dependencies into the
  ISO. Add a `wayland-sessions` entry "WineExplorer (Wayland)" next to the X11 one; X11 stays LightDM's default.
- Put the Wayland session start in its own script next to `scripts/wine-explorer-session.sh`. Gemini is changing
  that file for the prefix split (`docs/PREFIX_SPLIT_WORKORDER.md` S4), so share code through a small sourced helper
  rather than editing the same lines, and rebase often.
- The greeter can stay X11.
- Extend `installer/tests/install-interactive.sh` (or add a sibling test) to log Bob into the Wayland session and run
  the real-program checks above: open, type, move, resize (edge and corner), maximize/restore, minimize via the
  taskbar, Alt+Tab, close. Also run the existing 32/64-bit Wine checks there.

**Review point 1:** report in `WAYLAND_SESSION.md` with the real test output and screenshots of open programs; stop.

## C. Tray icons (`docs/TRAY_SNI_WORKORDER.md`)

Implement that work order as written: StatusNotifierItem through wexhost (D-Bus) and XEmbed for old X11 programs. It
must work in both sessions. Acceptance with real programs: the Steam client (close to tray, reopen from the icon,
its menu), one libayatana app (for example `nm-applet` or a small AppIndicator demo), and one XEmbed app (for example
`stalonetray`'s test client or `xembed`-based `volumeicon`).

## D. Regression and report

- X11 session on the same ISO: existing install tests, D1/D2 effects, tray, eight-dialog smoke.
- Wayland session: the part B test plus tray.
- Update `WAYLAND_L2.md` (close L2), `STOCK_CHANGES.md`, `CODEX_HANDOFF.md`.

**Review point 2:** stop and report.

## Boundaries

- Gemini works on the prefix split (`installer/scripts/wex-*.sh`, `scripts/setup-machine-root.sh`,
  `scripts/wine-explorer-session.sh`, `src/dlls/wexhost/unixlib.c` `user_create`, Wine `server/registry.c`,
  `dlls/setupapi`, `dlls/ntdll/unix/file.c`). Do not edit those except through the helper approach above. Pull master
  before each push.
- `CLAUDE.md` rules apply: WoW64 builds, stock-Wine changes listed in `STOCK_CHANGES.md`, no Microsoft material.
