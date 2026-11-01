<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# G3 independent correctness repair and G4 handoff

Date: 2026-10-18. Branch in both repositories: `codex/g3-correctness`.
Parent baseline: `5331435aafdcca30b12a79bf4af0733923abde14`.
Wine baseline: `af80a466ad7d4892b3ffa38acedfa68a50a68c08`.
Wine repairs: `e8cafdb08c8d5a2808265cda055d28201ed661ce` (copy/fence ordering),
then `6422d0a1922201050f15ca325442ada25c420f14` (DXGI construction).
The published parent commit pins the repaired Wine commit; resolve that gitlink
with `git rev-parse HEAD:wine` rather than using the historical acceptance pin.

## Defects and changes

1. CopyResource cast both resources to Texture2D before checking their type.
   Buffer and Texture1D copies could assert, and Texture3D was equally invalid.
   Inspect dimensions first and use the shared-image hook only for Texture2D.
   Other resource types keep Wine's ordinary copy path.
   Runtime tests also uncovered DXGI initialization querying every outer as a
   Texture2D before its inner was assigned. On a buffer that forwarded through
   a NULL DXGI pointer during CreateBuffer, before CopyResource even ran.
   DXGI now queries only the interface matching WineD3D's resource dimension.
2. Pending Wait was drained only by CopyResource/Signal. Clear, draw, dispatch,
   End(query) and Flush could pass an unsignaled fence. Replace this special list
   with a WineD3D default-queue callback packet. The API enqueues and returns;
   the CS sleeps on an event until completion, without a polling loop. All later
   default-queue commands are behind the barrier. Deferred contexts reject the
   operation; a disabled asynchronous CS returns E_NOTIMPL, not fake success.
3. Signal published a CPU value without establishing general GPU completion.
   Queue its callback after earlier commands and finish every GL context owned
   by the device, or submit/wait the Vulkan graphics queue. Publish the value
   only afterward. Backend failure publishes the device-loss sentinel UINT64_MAX.
   The inherited shared-texture CPU readback transport is retained, not described
   as zero-copy. G4 must replace these expensive barriers with backend fences.
4. Queued fence commands hold private refs separately from public COM refs.
   Public ownership retains the device; queued refs retain the fence allocation
   until execution without final-releasing the device from its own CS thread.
   Failure paths unregister the command's waiter before closing its event.

New original GPL-2.0-or-later probe `scripts/test_g3_ordering_probe.cpp` checks
real buffer, Texture1D and Texture3D copy bytes, an unsignaled cross-device fence before
Clear/End/Flush (query stays incomplete for 200 ms, then green pixel readback),
and immediate release of all public objects after queued Signal. `--fence-only`
isolates ordering from the baseline's earlier buffer assertion. This does not
cover every draw/dispatch operation or delayed physical-GPU workload.

## Reproduction and evidence

Use an isolated combined WoW64 build (`--enable-archs=i386,x86_64`), both MinGW
C++ compilers and Xvfb. No installed Wine fallback is accepted:

```
python3 -B scripts/check-g3-correctness.py --wine-build ABSOLUTE_BUILD \
  --output-dir NEW_EVIDENCE_DIRECTORY --full-suite
```

The runner allocates its own display and prefixes, selects the matching build's
wineserver, forces software OpenGL, saves binary hashes/logs/results, bounds test
and cleanup times and kills only servers for its own prefixes. It keeps timeout
diagnostics. Failed server cleanup preserves prefixes and reports their path.
The full-suite option reruns the existing G0-G3 harness as additional, not
substitute, coverage. Native controls are MinGW -O2 -Wall -Wextra -Werror PEs.

Final evidence: `docs/evidence/CHROMIUM_G3_20261018.json` contains exact source/
binary hashes, environment, PE results, original-suite JSON and cleanup results.
All ten modified Wine source files matched the published source (LF-normalized).
Compiler: Debian GCC 14.2.0-19 and MinGW GCC 14-win32; Python 3.13.5.

| Check | x86_64 | i686 | Scope |
|---|---|---|---|
| Combined WoW64 runtime build, final incremental build | PASS | PASS | Upstream test compilation disabled; no upstream suite claim |
| Native Windows: all five new regressions | PASS, exit 0 | PASS, exit 0 | Same PE files as final GL run; native adapter not inventoried |
| WineD3D/OpenGL: all five new regressions | PASS, exit 0 | PASS, exit 0 | Xvfb, llvmpipe; functional software control |
| WineD3D/Vulkan: all five new regressions | PASS, exit 0 | PASS, exit 0 | Explicit Mesa lavapipe ICD; functional software control |
| Existing G0/G1/G2/G3 project probes | PASS | PASS | OpenGL, overall harness exit 0; not a real-browser/hardware acceptance run |
| Owned-prefix server wait/absence, Xvfb cleanup | PASS | PASS | All final prefixes removed; no test Wine/Xvfb process left |
| Physical older GPUs, real Chromium/Electron/CEF, installed desktop/theme matrix | NOT RUN | NOT RUN | Mandatory subsequent gates, not inferred from these probes |

