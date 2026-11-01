<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Installed Edge headful r1: readiness timeout, cause unproven

Executed ONE unchanged reviewed `edge-headful-r1-20261023` attempt. **FAILED**:
`TimeoutError('No nonce-bound ready browser window')`, consumer SSH exit1.
This is a startup/readiness failure, **not a graphics failure or sandbox-cause
diagnosis**. No retry, offset adjustment, fallback, security bypass, production
patch or additional browser attempt. No unchanged tests rerun.

## Actual checks and evidence

Preboot archive/all 9 members/current sources, 5 host helpers and installed
provenance reference matched the reviewed preparation. Archive SHA256 remains
`59002220800061d7847c27c9429d7ca407079b7eb2eeab57930a3f00c0744b4d`;
diagnostic source `18363266`. Paths were fresh; no QEMU process running. Existing
lab key matched public seed, copied mode600 without displaying private contents.
Only the owned D:-backed guest was booted. Initial SSH readiness reset during
boot; next read-only check succeeded. No consumer retry.

Exact launch/run/stop commands are unchanged in the accepted preparation manifest
(EDGE_HEADFUL_PREP). Actual run command:

```powershell
wsl -d Debian --exec env WEX_G4_CONTROL=edge-headful WEX_TIMING_RUN_NAME=edge-headful-r1-20261023 WEX_TIMING_BOOT_NAME=edge-headful-r1-20261023 python3 -B /mnt/d/DeepThought/Projects/WineExplorer/build/g4-virgl-20261022/presentation-timing-trial.py run
```

Raw-UTC 650/435 guards passed with **845.692215/843.347409s** remaining. Guest
free bytes **4226936832** before transfer/creation (>1 GiB). All 17 expected
binary hashes (16 stack + installed Edge) and 9 deployed source hashes matched.
Owned host acceleration and inner EGL/outer GLES2 VirGL checks passed:
`virgl (D3D12 (Intel(R) UHD Graphics 750))`. Observer compiled successfully;
SHA256 `fcc0126c30448b6ea389c02cfadcee5cf53c5f7135963b9acbbdd26148e6193f`.

Wineboot exit0. Before/after relevant payload inventory hashes match: pinned
msedge.exe `bb74db4c753133706f4ab0a483a87aa28a90ddd6eeb305fe001376ba73a40fc3`,
x64 COFF0x8664; msedge.dll and msedge_elf.dll also persist with matching hashes.
Microsoft Edge registration **156.0.4314.42** and exact C:/Z: mappings still pass.
The on-disk hive snapshots/hashes are identical and retain actual
PendingFileRenameOperations text, including the Edge directory. These are
recorded on-disk observations, not proof that the live queue was processed or
not processed; no pending operations were edited. Version remains registration-
sourced, not an independent PE-version claim.

One browser launch used the original offline file URL and NEW private profile.
Chromium log line281 records `FileURLLoader::Start` for the exact fixture URL
and nonce `d959fcf851339aa6294812cebc9330a6`. It does not prove the document/JS
executed. Across **130** native observations, one visible Edge-owned
Chrome_WidgetWin_1 (Win32 PID432/HWND262232) retained the title
`_/Z:/home/debian/wex-g4/edge-headful-r1-20261023-stage/scripts/g4-edge-probe/index.html`.
Window=(10,10,1050,700), client=(14,10,1042,696), fixed scale65536 (1.0), exact
C: executable owner. No nonce readiness title or page/viewport/readback verdict
arrived. Geometry alignment against the page could NOT be evaluated.

**287** process snapshots include CrBrowserMain, utility processes and sampled
GPU role Linux PID1067/start_ticks7717 (`CrGpuMain`, actual `--type=gpu-process`
argv). Browser launcher Linux PID1008 is recorded separately from Win32 PID432;
no PID equivalence is claimed. No renderer role was sampled and no
`--type=renderer` launch was found in the retained browser process trace; neither
proves the cause or absence of every possible short-lived process. Chromium
logging exists; known fatal/GPU-exit/renderer-exit/graphics classifiers report0,
but other optional API/overlay/network warnings/errors remain. Do not infer
causality from an isolated stub. Browser GPU compositing remains NOT_PROVEN.

Readiness timeout after the bounded45s wait; complete diagnostic77.271660s.
**0 front PNGs**: the fixed capture schedule was never entered. There is no
failure-front image in this harness/run, and no pixels/readiness/WebGL acceptance.
No incidental screenshot or live adaptation was added. Parent plans a separate
bounded startup diagnostic, including a failure front, after reviewing this run.

Evidence root:
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/edge-headful-r1-20261023/`.
`edge-headful-evidence/` retains results, before/after persistence, full browser/
Wine/compositor/native EGL logs, all native-window stdout/stderr and process
snapshots. `execution.json` retains command, deployment hashes, guards and all
**280** archived-file hashes; independent rehash found zero mismatches. Archive
exit0, SHA256 `901460ddf39889f1c66c790bdf5384981acdbb8ffe66fbf16d2c354381993c9d`.
`review-summary.json` records execution separately from preserved preparation.
Installed prefix and new failed profile retained on the D:-backed guest disk;
no prefix/profile/vendor payload copied into Git or deleted.

## Exact owned shutdown

Prefix cleanup kill/wait0, browser exit-after-cleanup0; remaining servers=[] and
prefix processes={}; no cleanup/monitor error. Separate identity-validated SIGINT
stopper exit0: launcher PID359/start_ticks40220, QEMU374/40266, Xvfb361/40231.
Final launcher results attest signal-driven graceful QMP powerdown, QEMU exit0
and both children stopped; serial log ends in filesystem sync/ACPI S5/powerdown.
Read-only post-stop /proc check found none of those three owned PIDs.

Persistent Windows WSL PID35656 started **00:46:09.7787309Z**, exited0 at
**00:48:47.3692099Z**; controller exec also exited0. Session proof is
`../edge-headful-r1-20261023.wsl-session.json`. Shutdown about158s after start,
well within900s with90s cleanup reserve intact. No other VM touched.
Return for parent diagnosis/review; no further run or production change.
