<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Antigravity: combine existing token support with graphics and test Chromium

## Parent review and reason for this step

Reviewed255f516f/02f5d25b/f76dadfc/bdc2ec34/449ce791, imported as
63d48415/011e69fa/11333ae9/821a6320/0df98eca. Parent independently44 focused
observation/renderer/runtime/native/view tests passed7.362s,0 skips. Selected
imported source/test files equal worker449ce791; only expected unused wine
gitlink mismatch remains. No ChildObserver import. Transient periodic identity
observation now records warnings after initial binding; real ownership/deadline
guards stay. Epsilon corrects floating subtraction without extending operation.

Actual first observation-control aborted BEFORE helper launch on floating budget
comparison (helper_started=false), so it is not a second observed browser result.
Actual r2 journal has held-child bound and WAIT_TIMEOUT258/wait_ms45000 terminal
record; two observation timeouts were warnings. Guest metadata21/21 passed again.
No fixture HTTP request. Consumer still reports helper exit1/nonacceptance; a
long-lived main browser is not page success. Screenshot timed out, so no new
presented-front conclusion. Claims of all children surviving189s or page discard
causing the navigation stall are not established by these observations. The
page_discarding_helper log records a requested discard operation, not proof that
the foreground page was actually selected/discarded or that it caused zero HTTP.

The next step is a real compatibility experiment using the existing token work,
not another harness project or a claim that token support is already proven to
be the root cause. User expressly authorized merging/reusing Token Security when
needed. Current browser has progressed beyond registration exports into actual
sandboxed subprocess startup; integrate the existing implementation rather than
writing another lowbox/handle-table stack or repeating the same baseline.

## Task and ownership

Use NEW own D: root branch codex/chromium-token-integration from this parent
work-order commit and NEW own Wine branch codex/g4-token-integration. Preserve all
existing histories/checkouts/builds/evidence. Do not edit parent C: checkout,
main D: master, other Claude live token checkout, Luna clean build or old dirty
shared guest graphics build. Read/fetch published objects as needed.

1. Inspect CURRENT published Claude token branch and its original acceptance/
   build notes. Historical pointers: root ccr-08223ee4-p830bf @97d534f8 and Wine
   4fa6f4ac, plus reports mentioning097a76a/0b3891f/69c1d6b55e0. Resolve actual
   refs/ancestry; do not assume these are independent patches to blindly pick.
   Review specifically ProcessHandleTable/handle closer, restricted/capability
   tokens, NtCreateLowBoxToken and process security-capability propagation. Record
   concrete missing behavior in graphics branch4315be83 plus accepted metadata
   candidatecb1d117. API success stubs are not genuine enforcement/compatibility.
2. Merge the relevant EXISTING Wine token history with accepted graphics/SID/
   registration history on your dedicated branch, resolving genuine conflicts.
   Preserve both histories; never replace the graphics gitlink with token-only
   Wine. Include necessary committed project overlay headers/modules in the
   isolated root assembly, but leave unrelated OOBE/UI/UAC caller migration work
   out of the browser experiment. No new security architecture or API duplicate.
3. Build a compatible complete combined Wine client/server in OWN D-backed lab
   using normal committed project assembly. Guest server/client/ntdll must come
   from the SAME compatible combined build; a four-DLL view alone is insufficient
   for server token/protocol changes. Never overwrite the old installed build.
   If active token ownership or unresolvable conflict blocks safe integration,
   report the exact blocker and useful source findings rather than guessing.
4. Run existing focused real PE tests for relevant handle enumeration/restricted/
   lowbox token behavior and registration/SID regression on the combined build,
   both x64/i686 where the existing suite supports them. Native20-style mock
   orchestration tests alone are not token validation. Keep scope functional;
   no exhaustive UAC/ACL/fault/security certification matrix. Explain any inherited
   failures and concrete risk to this consumer. No fake sandbox success results.
5. With combined build working and relevant tests passing, execute ONE fresh
   normal Chromium comparison in existing owned D: R4 VM/runtime. Stage candidate
   build privately at a NEW guest location with a matched full server/client
   runtime selection; preserve old build/prefix evidence, use fresh profile/output.
   No reinstallation/download/new image. Check actual free space BEFORE copying;
   stop/report insufficient space, don't delete evidence. Minimal path/helper/
   adapter adjustment and matching output-only native PE are allowed; no new
   generic harness or repeated offline approval gate for those routine changes.
6. Preserve normal sandbox/features, original HTTP page/eight flags, privateLO,
   unique sockets and actual inner/outerVirGL. Record actual loaded combined build
   and metadata probe, HTTP HTML/JS/readiness/native journal and front image if
   available. Treat outcome as comparison, not blanket sandbox/G4 acceptance.
   Keep existing bounds/cleanup reserve and exact owned shutdown. Do not add a
   second browser retry, no-sandbox/workaround flag, debugger/attach or unrelated
   production patch. If still no page, retain exact new outcome and STOP; don't
   chase arbitrary warning lines or resume harness perfection.

Commit AND push both candidate Wine/root branches and original
docs/CHROMIUM_TOKEN_INTEGRATION_RESULT_20261027.md with actual refs/merge decisions,
build recipe/results, focused tests, actual browser result and remaining blocker.
Private payload/art/profile/screenshots stay D: or D-backed WSL. Candidate root
gitlink may name the combined candidate, clearly unmerged/unaccepted for master.
No master merge or other VM/session changes. Check limits:95%five-hour bounded
cleanup/checkpoint/push/defer; weekly99%stop; never consume reset credits. Reply
codex with commits/evidence then stop for parent review.