GL inventory from the owned display: Mesa 25.0.7-2+deb13u1, llvmpipe
(LLVM 19.1.7, 256 bits), OpenGL 4.5, **Accelerated: no**. Wine's front-end
reported a GTX 470 in its compatibility descriptor; that is NOT the physical
adapter used. Vulkan selected `/usr/share/vulkan/icd.d/lvp_icd.json`, whose
library is `libvulkan_lvp.so` (software lavapipe). Both Wine tests ran as uid 0
in disposable prefixes, not installed ordinary-user acceptance. Probe loop FPS
is not measured display FPS or acceleration evidence.

Final local evidence directories are `evidence-fixed-gl5` (including
`full-suite.json`) and `evidence-fixed-vk2` under the root below. The Vulkan run
did NOT run the original full G0-G3 suite; its result is the five new regressions.

WSLg-specific reproduction: install Xvfb if missing, create an OWNED empty socket
directory with mode 1777, then run the command above inside
`unshare --mount --propagation private`. In that namespace only, bind your owned
directory over `/tmp/.X11-unix`. For this run the owned directories were
`/var/tmp/wex-g3-repair-20261018/x-sockets-gl3` and `x-sockets-vk1`. No global
mount, WSLg socket or permission was changed. For optional Vulkan add
`--renderer vulkan --vulkan-icd /usr/share/vulkan/icd.d/lvp_icd.json` (select the
actual software ICD on your machine). The runner rejects a read-only global
socket directory and gives this guidance rather than trying to modify it.

## Scope that remains unaccepted

This repair is not blanket G3/G4 or hardware-acceleration acceptance. The G4
work order makes the following mandatory entry gates: repeated GPU-sampled shared
patterns (not only CPU mapping); multiple fence observers/processes; resource
descriptor/format/pitch validation; handle/mapping ownership and creator death;
device loss and allocation failures; delayed non-shared GPU writes and multiple
contexts; upstream d3d11/dxgi regression suites; physical older Intel/AMD/Nvidia;
real default Chromium/Electron/CEF; X11 effects OFF/ON and actual Wayland;
Classic/Open Luna and taskbar/toolbar/tray regressions. The inherited auto-reset
shared event and width*4 transport require particular scrutiny. Do not infer their
correctness from a one-observer RGBA8 test.

No token-security/OOBE implementation, runtime package, theme art, original
Gemini checkout or master branch was modified for this repair. Do not merge
master on the strength of these low-level tests alone. Next implementation
order: `docs/CHROMIUM_GRAPHICS_G4_WORKORDER.md`.

## Failed attempts and exact workarounds

- Referenced submodule initialization failed twice: `fatal: reference repository
  'D:/DeepThought/Projects/WineExplorer/.git/modules/wine' is shallow`. Used an
  independent `git clone --no-hardlinks` and reset only its origin URL to the
  public Wine fork; original repository untouched.
- WSL cloning the managed Windows worktree failed: `fatal: not a git repository:
  /mnt/c/.../WineExplorer/D:/DeepThought/Projects/WineExplorer/.git/worktrees/WineExplorer1`.
  Its worktree gitfile is Windows-specific. Cloned the common parent repository
  onto Debian's native filesystem and selected our branch instead.
- Initial staging invoked `git --git-dir=.../wine/.git diff --name-only` without
  cwd/work-tree, compared against the wrong tree and copied unrelated CRLF files
  into our isolated Linux source. Configure warned `AC_CHECK_FUNCS(thr_kill2` and
  failed at line 11104: `syntax error near unexpected token` at the generated
  `as_ac_Header=...` statement. Stopped only the owned staging PID after verifying
  its destination FD. Preserved that failed source. Fresh clones use
  `-c core.autocrlf=false`; an explicit nine-file allowlist stages only our Wine
  edits with CRLF normalized. No production file was rewritten by that script.
- A subsequent NTFS `git diff --name-only` scan took minutes; stopped only its
  exact verified PID before build. The same explicit allowlist avoids that scan.
