<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Independent G4 correctness repair and completion handoff

Date: 2026-10-19. Parent and Wine branch: codex/g4-correctness.
Reviewed parent: b58b94856cb0b8818cbc9a4ac79c813c72a6a9cd.
Reviewed Wine: b203821512a870921c4253451f1129acea065fd7.
Repaired Wine: f3ecadd3bbe54272fa2fdee13418bb89db7ec1c7.
Compositor remains acc46fe619ef1dd917ffd0b0aaf66ec13083290b.

**Verdict: the limited correctness repairs below pass their independent probes.
Full G4, default browser graphics, physical acceleration and desktop/theme
acceptance remain incomplete. Do not merge master or declare G5 entry.**
The original Gemini acceptance report is preserved with a historical-claims
warning. Its blanket PASS and zero-theme-regression conclusions are not reused.

## Repairs and deliberate capability boundary

1. Shared-fence signaling no longer uses PulseEvent. Each imported fence observer
   gets its own named auto-reset event, with persistent notification even when
   signaled before its registered wait is active. A named cross-process mutex
   protects the registry and completion publication. A post-registration value
   check closes the slot-publication gap. Signals notify every registered observer,
   not one shared auto-reset waiter. Slots are reclaimed on release or when their
   event no longer exists; exhaustion is an explicit error. Capacity is currently
   64 imported observer objects, not a claim of unlimited subscribers. Callback
   registration errors remove the just-added local waiter rather than fake success.
   No periodic poll or permanently signaled manual-reset callback loop was added.

2. Supported composition commits bind synchronously through a private builtin
   IWineDCompSwapChain COM interface. No posted heap pointer or swapchain-owner
   message pump is needed. Binding changes and Present are serialized, state
   access retains the WineD3D mutex, and WaitForCommitCompletion waits behind the
   device commit lock. Successful completion means supported binding state is
   applied, NOT that the physical display has scanned out a frame. Native DXVK
   does not implement this private interface and is not silently claimed compatible.

3. Removed false-success behavior for unimplemented opacity, rounded radii,
   animation setters and matrix rotation/shear/reflection in the guarded slice.
   Finite/range checks reject invalid transforms/offsets/clips before conversion.
   Foreign effect/clip objects are rejected before casting to owned implementations.
   Non-unity opacity and positive corner radii return E_NOTIMPL. Nonfinite input
   returns E_INVALIDARG. This is honest rejection, not completed rendering support.

4. Preflight rejects multiple content leaves before changing active state.
   Independent swapchain blits cannot provide retained alpha/z-order composition.
   The coherent initial slice is ONE opaque content leaf per composition device.
   A rejected multi-leaf transaction leaves the previous active content bound.
   The next worker must implement retained composition, then replace these
   rejection assertions with stronger positive pixel assertions.

5. Real pixel checks exposed an additional stale-drawable bug: moving/clipping a
   leaf left old OpenGL pixels visible. Internal high-bit presentation flags now
   clear the opaque composition drawable before its partial blit, and clear-only
   on empty clipping/detach. GL state invalidation, Vulkan clear/transfer barriers,
   and the existing GDI fallback are handled in the backend. Ordinary presents
   do not set these flags. Both tested backends show old pixels removed.
   Outside the supported opaque leaf this produces black, NOT preserved arbitrary
   application/background transparency; that broader composition remains pending.

6. New original test_g4_correctness_probe.cpp reads actual on-screen target-client
   pixels using ClientToScreen, not just successful calls or staging readback.
   It verifies pending versus committed placement, nonuniform clipped-pattern
   cropping, automatic old-area clearing, empty clip, detach, cross-thread commit
   without owner-thread message pumping, 60 changing frames after one Commit,
   rejected transactions/properties, and finite-input rejection. Fence coverage
   includes three separate local imported objects plus an actual child-process
   imported observer over 128 timeline values. This is useful stress coverage,
   not a deterministic proof of every registration/teardown interleaving.

The repair keeps the useful G0-G3 implementation and original probes; it does not
erase the old acceptance tests to turn missing features into success. Their
HRESULT-only composition results are historical. Run the new independent mode
for the explicitly supported capability boundary.

## Independent results

Evidence: evidence/CHROMIUM_G4_REPAIR_20261019.json contains source/binary hashes,
exact commands/environment, per-architecture results and prefix cleanup. All
12 selected graphics source/header files in the owned native build matched the
published worktree bytes. These are raw working-file hashes; Git EOL normalization
can change blob hashes.

| Check | x86_64 | i686 | Scope |
| --- | --- | --- | --- |
| Combined WoW64 runtime build | PASS | PASS | Runtime targets, tests disabled in configure |
| New G4: five behavioral cases, OpenGL | PASS | PASS | Actual target-client screen pixels, Xvfb software control |
| New G4: same five cases, Vulkan | PASS | PASS | Explicit Mesa lavapipe ICD, software control |
| G3 ordering/sharing probe, eleven cases | PASS | PASS | OpenGL, limited inherited entry-gate coverage |
| Existing G0/G1/G2/G3 project harness | PASS | PASS | Four probe executables per architecture, not a browser |
| Existing frame-latency/minimize probes, five cases | PASS | PASS | API/semaphore functional checks, not measured display efficiency |
| Final owned-prefix/server/display cleanup | PASS | PASS | Bounded waits, no exact-build-path runtime process remained |
| Native Windows controls / upstream Wine d3d11-dxgi suites | NOT RUN | NOT RUN | Do not infer from compilation or disabled upstream tests |
| Default real Chromium/Electron/CEF/WebView2 | NOT RUN | NOT RUN | No consumer capability or sandbox acceptance claim |
| Physical GPUs, installed Classic/Luna, real Wayland | NOT RUN | NOT RUN | Required next-stage matrix |

