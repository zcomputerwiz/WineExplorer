<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Terra work order: native Windows navigation trace comparator

User explicitly requested a Terra worker run the same Chromium activity on the
Windows host to establish expected behavior. This is a disjoint native Windows
comparison, not a second Wine investigation or a prerequisite that interrupts
Antigravity's active navigation-phase task.

## Owner and isolated scope

Original Terra Meitner was not_found when resumed. Assigned Terra Lovelace
01a182d2-8a8c-7540-a62c-12b790dcb438 (gpt-6.1-terra, high), reusing the clean
D:/DeepThought/Worktrees/chromium-r5-preparation/WineExplorer checkout.
Create own codex/chromium-windows-navigation-baseline from current published
parent after fetching. Preserve the completed API-audit branch and ignored
R5/R6 assets; do not reset, clean, edit submodules or touch other worktrees.
All new private runtime/profile/log/trace/artifacts on fresh
D:/DeepThought/ChromiumLab/windows-navigation-baseline-20261028 paths.

Read CHROMIUM_NAVIGATION_PHASE_WORK_ORDER_20261028.md and current continuation
policy. Wine at8GiB has a blank target page, no fixture HTTP requests, one
speculative local resolution, background network activity. The service652 log
is not a process-death proof. Parent wants a native successful navigation phase
graph and its URLRequest/renderer/commit order for comparison.

## One native Windows baseline

1. Use the exact cached Windows Chromium snapshot1706509/version156.0.8078.0,
   source7d982409. Existing cache is
   D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/chromium-1706509-r1-20261023.cache/
   with runtime/, chrome-win.zip, acquisition.json, metadata.json and REVISIONS.
   Inspect/reuse privately; if extraction needed use NEW D: directory. Record
   actual executable/version/hash for identification, not a bureaucracy gate.
   No download/install/update, vendor publication or host default-browser change.
2. Serve the existing unmodified scripts/g4-edge-probe/index.html and scene.js
   through a host127.0.0.1-only ephemeral HTTP fixture with fresh nonce and request/
   readiness recording, matching the guest URL/path/nonce semantics. Use a NEW
   D: profile. Original browser arguments/app activity remain normal; logging-only
   trace flags are allowed, no sandbox/feature/GPU/renderer-limit switches.
3. Capture Chromium's existing bounded startup trace covering navigation and
   necessary loading/IPC categories for roughly35 seconds, supported switches
   verified against pinned source. Capture Default NetLog capped8MiB, duration
   integer40 (not40s), stopping/flushing before a45-second observation ends.
   Keep all logs/trace private. Prefer existing decoding/export tools, not a
   custom framework or full source build. No debugger attach/suspend, remote
   debugging ports, ETW/global system trace, global crash-policy/firewall edits,
   TLS-key/sensitive capture or inspection of unrelated host browser sessions.
4. Establish actual fixture HTML/JS fetch and nonce readiness. If successful,
   identify the target navigation track and ordered phase transitions/callbacks,
   URLRequest start/connect/response, renderer readiness/commit and first paint
   or presentation where recorded. Map process IDs using actual creation/log
   records; no count-based process identity guesses. Record whether652 occurs
   even in this successful native run. No claim of OS process crash from it.
   If the native page fails, report that actual result and stop; don't change
   configuration/flags or start a repeated matrix to force a pass.
5. Stop/reap only the exact owned browser process tree, HTTP server and helpers
   after trace flush, using fresh profile and actual PID/start identity. No broad
   taskkill/pkill, unrelated browser closure, deletion or cleanup of old evidence.
   Retain all failed attempts. A live browser45-second wait is not page failure.

The Windows host may have external connectivity and a different GPU from the
private-loopback VirGL guest. Record these environmental differences honestly;
do not alter host global networking or add feature flags to fake equivalence.
Focus comparison on target nonce URL and navigation/IPC order. Native success
does not prove Wine API or graphics correctness, nor pinpoint its missing step.

## Deliverable

Commit AND push original docs/CHROMIUM_WINDOWS_NAVIGATION_BASELINE_20261028.md
on own branch, optionally a small useful original decoding/launch recipe if
needed (no new harness project). Report actual outcomes, source/flag versions,
private evidence paths and compact trace-event comparison table with event names,
phase sequence and actual timestamps/IDs, limitations and at most1-3 targeted
comparison questions for Antigravity. Never commit runtime/vendor traces/profile
or proprietary payload. Notify parent with commit/files/results and stop.

No messages to other agents or changes to Antigravity's VM/Wine/build/runtime/
branch. Parent will review and forward the comparator. At95%five-hour safely
finish owned cleanup/checkpoint/push or defer before launch; weekly99%stop;
never consume credits. Prioritize a useful functional baseline over harness polish.
