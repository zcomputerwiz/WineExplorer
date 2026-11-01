<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# G4 native geometry and live fractional scaling — 2026-10-21

**Scoped progress, not G4 merge acceptance.** Parent checkpoint before this
slice:89c7440a; Wine remains571e907 (no Wine production changes this slice).
Compositor fix94faa651 is pushed on codex/g4-native-render-bridge, labwc fork.
Qwen's OOBE work, existing VMs/sessions, master and token work were untouched.

## Empty-intersection clipping bug fixed

Extended the original two-connection native role test with real output-pixel
bounds, not only counts. It now tests relative movement, negative offset
clipping, a completely outside child, explicit render bounds, empty-parent
detachment,2x producer buffer scale, and parent-buffer shrink/regrowth in
addition to all16 previous lifetime/revocation controls.

Negative run `r4-native-geometry-first` returned1: a completely outside64x64
child leaked all4096 red pixels at [780,500,844,564] despite expected0; shrinking
the parent to100x100 likewise leaked4096 pixels where none should be visible.
All other controls passed. Cause: wlr_scene_subsurface_tree_set_clip treats an
empty clip as no clipping. Passing a zero-area intersection did not hide content.

`render_update_clip()` now explicitly disables the render subtree for an empty
intersection, enabling it again when the intersection becomes nonempty. Bounds
and all prior assertions remain; no new drawing fallback/underlay substitution.
Rebuilt only the idle owned compositor. `r4-native-geometry-fixed`: all24 controls
PASS, including the two independently reproduced failures. Positive movement
and clip cases assert exact screen bounding rectangles as well as pixel counts.
No image/test expectation was weakened to make the production change pass.

`r4-native-geometry-w32`:32-bit own/foreign full-target DComp each produces
exactly4096 actual red front pixels with producer exit0, confirming the clip
change did not lose the WoW64 consumer route.

## Real fractional-scale consumer checks

Installed Debian's wlr-randr0.4.1-1 only (12.3kB download,45.1kB installed).
The harness can change only its live owned private wexcomp endpoint, checks
exact runtime/socket identity before changing anything, and verifies the actual
reported HEADLESS-1 scale afterward. A command's exit code alone is insufficient.
It never invokes an output change against the user's display or existing VM.
Only explicit1.0/1.25/1.5/2.0 diagnostic factors are accepted.

Consumer runner additions:

- `--output-scale 1.25` sets the owned headless output before Wine startup.
- `--transition-scale 1.5`, requiring held lifecycle visuals, changes it during
  the original fixture's input checkpoint. One bounded1-second hold allows
  native scale/repaint messages before capture; no production poller is added.
- An original read-only Win32 helper in the same isolated prefix finds exactly
  one fixture HWND and reports its published parent render scale, visibility,
  position and size. All seven non-minimized checkpoints must publish the
  expected16.16 scale, remain visible, and have nonzero dimensions. Minimized
  content is separately captured; its hidden state is not rejected by this check.

Evidence root `/var/tmp/wex-g4-tree-20261020`:

| Run | Result |
| --- | --- |
| r4-native-scale125-gl / -vulkan |60-second real Electron lifecycle and eight held-stage captures complete on reported125% output |
| r4-native-scale-transition-gl / -vulkan |60-second lifecycle passes across live125%→150%, all14 HWND state readbacks match |

At ready: published fixed=81920, visible=1, position25,25, size740x520.
After live change: fixed=98304, visible=1, position26,26 (logical rounding),
size740x520. Subsequent resize/restore/reload publishes628x478; resize_back
publishes748x578, still98304. The actual Wine property changed along with the
compositor scale: not an environment-variable request or claimed DPI setting.
Native winewayland mappings are observed without winex11 in each run.

Inspected original fixture images at ready/resize_back for static125% and at
input/restored/resize_back for the live transition show content following the
frame, entered text/button state retained across scale/restore and reset on
reload, and visible content after growing again. These captures are independent
grim output, not Chromium's internal screenshot or RAF/API-only proof.
The static125% Vulkan resize_back capture again has the known transient outer
border mismatch; both live-transition resize_back captures have complete frames.
That prior frame-timing observation remains OPEN, not declared fixed by clipping.
Hardware pointer/key routing and per-monitor installed-shell DPI are not proven
by Electron's injected input or this headless scale change.

Six compositor-harness and11 consumer-harness unit tests PASS. The final parser
was tightened to reject hidden/zero-size HWNDs after the real runs; all14 saved
native-window stdout logs were then replayed through that exact parser and PASS.
Duplicate/missing/wrong-scale/hidden/zero-size records are unit-tested rejection
cases. Final observer also prevents issuing the same transition repeatedly on
a capture error. These are stricter diagnostics, not a production modification.

## Identities, isolation and next slice

Compositor binary SHA256:
`4a24dd13fed3d733d4edd6f213e4db284ff0dee9229e5df898d694d4446e0ec8`.
The directory suffix acc46fe remains historical, not its source identity.
Other Wine/runtime identities and paths remain those of
CHROMIUM_GRAPHICS_G4_NATIVE_BRIDGE_20261021.md; no private runtime is published.
All runs use run-isolated-graphics.py with fresh per-run socket/output roots.
No owned binary was rebuilt during a probe. There is no new VM/disk this slice;
any future VM/disks/snapshots must be host D:.

Remaining relevant acceptance: deterministic settled frame/repaint around the
transient resize border; installed Classic/open Luna/taskbar locked/unlocked/
toolbars/tray checks on our shell; native accelerated virtual-GPU evidence.
Only require generic HWND role migration/partial foreign GDI or other API gaps
as merge gates if an actual required Chromium path needs them. This slice does
not prove arbitrary reparenting across immutable Wayland roles, multiple outputs,
all DPI values, real pointer routing, zero-copy/private DXVK interop or the full
remaining synchronization matrix. Existing scoped proofs are not invalidated,
and inherited upstream DXGI/legacy-sharing failures remain documented.

## Failed diagnostics retained

- `r4-native-geometry-first`: exit1 with outside_parent expected0/actual4096,
  parent_shrunk expected0/actual4096. All other controls passed. Rebuilt the
  compositor with explicit empty-intersection subtree visibility; fresh
  r4-native-geometry-fixed passes24 without weakening assertions.
- `wlr-randr --help` tried a read-only connection to the default WSL display
  and printed `compositor doesn't support wlr-output-management-unstable-v1`
  instead of help. No display settings changed. Read the installed man page
  with `zcat /usr/share/man/man1/wlr-randr.1.gz` instead. All subsequent scale
  mutations require the validated owned private runtime/endpoint.
- An early status read `cat
  /var/tmp/wex-g4-tree-20261020/r4-native-scale-transition-vulkan/stage-input-native-window.stdout.log`
  returned `No such file or directory`: that held stage had not yet run. The
  eventual file exists and passes strict parsing; not a consumer test failure.

No master merge or G5-ready claim. Preserve other agents' edits and keep the
one-time usage-reset credit untouched. Continue from branch HEAD, not historical
blank-content reports or the initial clipping-failure binary.

Final process inventory shows only its own shell/grep, no owned Wine server,
compositor, Xvfb or test process left running. Fork working trees are clean.
Primary usage remaining20%; below5% shutdown/continuation rule is not reached,
so no reset wake or reset credit was consumed for this checkpoint.