- First full build encountered an unrelated MinGW compiler ICE:
  `../../wine/libs/symcrypt/lib/hmacmd5.c:56:1: internal compiler error: Segmentation fault`,
  target `libs/symcrypt/x86_64-windows/lib/hmacmd5.o`. Single-target `make -j1`
  succeeded unchanged; resumed full `make -j4`. Original failure stays in the log.
  Subsequently interrupted only our verified make PID 13171 to prioritize runtime
  acceptance over unrelated upstream test compilation. Reconfigured the same
  owned tree with `--enable-archs=i386,x86_64 --prefix=/usr/local --disable-tests`
  and resumed `make -j8` into `build-runtime.log`. This builds both runtime
  architectures; it is NOT an upstream test-suite pass. The new project probes
  remain separately compiled/run for both PE architectures.
- A read-only WSL inspection using shell variable `$p` was expanded by outer
  PowerShell, producing `/clean-source/...` and `No such file or directory`.
  Reissued with explicit absolute paths. Native PE launch initially had a single
  leading UNC slash and was not recognized; constructing a two-slash UNC path
  correctly ran both architectures.
- The first acceptance-document patch matched only a truncated table row and
  failed `Failed to find expected lines`; the atomic patch made no changes.
  Reapplied against the exact status header and explicitly labeled the old table
  as historical author claims rather than overwriting its detailed evidence.
- Initial unprivileged diagnostics at the managed worktree returned `fatal:
  this operation must be run in a work tree` / `Permission denied`; unprivileged
  WSL returned `Wsl/E_ACCESSDENIED`. Authorized escalated calls worked.
- Initial runtime launch failed `FileNotFoundError: [Errno 2] No such file or
  directory: 'Xvfb'`. Installed Debian xvfb, xserver-common, mesa-utils and
  mesa-utils-bin. Next launch failed `Owned Xvfb failed: inspect xvfb.log` with
  `Failed to find a socket to listen on`: findmnt confirmed WSLg's socket bind
  is read-only. Used the private mount namespace above, not a global remount or
  socket deletion. The initial Xvfb diagnostic was very repetitive and a whole-
  log read was truncated; subsequent inspection used bounded head/tail.
- First actual Wine run (`evidence-fixed-gl3`) faulted at relocated PC
  `00006FFFFB19F049`, read address 0. The first addr2line lookup used the relocated
  address and returned `??`; translating to PE ImageBase 0x180000000 + RVA 0xf049
  identified `d3d11_buffer_QueryInterface`, buffer.c:57. DXGI's construction-time
  unsupported QI forwarded through NULL. Added the DXGI type guards, rebuilt both
  DLLs, and reran. The debugger was bounded by the runner's 120-second timeout;
  only owned test processes/servers were cleaned up.
- Read-only rg inspection also named nonexistent `dlls/d3d11/resource.c` and
  reported OS error 2; located the helper in utils.c instead.
- First complete GL/Vulkan functional runs (`gl4`/`vk1`) passed all requested
  assertions, but the runner incorrectly treated wineserver -k exit 1 as cleanup
  failure when the server had already auto-exited. Verified server/main.c and
  request.c semantics; now accept only 0/1 from -k, always perform bounded -w,
  and verify no matching build/prefix server via /proc. Final gl5/vk2 runs pass
  assertions AND cleanup with no ignored test errors. The earlier diagnostic
  prefixes were preserved, not used for the final acceptance runs.

## Local state

Independent source/build/evidence root: Debian
`/var/tmp/wex-g3-repair-20261018`. The old failed `source` directory is preserved;
`clean-source` is the native-filesystem build. Logs include `assemble-clean.log`,
`configure-clean.log`, `retry-compiler.log` and `build-clean.log`. No existing VM
was reset or restarted. Runtime configuration/build logs are
`configure-runtime.log` and `build-runtime.log`. Runner success and forced-timeout
logging were separately checked with original temporary Python fixtures; both
passed and their temporary files were removed. Test-prefix/display cleanup and final build state are
recorded with the results above. Local paths are not portable evidence bundles;
rerun the committed probes on the published source for independent verification.
Earlier diagnostic-only inactive prefixes remain at
`/tmp/wex-g3-correctness-3mt_dh_b` and `/tmp/wex-g3-correctness-a1w7o2ds`.
Their servers are gone. Final gl5/vk2 prefixes were removed. No build, test Wine
server or Xvfb started by this repair remains running. Other workers' VMs and
processes were left untouched. Both Wine repair commits were pushed before the
parent handoff; the original Gemini checkout and master were not switched/reset.
