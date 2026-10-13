<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Wayland L1 Wine integration

Status: in progress, 2026-10-13. Acceptance is not yet claimed.
Scope: WAYLAND_INTEGRATION_WORKORDER.md; shipping X11 stays unchanged.

## Environment

ProtonExplorer-VM (SSH 2223) only. Compositor source/build is
~/wexcomp-build, branch wexcomp at ad962566bd169c3c27fafdaac9780a599fb7624a.
Debian wlroots 0.18.2 development packages installed; meson/ninja build passes.
No dev runtime or ISO defaults are changed. Gemini's WSL wexcomp/wexbuild
distributions are untouched. Wine edits use the separate host fork worktree
build/wayland/wine, branch codex/wayland-integration from production d0a0af0681.

## Integration finding

wex-window-v1 v1's sibling arguments are connection-local objects. Sharing the
privileged socket does not permit a Wine process to reference another process's
Wayland window object. Relative cross-process stacking needs a protocol extension
with shared window handles; the user has been asked which team should implement
that compositor extension. Zone integration can proceed independently.

## Command failures and corrections

- Reading docs/WAYLAND_L1.md failed because it did not exist yet; this report
  creates the required acceptance record.
- Initial pkg-config check failed for wlroots-0.18; install libwlroots-0.18-dev
  and compositor dependencies. Meson/ninja then pass.
- git worktree add --no-checkout plus sparse-checkout set did not populate the
  source files yet, so reads of the new window.c/wayland_surface.c paths failed.
  Explicit checkout codex/wayland-integration populated them.
- Reading src/dlls/wexshell/taskbar.c failed; the taskbar implementation is
  systray.c. Searches subsequently use the module directory.
- First compositor acceptance run failed with ModuleNotFoundError: PIL in
  tests/wex/verify.py. Install Debian python3-pil and rerun. The runner stopped
  its disposable headless compositor normally; no shipping session was touched.
- The next compositor run reached the shell-layer pixel check but raised
  IndexError: the output was still at the backend's default size because
  wlr-randr was missing and the runner suppresses that command's failure.
  Installed wlr-randr; the complete existing suite now passes 25/25.
- Initial Wine driver compile rejected Unix swprintf and L-string arguments:
  host wchar_t is 32-bit, Wine WCHAR is 16-bit. Use ASCII formatting and UTF-16
  u literals. Linking then rejected RtlOpenCurrentUser, which is not exported
  by Unix ntdll; use native token/SID lookup and NtOpenKey instead. Driver builds.
- Reading src/dlls/wexshell/plugin.c failed; exports are in desktop.c.
- The first placement capture still showed centered windows. Move config
  initialization before toplevel creation, send the requested zone position
  before its initial commit, and ignore the initial add-item position event.
  Placement acceptance remains pending the corrected capture.
- The stationary two-process comparison then found the first process centered
  while the second placed correctly. NtCreateMutant returns the successful
  informational STATUS_OBJECT_NAME_EXISTS when another process holds the
  creation guard; the initial nonzero-status check wrongly disabled zones.
  Accept all nonnegative NTSTATUS values, including abandoned-wait recovery,
  and rerun. Minimized sentinel rectangles are excluded from placement requests.
- Corrected placement passes: six stationary windows exactly match X11 pixels;
  a second process shares the same opaque handle and lands at (740,50).
  Upstream zone branch aed8b9599c is pushed; retained integration commit
  853c5b02c7 is local on codex/wayland-integration. Production pin is unchanged.
- Private patch transfer failed because directly SCP'd driver files contained
  mixed CRLF/LF, so Git's LF patch context did not match. No partial patch was
  applied. Transfer the complete changed text inputs with LF-normalizing tar
  instead. The following shell build then stopped in makedep: wexwayland.h
  existed but was not listed in include/Makefile.in. Register the header;
  driver, wexshell and stock Explorer host then build successfully together.
- git sparse-checkout add --no-cone rejected the unknown option; add uses the
  existing mode. The following read/patch for include/Makefile.in failed since
  it had not been populated. sparse-checkout add include/Makefile.in works.
- First native /shell session exited with "Failed to create PIDL for Z:\\shell".
  The updated host executable/prefix hashes match, but wexshell's command
  dispatcher intercepts commands before host parsing and still treats /shell
  as a browser path. Exclude the /shell token just as /desktop is excluded.
  Its compositor and nested Xvfb stopped normally. A screenshot attempt after
  exit failed to create display, then SCP/view failed because no PNG existed.
- The candidate fork commit staged include/Makefile.in but Git refused the new
  include/wexwayland.h outside the sparse patterns. The shell/driver commit
  ce019d67c7 and host commit d7d46937a3 were followed immediately by explicit
  git add --sparse and header commit 996f87f33f. Candidate head is complete and
  pushed; production pin is unchanged. Never use the intermediate candidate
  without its header commit.
- Native /shell now starts successfully at 1024x768. The public-resource desktop
  and taskbar are visible at the bottom; candidate driver and wexshell loaded.
  Session /srv/protonexplorer/wex-wayland-l1/checks/sessions/session.4h50U8
  is running nested on Xvfb :20 for acceptance; shell/activation checks ongoing.
- Archive generation later failed on include/wexwayland.h after a sparse pattern
  update hid the newly tracked header. PowerShell continued with a partial tar;
  only the isolated lab source was affected. The generator now reads tracked
  sparse-hidden content from HEAD, and a complete tar/rebuild was repeated.
- The first action sequence failed its focus assertion: commands read state
  immediately and the new probe launched another Notepad while an earlier one
  remained. Reuse an existing Notepad, poll foreground state after real clicks,
  and wait only for the finite topmost client rather than the long-lived probe.
  Activation now passes, including WS_EX_NOACTIVATE after the two-line generic
  win32u click-style check. Windows behavior reference:
  https://devblogs.microsoft.com/oldnewthing/20240919-00/?p=110283
  That fix and X11 fallback validation are still candidate work.
- Failed reads of warp/maximized reports and SCP/view of topmost.png occurred
  after the failed assertion stopped their generation; no image acceptance was
  claimed. A small patch using the wrong ${wine_bin} text failed to match the
  fixture; corrected to its actual $wine_bin spelling.
- A later docs/DWM* and docs/COMPOSITION* rg call failed because PowerShell
  does not expand those file globs for rg. Use a directory with -g instead.
- Successful checks: work area (0,0)-(1024,738), taskbar (0,738)-(1024,768);
  physical clicks select red/blue, green NoActivate preserves red;
  topmost yellow overlays maximized Notepad but not the taskbar; pointer warp
  returns (950,600). Restore/map activation is now deferred until a buffer commit,
  with a Win32 foreground check to discard stale activation requests. Retesting.
- Taskbar-click minimize initially restored Notepad immediately. Trace shows the
  driver's preexisting heuristic treats any configure while minimized as a
  compositor restore. For private wex views, acknowledge such configures without
  SC_RESTORE: this compositor restores only on explicit activation. Actual taskbar
  clicks now pass minimize (IsIconic=1) and restore (IsIconic=0, foreground Notepad);
  keyboard Alt+F opens its File menu. Status TASKBAR_CHECK_PASSED.
- The first X11 comparison failed its NoActivate assertion, then the adjusted run
  failed the tray probe. The named /desktop=X11Check applies only to Explorer;
  terminal probes were joining a different default desktop. Set the prefix's
  Explorer Desktop/Desktops registry entries before launching every process;
  repeat the old/new X11 comparison and the original 800x600 smoke fixture.
