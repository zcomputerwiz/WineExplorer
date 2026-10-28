<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Parent review: real guest OOM and one 8 GiB Chromium comparison

Reviewed delivered root e477a0b4 and the retained control-channel trial's actual
serial.log, in addition to the earlier native journal/helper/PNG/QMP review.
No new browser run or build was performed by parent.

## What is now established

Private D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/
chromium-control-channel-20261027/serial.log contains a real global guest OOM:

- Line929: kernel84.055919 wine_threadpool invoked oom-killer.
- Lines961-969: active_anon884654/inactive_anon66565 pages; free22255;
  normal zone free33248kB below min33656kB; all_unreclaimable yes.
- Lines983-985: free swap0, total swap0,1048442 pages RAM.
- Lines1031-1032: kernel87.900824 selects CrRendererMain Linux PID1183;
  kernel87.986467 reports Killed process1183, anon-rss748648kB.

This corrects the earlier idle-only assessment: normal idle memory APIs did not
exclude actual OOM under Chromium load. Guest memory capacity is now a supported
experimental variable. A small RAM-only comparison has higher value than another
Wine trace, speculative DXIL/API patch, or testing-framework round.

The report overstates exact identity: three CrRendererMain processes appear in
the task table. Matching one kernel OOM kill to one Wine violent-death flag by
elimination/RSS is strong circumstantial correlation, not a recorded Wine0268
to Linux1183 mapping. Browser Wine00dc/Windows220 maps to Linux1082 in the actual
NtTerminateProcess trace; the report's Linux220 label is wrong. Reported serial
line numbers also differ from the retained file. These limits do not block the
justified RAM experiment. No unsupported claim excluding every Wine fault is
accepted. Contingency Wineaa79f01 traces were not exercised in this trial and
need not be built, deployed or imported for the comparison.

API assessment c29b77d0 has arrived as source-only documentation, not runtime
acceptance. Parent independently confirms fffa7de ProcessHandleCount zeros the
returned count at ntdll/unix/process.c1368-1384. Whether Chromium's sizing/retry
path makes this fatal remains unverified. Static test source does not prove
complete Mojo/IOCP behavior or exclude IPC as a cause. Do not implement that
candidate, full EGL, or handle-list error refinements ahead of the OOM comparison.
Remaining matrix validation is a scoped follow-up, not another browser gate.

## Work order to Antigravity: ONE fresh normal browser at 8 GiB

Use the SAME implementer and own D: root branch codex/chromium-memory-8g.
Reuse the last actually deployed matched full combined Wine candidate from the
control-channel run (02d4ef2 diagnostic lineage over fffa7de). Keep its client,
server, modules and browser runtime identical; no Wine rebuild or new trace
deployment. Confirm actual preserved candidate availability first; previous
cache deletion is not permission to delete anything else or silently substitute
a different build. If reconstruction is unavoidable, use the exact existing
private artifacts and report it; no new code/build matrix.

Change only the owned QEMU launcher's RAM from4096 to8192 MiB for ONE fresh
session/control. Verify host/WSL available memory safely supports that allocation
and existing workloads; do not change global WSL settings or other VMs. No swap
installation, browser memory-capping/renderer-limit flags, feature changes,
sandbox bypass or image replacement. Keep normal original HTTP fixture/browser
arguments, existing owned R4 disk/prefix/runtime, actual VirGL/private loopback/
unique sockets, metadata probe and existing lifetime/cleanup reserves. Fresh
stage/output/profile/helper paths; no evidence/candidate deletion or path reuse.

Record actual QEMU argv and guest MemTotal/SwapTotal before the consumer. Retain
full serial/kernel OOM evidence through the browser interval; a compact guest
meminfo sample at start and end is enough. Reuse existing artifacts/results;
do not add a monitoring framework or global process scanner. If affordable
existing creation logs map Wine/Linux child identities, retain them; do not make
perfect identity instrumentation another prerequisite to the control.

Acceptance is the actual outcome: fixture index/scene.js and nonce readiness,
visible front, native held-handle status, kernel OOM count, clean owned shutdown.
Distinguish helper/browser lifetime timeout from page failure: a still-running
browser at45 seconds is not itself evidence of a failed loaded page. If the page
loads, preserve the result and report scoped Chromium success, without claiming
full Edge/G4/security acceptance. If it fails, report the new actual failure and
whether OOM persists; stop rather than increasing RAM repeatedly or introducing
swap/flags/API patches. No identical second run.

Only routine minimal fresh-mode/path/launcher changes are needed, following the
existing trial pattern; no extra offline preparation approval cycle. Preserve
all other ownership/space/raw-UTC/budget/exact QMP ACK+postrequest guest SHUTDOWN/
QEMU0/no-fallback/owned-child cleanup requirements. All private files on D: or
D-backed Debian; no main master merge, other worker/shared build edits, new
download/reinstall, debugger attach or security bypass.

Commit AND push source/recipe/report on own branch and
docs/CHROMIUM_MEMORY_8G_RESULT_20261028.md with actual RAM/page/OOM/cleanup evidence;
reply to Codex and stop. Check live usage before launch:95%five-hour defer unless
already launched, then clean up/checkpoint/push; weekly99%stop. Never credits.
