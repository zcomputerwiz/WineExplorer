<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Gemini repair work order: token security, elevation and Chromium

Date: 2026-10-18. Status: required repairs, not acceptance.

Reviewed parent: `36540d4de34226208eb61c440229e4e5157edd9e`
(`gemini/token-security`). Reviewed Wine gitlink:
`097a76a567fe370fde2e55c328f1206cbfca1689`; original Wine baseline:
`ed5fe967ed471ca524666631ad70dabbed6af076`.

Read `CLAUDE.md`, `TOKEN_SECURITY_WORKORDER.md`,
`TOKEN_SECURITY_REFERENCES.md`, `TOKEN_SECURITY.md`, `ELEVATION.md`,
`PREFIX_SPLIT.md`, `PREFIX_SPLIT_WORKORDER.md`, `DBUS_PLAN.md`,
`STOCK_CHANGES.md`, and `WEBVIEW2.md`. This order adds repair gates; it does not
waive the original functional acceptance requirements.

## 1. Scope, ownership and reporting

Repair the existing implementation in coherent commits on `gemini/token-security`
or a clearly identified repair descendant. Cherry-pick this document from its
handoff branch if desired; do not merge unrelated graphics or OOBE implementation.
Record your actual starting parent and Wine SHA if the branch has advanced.
Do not switch/reset/stash another agent's occupied checkout, use their build
directory, or overwrite their Wine fork branch. Publish Wine commits first on
your own branch, then pin and publish the parent repository. The recorded Wine
SHA must be fetchable from a fresh clone, not merely present in your local objects.

No master merge, force-push, new chat/agent dispatch, VM reset/reinstall, broad
permission grants, or unrelated compositor/security architecture rewrite is
authorized. Coordinate shared `wexhost`, packaging, and machine-prefix ownership.
Use isolated prefixes/builds first. Use a designated disposable installed-test VM
only when designated; preserve existing desktops, accounts, disks and workloads.
Do not use another agent's WSL distribution. Record and restore VM/session changes.

The target remains **functional NT token semantics, genuine authorization and app
compatibility**. Hostile native-syscall containment, a secure desktop and a certified
browser sandbox remain deferred. A dimmer overlay is not a secure desktop. Wrong
password acceptance, ignored NT denies, and falsely successful APIs are functional
defects, not deferred hardening.

Immediately correct the blanket T0-T5 COMPLETE/full-semantics claims. Track each
gate below as PASS, FAIL, PARTIAL, BLOCKED or NOT RUN, with exact evidence. A blocked
milestone stays incomplete while independent repairs proceed. Do not substitute
documentation downgrades for required implementation or silently narrow scope.

## 2. Review evidence to preserve

These were independently observed against the reviewed revisions:

- `src/programs/consent/consent.c:461-473`: any nonempty password ends the
  credentials dialog with IDYES. No administrator authentication occurs.
- `consent.c:497-501`: the presence of `WINE_CONSENT_AUTO_APPROVE` bypasses the
  production UI. Even a value of `0` is treated as approval.
- Wine `server/token.c:1151-1174`: the restricting-SID pass collects allow ACEs
  but ignores deny ACEs. An isolated harness compiled the actual
  `token_access_check` function with ordinary SID N, restricting SID R, and DACL
  DENY R write; ALLOW N write; ALLOW R write. It returned
  `ret=0 access_status=0 granted=2`, not access denied. This was an extracted-function
  reproduction, not an installed-Wine acceptance run.
- `server/token.c:1559-1575` appends new restrictions to duplicated old ones.
  Existing restrictions must be intersected, not unioned.
- `dlls/kernelbase/process.c:1911` accepts SECURITY_CAPABILITIES at attribute-list
  validation, but the process-creation switch near line 645 has no corresponding
  application path and falls through as unsupported.
- `dlls/ntdll/unix/process.c:1388` changes ProcessHandleTable logging, still returning
  success with length zero. `ThreadPagePriority` setters discard state and queries
  always return 5. A second extracted-production-code harness observed setting 1
  succeed, querying 5, and setting through a NULL handle succeed.
- `scripts/test-edge-local.sh:29` disables RendererAppContainer and Win32kLockdown,
  despite the stock-sandbox caption and acceptance claim for its local capture.
- Both browser test scripts use global `killall -9` before/after testing. They were
  deliberately not executed during review, to avoid disrupting unrelated sessions.

