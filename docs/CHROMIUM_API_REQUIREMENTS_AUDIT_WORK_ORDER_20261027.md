<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# GPT-6.1 Terra: active Chromium API requirements and Wine assessment handoff

User requests an independent Chromium source audit to identify APIs actively
required, followed by a concrete work order for Antigravity to determine which
Wine implementations suffice and which need attention. This is a bounded
parallel research/documentation task, not another implementation/harness project.

## Owned checkout and scope

Reuse STOPPED clean D:/DeepThought/Worktrees/chromium-r5-preparation/WineExplorer
(old Luna53dfd244, no live agent). Verify status before switching. Create branch
codex/chromium-api-requirements from this pushed parent work-order commit,
preserving old branch/history and ignored private R5/R6 files. Do not initialize/
stage/update any submodule or touch builds/prefixes/private adapters. Parent C:
checkout/main D:master and active Antigravity checkouts are not yours to change.

Your tracked write set is ONLY these NEW files:
- docs/CHROMIUM_ACTIVE_API_REQUIREMENTS_20261027.md
- docs/CHROMIUM_WINE_API_ASSESSMENT_WORK_ORDER_20261027.md
Optional original machine-readable matrix under docs/ with the same prefix is
allowed if it helps, not a new tool/framework. All source caches/private evidence
on D: or D-backed WSL. Fetch a bounded public source subset as needed, not a full
Chromium build/large clone/runtime download. No VM/SSH/browser/Wine/probe execution,
compilation, code patch, token merge or messaging other agents. Parent reviews
and dispatches the Antigravity handoff after your delivery.

## Exact target and available evidence

Windows x64 Chromium snapshot1706509, version156.0.8078.0, source revision
7d9824099030f5c28c939ba08654562faaad7c09. Use this revision, not HEAD. Original
headful --app local HTTP startup fixture, fresh --user-data-dir, normal sandbox/
features, no-first-run/no-default-browser-check and ordinary logging; network
is private lo-only, actual guest VirGL via WineExplorer compositor. Full Edge
acceptance follows Chromium, do not expand into Edge updater/services now.

Wine current comparison base is combinedfffa7de0df386ae16c4d47b1d0e450d8b7fbcbd3
(graphics/SID/registrationcb1d117 + existing token historyae5fec1). Diagnostic
childa91ceea adds inert termination tracepoints. Native API implementation
assessment is Antigravity's NEXT task, not your duplicate audit of all Wine.

Read parent docs CHROMIUM_CONTROL_CHANNEL_WORK_ORDER_20261027.md,
CHROMIUM_ECN_REVIEW_AND_TERMINATION_WORK_ORDER_20261027.md and current continuation
policy. Existing candidate launches browser/GPU/network/renderers; metadata21/21
passes, but page gets RESULT_CODE_KILLED/HTTPempty. Server kill_process(violent1)
preceded later browser terminate; it can result from control-socket HUP/error,
not proof of a CPU crash. Antigravity is ACTIVELY investigating that path. Do not
duplicate their tracing or draft a competing implementation task for it.
ECN accept-ignore stubd629841 was rejected; UDPSocketWin receive ECN is different
from TCP option setup. Optional lookup warnings are not missing mandatory APIs.

## Source audit deliverable

Focus on20–40 highest-value API/NT-class or tightly related behavior rows spanning:
browser/child startup and attributes; sandbox token lowering/handle closure;
Mojo named-pipe/shared-memory/handle transfer/IO completion; renderer initialization;
TCP loopback navigation; DXGI/D3D11/DirectComposition/ANGLE/Skia critical paths.
Where Win32 calls depend on NT classes/server behavior, include that transitive
requirement rather than treating an exported wrapper as sufficient. Separate
native Wine control transport as an internal dependency, not a Chromium Win32 API.

Each row MUST contain:
- Actual API/NT information class/behavior, process role and trigger.
- Pinned source file, function and precise call-site permalink/line evidence.
- Conditional build/feature/default/runtime assumptions. A source call existing
  is not proof this exact stock build executed it; label confidence/evidence.
- Failure handling: checked fatal/launch-abort/required correctness; optional
  with fallback; informational/best-effort; conditional/inactive; unknown.
  Describe actual fallback/return branch and consequence, not just a symbol list.
- Minimum required semantics/data/ownership/lifetime/synchronization. Success-only
  stubs do not satisfy APIs with observable token/socket/IPC/graphics behavior.
- Existing retained runtime evidence where available, clearly separate from
  source reachability. Identify major unresolved facts rather than inventing them.
- Priority for getting this page to render, and the smallest useful functional
  check Antigravity could use to assess sufficiency (real PE/native oracle or
  existing consumer test, not a large conformance matrix).

Prefer primary Chromium/Microsoft/Wine sources. Pin links. Cover dynamic imports
and NT information classes, not just PE imports or grep counts. Include a compact
startup dependency map and ranked5–10 most important requirements to inspect.
Optional modern APIs/telemetry/ECN/full SDK coverage should be explicit TODOs
unless the active source path demonstrates a blocker. Do not claim the list is
exhaustive or that source analysis alone proves the renderer-kill cause.

## Draft Antigravity assessment work order

The second deliverable must be immediately actionable after parent review:
- Exact candidate Wine basefffa7de and evidence-aware source mapping, disjoint
  from the active control-channel trace task. Reuse existing token code/tests.
- For each high-priority requirement, compare actual wrapper/NT/server/WoW64
  behavior and existing real results. Statuses: sufficient with evidence;
  implemented but unverified; partial/stub; absent; inactive/deferred; unknown.
- Existing nonfatal warnings are not blockers. Broad security/UAC/conformance
  gaps stay TODO. No fake API success, capability claims or weakened tests.
- Identify a short ranked implementation backlog, ideally1–3 concrete high-value
  fixes with source-backed acceptance criteria, not a huge required-API rewrite.
- Code assessment/report first. No VM/build/browser/production modifications in
  that downstream audit without separately approved implementation scope. Parent
  retains selection/review of actual fixes, including current active diagnosis.

## Completion

Self-check citations against actual pinned source/branches and keep conclusions
bounded. Commit AND push your own documentation branch; return exact commit,
files and unresolved assumptions, then stop. No new worker or cross-agent messages.
At>=95%five-hour usage finish bounded checkpoint/push/defer; weekly>=99%stop.
Never consume reset credits. Goal is a practical map to real Chromium behavior,
not another perfect testing harness or exhaustive API certification.
