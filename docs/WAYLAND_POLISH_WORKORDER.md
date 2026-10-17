<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work order (Codex): Wayland session polish, the open items of WAYLAND_SESSION

Status: end review ready, 2026-10-17, with the restored-window portion of item 3 time-boxed and open.
Assignee: Codex. Report: `WAYLAND_SESSION.md` and `wayland-polish/final-regression.md`.
Follows `WAYLAND_SESSION_WORKORDER.md` (done).

## Goal

Close the open items of the accepted Wayland session work, so that a normal day in "WineExplorer (Wayland)" shows no
visible glitches with the programs already in the test set. Same test budget as before:
- acceptance is real programs on an installed VM;
- one check per behaviour;
- no synthetic clients or new protocol versions unless a listed program needs one;
- time-box each investigation to about half a day, then record it and move on.

One review point, at the end.

## Items

1. **Inset side edges of hosted native windows (your next item).**
   - volumeicon's Preferences shows inset left and right edges inside its Wine frame. Evidence:
     `build/wayland-session/final-wayland-tray/tray-volume-preferences.png`.
   - Find the cause: the hosted client rectangle against the frame's client area, margins, scale rounding or the
     GTK shadow/extents.
   - Fix it where it starts.
   - Check the same thing on the other SSD programs of the set (foot, QTerminal, xterm) at 100% and 150%, so the fix
     does not move them.
2. **Late repaint of QTerminal's Exit dialog at 150%.**
   - After a taskbar switch away and back, the Yes/No buttons can stay unpainted for several seconds. 100% passes.
   - Repro and the earlier trace: `WAYLAND_SESSION.md`, "Still open", `qt-transient150-controls.log`.
   - Likely suspects: damage or frame-callback handling for a transient child restacked through its own hosting root
     at a non-integer scale.
   - Acceptance: the existing bounded five-second check passes at 150%.
3. **Changing the output scale while an SSD window is maximized.**
   - Today that detaches the hosted group. Starting at 150% and then maximizing already works.
   - Make a live 100%→150%→100% change keep a maximized foot window maximized, hosted, focused and sized to the work
     area.
   - Also check a restored window and the taskbar.
4. **Steam close-to-tray, reopen and menu (signed in).**
   - The user does this manually; no credentials go through tests.
   - Write the short manual steps into `WAYLAND_SESSION.md`, and fix whatever the user reports.
   - Until then it stays "manual, not claimed".

## Acceptance (report in `WAYLAND_SESSION.md`, new section)

- Each item is fixed with real-program evidence in the session where it showed:
  - screenshots;
  - the action-runner output, raw logs committed.

  If an item is time-boxed out instead, say so with its notes.
- Regression, on a fresh ISO:
  - the Wayland variant of `installer/tests/install-interactive.sh` and the real-program checks from part B;
  - the tray checks in both sessions;
  - the X11 install test;
  - a 32-bit Windows program in both sessions.
- `STOCK_CHANGES.md` and `CODEX_HANDOFF.md` are updated; new Wine commits are on the fork, with the pin advanced
  only after both architectures build.

## Boundaries

- Jules has two small PRs open against master:
  - `src/dlls/wexhost/tray.c` (`named_icon`, IconThemePath check);
  - the unused `hInst` lines in `src/dlls/netshell/nettray.c` and `src/dlls/stobject/{volumetray,trayvol_dialog,powertray}.c`.

  Do not edit those lines until they are merged; pull master often.
- Gemini works on art only (`assets/`, the icon and cursor builders). Do not touch those files.
- Git:
  - push verified steps to master, building master at its pinned Wine before every push;
  - use `GIT_TERMINAL_PROMPT=0 GCM_INTERACTIVE=never` and timeouts;
  - never force-push.
- `CLAUDE.md` rules apply: WoW64 builds, GPL headers on new files, no C escapes through heredocs, `sed` or
  `python -c`.
