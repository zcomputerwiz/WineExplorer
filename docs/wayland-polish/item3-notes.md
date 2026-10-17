<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Item 3: live output scale changes

The staged real-program sequence passes; fresh-ISO acceptance is not yet claimed.
See [raw output](logs/item3-staged.log) and the six `scale-` screenshots.
Maximized foot fills work area `(0,0)-(1280,770)` at every scale. Restored
foot remains on-screen, hosted and focused; its character-cell size hints
can adjust the restored dimensions slightly. Actual native and Wine client
bounds agree within one physical pixel. Taskbar bounds remain
`(0,770)-(1280,800)`, and typing reaches the native terminal.

The original real-foot failure is preserved in [the baseline log](logs/item3-before.log).
The driver detached an active group as soon as its logical client rectangle
changed. Independently, labwc reapplied native maximize geometry using its own
caption margins. These two paths cannot preserve a Wine-decorated group.

The candidate keeps the group active and uses the existing retained-buffer
resize machinery for `configure_foreign` layout updates. It waits for a fresh
frame commit and acknowledged native content of the requested dimensions,
then publishes both roots together. The compositor skips its independent
layout adjustment for the hosted native member. No protocol version or wire
signature changes are needed.

Real-program iterations exposed three additional requirements:

- A later layout acknowledgement must not replay first-adoption activation:
  that steals native focus while the maximize controller needs frame focus.
- The maximized frame needs a new xdg configure in the new logical coordinate
  space. Otherwise Wine correctly refuses a frame smaller than its old
  maximized configure; native content is ready, but the frame never publishes.
- The shell must not rescale its saved, old-coordinate native observation into
  a new physical frame. The restored window grew and moved off-screen after a
  scale round trip. Its existing physical placement remains authoritative
  while the coordinated layout response is pending.

The runner now waits beyond the existing two-second transaction timeout before
accepting each state. The earlier immediate check could accept a transient
ACTIVE group before it detached. It also checks actual native bounds against
the Wine client rectangle within one physical pixel, keyboard input, taskbar
bounds and the work area. The real sequence is maximized and restored foot,
100% → 150% → 100%.

## Failed commands and workarounds

- SCP initially requested `winewayland.drv.so`; the built and installed module
  is `winewayland.so`. The resulting staging HTTP 404 stopped the chained
  install before replacing anything. Correcting that filename allowed staging.
- SCP of `linux_windows.c` initially omitted `wine/` from the build-source
  destination. That directory does not exist; copying to
  `wine/dlls/wexshell/linux_windows.c` updates its owned-source symlink target.
- PowerShell `rg` arguments containing literal wildcard path components
  (`run*py`, `zone*`, `wayland*`, `*.c`) failed with error 123. Directory searches
  or exact paths work. An initial read also placed `linux_windows.c` in
  wexhost; its actual module is wexshell.

Temporary compositor timeout diagnostics have been removed from the source
and rebuilt. The development QEMU VM still has the tracing compositor until
its next restart. It has staged candidate
modules, so it is not fresh-ISO acceptance evidence. The preserved previous
acceptance VMs remain stopped and unchanged.
