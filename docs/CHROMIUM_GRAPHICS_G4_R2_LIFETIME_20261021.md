<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# G4 R2 event lifetime and WoW64 Vulkan memory ownership

Branch `codex/g4-r1-completion`. Parent main `63c551be` / Wine `61929ac`.
Published Wine steps: `2c1e3ef` (event ownership), `1dcd479` (Vulkan ownership).
No master merge. Qwen's OOBE work, security-token branches and VM sessions were
not changed. This is verified R2 progress, not completion of all R2/G4 gates.

The user-approved deferral of bottom-child clipping and simultaneous populated
top/bottom HWND targets remains in `CHROMIUM_GRAPHICS_G4_HWND_INTEGRATION_20261021.md`.
Those remain compatibility failures, not passing tests. Reopen if a selected
Chromium consumer actually needs them; they do not block the current track.

## Fence event-object lifetime

Public references:
[ID3D11Fence](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_3/nn-d3d11_3-id3d11fence),
[CreateSharedHandle](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_3/nf-d3d11_3-id3d11fence-createsharedhandle),
[SetEventOnCompletion](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_3/nf-d3d11_3-id3d11fence-seteventoncompletion).
The precise closed-handle behavior below was checked on native Windows, not
inferred solely from those short API descriptions.

`fence_event_handle_lifetime` creates a shared producer and imported observer,
registers an unsatisfied event, duplicates that event as a witness, closes the
numeric registration handle, then signals through the producer. Native Windows
x64 AND x86 both return wait0 / completed1. Unmodified Wine on both PEs returns
wait258 (timeout) / completed1: the timeline advances but the event is lost.

Cause: pending waiters stored the caller's numeric handle without retaining the
event object. Closing/recycling that handle could lose or misdirect notification.
Wine now duplicates the handle for each pending registration and closes its
reference on notification, observer teardown, registration rollback and internal
CS-wait cancellation. Public callers' handles are not closed by Wine.

Internal wait cancellation previously matched numeric event handles. It now
matches the unique still-live CS command owner, separately from the retained
event handle, avoiding both duplicate-handle mismatch and recycled-handle ABA.
There is no shared descriptor/layout change, public ABI addition, timer or poller.
No claim is made that every cancellation-allocation fault has been injected.

`fence_waiter_cleanup` performs33 cycles of64 unsatisfied registrations, per
local/imported mode (2112 per mode), closes the caller event and releases the
fence. It measures real process handles after warmup, allowing at most2 handles
of bounded infrastructure variation, not per-cycle/per-waiter growth.

An initial measurement used GetProcessHandleCount, which is a success-valued
zero stub in this Wine's NtQueryInformationProcess(ProcessHandleCount). Those
zero-count runs are NOT accepted leak evidence. The corrected fixture falls
back to actual SystemExtendedHandleInformation enumeration, filters by process,
rejects zero/malformed/unbounded tables, and calibrates by creating8 events and
requiring a measured increase of at least8. No production handle-count API was
changed merely to satisfy this test.

Corrected isolated Wine counts: x64 calibration34->42, local34->34/imported39->39;
x86 calibration36->44, local36->36/imported41->41. Native x64/x86 also pass;
minor native lazy-runtime variation stays within the stated bound. This is
kernel-handle coverage, not an exhaustive COM/mapping/heap leak certificate.

## WoW64 Vulkan low-address host-memory leak

While expanding the suite, a real32-bit Vulkan failure appeared near the end of
the same-process run. It reproduced serially, not just under concurrent load.
`--memory-profile` now records VirtualQuery committed/reserved/free/largest-free
bounds before each G4 case and at completion. It changes test logging only.

Pre-fix `fence-vk-memory-profile/i686.stdout.log` shows committed memory growing
in approximately64MiB steps:88,281,088 bytes initially,577,208,320 before the
cleanup case,1,854,386,176 before application-background tests,1,988,603,904
before detach. Stderr records a failed64MiB low-address NtAllocateVirtualMemory,
VK_ERROR_OUT_OF_HOST_MEMORY, then an exception in vkCreateBufferView. Exit3 is
a failure, not a timeout, missing marker success, or evidence of bad RGB output.

Source: `dlls/win32u/vulkan.c` synthesized a low-address host-pointer import for
WoW64 VK_EXT_external_memory_host, but left the outer `mapping` NULL. Successful
VkDeviceMemory objects therefore never recorded that allocation in vm_map, so
normal vkFreeMemory could not release it. The helper also missed freeing the
allocation when host-pointer-property discovery failed.

