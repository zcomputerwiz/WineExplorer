<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# L2 implementation and acceptance

Status: in progress; no acceptance claimed.

Scope and agreed decisions: WAYLAND_L2_WORKORDER.md. Project source is on master;
Wine and compositor work use codex/wayland-l2, initially f0108dc and 9a47d74f.
Host compositor worktree: build/wayland-l2/wexcomp. VM compositor checkout remains
~/wexcomp-build, used only for isolated nested tests. No default/session deployment.

## Planning inspection corrections

- rg rejected docs/DWM* positional glob and src/programs/wexhost, which is absent.
  Use the docs directory with -g; host services are src/dlls/wexhost.
- VM rg is unavailable; use host Git snapshots or grep on the VM. Initial reads
  assumed src/wex and src/foreign.c; actual compositor code is src/protocols and
  src/foreign-toplevel, decorations are src/decorations.
- The raw GitLab wlr protocol URL was unavailable through the web tool. Existing
  installed wlroots headers, labwc source and protocol documentation establish
  the available interface and public resource lists.
