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

## Backend-dispatch checkpoint

The existing X11 manager now implements a backend table. New StartEx and
capability APIs preserve the legacy call indexes; BindHost moves the X11 parent
lookup out of Explorer's binding path. AUTO selects X11 for an X11 desktop and
reserves the no-X-desktop path for Wayland. Wayland still returns not supported
at this checkpoint; native Wayland integration is not yet implemented.

The WoW64 build exposed the missing wexhost WoW64 Unix-call export. Both tables
now use the same fixed-width bridge records, including ULONG64 pointer slots.
The 32-bit and 64-bit backend probes both passed with zero failures, including
invalid selection, capabilities, duplicate start, invalid host and repeated
start/stop. The X11 800x600 smoke passed: two tray icons, zero overlaps or
out-of-bounds windows, plus all eight native dialog placement cases.

Build: Proton VM, /srv/protonexplorer/wex-wayland-l1/wow64-source/build/wine64,
configured --enable-archs=i386,x86_64. Module log: checks/l2-dispatch-build.log.
Probe logs: checks/l2-backend{32,64}.log. Prefix: prefixes/smoke-l2. Reproduce
with scripts/check-wm-backend.sh, setting WEX_CHECK_PREFIX to an isolated prefix.
No installed runtime or real desktop changes.

Corrections during this checkpoint:

- The first multi-file patch was rejected for a mismatched wm_thread context;
  no files were changed by that attempt. Reapplied with the actual context.
- wm.c initially included winbase.h without its prerequisite NT types, causing
  RTL_CRITICAL_SECTION errors. Use winerror.h and the shared header's NT types.
- The backend table was exposed to PE compilation, where unixlib_entry_t is
  absent. Guard the table with WINE_UNIX_LIB. Both module architectures now build.
- Linking the probe initially used stale import libraries: the module 'all'
  target does not regenerate them. Explicitly build both libwexhost.a targets.
- Explorer launches detached applications, so redirected launcher stdout did
  not contain the probe report. Give the probe an explicit output file and poll
  for completion; do not use Explorer's exit as application completion.
- The first 32-bit probe returned ERROR_NOT_SUPPORTED because wexhost lacked
  __wine_unix_call_wow64_funcs. Added the fixed-width ABI table and reran both
  architectures successfully. Failed logs were superseded by successful reruns.