Retain regressions for these exact failures. Add them before the fixes, demonstrate
the relevant failures on the reviewed commit, and rerun the unchanged assertions
after fixing. If a Windows-version contract differs, document a real control result
and revise the expectation with evidence, not to accommodate the implementation.

## R1. Real authorization, not password-shaped consent

Implement a concrete `wexhost`/polkit authentication and authorization path for the
exact elevation request. The native dialog may collect interaction for the existing
authentication mechanism; it must not decide credentials are valid because a field
is nonempty. Do not add a custom password checker, password files, or broad persistent
polkit allowance. Do not put passwords in argv, environment, logs, artifacts or Git.

Bind the approved executable, arguments, working directory, requester and chosen
administrator identity to the launch. Distinguish requesting user, authenticating
administrator and machine backend UID. Standard-user over-the-shoulder elevation
must not simply fabricate a linked full token for the requester. A Windows full or
SYSTEM token must not automatically become Linux root. Define authorization lifetime
and concurrency explicitly; approval for A must not approve B.

Remove production environment-based auto-approval. Test doubles must be in a
separate test-only helper/build/fixture, not a switch inherited by shipping consent.
Do not let helper startup/authentication errors masquerade as successful approval.
Match displayed executable identity and verified publisher to the actual target;
unverified/unsigned targets must remain Unknown publisher. Independent/open art only.

Proof required, using isolated test identities and a benign child which records its
PID, user SID, elevation type, integrity, Linux identity, cwd and argv:

1. Admin consent approve launches exactly one expected full-token child; decline,
   close and cancellation launch none. Ordinary applications remain filtered.
2. Standard user: valid administrator authentication succeeds with the documented
   correct identity; empty, wrong, arbitrary nonempty password, wrong account, cancel,
   unavailable polkit agent/service and authorization timeout do not launch a child.
3. Set the former AUTO_APPROVE variable to `1` and `0` in the requesting environment:
   neither can suppress required shipping authorization. Test isolated fixtures, not
   the user's session. Demonstrate the shipping binary lacks the bypass path.
4. Two concurrent requests A/B: approve A, deny B; only A's exact target launches.
   Exercise paths with spaces/non-ASCII, embedded quotes and arguments that resemble
   helper options. Capture exact parsed child argv, not just an echo of the input.
5. Refused/failed requests leave shared files/HKLM/All Users shortcuts unchanged.
   Compare before/after hashes, registry values and ownership/ACLs. Clean up fixtures.
6. Exercise real authentication separately from mock-helper unit tests. At least one
   real interactive approve/deny and valid/wrong-credentials test is required on both
   X11 and Wayland. Do not store credentials or substitute an auto-approve hook as proof.

## R2. Correct effective-token and access-check behavior

Use the normal ordered DACL rules in both ordinary-SID and restricting-SID passes;
combine the two results according to the actual Windows contract. The restricting
pass cannot be an unordered OR of allows. Audit deny-only handling: a SID whose
ENABLED flag was cleared but USE_FOR_DENY_ONLY set must still match deny ACEs.
The existing `token_sid_present` enabled-first logic needs checking on the new
filter/LUA paths. Do not introduce a second divergent object-opening MIC algorithm.

Required 32/64-bit tests in production Wine test paths:

| Fixture | Required result |
|---|---|
| Normal SID N, restriction R; DENY R write, ALLOW N write, ALLOW R write | Write denied in AccessCheck and a real supported object open |
| ALLOW N write only, with restriction R | Denied |
| ALLOW R write only, no ordinary matching allow | Denied |
| ALLOW N write and ALLOW R write, no matching deny | Granted |
| Same allows with a different SID's deny | Granted when no other rule denies it |
| Deny-only admin SID, explicit admin deny followed by user allow | Denied; deny-only admin allow must never grant |
| Low token, medium label NO_WRITE_UP | Read allowed if DACL permits; write denied |
| Label NO_READ_UP / NO_EXECUTE_UP / zero policy | Match the applicable documented/control behavior, not unconditional write denial |
| MAXIMUM_ALLOWED and generic mappings | Correct resulting rights; no masked-out right reported as granted |

