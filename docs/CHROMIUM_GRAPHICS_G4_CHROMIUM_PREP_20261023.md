<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium normal HTTP baseline — PREPARED_NOT_RUN

Offline continuation of CHROMIUM_PREP_HANDOFF and CACHE_R1, including parent's
explicit notice-format correction. No VM/SSH/guest staging, Wine/browser/installer
execution, alternate/download retry, symbols, credits decoder or source build.
Parent review required before ONE later baseline; preparation is not live authority.

## Additive cache review (original failure remains immutable)

Exact private cache `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/`
`chromium-1706509-r1-20261023.cache/`; new subdirectory `notice-review-r1/`.
Its cache-review.json SHA256
`7f1aeddeb700b6474a762ab9079ea61e35ba1347425d10d795e895baa2443013`.
Status PRIVATE_DIAGNOSTIC_CACHE_REVIEWED_NOT_EXECUTED references unchanged failed
acquisition.json SHA256
`fd7eebd5dcb30e9a66074b09135faef6d2d7f961c246f1035dd7a5c52145b995`.
Only the reviewed `No retained license/notices` failure is eligible; other errors
still fail closed. Independently rechecked identical ZIP size/published MD5/SHA256
`6bdfecb33dbfa7159e5666798bdc28c76347bef913573e899cb43930b78d26cc`, safe archive
inventory, all258 tree hashes/851,393,923 bytes, six x64 core/support PEs and three
exact D: FileVersionInfo rows156.0.8078.0. No redownload or re-extraction.

Fetched only exact7d982409... source LICENSE/about_ui.cc/credit_utils.cc/
about_ui_resources.grdp/win-archive-rel.json Gitiles TEXT, retaining and hashing
raw base64 responses plus decoded source bytes in that new subdirectory. Entire
original ZIP/runtime/resources.pak remain retained. Compiled credits NOT extracted
or inspected; root LICENSE is not claimed to cover every component, and no
redistribution/ISO clearance is claimed. Parent independently audited five raw/
decoded source hashes/roundtrips and matching original/review/runtime maps.

Exact adjacent earlier replay names (no extra `.cache` component):
`chromium-1706509-r1-20261023.version-replay.json` and
`chromium-1706509-r1-20261023.cache-audit.json`. Additive source-fetch logs:
`chromium-notice-review-r1.stdout.log` / `.stderr.log`.
Pinned generated runtime-manifest.json SHA256
`8fc037ca464bc22952c9f9d63447cf0feb7dcab07e20489cb2d3a4ef3e1a8ba7`.

## Small explicit adapter

New check-g4-chromium-headful.py calls the existing runner through one explicit
Chromium policy; Edge CLI/default paths/vendor suffixes and tests remain separate.
Normal Chromium requires privateLO + two-route original HTTP, forbids diagnostic/
thread/stack modes. Original637-byte HTML/scene.js unchanged (legacy WEXEDGE title
protocol remains). No arbitrary flags, security/GPU workaround, debugger or spoof.
Exact staged portable runtime/full Z: image alias backed by verified root mapping;
no invented C: alias or basename fallback. Chromium accepts only its own empty/
` - Chromium` suffix, never Microsoft Edge's. Before/after Wineboot installed Edge
payload/registration persistence is still checked, along with entire pinned
portable runtime before boot/after boot/after collection. Shared retained Edge
prefix, NEW portable tree and fresh profile: NOT a pristine-prefix comparison.

Existing strict WebGL1/2/FBO/renderer checks, actual native client/scale mapping,
fixed0/1/3s fronts with last two exact pixel verdicts, before/after capture checks,
backend/process/log/cleanup gates retained. Chromium additionally requires native
creation/top-level/full-image identity and exact independent Linux Popen PID/start/
prefix/full argv before/after collection; no Windows/Linux ID mapping. PrivateLO
after must equal before. Any browser error retains one bounded failure-front
before cleanup, preserving primary error even if capture fails. Normal Chromium
also enforces128MiB soft log quota in monitor/alive/final checks, without becoming
always-exit1 diagnostic mode. GPU compositing/effective sandbox/fullG4 NOT_PROVEN.

## Exact immutable preparation

`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/chromium-headful-r1-20261023.preflight/`
preflight.json SHA256
`a89d0103f4e3c74ffbe36af776f58ef2e226fcdf277c3682ba09857876024d28`;
source-stage.tar SHA256
`60e3ff9c9f95a7d8e6ad23f054b6d3f6a9a255a9486e2896ac541168529cfab9`.
Actual offline-audit.json verifies15 tar members =13 current sources + pinned ZIP
and runtime inventory, all source/archive bytes, five actual C: helper hashes,
two seed sources,19 exact capacity-accepted binary pins INCLUDING Edge/labwc/tools,
runtime258 hashes and absent future host run. source_commit records precommit
checkout HEAD5964b3a9; actual WIP inputs are pinned by per-file/archive hashes,
not falsely attributed to that commit. New code/report commit supplies Git history.

