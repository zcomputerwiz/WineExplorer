<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# SID implementation/guest proof checkpoint

Wine e096ae4373d778fde4a876f94798711f369e7013 implements the demonstrated
DeriveAppContainerSidFromAppContainerName gap. Parent reviewed code,87 build/unit
artifacts and independent fault test; real x64/i686260 assertions each pass0fail/
0skip (one old unrelated todo). Parent additionally reviewed57 normal-builtin
route artifacts/assertion/loader logs181261e3. No root Wine gitlink import yet;
sparse Wine worktree C:root/build/wine-userenv-appcontainer, branch
codex/g4-userenv-appcontainer pushed. Existing M wine remains untouched.

R3 prep b14c1cfa accepted after parent68 tests34.783s/no skips,49 artifacts/53
inputs/21TAR/258 runtime observations. ONE live R3 result ae8890a1 reports actual
normal-route API probe45/45 passed in guest private readonly module view, outside
physical DLLs unchanged, strict QMP/QEMU/Windows shutdown. Parent READ report but
has NOT independently audited new47-file seal/18-file archive/API JSON yet.
Start next cycle with that review; no new boot/calibration/VM layer needed.

Browser DID NOT LAUNCH. Probe intermediate cleanup kill0/wait0/servers empty,
but graphics_resources.sample(PREFIX) included PID937/start3373/name labwc,
exactly owned compositor. Guard stopped before HTTP/browser/native-parent phase.
This is a test-harness ownership-scope problem, not an API/browser failure.
Private root D:lab/chromium-userenv-r3-20261024 and .hostaudit preserve everything;
archive acc3445accc9be46c802914c68d93ee8fd10e5e8cd84bb9b109d15d237080b03.
R3 output/stage now exist: preserve, never overwrite or resume as fresh evidence.

NEXT high-value fix: compositor is a native Linux process; OwnedWexcomp.start
copies self.environment into local Popen env, inheriting WINEPREFIX. The metrics
sampler intentionally includes any matching marker, including compositor, so it
cannot directly serve this intermediate Wine-only idle assertion as currently
configured. Prefer an opt-in for the NEW control to omit WINEPREFIX from ONLY
the compositor child's local Popen env (preserve older metrics defaults). Keep
caller Wine environment/prefix and shared Wayland endpoint updates intact. Do
not broadly ignore arbitrary PIDs or weaken actual Wine/server leak detection.
Test an actual native child env plus genuine marked-owner detection, caller env
retention and existing endpoint setup. Inspect actual class before implementing.

Plan/reuse Luna for bounded offline fix and fresh R4 mode/stage/output/profile/
output-only parent helper/preparation on SAME R4 disk, same reviewed SID DLLs/
probe/runtime. No new SID implementation/rebuild/calibration/layer. Parent reviews
new code/tests/prep before ONE browser diagnostic. Keep API probe and intermediate
cleanup, private view/normal loading, normal security flags and assertions.

Full Chromium/Edge/G4 rendering remains unproven. Source API computes identity,
not profile/token enforcement. User's historical IDs/hashes logging policy stands.
Only C: graphics root, private D: data/D:-backed Debian; mainDmaster/other workers
unchanged. Reuse completed Luna01a163b6-80db-7cb1-9ab3-7687bbd04067. Allowned
processes reported stopped. No master merge/bypass/spoof/proprietary publication.
98% five-hour/92% weekly: stop now. Reset2026-10-25 02:38:20UTC/wake02:42UTC.
No credits consumed. Continue until weekly99%;95% five-hour checkpoints remain.