Also cover INHERIT_ONLY ACEs, owner/default/null/empty DACL cases, and object-label
defaults (including unlabeled-object behavior) against documented/native controls.
Exercise registry, file, process/thread, event/mutex and named-pipe paths selected
for the supported vertical slice; list any uncovered object type as incomplete.
Verify actual operations through resulting handles, not just a TRUE AccessCheck.
Preserve previously granted handle rights when lowering tokens; test newly opened,
inherited and duplicated handles separately with their actual granted masks.

Use two threads: impersonate a restrictive token on one, leave the other normal,
test access on both, then RevertToSelf and retest. Validate OpenAsSelf and token
type/impersonation level. One thread's temporary token must not affect the other.

## R3. Filtering must not broaden an already restricted token

Replace append/union with the specified restricted-on-restricted semantics. Preserve
correct disable/remove privilege behavior and token duplication independence. Handle
allocation/protocol validation errors by failing the operation, not silently dropping
a requested restriction and returning success.

Tests must include:

- First token restrictions `{R1}`; filter again with `{R1,R2}`. Query must still yield
  `{R1}`, not `{R1,R2}`. A DACL granting ordinary N and only R2 remains denied before
  and after re-filtering. A DACL granting N and R1 remains allowed if otherwise valid.
- Overlapping, identical, empty-input and disjoint restriction sets; establish the
  native contract for disjoint sets rather than interpreting an empty list as an
  automatic unrestricted token. Check TokenIsRestricted/HasRestrictions consistently.
- Duplicate and re-filter must not mutate the source token or its labels/privileges.
- DISABLE_MAX_PRIVILEGE, SeChangeNotifyPrivilege exception, explicit removal,
  deny-only groups, LUA flags and supported/unsupported flag combinations. Keep
  unsupported behavior explicit; do not advertise every filter flag as implemented.
- Bad counts, truncated/malformed SID data, NULLs where invalid, insufficient token
  rights, allocation-failure paths, sizing probes and undersized buffers. No successful
  partly filtered token, leaked handle, server crash or overwritten guard bytes.

Behavioral authority: [CreateRestrictedToken](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-createrestrictedtoken).

## R4. AppContainer capabilities must reach the actual child

Wire SECURITY_CAPABILITIES through process creation to the real child token. Retain
and query package SID and capability SID attributes, duplicate them correctly, and
apply them on the supported access-check paths. Accepting the attribute then ignoring
it, or merely returning a token with an AppContainer-looking flag, is not completion.
Audit native and WOW64 pointer/size translation and LowBox handle-list handling.

Build a tiny parent/child PE probe for both architectures. The parent supplies a
known package SID and test capability through STARTUPINFOEX; the child reports its
own TokenIsAppContainer, package SID, TokenCapabilities, integrity, restricting SIDs
and actual accessible objects over an explicitly inherited test pipe. Prove:

- Queried child values match the requested policy, not a parent-side approximation.
- A capability-protected supported object grants access with the matching capability
  and denies without it/with a different capability, subject to the ordinary checks.
- Duplicating the token preserves package/capability state; parent token is unchanged.
- Invalid SID/attributes/counts, insufficient rights, malformed inputs and unsupported
  policies fail appropriately without accidentally creating an unrestricted child.
- No elevation prompt, administrator token or privileged elevation service is used
  merely to construct the browser's restricted targets.

If this cannot be delivered yet, mark T4 incomplete. A diagnostic feature-disable
run may help isolate the missing piece but cannot satisfy this gate.

## R5. Replace false-success compatibility APIs with real semantics

Inventory every API/attribute changed by the branch and classify representation,
query, persistence, Windows-side enforcement and Unix enforcement separately.
Changing FIXME to TRACE is not implementation. Do not globally break old Wine
stubs without regression assessment; isolate any intentionally deferred compatibility
behavior and never label it an implemented or enforced policy.

Specific proof requirements:

- **ProcessHandleTable:** return actual handle data using the selected API's correct
  structure/width and length/error contract. Open identifiable test handles, enumerate,
  verify those handles are present, close them and verify removal. Test buffer sizing,
  invalid/unauthorized process handles and WOW64 conversion. Use the pinned Chromium
  handle-closer path/probe to show designated unwanted handles really close and a
  required IPC handle remains usable. Success with zero length does not prove closure.
- **ThreadPagePriority:** set a nondefault supported value (for example 1), then query
  it; exercise a second thread to prove state is per thread. Restore the original value.
  Invalid handle/access/value/length tests must fail correctly. An advisory policy
  may lack Unix enforcement initially, but the Windows-visible state cannot be fake.
  Compare [SetThreadInformation](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setthreadinformation)
  and [GetThreadInformation](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getthreadinformation).
