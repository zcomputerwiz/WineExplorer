<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Item 3: live output scale changes

**Partial result; restored-window scaling remains open and is time-boxed out.**
The initial staged and fresh-image sequences passed, but a later fresh-image
run reproduced restored foot growing off-screen at 150%:
[raw failure](logs/fresh-scale-order-race.log). Maximized scaling still passes.
The paragraphs below describe the initial run, not a complete acceptance claim.
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
The [stale-configure failure](logs/item3-stale-configure.log) and
[stale restored observation](logs/item3-stale-observation.log) are preserved.
The latter runner reported a pass before the native-alignment and on-screen
assertions were added; its off-screen restored coordinates are a failure,
not accepted evidence.

## Failed commands and workarounds

### Comparison with established compositors

The user suggested checking other compositors before adding more machinery.
The following primary sources were read on 2026-10-17:

- [Sway xdg-shell commit handling](https://github.com/swaywm/sway/blob/master/sway/desktop/xdg_shell.c),
  `handle_commit`: floating containers follow the client's committed geometry;
  transaction readiness is tied to the configure serial. Hidden retained
  surfaces still receive frame-done events when a commit is not yet ready.
- [Sway transactions](https://github.com/swaywm/sway/blob/master/sway/desktop/transaction.c),
  `transaction_commit`, `transaction_notify_view_ready_by_serial`, and
  `handle_timeout`: preserve the previous scene, wait for the configured
  response, then apply the queued state. Timeout advances the transaction.
- [Labwc xdg-shell handling](https://github.com/labwc/labwc/blob/master/src/xdg.c),
  also present in our pinned fork's `src/xdg.c`, `handle_commit`: reconcile a
  terminal's actual cell-constrained size with pending geometry. Once the
  pending configure completes, update wlroots' stored scheduled dimensions
  without issuing another configure, avoiding a request/response loop.
- Foot **1.21.0** [Debian source archive](https://deb.debian.org/debian/pool/main/f/foot/foot_1.21.0.orig.tar.gz),
  SHA256 `5af8d028ddcf33129fa0c38e0d4eaf4d24b695650522cac49d88a8d41366588f`:
  `wayland.c:update_term_for_output_change` requests `RESIZE_KEEP_GRID`;
  `render.c:render_resize` preserves the grid for floating windows and can
  round configure sizes to cells. Maximized windows skip both floating-only
  adjustments. `xdg_surface_configure` acknowledges the serial, renders, and
  sends an empty surface commit if no resize was necessary.

Our retained-scene approach follows the same broad transaction pattern.
An important difference to investigate is that `reconfigure_commit` requires
the native current dimensions to equal the requested Wine client dimensions,
while floating clients may choose a different size. The automatic timeout
drops the hosted scene. These source findings are **a follow-up lead, not a
confirmed cause** of the off-screen failure: its final readback is ACTIVE,
and no trace yet establishes whether an earlier timeout or an independently
queued observation changed the frame. Do not relax geometry checks or add
an interactive-resizing state merely on this evidence. The next real-foot
trace should correlate scale, configure serial, requested/committed native
size, Wine frame/client geometry and transaction completion/timeout, then
reconcile the authoritative state in that transaction.

Two owned-shell trials did not solve the real case and are removed:

- [Scale-observation guard](logs/restored-scale-guard-trial.log): remained
  on-screen but shrank/moved severely on return. Its old runner printed PASS;
  the recorded coordinates are a failure. The runner now also rejects large
  position/size drift.
- [Active-frame authority](logs/restored-scale-authority-trial.log): ignoring
  native geometry observations for an ACTIVE SSD group still grew off-screen.
  This does not establish that observations are the sole cause.

Both guest DLLs were restored from the original clean ISO package and their
hashes verified before normal shutdown:
[restoration log](logs/fresh-runtime-restored.log). No trial source is kept.
The build VM source must also be resynced to the committed checkpoint before
the final pinned build; its last local trial build is not a release artifact.

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
- A later read of `wexcomp/src/wex-scene.c` failed because the file actually
  lives at `wexcomp/src/protocols/wex-scene.c`; the corrected path was read.
- The web reader could not access Codeberg or Debian Sources' `wayland.c`.
  The published Debian source tarball above was downloaded into ignored
  `build/wayland-session` and only the required source files extracted there.
- Two patch attempts during the follow-up investigation failed on mismatched
  context and made no changes; smaller exact-context patches succeeded.

Temporary compositor timeout diagnostics have been removed from the source
and rebuilt. The development QEMU VM shut down normally after the check.
Its disk still has the tracing compositor until its next runtime update.
It has staged candidate
modules, so it is not fresh-ISO acceptance evidence. The preserved previous
acceptance VMs remain stopped and unchanged.
