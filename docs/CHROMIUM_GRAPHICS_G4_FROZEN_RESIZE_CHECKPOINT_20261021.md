<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# G4 one-issue checkpoint at5% usage remaining

Current selected issue: native resize/frozen-scene presentation. **Not fixed.**
Wine571e907 / compositor94faa651 remain unchanged. No production edits, master
merge, token/OOBE import, sandbox bypass or proprietary payload/art publication.
User authorized resuming WSL after relocation; earlier relocation STOP lifted.
Disk cleanup4742e489 removed only62 obsolete owned prefixes (~111GiB), preserving
all evidence/current r4 controls/builds/runtimes and others' work. See DISK_CLEANUP.

## Confirmed observation and fresh negative reproduction

Inspected actual old interrupted-run images, not just the oracle. Vulkan
resize-border-0.25.png has the frozen green page/blue square and incomplete
right/bottom frame. At4 seconds content/menu disappear, leaving a black client
and complete caption/frame. GL4-second image is white/blank instead. Thus the
old border observation masks a deeper frozen-content loss. RAF still increments
without DOM changes; that is not visible presentation proof. The oracle correctly
requires blue1600 as well as rims, so do not remove its content assertion.

Added opt-in --trace-native to the owned consumer runner: requires native
wexcomp; WINEDEBUG gains+waylanddrv, WAYLAND_DEBUG=client for this run only.
No Wine/compositor rebuild.11 consumer-harness tests still PASS.

Fresh full60-second run after relocation:
/var/tmp/wex-g4-tree-20261020/r4-frozen-resize-postmove-trace-vulkan.
Original lawfully cached Electron runtime/fixture, lifecycle + held visuals +
--resize-border --trace-native, Vulkan/lavapipe. Host exits0, all lifecycle API
checks pass, no logged GPU crash, but settled2/4-second samples have complete
rims and0 blue pixels; resize_border_diagnostic.passed=false, diagnostic_exit4.
This run was NOT interrupted. Saved cleanup remaining_servers=[]; final process
inventory matches only its shell/grep. No owned tests/Wine/compositors remain.

## Continue here

Read wine.stderr.log, host.log and actual PNGs together. Trace shows the parent
identity stays1fd0ad9ca04e2ba438956887f4100984 across final resize. Render role94
and129 alternate parent attachments/detachments with client-surface destruction;
at final growth129 attaches(4,30,740,544),94 then attaches same geometry and129
detaches. Determine actual buffer/scene ordering and retained lifetime after
this sequence; stable parent identity makes simple parent destruction less
likely, but no root cause is established. Initial guesses about scene order,
GDI replacement or immutable-role changes are hypotheses, not proven causes.

Inspect Wine dlls/winewayland.drv/{wayland_surface.c,window.c,window_surface.c}
and wexcomp src/protocols/wex-window.c. Root compositor content_tree comes from
wlr_scene_xdg_surface_create; native protocol parent-commit/resize tests already
pass24, so create a discriminating reproducer before changing stacking behavior.
No extra redraw/animation fallback or assertion relaxation is acceptable proof.

Owned paths remain:
Wine /var/tmp/wex-g3-repair-20261018/clean-source/build/wow64 (combined WoW64);
compositor /var/tmp/wex-g4-tree-20261020/wexcomp-build-acc46fe/labwc;
scripts under clean-source/scripts. New trace runner synced there. Always
run-isolated-graphics.py, unique per-run filesystem X sockets and fresh outputs;
never rebuild during probes. No fresh VM needed yet; if used all storage host D:.

## Failed diagnostics and continuation scheduling

- Windows `rg ... wexcomp/src/xdg*` failed `The filename, directory name, or
  volume label syntax is incorrect. (os error 123)`. Read exact xdg.c instead.
- Initial ls/grep of fresh trace wine.stderr.log returned `No such file or
  directory` because wineboot had not yet finished. Subsequent directory
  inventory located the completed consumer trace; not a test startup failure.
- WAYLAND_DEBUG also makes grim emit wire logs to inherited stderr; fresh tool
  output was noisy/truncated. Full relevant Wine logs remain in the run directory.
  Use narrow file queries rather than printing all wire logs again.

User's5% remaining checkpoint rule reached (95% used, reset1792625547). Finish
this note/commit/push then stop current work. Existing one-time heartbeat updated
to resume this exact issue at6:34pm America/Chicago,21Oct2026, after reset.
One-time reset credit untouched. This is one continuation, no status-only loop.
Official scheduled-task docs were checked via OpenAI Docs skill before updating
the existing automation; no duplicate task was created.