- **Mitigations/job/UI limits and accepted process attributes:** set/query the chosen
  consumer's supported state and test its relevant functional effect. Do not return
  blanket zeros/TRUE or ignore a child attribute and claim the feature works. Record
  requested/reported/enforced values independently, including any unsupported part.
- **Claims queries:** an actually empty claim set is valid, but validate token handles,
  access, buffer sizes, lengths and native/WOW64 layout. An empty block alone does not
  establish support for setting or enforcing claims; scope the coverage accordingly.

## R6. Genuine elevation launch and machine-write integration

Test manifest behavior for asInvoker, requireAdministrator and highestAvailable under
an administrator's filtered token, full token and a standard user's token. In
particular, highestAvailable must not force standard-user credential elevation.
For required elevation, direct CreateProcess must reject appropriately; shell launch
must invoke authorization and return the actual target process handle on success.
Test explicit runas separately. Retain and test documented helper-unset compatibility.

The child must wait on a test event, exit with a chosen nonzero code (e.g. 37), and
report its identity/argv/cwd. Assert hProcess is the child, wait times out while it is
alive, becomes signaled after exit, and GetExitCodeProcess returns 37. Test closing
the handle and the supported query/terminate rights. A helper/proxy PID or handle is
not silently equivalent to the target. On refusal hProcess must be NULL and no
target launched; distinguish cancellation from actual launch/authentication errors.
Bound helper waits and test missing/crashed/hung helpers and concurrent requests.

Deliver the original machine-tier requirement, or explicitly leave T3 partial:

1. Provide repeatable migration AND rollback of normal-session machine-tree/HKLM
   write permissions, preserving existing data, owner/group/ACLs and service behavior.
2. A non-elevated user cannot modify the shared tree/HKLM; merely querying a linked
   token does not authorize host writes. A genuinely authorized benign 32/64 installer
   can create a test shared file/value; a second user sees it after restart.
3. Reject wrong/cancelled auth with unchanged machine state. Distinguish approved
   Windows administrator identity from a dedicated non-root machine backend UID.
4. Recheck wineboot/HKLM save/merge, updates, fonts, themes, OOBE/components and All
   Users shortcuts. Coordinate with their owners; do not import or re-enable unreviewed
   OOBE components just to pass an installer test. Unattended setup remains usable.
5. For a cross-UID wineserver launch, resolve the genuine process-handle contract or
   present the limited alternative for review. Do not silently replace it with a helper
   handle. A same-server consent demo alone does not satisfy machine-tier integration.

## R7. Safe, reproducible runners

Fix runners BEFORE invoking them. No global killall/pkill-by-name, killing another
wineserver, deleting another X socket/lock or reusing a live desktop/profile. Use a
fresh checked mktemp directory for prefix/profile, a free isolated display allocated
by the runner, owned process groups/PIDs, and a scoped matching wineserver. Reap owned
children on success, error, signal and timeout. Delete only validated owned paths.

Prove isolation with separate harmless sentinel fixtures, not the real desktop:
start a second test-owned Wine prefix/process and display outside the runner's
ownership, record their PIDs/start times and a heartbeat. Run the runner through
success, early failure, timeout and signal cancellation. Sentinels must remain alive
with the same identities; only owned processes/sockets/files are removed. Check no
owned children remain. Retain output and before/after process inventories.

No real passwords or vendor executables in Git. Share source probes and redacted
logs; screenshots must contain no personal/session data. Preserve open-art provenance.

## R8. Browser acceptance without policy bypasses

Pin a maintained freely obtainable browser executable/hash and corresponding Chromium
source revision; document any vendor-specific uncertainty. Use a fresh profile and
normal-user broker. Keep graphics diagnostics distinct from token/security evidence.
An explicit, documented DirectComposition/SwiftShader rendering workaround may be
used for this token gate; hardware acceleration is the separate graphics workstream.

Acceptance may not use --no-sandbox, --single-process, --disable-gpu-sandbox,
--disable-features=RendererAppContainer,Win32kLockdown or equivalent policy-disabling
configuration. Diagnostic runs with those options must be separately named, logged,
and marked NON-ACCEPTANCE. Parse all relevant feature-list switches and record actual
broker/child Windows command lines, environment/configuration and applicable policies,
not just the launcher script. Different process roles can legitimately use different
policies; compare against the pinned build's actual policy, not assumptions about HEAD.

