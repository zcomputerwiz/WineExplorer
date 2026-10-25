<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Next startup blocker: AppContainer registration contract oracle

Assigned to existing GPT-6 Luna Dirac. Read parent R5_AND_SHADER_PARENT_REVIEW
first. R5 consumed; no more VM/SSH/browser runs in this assignment. Work in your
clean D:/DeepThought/Worktrees/chromium-r5-preparation/WineExplorer checkout;
create codex/chromium-appcontainer-registration-oracle from5823aed6. Leave parent
C: branch/main D: master/submodules/other workers untouched. All private data D:.

Goal: establish real Windows behavior for the four kernelbase exports Chromium
binds: HRESULT WINAPI AppContainerRegisterSid(PSID,LPCWSTR,LPCWSTR), HRESULT
WINAPI AppContainerUnregisterSid(PSID), HRESULT WINAPI
AppContainerLookupMoniker(PSID,LPWSTR*), void WINAPI AppContainerFreeMemory(void*).
Exact Chromium source7d9824099030f5c28c939ba08654562faaad7c09 sandbox/win/src/
app_container_base.cc lines43-86 and176-205. Missing first export matches the
new startup failure; exact crash symbolization remains unproven. Research only
primary sources and original probes; no Microsoft implementation disassembly,
proprietary source copying, GPLv3 implementation import or empty-success stub.

Deliverables:
1. Original small bounded Python ctypes or C native Windows oracle, committed
under scripts, explicit signatures and correct owning deallocators. Default
must be read-only/help; mutation requires explicit --run and a fresh JSON output
path on D:. No browser, installer, privilege change or arbitrary target names.
2. Use OS-generated random moniker with fixed WexGraphicsOracle prefix and
legitimate package SID from existing Derive API; record build/architecture,
exports, HRESULT unsigned hex, LastError separately (not HRESULT semantics),
lookup strings/output ownership and operation order. Preserve raw JSON privately.
Probe missing lookup, fresh registration, lookup, duplicate registration, lookup
in a separate native process to prove persistence, unregister, missing lookup,
second unregister. A second unique moniker/SID verifies isolation. Never touch
pre-existing names/registrations: check absence first and abort on collision.
Cleanup in finally only identities this process successfully created, even on
assertion/error; report cleanup failures, never bulk delete. Do not test invalid
pointers or arbitrary SIDs. You may execute these bounded original native host
Windows probes as part of this assignment; no host browser install/run involved.
3. Meaningful mocked tests for no default mutation, signatures/allocators,
collision refusal, failed registration not owned, cleanup after mid-sequence
error and child-read persistence observations. Then actual native Windows run
and report exact observations, including unavailable exports/permission errors;
no guessed fixed HRESULTs. Ensure child is bounded/reaped; caller timeout kills
only owned child. Do not invoke production DLL APIs on non-Windows in tests.
4. Concise implementation proposal grounded in observed behavior and pinned Wine
source: correct export/spec location, genuine per-user persistent mapping,
atomic duplicate handling/concurrent register, lookup allocator/free ownership,
UTF16/invalid-input/error behavior, unregister lifecycle. Inspect kernelbase /
userenv registry idioms read-only. Separate identity/profile metadata from token
and ACL enforcement; actual missing security functionality must not be hidden.
No Wine production patch yet: parent reviews contract before implementation.

Run tests, native oracle, check diff, commit AND push source/tests/original
report. Do not commit private registry dumps, DLLs, runtimes or raw user data.
Return branch/commit, tests, observed HRESULT/lifecycle summary/private JSON
paths, cleanup result and proposed next bounded implementation. Stop for parent
review, no new VM trial/reprep/rebuild/retry. Check live usage; >=95%five-hour
finish active bounded cleanup/checkpoint/push and defer; weekly>=99%stop. Never
consume reset credits.
