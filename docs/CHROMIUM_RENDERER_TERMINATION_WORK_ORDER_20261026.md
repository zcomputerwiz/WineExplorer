<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Antigravity: get past Chromium's killed-page failure

## Parent review and current evidence

Runtime-unblock0206d03c and report4b1c31fc imported as95f26352/bced4ab6.
Parent read runtime/mode/view diff, actual native journal/consumer result,
selected Chromium and Wine trace sections and personally viewed terminal-front.png.
Independently33 focused runtime/native/view tests passed under Debian,8.381s,
0 skips. Required258-file presence plus surplus debug.log clears old file-set
blocker; missing payload still fails. Historical hash differences remain warnings.

Actual front is Chromium's rendered Aw,Snap page, RESULT_CODE_KILLED. This is
useful UI progress, not successful fixture rendering/WebGL/sandbox acceptance.
Main220, GPU568 and renderer processes appear in logs. HTTP requests remain
empty. Native journal records WAIT_TIMEOUT258 after45000ms; consumer result
error is ValueError('Linux helper failed/exit mismatch'), not natural helper0.
Inner/outer virgl are recorded. Do not infer successful Chromium compositing or
shader execution merely from separate EGL controls or a started GPU process.

Report causal claims are too strong: sad_tab and page_discarding messages do NOT
prove performance manager caused the earlier termination, nor do nearby
ProcessHandleTable/mitigation warnings prove a fatal path. First network service
also crashes/restarts at23:54:04.196. Browser reports killed page23:54:20.830;
renderer916 continues logging afterward. Earlier renderer PIDs02c4/02cc have
handle-table warnings around Wine52.158, while0394 has them at67.621. Map actual
roles/lifetimes, don't label every renderer the killed page or every warning fatal.

Real mode-wiring omission: new runtime-unblock is absent from both
MODE in ('userenv-r4','userenv-r6') strip_wineprefix selection and
elif MODE=='userenv-r6' metadata probe dispatch. Consumer result has NO api_probe
record. Four DLLs mounted does not prove21-check probe ran. This is a small
functional comparison correction, not grounds for a harness redesign.

## Task: narrow cause, actual fix, next real page control

Use own D: NEW codex/chromium-renderer-unblock from this parent commit; existing
runtime-unblock checkout may be reused after preserving its committed branch.
First correct the two mode family omissions (and any corresponding fresh mode),
add a focused execution-path test proving probe and compositor option selection,
and correct report conclusions. No repeat of accepted full unit matrices.

Analyze retained logs FIRST and exact pinned Chromium source7d9824099030f5c28c939ba08654562faaad7c09:
renderer startup/sandbox lowering/handle closer, network-service restart and the
code paths producing termination result. Cross-check actual used Wine4315be83
plus candidate metadata modules. Optional API lookup/stub output alone is not a
reason to implement it. Public upstream source can be researched. Keep clear
distinction between directly observed failure and candidate inference.

Goal: identify and repair the smallest real Wine incompatibility preventing the
page from surviving/loading. A narrow functional patch with focused native/PE
tests and ONE fresh normal browser comparison is authorized if the causal path
is supported. If retained logs cannot distinguish the kill reason, use ONE
targeted fresh diagnostic with narrowly scoped process termination/status traces
or existing Chromium logging; no debugger attach, privilege/sandbox/feature
bypass, generic tracing framework or repeated blind trials. Preserve earlier
failed evidence. If only a diagnostic is justified, report useful exact result
and next small candidate, rather than pretending a speculative patch is a fix.

If the blocker actually reaches missing lowbox/restricted/capability handling,
reuse/review the EXISTING Claude Token Security branch (historical root97d534f8,
Wine4fa6f4ac; inspect current refs) on a dedicated integration branch preserving
both histories. User authorizes that integration. Do not implement a second token
stack or replace the graphics Wine pin wholesale. Combined Wine changes require
own D-backed clean combined client/server build and focused relevant functional
tests before real consumer. No unrelated UAC/caller/security conformance matrix.
Do not rebuild old dirty/shared graphics build or alter another worker checkout.

Keep existing owned D: R4 VM/runtime, original HTTP fixture/eight normal flags,
accepted metadata DLLs unless the justified fix requires compatible combined
build. Fresh stage/output and matched native helper contract if another control
is needed; minimal output-only helper rebuild permitted. No new image/download/
reinstall, no consumed-path overwrite. PrivateLO/unique filesystem sockets,
actual backend, readiness/free space/prefix idle, current budgets and exact owned
shutdown remain. At>=95%five-hour finish bounded cleanup/checkpoint/push/defer;
weekly>=99%stop, never credits. No master merge/OOBE/other VM changes.

Commit AND push original code/tests/report docs/CHROMIUM_RENDERER_UNBLOCK_20261026.md
with exact supported cause/evidence, correction, test outcomes and actual next
browser result. Private screenshots/runtime/logs stay D:. Reply codex then stop
for parent review. Prioritize real Chromium behavior, not testing perfection.
