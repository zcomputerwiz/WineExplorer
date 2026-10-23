<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Next bounded item: identify Chromium's native exit, not guess from Unix127

## Parent-reviewed baseline

ONE normal upstream Windows Chromium156.0.8078.0 baseline ran in the owned
D:-backed VirGL guest, chromium-headful-r1-20261023. It failed before readiness:
browser-launcher alive check failed; numeric Linux Popen127 was collected during
cleanup wait. No HTTP requests, sampled renderer or front pixel acceptance. A
GPU child was sampled, not proof of usable GPU compositing. No visible browser
window was observed: an HWND-bound observer has no proven binding opportunity.
The actual terminal
front is black plus pointer, NOT the historical Edge white front.

Parent independently inspected failure-front.png (ac1026b75415f141637184f89ed2b790f8447a7d5059207b302e8fbe4425aa59),
all37 TAR member/extracted/execution hashes and archiveSHA
6add3fb82b90d7f936c10789596111151637a0e0de9d72ce8f289ae79679ba66.
All15 deployed input hashes,258 runtime hashes and19 binary pins equal immutable
prep; privateLO before/after identical, actual inner/outerVirGL, guest free
16947441664 bytes,650/435 rawUTC gates passed. Exact QMP graceful shutdown,
QEMU0/Windows controller0 and own Linux launcher/QEMU/Xvfb PIDs absent verified.
Controller38928 and Linux435266/435281/435268 independently absent after shutdown.
Procedure0/archive0 do NOT override consumer1. Preserve every original byte.

## Why this diagnostic

Read-only Git-object inspection of actual pinned Wine4315be83 in the D: Wine
object store: dlls/ntdll/unix/thread.c:86-90 get_unix_exit_code passes status
through except nonzero codes whose low byte is zero become1. abort_process:1508
uses _exit(get_unix_exit_code(status)); exit_process:1541 uses process_exit_wrapper.
A Unix exit127 therefore does not uniquely establish a full Windows status.
Do not infer command-not-found, missing import, sandbox failure or graphics fault.
Exact upstream Chromium source7d982409... chrome/common/chrome_result_codes.h
and content/public/common/result_codes.h were fetched read-only; ordinary result
enums do not provide a127 diagnosis. This is not proof that arbitrary Chrome code
cannot return127. Primary sources:

- https://chromium.googlesource.com/chromium/src/+/7d9824099030f5c28c939ba08654562faaad7c09/chrome/common/chrome_result_codes.h
- https://chromium.googlesource.com/chromium/src/+/7d9824099030f5c28c939ba08654562faaad7c09/content/public/common/result_codes.h

## Offline implementation authority ONLY

Parent plans/reviews; reuse GPT-6 Luna Dirac, no duplicate. Explicit C: graphics
worktree, codex/g4-r1-completion. D: master and both submodules untouched. No
production patch, debugger/attach/suspend, token changes, browser rerun or boot.

Implement the smallest explicit Chromium-only native launch/wait helper alongside
existing runner patterns, NOT a second browser framework. A tiny native parent
creates ONLY the exact pinned child using ordinary CreateProcessW, creation flags0,
no handle inheritance, unchanged inherited environment, holds the returned process
handle and waits. This changes launch parentage: explicitly an instrumented
diagnostic, NOT a pristine comparison. No visible HWND is required.

1. Validate the complete fixed normal Chromium argument schema, exact retained
   staged full image/hash-manifest binding, nonce loopback HTTP URL and fresh
   owned profile/log paths. No arbitrary executable/flag passthrough, shell,
   suspended/debug creation, privileges, job/token changes or PID scan fallback.
   Set lpApplicationName and correctly quote the mutable Windows command line.
   Verify returned PID/full image/creation time from the held handle; close the
   primary-thread handle. Early exit before identity query is an unresolved race,
   not successful observation. No Windows/Linux-ID mapping or arbitrary process
   access. Preserve raw failed creation/query records.
2. Use only query/wait operations on the returned process handle, no inheritance/
   debug/memory access. Record a flushed creation/binding event before waiting. Use
   WaitForSingleObject with a bounded deadline then GetExitCodeProcess on THAT
   held handle. Preserve unsigned32 decimal AND hex, creation/image identity,
   signaled/timeout/error distinctions and monotonic times. STILL_ACTIVE259 is
   not necessarily liveness: a signaled process may actually exit259. A late
   missing HWND is irrelevant, not grounds to replace the handle. Native child
   status must remain distinct from the helper's own diagnostic exit status.
3. Independently record Linux helper-Popen PID/start/full argv/prefix and poll/wait exit
   result immediately when observed, BEFORE cleanup signals. Record cleanup-start
   event so natural-exit vs observer/cleanup-induced observations remain separate.
   The Linux helper Popen is NOT the native browser child; never substitute its
   code/PID. Browser role samples stay separate. Preserve original primary
   failure, failure-front and raw flushed native events even on parsing failure.