Required acceptance runs on both X11 and Wayland:

- Local DOM/CSS/canvas page paints; type a unique marker into an input, scroll to a
  known target, resize and verify repaint, and capture before/after evidence. Screenshots
  alone without input/assertions and child properties do not establish functionality.
- Network page, if network is allowed; document any offline skip separately.
- Restart with the same owned profile; verify a deliberately set benign profile value
  persists. Also verify a fresh profile launches independently.
- Inventory broker/renderer/GPU/utility processes and query actual child token,
  integrity, restrictions, AppContainer/capabilities as applicable, jobs, mitigation
  state and inherited-handle behavior. Capture chrome://sandbox or equivalent available
  diagnostics, without treating that page as proof of hostile-code containment.
- Exercise the initial-thread startup impersonation and later lowering in the small
  probe first. Show the final process primary token and thread revert behavior; merely
  observing multiple process names or a rendered page is insufficient.

Fix captions/report claims for the old local capture; it was generated by a policy-
disabled diagnostic script. Do not reuse it as a new passing stock-policy result.

## 3. Build, regression and installed acceptance

Build combined WoW64 with `--enable-archs=i386,x86_64`, not x64-only. Supply exact
configure/build commands, compiler versions, parent/submodule SHAs and test executable
hashes. Run both actual 32- and 64-bit executables; compilation is not execution.

Provide a safe checked-in runner (suggested `scripts/check-token-security.sh`) which
locates the actual built Wine test binaries, creates owned disposable state, runs the
relevant advapi32 security, ntdll info/object/process/thread and shell32 elevation cases,
then the focused probes above. Record exact commands used; do not publish placeholder
paths as successfully executed commands. Use bounded waits and explicit exit statuses.
Tests must assert negative cases and actual state changes, not just STATUS_SUCCESS.

Run ordinary 32/64-bit app regressions and existing polkit applets on X11 and Wayland.
Check Classic and Open Luna at 800x600, locked/unlocked taskbar, toolbars and collapsed/
expanded/absent system-tray show/hide chevrons. Include consent dialog keyboard focus,
default cancel/close behavior, long paths, Unicode, and multiple-monitor placement.
Do not redesign the theme or claim a dimmer makes the shared display secure.

Only after isolated gates pass, run the actual deployed binaries on a designated
fresh installed image: repeat real authorization, machine-write denial/success,
cross-user persistence and browser checks. Record image hash, installed binary hashes,
session backend, results and restoration. If no VM designation/interactive credential
test is available, state BLOCKED/NOT RUN and request the missing coordination; do not
reset an existing VM or mark the full work order complete.

## 4. Evidence bundle and staged review gates

Deliver `docs/TOKEN_SECURITY_FIX_ACCEPTANCE.md` plus a reproducible evidence index.
For every R1-R8 test record: test ID, fixture, command, expected result, actual result,
before/after revision, architecture/backend, exit code, assertions/failures/todos/skips,
and log/probe source locations. Distinguish extracted-function fixtures, full Wine
tests, mock-helper tests, real authentication, browser interaction and installed-image
acceptance. Do not combine them into a misleading single passed-test count.

Update TOKEN_SECURITY.md, ELEVATION.md and STOCK_CHANGES.md to match delivered behavior
and actual limitations. Explain inherited issues discovered on the supported paths.
Document all failed commands, their causes/workarounds, pending work and VM/session
state. Host containment stays deferred; do not label the result a secure sandbox.

Review checkpoints, in order:

1. **A — runners + tokens:** R7 isolation, R2/R3 denials/filtering, R4/R5 API/child
   semantics; both architectures. Submit this coherent checkpoint for review.
2. **B — elevation + machine integration:** R1 and R6 real authorization, genuine
   handles, permission migration/rollback and shared state; X11/Wayland evidence.
3. **C — browser + deployed regression:** R8 actual applicable policies, interaction,
   persistence, desktop regressions and installed-image acceptance.

Publish coherent commits and exact parent/Wine branch names and SHAs at each checkpoint;
do not merely say ready. Do not merge master. A remaining required gate means the
corresponding milestone is partial, even if the browser happens to render a page.
