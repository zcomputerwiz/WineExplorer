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