The fix transfers only Wine-synthesized allocation ownership to vm_map. Explicit
application-supplied host-pointer imports remain caller-owned. Failure after
allocation/property query/metadata allocation/Vulkan allocation now frees the
Wine-owned allocation; host Vulkan memory is released before its backing pages.
Failure paths are source-reviewed, not all fault-injected. Other alignment,
external-import/export and physical-driver conformance are not certified here.

`device_virtual_memory_reclaim` creates/releases9 actual D3D11/DComp devices,
uses one warmup, and allows at most32MiB committed growth—less than even one
leaked64MiB allocator chunk. It passes native Windows x64/x86, software GL and
lavapipe Vulkan on both PEs. In the final Vulkan run, the32-bit test stays at
107,450,368 bytes throughout all9 cycles; the whole27-case process finishes at
109,576,192 instead of exhausting its address space. The bounded test was added
after localization; the saved failing full-process profiles are the pre-fix
negative evidence, not a claim that this exact new test was run pre-fix.

## Verification and reproduction

Owned Debian source `/var/tmp/wex-g3-repair-20261018/clean-source`, combined
WoW64 build `build/wow64`, evidence `/var/tmp/wex-g4-tree-20261020/`.
Scoped rebuilds: `make -j6 dlls/d3d11/all`, then `make -j6 dlls/win32u/all`.
Logs `fence-event-build.log`, `fence-vulkan-host-memory-build.log`. Never rebuilt
while an owned probe used the build. Fresh prefixes and per-run filesystem X
sockets via run-isolated-graphics.py; no other worker's display/build was used.

Final controls:

| Evidence directory | Result / scope |
| --- | --- |
| fence-event-before | Both PEs reproduce lost-event timeout before the fix |
| fence-event-after-gl / -vk | Both PEs pass event-object regression |
| fence-cleanup-enumerated | Both PEs pass calibrated real-handle cleanup |
| fence-hostmem-fixed-27-vk | All27 groups both PEs PASS, explicit lavapipe; memory profile bounded |
| r2-final-27-gl | All27 groups both PEs PASS, software GL; memory profile bounded |
| r2-final-g0-g3 | All11 ordering groups plus G0–G3 PASS both PEs on final binaries |
| r2-final-pacing | All5 pacing groups PASS both PEs on final binaries |
| r2-final-webgl-gl / -vk | Real WebView2 WebGL1/2 default/FBO exact red pixels PASS |
| r2-final-electron-gl / -vk | All7 real Electron lifecycle/input checks PASS |
| r2-final-frozen-vk | Actual screen-18 image inspected, correct frozen presentation |

WebView2 final samples/frames: GL20/291, Vulkan20/309. Electron lifecycle:
GL34/357 post-reload frames, Vulkan34/359. All checked GPU/Skia/fatal/crash
signature counts are zero; no lost WebGL contexts or API errors, complete FBOs.
Frozen Vulkan actual front image shows one blue square at x160, expected frozen
text, uniform cream background, input/button, menu and scrollbar. It is not
capturePage-only evidence. Runtime/SDK/image payloads remain private and lawful,
outside Git/ISO; runtime hashes/versions match the prior checkpoint reports.

All final correctness/baseline/consumer cleanup records have no remaining server
processes; scoped waits succeed. Kill exit1 means an already-exited owned server,
not ignored surviving processes. The pacing runner has no cleanup JSON field;
the final process check independently found no owned Wine/build test processes.
Test prefixes/screenshots remain private for diagnosis; no VM/session was changed.
Wine-free test_graphics_harness.py: all4 unit tests PASS.

These are software Mesa controls, not physical older Intel/AMD/Nvidia proof.
Synthetic GL GTX470 branding is not a physical GPU. Wine Vulkan is explicitly
selected with `/usr/share/vulkan/icd.d/lvp_icd.json`; OpenGL remains supported.
Animation and sandbox-request flags are not visual/security acceptance.

Use the common isolated command:

```sh
python3 -B /var/tmp/wex-g3-repair-20261018/clean-source/scripts/run-isolated-graphics.py \
  --socket-root /var/tmp/wex-g4-tree-20261020/isolated-sockets -- \
  python3 -B /var/tmp/wex-g3-repair-20261018/clean-source/scripts/check-g3-correctness.py \
  --wine-build /var/tmp/wex-g3-repair-20261018/clean-source/build/wow64 \
  --output-dir /var/tmp/wex-g4-tree-20261020/NEW_UNIQUE_DIRECTORY \
  --g4-correctness --memory-profile
```

