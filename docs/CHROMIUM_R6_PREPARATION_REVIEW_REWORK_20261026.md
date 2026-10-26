<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# R6 preparation review: align profile and bound probe ownership, then finalize

Reviewed pushedd4daff2cf65fb5765691269fffc8310c5815c38f. Fresh R6 mode, four-DLL
readonly view and pre-browser probe gate are useful. Parent independently ran
R6/view/native parser/contract tests:31 passed; one pre-existing source-location
fault test required pointing its SOURCE at the exact accepted owned cb1d117
userenv source, then passed (0.309s). This is test-input selection, no assertion
change. Worker19-test claim is a subset, not a new live validation. No guest run.
Luna's independent real units now accepted; see
CHROMIUM_APPCONTAINER_INDEPENDENT_ACCEPTANCE_20261026.md for paths/results.

## 1. Profile contract mismatch (blocking, worker correctly flagged)

Python configure(userenv-r6) chooses LAB/(HOST+'-profile'), but the reviewed
native parent contract derives WEX_EXIT_OUTPUT + L"\profile". Original helper
is output-only override, so the exact command is rejected before normal launch.
Simplest fix: use OUTPUT/'profile' in R6 too. It is already fresh because OUTPUT
is new. Keep the reviewed parent helper/header and eight browser arguments;
update tests/report to verify Python command/child_args/native contract all
agree. Do not encode the mismatch as an expected passing test. No new header
variant or vendor modification is necessary.

## 2. Probe must own a registration before removing it (blocking)

Current C probe checks initial lookup but continues register/duplicate/unregister
even if initial lookup found an existing mapping or failed. A fresh random name
makes collision unlikely, but the helper must not remove a mapping it did not
create. Abort metadata mutation on initial absence/NULL-output check failure.
Track ownership ONLY after fresh registration S_OK. Perform guaranteed bounded
cleanup on every subsequent error path and unregister ONLY owned registration.
Never treat a duplicate as fresh ownership. Record genuine lifecycle HRESULTs
and cleanup result so parent can assess the result; no false pass on a cleanup
failure or primary-error replacement. Do not free unknown output pointers on
failed lookup unless an ownership contract is established.

Also derive the SID from the actual moniker passed to RegisterSid: current code
Derive(name=nonce) then RegisterSid(moniker='wex-r6-'+nonce) differs from the
native oracle/real consumer's matching package name. Use one prefixed nonce name
<=64 UTF16 units for both, preserving original nonce in the output record.

Add meaningful sequence tests through actual original probe logic (e.g. small
injectable API table/core called by compiled mock harness), not just source-text
count matching. Cover initial existing/access error -> no mutation; failed fresh
registration -> no unregister; post-register lookup error -> owned cleanup;
cleanup failure -> nonzero/no acceptance; canonical successful lifecycle. Keep
strict decoder negatives/probe-before-browser tests and backward-compatible
old modes. Do not expand into a general API/security conformance project.

## 3. Final fresh preparation using accepted artifacts

After the bounded code fix, stage accepted metadata/userenv DLLs from the paths
in the parent acceptance report. Do not build another DLL set or modify Luna's
build. Compile only original helper/probe where changed; preserve old artifacts
and report new probe hashes. Assemble a fresh actual PREPARED_NOT_RUN inventory
with exact current source/helper/module/runtime inputs and launch/run/stop
commands using your correct D: worker root. No fabricated hashes. Existing R6
paths remain unused in guest; confirm fresh live paths later, not via offline
claims. Use original R5 adapter pattern with unchanged budgets/space/privateLO/
VirGL/namespace/owned-cleanup semantics and historical differences as warnings.

Tests appropriate to changed components, submitted diff check, commit AND push
SAME codex/chromium-r6-preparation branch with report and prep location. No VM,
SSH, browser, installation, token merge or live API-probe execution in this
work order. Parent reviews corrected code and final prep before ONE real trial.
No new Windows-native oracle needed unless a concrete contract ambiguity remains.
Stop and reply codex with commit/report/test evidence/preparation paths.
