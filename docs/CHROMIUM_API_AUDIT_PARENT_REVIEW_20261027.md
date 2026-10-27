<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Parent review: Terra requirements audit accepted for Wine assessment

Reviewed Terra7f15743645d8f601c63cb793dfc36cb925fc46a4, two documentation
deliverables and27 prioritized rows. Accepted as a pinned source requirements
map, not runtime coverage proof, a renderer-kill diagnosis or Wine sufficiency.
No VM/API/consumer tests were requested or run as part of this source review.

Independently fetched exact Chromium7d982409 source and sampled critical citations:
handle_closer_agent155–193 confirms the ProcessHandleTable uint32 handle array
and failure branch; target_services126–154 confirms conditional LowerToken fatal
branches; platform_channel55–117 confirms pipe open attributes and the required
ERROR_PIPE_CONNECTED result; message_pump_win927–955 distinguishes timeout from
failed completion; platform_shared_memory_region_win56–72 checks NtQuerySection
SectionBasicInformation/SEC_IMAGE; tcp_socket_win1087–1157 distinguishes ignored
optional tuning from WSAEventSelect/nonblocking connect/FD_CONNECT handling.

Those sampled source facts agree with the audit. Other citations were checked by
Terra, not independently re-read exhaustively by parent. Build features/Finch,
selected ANGLE backend/DEPS revision and actual per-child executed paths remain
unresolved as documented. Requirements are conditional, not a huge API checklist
whose every gap blocks a local page. Source paths are useful precise leads for
the next Wine assessment.

One high-value assessment detail: row19's real TCP control must exercise
WSAEventSelect/nonblocking connect/FD_CONNECT semantics, not reuse a successful
blocking TCP GET probe as proof. Existing successful TCP bytes and UDP ECN warnings
are different scopes. Row10 is ProcessHandleTable's uint32 array here, not the
different process-handle snapshot structure. Do not substitute the wrong layout.

## Antigravity dispatch supplement (read with Terra's assessment work order)

Parent authorizes docs/CHROMIUM_WINE_API_ASSESSMENT_WORK_ORDER_20261027.md now.
Use NEW own D: documentation branch codex/chromium-wine-api-assessment from this
parent review commit. NEW files only:
- docs/CHROMIUM_WINE_API_ASSESSMENT_20261027.md
- optional small machine-readable assessment table in docs/ with same prefix.
No edits to current control-channel source/builds/runners/private adapters.
Do not reuse/switch an active agent's checkout. Read combined Wine sources via own
read-only Git objects (actual hashfffa7de0df386ae16c4d47b1d0e450d8b7fbcbd3);
no checkout/submodule update in another worker's repository. Diagnostic source
children are evidence references, not a separate API base unless behavior changed.

This assessment runs in parallel via a separate source-only local delegate if
available, or queues behind the current control-channel trial. That trial remains
the critical path: do not stop it or duplicate its instrumentation. No new VM,
API/probe/browser execution, build, implementation patch or API-success stub in
this assessment. Do not implement the entire table or add a test harness.

Cover all27 rows with honest statuses; prioritize source depth for P0 startup,
sandbox/handle format, pipe/IOCP/sections and nonblocking TCP. Conditional graphics
can remain implemented-unverified or inactive/unknown with clear reasons. Inspect
actual existing test cases/results for contract relevance, do not just cite total
pass counts or require a new full unit matrix. Distinguish source implementation,
tested scoped semantics and actual Chromium reachability. Historical file/version/
hash differences remain diagnostics, not gating paperwork; build/source association
must be honest, not necessarily byte-identical across unrelated prior consumers.

Final report: per-row wrapper→NT→Unix/server/WoW64 mapping, precise source links,
status, existing evidence and missing fact, followed by AT MOST1–3 concrete ranked
fix candidates/next functional checks. Zero supported fixes is a valid conclusion.
Exclude optional warnings/ECN/full UAC/security hardening from blocker status unless
active behavior justifies it. Preserve Wine's internal control-channel vs Win32
Mojo pipe distinction. No claim that source audit explains the actual killed page.

Commit AND push the original assessment report on your own branch; reply codex
with exact commit, status counts, strongest1–3 candidates/unknowns and limitations,
then stop for parent review/selection of implementation. No master merge or other
worker/VM changes. All caches/private evidence D:/D-backed WSL.95%five-hour safe
checkpoint/push/defer,weekly99%stop,nevercredits. Follow user priority: real Chromium
functionality, not exhaustive API/security certification or perfect tooling.