Vulkan adds `--renderer vulkan --vulkan-icd /usr/share/vulkan/icd.d/lvp_icd.json`.
Focus with `--g4-case NAME`; default assertions are not weakened. Native Windows
uses the identical free-source compiled probe, `--case NAME`, no Wine DLL
replacement/registry edit/runtime install, and retains executables only in the
private test cache. See previous reports for consumer runner arguments.

Final changed binary SHA-256:

| Artifact | SHA-256 |
| --- | --- |
| x64 d3d11.dll | c729f31574395c617a8ca7a62de5e796c367c52a0fa0957df5a0f63920ffdec3 |
| x86 d3d11.dll | f81c0bd0efaac4734cf16a459452d62b32057f82ec78598bd5de90375000c21b |
| native win32u.so | cac4026d225ed5cc54a7d9ca0c8750a27d079be2fef9c7ef605ab56197b4d4c9 |
| x64 win32u.dll | 3d23f2187e2897e19fca533e43fbf0fcc264ba0c960233df71919786d2d2a0d6 |
| x86 win32u.dll | b382bfff27b77879aab931fb332dd01c85ad49ffd08f38c83fb8e21e9ff6ec69 |

## Failures retained, not waived

- Initial searches used nonexistent `dlls/d3d11/fence.c` and
  `dlls/wined3d/fence.c`; fence code is in d3d11/device.c. The failing command:
  `rg -n 'SetEventOnCompletion|set_event|event.*completion|waiter|observer' wine/dlls/d3d11/fence.c wine/dlls/wined3d/fence.c`.
- `fence-event-before`: deterministic expected pre-fix failure, both PEs.
- `fence-final-26-gl` initial PASS cannot certify cleanup because it measured
  the zero stub. `fence-final-26-vk` also exits3 in i686 near detach.
- `fence-enumerated-full-26-vk` fixes handle measurement, still exits3 for i686;
  `fence-vk-memory-profile` localizes the real low-address leak. Neither is a
  pass. Isolated detach/expose both pass unchanged pixels, which did not waive
  the accumulating memory failure; the subsequent ownership fix repairs it.
- PowerShell rejected the unquoted comma-containing `--wine-debug` value before
  execution ("Missing argument in parameter list"). Exact failed invocation:

```powershell
wsl -d Debian --exec python3 -B /var/tmp/wex-g3-repair-20261018/clean-source/scripts/run-isolated-graphics.py --socket-root /var/tmp/wex-g4-tree-20261020/isolated-sockets -- python3 -B /var/tmp/wex-g3-repair-20261018/clean-source/scripts/check-g3-correctness.py --wine-build /var/tmp/wex-g3-repair-20261018/clean-source/build/wow64 --output-dir /var/tmp/wex-g4-tree-20261020/fence-enumerated-full-26-vk --g4-correctness --renderer vulkan --vulkan-icd /usr/share/vulkan/icd.d/lvp_icd.json --wine-debug -all,+winediag,err+all
```

  Reissued with the final argument quoted as
  `'--wine-debug=-all,+winediag,err+all'`; Python accepted and ran it.
- Wine source has no origin/master ref; `git -C wine diff origin/master --
  dlls/win32u/vulkan.c` failed bad revision while checking provenance. Local
  file/history inspection was sufficient; no ref, branch or remote was changed.
- A documentation-only apply_patch attempted to match the nonexistent literal
  `Original?runtime`; exact-context validation rejected the entire patch with no
  edits. Reapplying only verified table/paragraph contexts succeeded; no source,
  test, runtime or evidence file was affected.

## Next work / still-open gates

Deterministic signal-during-registration/rearm interleavings, creator exit,
fault-injected allocation/callback/cancellation teardown, broad COM/mapping/FD
leaks and multi-context GPU ordering remain open. This fixes two actual lifetime
defects, not that whole proof matrix. Next isolate registration/rearm with a
bounded deterministic control, then efficient backend GPU sharing/completion
and private DXVK interop; avoid an unreviewed synchronization rewrite.

Actual installed Classic/Open Luna, X11 outer compositor OFF/ON, Wayland/wexcomp,
resolutions/taskbar/toolbars/tray and physical older GPU acceptance remain NOT
RUN. Inherited upstream DXGI67 failures and D3D11 legacy-sharing exit5 remain
OPEN; no blanket pass or assertion relaxation. No master merge or G4-complete
claim. One-time usage reset credit remains untouched.