Existing ignored presentation-timing-trial.py has only explicit Chromium command/
inventory/staging/evidence branches; no new orchestration framework. All artifacts
D:, source/helpers C:, current system-capacity-r1-20261023.qcow2 and launcher SHA
`d96f6854f0360cfb03c24ea6092aa8b3930cc1075af5cfb8aadfdeac65efaf82` recorded.
Old prep records untouched (their prior helper hashes are historical, not reusable).
Future commands, recorded verbatim in manifest:

```text
pwsh -NoProfile -File C:\Users\Owner\.codex\worktrees\chromium-graphics-plan\WineExplorer\scripts\g4-virgl-launcher-session.ps1 -RunName chromium-headful-r1-20261023
wsl -d Debian --exec env WEX_G4_CONTROL=chromium-headful WEX_TIMING_RUN_NAME=chromium-headful-r1-20261023 WEX_TIMING_BOOT_NAME=chromium-headful-r1-20261023 python3 -B /mnt/c/Users/Owner/.codex/worktrees/chromium-graphics-plan/WineExplorer/build/g4-virgl-20261022/presentation-timing-trial.py run
wsl -d Debian --exec python3 -B /mnt/c/Users/Owner/.codex/worktrees/chromium-graphics-plan/WineExplorer/scripts/stop_g4_virgl_launcher.py --identity /mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/chromium-headful-r1-20261023/launcher-identity.json --timeout 60
```

Future guest minimum4,431,218,372 bytes BEFORE transfer/create (ZIP + expanded
tree +3GiB reserve); actual run must check, not assume last maintenance free16.9GB.
Verify19 binaries +15 deployed source/archive files; bounded safe extraction into
fresh vendor/extracted (120s/125s SSH), then hash all258 guest runtime files BEFORE
consumer. Runner separately requires >=1GiB remaining. RawUTC650 before preflight,
435 immediately before consumer, actual child PID/start/NEWdrive,900s launcher/
90s shutdown reserve,285s runner/55s tail/300s SSH and separate identity SIGINT/QMP
stopper remain mandatory. Archive errors retained separately; no consumer means
no finally archive SSH. Live shutdown acceptance must explicitly require QEMU0
AND Windows controller0, not stopper0 alone. No deletion/reinstall/retry permitted.

## Actual focused verification

From explicit C: worktree WSL scripts directory:
`python3 -B -m unittest test_g4_chromium_cache test_g4_chromium_headful test_g4_edge_headful test_g4_edge_http_fixture test_g4_edge_owned_threads test_g4_virgl_budget -q`
=>158 tests PASS/no skips,5.985s. Imported Edge TestCase is not exposed twice;
earlier development195 count included duplication and is superseded by this run.
Original Windows Node test_g4_edge_scene.js =>PASS actual GL/FBO/final colors,
two-frame/timeout/API/error/context-loss negatives. No unchanged full graphics/
socket/crosscompile reruns; native observer source unchanged.

New executable negatives include review wrong failure/SHA/ZIP/tree/missing/extra/
symlink/source fetch/base64/version failures; Chromium vendor suffix/full-image/
nonce/native creation/geometry, independently restored runtime faults, PE even
with matching synthetic inventory, URL/profile/flags, missing Linux identity/
changed owner/privateLO, timeout/capture/cleanup/quota failures, primary/failure-
front retention, and private procedure extraction/runtime-hash-before-consumer.
Synthetic tests use no real network, browser, guest or cache mutation.

Stop for parent code/cache/manifest review. C: branch only, pre-existing `M wine`
unstaged; D: master/other agents/submodules untouched. No actual baseline result.

## Independent parent review / ready for ONE baseline after reset

Parent read adapter/common-runner changes, cache/review code and tests, and
explicit private procedure additions. Requested/fixed normal-mode failure-front
and quota coverage, independent negative-test restoration, nonduplicated Edge
TestCase discovery and actual before/after namespace mismatch coverage.
Independently passed158 focused tests/no skips in6.103s and the complete original
Windows Node fixture control. Native observer and production binaries unchanged.

Parent independently hashed ZIP and all258 extracted files; additive review's
five raw/decoded source hashes/roundtrips and exact three version-resource paths
pass, original failure record unchanged. Runtime inventory8fc037ca... matches
adapter. Independently verified all15 TAR members/current inputs, five current
helpers, two seed sources, archive60e3ff9c.../preflighta89d0103... hashes,19 pins
against accepted capacity and258 runtime entries. Correct new overlay/C:source/
D:private storage, normal HTTP/privateLO argv and runtime paths; future run absent.

OFFLINE preparation is accepted. Do NOT execute this window: nearing95% usage
cutoff. After reset, with fresh usage/space/source/helper/manifest/identity checks,
ONE exact prepared Chromium baseline is authorized. No second build/retry,
debugger/symbol acquisition/alternate URL or additional Edge arm. Preserve every
failure and actual failure-front. Always exact-identity shutdown and explicitly
check QMP/QEMU/Windows0; archive/consumer results and actual pixel proof remain
separate. Parent reviews live evidence before deciding the next experiment.

This normal baseline intentionally does not add the old Edge diagnostic arm's
file/SEH/vmodule tracing. Thus source revision/vendor AND diagnostic overhead
differ from those historical arms; a result alone cannot establish vendor-only
causality. Effective sandbox and full GPU-compositing/G4 remain NOT_PROVEN.
