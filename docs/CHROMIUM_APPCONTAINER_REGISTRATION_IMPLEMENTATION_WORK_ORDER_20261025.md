<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Antigravity work order: genuine AppContainer registration APIs

Priority: unblock normal Chromium startup without browser/security workarounds.
Parent reviewed native oracle c0d8e2ae and R5 logs; read
CHROMIUM_APPCONTAINER_ORACLE_PARENT_REVIEW_20261025.md and
CHROMIUM_R5_AND_SHADER_PARENT_REVIEW_20261025.md first. No shader rework needed.

## Ownership and branches

Use an isolated owned D: worktree/root branch
codex/chromium-appcontainer-registration-implementation based on current parent
work-order commit. Wine source must be a separate owned checkout/worktree based
on e096ae4373d778fde4a876f94798711f369e7013 (accepted SID derivation on4315be83),
branch codex/g4-appcontainer-registration. Fetch required bases if necessary.
Do not touch parent C: branch, main D: master, existing M wine/uninitialized
compositor, old /var/tmp/wex-g3-repair-20261018 source/build, R5 worker, other
workers/VMs or their prefixes. All build/prefix/evidence storage on host D: or
D:-backed Debian WSL. Do not initialize root submodules or import unrelated
OOBE/token work. Do not merge master. Push BOTH Wine and root work-order branches;
report exact commits. Parent will decide gitlink integration after review.

## Task A: implement real identity mapping APIs

Implement kernelbase exports with exact calling convention/signatures used by
pinned Chromium7d9824099030f5c28c939ba08654562faaad7c09:

- HRESULT WINAPI AppContainerRegisterSid(PSID,LPCWSTR moniker,LPCWSTR display)
- HRESULT WINAPI AppContainerUnregisterSid(PSID)
- HRESULT WINAPI AppContainerLookupMoniker(PSID,LPWSTR*)
- void WINAPI AppContainerFreeMemory(void*)

Use kernelbase.spec and a suitable kernelbase source file; match surrounding Wine
style and licensing. Implement genuine current-user persistent SID-keyed UTF16
mapping, independent lookup allocation and correct matching free. Store moniker
and display text; do not claim untested Windows registry-path compatibility.
Use Wine HKCU APIs and existing allocator/registry conventions. Never use a
process-only dictionary, return unconditional success, or hide missing token /
capability / ACL enforcement. These APIs register metadata; they do not create
a lowbox token, directory profile or security boundary. Preserve accepted Derive
implementation and normal userenv route, existing CreateAppContainerProfile
limitations and unrelated kernelbase/security behavior.

Required behavior demonstrated by native oracle: new registration S_OK;
duplicate SID HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS), original mapping intact;
lookup missing and second unregister HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
lookup returns separately allocated exact moniker; unregister only that mapping.
Actual LastError values are not status assertions unless documented/observed.

Registration/lookup/delete must not observe half-published data: serialize across
processes using a user-scoped mechanism that also covers writers sharing HKCU
across sessions, or justify an equivalent transactional publication design.
Document abandoned-owner/recovery and per-user scoping. Atomic key disposition
must not allow a failed second writer to remove an existing mapping. Roll back
only newly owned incomplete writes. Propagate registry/access/allocation errors;
validate type/length/terminator, avoid overflow, initialize lookup output according
to observed or explicitly documented policy. No arbitrary persistent deletion.

Resolve edge behavior needed by implementation with a small ADDITIVE native
oracle extension if helpful: valid Unicode, duplicate/different moniker for same
SID, NULL strings/output safely passed as NULL (never invalid memory), mapping
input/output rules. Native mutation remains only fresh WexGraphicsOracle.* owned
identities with cleanup; no registration of arbitrary existing apps/SIDs. Do not
copy guessed Windows internals or create a full hostile-input research project.
If exact unobserved semantics remain, report precisely; prioritize truthful
working valid-call lifecycle over expanding scope.

## Task B: prove behavior in your isolated build

Add Wine C conformance tests under kernelbase/tests or suitable original native
probe/test executable; use proper dynamic binding for native Windows if APIs
aren't SDK declared. Test actual implementation, not a Python mirror. Required:

1. Same lifecycle as accepted oracle, including two distinct mappings, exact
   Unicode string, independent allocation/free, A deletion preserving B.
2. Separate process lookup of registered mapping using normal builtin loading;
   close/reopen across child lifetime to establish persistence, not one process
   cache. Both AMD64 and i686 actual test runs, exact return values/counts and
   skips reported. No token/sandbox success claim from mapping APIs.
3. Deterministic concurrent same-SID registration: parent barrier releases two
   real children together, exactly one S_OK/one already-exists; lookup never
   accepts partial/wrong metadata. Cleanup all owned test mappings.
4. Missing/null inputs safely handled, corrupt stored type/size/terminator and
   allocation/registry failure where practical via meaningful test hooks or
   restricted own test key. Existing mapping survives failed duplicate and
   failed publication; never test against arbitrary host registry keys.
5. Unaffected SID Derive tests and relevant existing kernelbase/security tests;
   identify inherited failures, don't weaken assertions or relabel skips.

Build only your owned Wine checkout and isolated prefixes; no loaded binary
writes. Host Windows native oracle execution is permitted for bounded original
fresh-identity tests, not DLL installation or browser execution. No VM boot,
SSH/staging/browser/installer or new graphics trial in this work order.

## Deliverables and stop

Commit AND push Wine production code/tests and root original source/tests/report
as appropriate. Report source/build commands, exact x64/i686 actual outputs,
concurrency synchronization/results, registry namespace/locking design,
cleanup/remaining limitations, and private D: raw logs. No DLLs/payloads/registry
dumps in Git. Root gitlink changes, if made in YOUR isolated initialized checkout,
must reference only reviewed SID base plus your change; never stage parent's
mismatch. Keep parent-visible report clear enough to review without your logs.

Stop after pushed deliverables for independent parent review. Do not prepare or
run R6, merge, change Chromium flags/features or use no-sandbox. Historical file
IDs/hashes/version differences are warnings; concrete wrong-owner/overwrite and
functional failures remain blockers. At95% five-hour usage finish safe bounded
cleanup/checkpoint/push and report defer; at99% weekly stop. Never consume reset
credits. Authorized bridge replies to codex: acknowledgement, material blocker,
completion with branches/commits/report/tests. Do not assign other chats without
human authority; local worker delegation within your existing setup is allowed.
