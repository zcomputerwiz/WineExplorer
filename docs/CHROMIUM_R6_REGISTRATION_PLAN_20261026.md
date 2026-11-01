<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# R6: registration APIs in the real Chromium startup path

Current parent4aabab76 policy: prioritize actual Chromium functionality; integrate
existing Token Security work when the consumer needs it, preserve both histories,
and do not substitute sandbox bypasses or require unrelated hardening milestones.

Source review of Winecb1d117/root7d258606: previous blocking no-DELETE rollback and
existing-mapping overwrite-on-read-error are repaired. Correct NT locking,
metadata lifecycle and required tests have useful improvements. Worker reports
49/49 per architecture. Independent parent-run validation is now assigned to
Luna Dirac on codex/g4-appcontainer-independent-validation using clean owned
source/build, committed project overlay, x64/i686 tests and actual loader routes.
This is NOT an independent pass yet. Remaining deliberate fault-injection,
full process-death/reboot/cross-user, general conformance and ancillary runner
hardening are recorded TODOs; do not restart an exhaustive API research project.

Important build prerequisite resolved by inspection: normal project
scripts/assemble.sh and assemble.py link committed src/include headers and
modules into Wine before autoconf. A bare Wine checkout missing wex*.h is not
evidence of undisclosed proprietary headers. Build preparation must record exact
committed roots and actual generated/overlay changes, without altering other
workers or the old dirty graphics source/build.

## Goal and comparison

ONE fresh real Chromium startup diagnostic with the previously missing four
kernelbase metadata APIs plus accepted userenv SID derivation available through
normal builtin loading. Keep existing Chromium snapshot1706509/source7d982409,
original HTTP page/JS, ordinary eight browser arguments, sandbox behavior,
private loopback and real inner/outer VirGL environment. Same existing D:-backed
VirGL disk; no new image, download, install or Edge retry.

R5 consumed5823aed6: SID45/45 passed but Chromium exited0x80000003, missing
AppContainerRegisterSid immediately preceding breakpoint, zero HTTP/console
front. Preserve R5. Do not reuse any consumed stage/output/profile/helper-output
path, and do not infer the next result from source or an API probe.

## Offline implementation — Antigravity

On a NEW isolated own root branch codex/chromium-r6-preparation based on this
parent plan commit, reuse accepted R5 runner pattern. Existing registration/Wine
branches stop unchanged while Luna validates. No competing implementation or
full build. No VM/SSH/browser authority in this preparation task.

1. Add explicit R6 mode/paths/CLI mutually exclusive with old modes. Fresh names
   chromium-registration-r6-20261026[-stage/output/profile] throughout host and
   guest, plus an output-only original native helper PE/header contract. Do not
   edit original historical helper outputs or the vendor runtime. Retain native
   held-handle WinPID/status and Linux-parent identity as separate observations.
2. Generalize the existing opt-in readonly module view narrowly to stage actual
   accepted userenv and kernelbase x64/i686 builtin modules. No shared installation
   or prefix file overwrite: use private mount namespace with readonly bind views,
   correct arch paths and outside module comparison. Existing userenv modes/defaults
   unchanged. Tests prove scope and failure cleanup; no patching DLL bits or native
   sandbox workaround. Final module paths come from Luna's reviewed build, pending;
   do not invent their hashes or build another competing DLL set.
3. Add a bounded ORIGINAL C/ctypes-or-PE metadata sanity probe appropriate to the
   guest Wine route before browser launch, within existing API45s cleanup allowance.
   Native PE preferred: dynamically resolve four kernelbase exports and Derive,
   report normal builtin paths, use nonce-derived <=64-unit names/legitimate SIDs,
   check initial absence, fresh register/lookup/duplicate/unregister/absence and
   exact strings. Free via matching APIs; cleanup only successfully owned entries.
   Fail before browser on probe failure; no arbitrary name deletion. Do not extend
   into concurrency/token/security conformance here; independent unit tests cover
   those scoped contracts. Log genuine HRESULTs and cleanup outcomes.
4. Extend meaningful tests for mode selection, read-only module coverage/x86+x64,
   failure cleanup, probe gating/browser sequence, no historical-path overwrite,
   original fixture/sandbox/backend controls and native journal. Preserve the timer
   arithmetic fix; do not enlarge deadlines or weaken guards to make tests pass.
5. Commit/push code/original helper/probe/tests/report even if final artifact
   inventory waits on Luna. Clearly report OFFLINE_CODE_READY_ARTIFACTS_PENDING,
   not PREPARED_NOT_RUN with fabricated modules. Once parent supplies accepted
   artifacts, assemble ONE fresh prep using existing adapter pattern and exact
   deploy/run/stop commands; parent reviews immutable preparation before live run.

No source build, VM/SSH/staging/browser, token import, master merge, other worker
checkout changes, proprietary code/art/payload publication, feature spoofing or
security bypass in this offline work order. Private evidence/build storage D: or
D:-backed WSL; unique socket roots and existing rawUTC/space/owned cleanup guards.
Only actual transfer/ownership/function errors block; historical hash/ID/version
differences are logged warnings. Do not repeatedly re-audit unchanged baselines.

## Parent live gate, after validation and preparation review

Authorize and run one R6 trial, same exact-owned launcher/adapter/stop pattern.
Capture real native terminal status, HTTP HTML and JS requests, readiness and
actual front pixels. Check child processes and truthful sandbox observations
separately; do not call startup/pixels/pass from API/backend success. Always clean
up/verify guest shutdown, preserve failure and stop before another trial/fix.
If startup progresses, move to the accepted shader/texture fixture and actual
presentation. If the next real failure reaches NtCreateLowBoxToken, restricted
or capability-token wiring, use existing token branch instead of implementing
another stack: review current published token pins, merge reviewed Wine histories
on a dedicated integration branch, build full compatible user/server components,
then run focused token-query/child-launch/x64+i686 and real-browser controls.
Do not replace a divergent gitlink and discard either history; no full token
hardening/UAC caller matrix required to pursue browser functionality.