4. Retain original fixture, normal HTTP/app flags, privateLO, actual VirGL and
   full runtime/source/binary pin checks. No disabling isolation or security to
   get a result. This is always nonacceptance diagnostic, not a repeat baseline
   promoted to rendering/G4 acceptance. Bound observer discovery+wait inside
   existing total/cleanup budget; always reap observer before wineserver teardown.
5. Add executable unit tests: malformed/extra args, wrong image/nonce/profile/
   quoting; exit before identity query; create/query/wait failure;
   timeout/missing/flushed-binding event; full-width0xc000007f versus127 and actual
   exit259; Linux identity drift, original error preserved, cleanup order/observer
   kill/reap, log quota and no sandbox/feature/workaround flags. Exercise actual
   decision logic, not source text assertions. Compile native helper warning-clean.
   Plan one tiny owned native child calibration to distinguish full-width status
   and Unix truncation; no calibration or Wine execution until parent review.
6. Fresh immutable PREPARED_NOT_RUN inventory: all current sources/helper hashes,
   retained staged runtime reuse (no redownload or recopy/ZIP extraction), updated
   native-helper hash, exact calibration and diagnostic argv/run names/space budget/
   storage/cleanup. Inspect actual space BEFORE any new guest transfer/profile;
   no deleted evidence. Parent separately reviews tests and manifest before
   authorizing calibration, then separately before ONE live Chromium diagnostic.

Commit AND push offline code/tests/report. STOP for independent parent review.
No second browser/version/Edge arm, symbols/source build or retry authority.
If held-handle identity cannot be proven or native launch changes the failure,
report that limitation; do not silently broaden discovery. A native
exit status narrows the next question but does not by itself establish cause.

Keep95% five-hour/99% weekly stopping policy and heartbeat enabled; no credits.
All private VM/cache/profiles/evidence remain D:. No master merge or other worker
VM/checkouts/OOBE/security changes. Historical Edge and stale/blanking failures
remain open; earlier scoped Electron/WebView2/pixel passes remain valid.

## Narrowed next handoff after helper1a48edc8

Parent independently accepted native helper/shared host tests and warning-clean
PE compilation. Complete ONLY OFFLINE calibration runner/tests/preparation next;
defer Chromium runner integration until actual native API calibration is reviewed.
No VM/SSH/Wine/browser/calibration execution in this handoff.

- Reuse existing ownership, isolated execution, compositor/backend, pin/space,
  bounded operation and cleanup patterns. Do not build a general framework or
  change old runners/defaults. One explicit native-exit-calibration control in
  existing private procedure is sufficient. Never invoke WineDbg or stack code.
- Fixed future run native-exit-calibration-r1-20261023; original child in its
  fresh stage/original/exit-child.exe, current helper alongside it. Use existing
  worker build PEs366c6928.../6087e3ef... only after rehashing all five source
  hashes/compile logs/build record. Do not use parent's separate compile outputs
  or rebuild/overwrite any executable during a probe. No vendor runtime staging.
- Before staging require >=1GiB guest free, idle retained Edge prefix, all19
  accepted binaries and fresh source/helper/seed/overlay identities. Keep exact
  unique sockets/privateLO/actualVirGL/650435rawUTC/900launcher90shutdown reserve.
  Expected local data/guest paths and exact launch/run/stop argv in immutable prep.
- Calibration invokes ONLY helper --calibration fixed image/events/nonce schema,
  never Chromium. Native child exits0xc000007f; helper success0 means observation
  success, child status must be unsigned3221225599/hex0xc000007f after signaled
  wait. Native process and helper IDs/creation/time/full image must agree across
  strict duplicate-key-free bounded JSONL events. Require exactly one bound and
  one exit in order, matching nonce/childPID/creation, monotonic event times,
  fixed flags/noinherit/inherited environment and actual expected command line.
  Missing/partial/extra/trailing/error/timeout/259-is-running substitutions fail.
- Independently bind Linux helper Popen PID/start/fullargv/prefix, observe and
  record code0 BEFORE any cleanup signal, not as native-child status. Record
  cleanup-start; reap helper first, then own prefix kill/wait, compositor/server
  and exact VM/controller shutdown. On exception preserve raw events/error,
  collect bounded logs, exact-identity kill/reap only owned helper; no broad
  process kill or error replacement. No remaining owned servers/processes.
- Tests must execute actual parser/runner decision paths including all malformed
  events/identity/status/time/command, failed or early helper exit, timeout/
  quota/space/hash/namespace drift, reaping and primary-error preservation.
  Existing shared-C unittest remains; do not inflate counts with duplicated tests.
- Fresh PREPARED_NOT_RUN inventory/source TAR/helper and two PE hashes, source
  hashes/environment19 pins/seed/newoverlay/budget/space/path/argv and unit results.
  No output/profile/guest mutation now. Commit AND push; STOP for parent review
  and separate ONE native calibration authorization. Browser execution is not
  authorized even by a later successful calibration. Preserve all failed arms.
