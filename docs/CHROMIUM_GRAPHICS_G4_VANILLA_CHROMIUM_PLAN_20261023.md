<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Windows Chromium first: approved diagnostic baseline

User approved this change of direction after checkpoint8149b103. This plan
supersedes the proposed next Edge navigation-trace experiment. Preserve all Edge
evidence and the installed Edge runtime; Edge remains an eventual acceptance
target. Do not repeat the failed Edge arms or start tracing them first.

## Order and ownership

Parent plans/reviews; reuse GPT-6 Luna Dirac01a163b6-80db-7cb1-9ab3-7687bbd04067
after inspecting status. All tracked work in C: graphics worktree on
codex/g4-r1-completion; D: main checkout/master and unused Wine gitlink stay
untouched. All browser files, profiles, VM storage and evidence stay D:.

At plan creation usage95%five-hour/60%weekly: checkpoint only, no new agent/run.
Continue after reset2026-10-23 15:27:29UTC; existing wake15:30UTC. Never use credits.

First finish the OFFLINE capacity execution preparation described in
SELECTED_STACK_CAPACITY_CHECKPOINT and CAPACITY_PLAN_20261023. Parent review
before image creation or the first writable larger-disk boot (automatic GROWROOT
IS mutation). Original backing chain/evidence preserved. Current free~692MiB
does NOT authorize staging another runtime/profile. Capacity StageA13 tests/
actual GPT replay passed; StageB still NOT_READY, no overlay created.

## Bounded implementation after capacity is accepted

1. Select ONE upstream Windows x64 Chromium ZIP snapshot from the project's
   [official download path](https://www.chromium.org/getting-involved/download-chromium/),
   preferably near Edge156's Chromium generation. Verify availability and record
   exact platform/build position/version/source revision mapping where provided,
   official URL, size and SHA256. Do not invent an exact Edge upstream revision
   or assume version similarity makes equivalent builds. If no nearby build is
   available, report the actual mismatch for parent review before substitution.
   No third-party repacks/ungoogled/modified builds and no Linux Chromium: this
   tests the Windows ABI/Wine route. No full source build at this stage.
2. Download/cache privately on D: with bounded size/time, inspect archive paths
   against traversal/symlinks/absolute names, reserve space before extraction,
   retain license/notices and verify expected x64 browser/support files. SHA256
   records reproducibility, not vendor authentication by itself. Preserve ZIP
   and manifest; no payload committed/bundled or host browser installation.
3. Add a small explicit Chromium adapter to existing isolated consumer controls,
   not a rewrite or silently broadened Edge acceptance. Preserve existing Edge
   tests/default behavior/identity checks. Exact chrome.exe full image/version/
   hash and fresh nonce/profile bindings replace only vendor-specific identity.
   Pin all accepted Wine/compositor/core sources/binaries and deployed helpers.
4. Use the SAME original637-byte HTML/scene.js/local HTTP transport/app-window
   mode/fixture assertions, private loopback-only network namespace, accelerated
   VirGL backend and geometry. No feature disabling, no-sandbox, GPU workaround,
   user-agent/version spoof, component install or remote-debugging endpoint.
   No new debugger attachment; the last completed stack diagnostic is not part
   of this comparison. Native/Linux identities remain distinct and fail-closed.
5. Test adapter paths, wrong PE/image/version/nonce/profile/owner, missing support
   files, hostile ZIP entries, short/oversize downloads, insufficient space,
   forbidden flags, lifecycle/capture failure and inherited Edge behavior.
   Real fixture/node execution and strict pixel assertions remain unchanged.
   Presence of GPU child or absence of no-sandbox flag is NOT proof of effective
   sandbox enforcement; report actual available sandbox evidence separately.
6. Produce fresh immutable PREPARED_NOT_RUN inventory with exact runtime/source/
   helper hashes, launch/run/stop argv, D:storage and original fixture comparison.
   Parent independently reviews tests/source/manifest before ONE live baseline.
   Unique X sockets/private namespaces/explicit inner+outer VirGL/space and raw
   UTC guards/exact identity cleanup remain mandatory. Existing900s launcher/
   90s shutdown reserve/285s runner/300s SSH bounds are not extended silently.

## One baseline and interpretation

Capture actual front pixels, HTML/JS requests, nonce readiness, strict WebGL1/2
results, roles and clean shutdown; retain all failures/hashes privately. Never
call a white window, animation or API success graphics acceptance. Do not weaken
assertions to make the comparison pass. No automatic retry/second build.

- Chromium succeeds: establishes scoped upstream-browser compatibility on this
  build/fixture. Edge-specific integration OR revision/build differences become
  candidates, not a proven cause; next compare intentionally after parent review.
- Chromium fails similarly: common Chromium/Wine boundary becomes a stronger
  hypothesis, not established merely by matching white output. Review actual
  evidence and choose targeted navigation tracing or a symbol-enabled source
  build next, with a separate resource/storage plan. No giant build now.
- Different failure: report that difference before selecting another experiment.

Neither outcome proves full Edge, effective security hardening, all lifecycle
cases or complete G4. Earlier Electron/WebView2/x64/i686 controls remain scoped
evidence. No master merge or other workers' OOBE/token/VM changes.