Native build configuration:
--enable-archs=i386,x86_64 --prefix=/usr/local --disable-tests.
Compiler: Debian GCC 14.2.0-19, both MinGW C++ 14-win32, Python 3.13.5.
Final runtime build had no warning/error matches in its retained build log.

Owned glxinfo inventory: Mesa 25.0.7-2+deb13u1, llvmpipe (LLVM 19.1.7, 256 bits),
OpenGL 4.5, Accelerated: no. Wine's compatibility adapter name GTX 470 is NOT
the physical hardware. Vulkan selected /usr/share/vulkan/icd.d/lvp_icd.json;
its hash is recorded. Existing harness FPS fields are enqueue/presentation-loop
rates, not measured displayed FPS.

## Reproduce

Use your own combined-WoW64 source/build and an owned display:

    python3 -B scripts/check-g3-correctness.py --wine-build BUILD --output-dir NEW --g4-correctness
    python3 -B scripts/check-g3-correctness.py --wine-build BUILD --output-dir NEW --g4-correctness --renderer vulkan --vulkan-icd ICD_JSON
    python3 -B scripts/check-g3-correctness.py --wine-build BUILD --output-dir NEW --full-suite
    python3 -B scripts/check-g4-pacing.py --wine-build BUILD --output-dir NEW

On WSLg use an owned socket directory bound over /tmp/.X11-unix in a private
mount namespace. Never alter/delete the global socket mount. Runners use their
own prefixes/displays, exact build's server, bounded execution and scoped cleanup.

The probes include open helper code from test_g4_composition_probe.cpp, but do
not invoke its historical HRESULT-only acceptance main. Both architectures are
compiled with -O2 -Wall -Wextra -Werror and static MinGW runtimes.

## Remaining work and exact next order

Read CHROMIUM_GRAPHICS_G4_COMPLETION_WORKORDER.md. The next stage must implement
real retained multilayer alpha/z-order/background composition, rounded clips,
general transforms and coherent transactions, then prove real consumers and
desktop/theme behavior. Correct software fallback and older Intel OpenGL-only
support remain required; accelerated zero-copy/pacing is measured separately.

Also pending: deterministic fence-registration race injection, exhaustive
observer/handle/process-death/device-loss/teardown tests, stronger GPU-sampling
coherence proof, shared-descriptor versioning/hardening, idle CPU/memory/transfer
measurements, upstream d3d11/dxgi suite baseline, real X11 outer composition ON/OFF
and real Wayland, Classic/Luna resolutions/taskbar/toolbars/tray states, and
older Intel/low-end AMD/Nvidia physical hardware. No unreviewed security-token
or OOBE implementation was imported to bypass consumer blockers.

## Failed attempts and exact workarounds

- Initial read-only git status in the main checkout failed because access to
  .git/modules/wexcomp was denied. Approved submodule inspection and
  --ignore-submodules parent status supplied the relevant clean state.
- The owned parent switched to codex/g4-correctness, but its Wine checkout did
  not yet contain b2038215: git switch returned fatal: invalid reference.
  Fetched the reviewed branch from the existing local Wine repository, then
  created the isolated Wine branch; the worker's checkout was never switched.
- The first composition patch had overlapping exact-function hunks and failed
  to find expected SetOpacityAnimation lines. A full read confirmed no partial
  write. Generated nonoverlapping hunks from the original/repaired text and
  applied them successfully.
- First make failed in both architectures with incompatible-pointer-type errors
  from winedxgi.h: wined3d resource/texture tags had only prototype scope.
  Added forward declarations before the private generated-header include.
- First new PE test compilation failed because SetTopLeftRadiusX(0) ambiguously
  selected float versus animation-pointer overload. Used explicit 0.0f/4.0f.
- First target-window GetPixel checks read black from Wine's GDI backing DC,
  not the OpenGL drawable. Switched the oracle to the actual screen DC at
  ClientToScreen coordinates. Its next failures exposed retained old pixels
  after movement/clipping, rather than being waived as a successful Present.
- Establishing the visible background through the actual screen DC did not
  resolve old backbuffer pixels outside a new clip. Added composition-only
  drawable clear/clear-only behavior in GL/Vulkan. Strengthened final assertions
  by removing manual background clearing between move/clip/detach transitions,
  and required 60 changing frames after only one Commit. Final results pass.
- Lookups for dlls/wined3d/swapchain_gl.c and swapchain_vk.c returned OS error 2;
  those split files do not exist. Located both backend implementations in
  dlls/wined3d/swapchain.c instead. No guessed file was created.
- First binding/Present lock patch used an incomplete function-signature context
  and failed to find expected lines. It made no changes; narrower exact-context
  hunks applied successfully.
- Final private present bits use high internal values, avoiding overlap with
  legacy public present flags. The publication build and all recorded final
  runs were repeated after the flag/numeric-guard changes.

## Retained local state

Owned native runtime reused ONLY this agent's earlier repair lab:
 /var/tmp/wex-g3-repair-20261018/clean-source/build/wow64.
Current logs/evidence and socket directories:
 /var/tmp/wex-g4-repair-20261019.
It is an assembled native source/build, not a clean Git checkout; the source
hash match is explicit, not inferred from its historical Git HEAD. Gemini's
/var/tmp/wex-g4-gemini source/build was inspected read-only during review, not
used or modified for these tests.

Final publication evidence directories are g4-publish-gl, g4-publish-vk,
g3-publish-gl and pacing-publish-gl; earlier failed-run logs are retained.
Final test servers and Xvfb contexts ended; no test VM was started or reset.
Owned prefixes were cleaned by the runners. Other workers' builds/processes,
main checkout, security-token/OOBE branches and master were left alone.
Wine was pushed before the parent gitlink. No master merge is performed.
