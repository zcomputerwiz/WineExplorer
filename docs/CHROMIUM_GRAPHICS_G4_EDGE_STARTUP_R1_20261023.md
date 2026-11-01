<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Edge instrumented startup r1: HTML read completes, readiness absent

Executed ONE reviewed diagnostic, unchanged `e29eb59c` source and preparation.
Primary outcome: `TimeoutError('No nonce-bound ready browser window')`.
Consumer exit1 is mandatory in diagnostic mode, not itself a product defect.
**No rendering/G4 acceptance or proven graphics/security cause.** No retry,
URL/backend/page/security adjustment, new download or production patch.

## Actual evidence

The original-file Win32 probe passed: size637, read64, expected original prefix.
More importantly, the **browser itself read the complete637-byte original HTML**:
`browser.stderr.log` lines59258–59270 show Wine PID0198/thread021c CreateFileW
on the staged index.html, returning handle0x938, ReadFile637 and
`NtReadFile = SUCCESS (637)` at Wine time73.224. Chromium log line397 records
FileURLLoader::Start on the same file URL/nonce. Do not call this an open/read
block. Successful reading still does not establish response/header/MIME delivery,
document/JS execution or renderer startup. No scene.js read or --type=renderer
launch was found in the retained trace, and no renderer role was sampled; these
are bounded observations, not proof of the blocker or absence of all transient
processes. Parent plans post-read/navigation diagnosis separately.

Terminal **failure-only** image was captured before cleanup, exit0,1280x720:
`edge-startup-evidence/failure-front.png`, SHA256
`b8b55298c5a3f734278690e823f8698bde6d15ef82dd5bbeb8a65674cf791f22`.
Actually viewed: full framed Edge window/caption, white client, no blue marker
or colored canvases. No fixture pixel verdict was applied. Its bounded capture
took0.115630s with a3s process timeout; no secondary capture error or retry.

Across40 native observations, visible Chrome_WidgetWin_1 Win32 PID408/HWND655478
retained the path-like title `_/Z:/.../scripts/g4-edge-probe/index.html`, exact C:
Edge owner, window(10,10,1050,700), client(14,10,1042,696), scale1.0.
No nonce readiness arrived, so page/client mapping and WebGL/front acceptance
remain unevaluated. Browser launcher Linux PID998 and sampled GPU role
Linux1056/start_ticks6868 (CrGpuMain, actual --type=gpu-process argv) are recorded
separately; no Windows/Linux PID equivalence or GPU-compositing proof is claimed.
No sampled GPU/renderer role exit before cleanup. Logging is intrusive: this is
an instrumented arm, not an unchanged-timing comparison with the first smoke.

Log quota checked40 times; combined post-cleanup browser logs **14353553 bytes**
versus134217728 limit, no quota stop/truncation. Full trace/logs are preserved.
Complete runner77.317565s, bounded45s readiness; no extension. Browser
exit-after-intentional-prefix-cleanup1 is recorded, not labeled a pre-cleanup crash.
Prefix kill/wait0, remaining servers=[]/prefix processes={}, no monitor/cleanup
error. Payload inventory and on-disk hive records match before/after ordinary
Wineboot; installed hash/registration and C:/Z: mappings pass. Pending registry
text retained, not cleared or interpreted as proof of live queue processing.

## Provenance, paths and shutdown

All9 archive/current-source members, sizes,5 host helpers and accepted installed
reference matched before boot. All17 guest binaries and9 deployed files matched.
Guest free **3658932224 bytes** before staging (>1 GiB); raw-UTC650/435 guards
passed with853.152273/851.380142s remaining. Host acceleration plus inner EGL and
outer GLES2 VirGL passed: `virgl (D3D12 (Intel(R) UHD Graphics 750))`.
Original helper compiled SHA256
`32826ff108fdda132fb7952ebece1c99b1c18b9f573b4c76af0fb074c2788e36`.

Exact manifest launch/run/stop commands were used (EDGE_STARTUP_PREP), including
`WEX_G4_CONTROL=edge-startup`, run/boot name `edge-headful-startup-r1-20261023`
and `--startup-diagnostic`. No diagnostic code changed during execution.
Evidence root:
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/edge-headful-startup-r1-20261023/`.
`execution.json` retains command/guards/hash/provenance; `edge-startup-evidence/`
contains results, complete browser/Wine/native/process/compositor logs, original
file probe and before/after persistence records, plus the terminal PNG.
All **105** archived-file hashes independently rechecked, zero mismatches;
archive exit0, SHA256
`a6e28aa393a505f9d534254803b16e11be24996f10829a1a575f20e679df8d1a`.
`review-summary.json` records actual execution separately from preserved
PREPARED_NOT_RUN history. Prefix/new failed profile retained on the D:-backed
guest disk; no vendor payload/profile/image in Git, no files deleted.

Exact-identity separate SIGINT stopper exit0: launcher374/start_ticks24230,
QEMU389/24282, Xvfb376/24241. Final launcher result: stop_reason=signal,
graceful QMP powerdown=true, QEMU exit0, both children stopped; serial ends in
filesystem sync/ACPI S5/powerdown. Post-stop /proc checks find none of those PIDs.
Windows WSL PID22820 started01:03:29.8247631Z, exited0 at01:06:10.7283003Z;
persistent controller exec also0. Session proof is
`../edge-headful-startup-r1-20261023.wsl-session.json`. About161s total, inside
900s with90s shutdown reserve intact. No other VM touched. No unit reruns for
this unchanged live dispatch. Return for parent review, no further run or patch.
