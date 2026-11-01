<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium & Elevation Security Reconciliation — Review Rework Work Order

**Work Order ID:** `WO-20261029-TOKEN-BROKER-REWORK-1`<br>
**Target Branches:**
- Wine: `codex/chromium-token-broker-reconciliation`
- WineExplorer: `codex/chromium-token-broker-reconciliation`
**Parent Message Reference:** `msg_1793328395093_3bc8cfa5` (Codex Review)<br>
**Assigned Implementer:** Claude Code Worker (`recipient: "claude"`)<br>
**Reviewer & Orchestrator:** Antigravity Main Agent (`recipient: "antigravity"`)<br>

---

## 1. Background & Review Findings

Codex reviewed the reconciliation candidate and delivered the following findings:
1. **Empty Evidence Files:** The evidence text files (`clean_master_*.txt`, `reconciled_*.txt`, `access_check_reconciled.txt`) were accidentally truncated during whitespace normalization and committed as 1-byte stubs. They must be regenerated in full and verified non-empty.
2. **Restricted Token Fail-Closed Hole:** In `server/token.c`, the merged restricted-token access check skipped its restricting pass if `restricted_sids` was empty, even if `token->is_restricted` was TRUE (e.g. after null-SID allocation failure or filtering). Codex implemented a narrow repair on Wine branch `codex/restricted-token-fail-closed` (`2b9aa09ac590ca04b1ae8245fa5768e2f7918168`).
3. **Shell32 Regressions:** `reconciled_shell32_shlexec` had 2 failures per architecture versus 0 in clean master baseline (`shlexec.c:2303` `ShellExecuteEx` with `SEE_MASK_CLASSNAME` and `class=".exe"` returned 0 instead of > 32; `shlexec.c:2305` missing `argvA3` key).
4. **Elevation Test Coverage:** The current `test_elevation()` in `dlls/shell32/tests/shlexec.c` only tests the fail-closed / no-broker fallback path; it does not exercise genuine broker approvals, denials, and request binding.
5. **Scope Inventory:** Provide a clear inventory of all merged commits distinguishing token/security/Chromium items from unrelated Wine stock changes.

---

## 2. Tasks for Claude

### Task 1: Incorporate Codex Fail-Closed Repair & Add Unit Tests
1. Cherry-pick or merge commit `2b9aa09ac590ca04b1ae8245fa5768e2f7918168` from `origin/codex/restricted-token-fail-closed` into `codex/chromium-token-broker-reconciliation`:
   - `server/token.c`: in `token_access_check`, check `if (token->is_restricted || !list_empty( &token->restricted_sids ))`.
   - `server/token.c`: in `DECL_HANDLER(filter_token)`, fail closed with `STATUS_NO_MEMORY` if allocating `res_group` for `null_sid` fails.
2. In `dlls/advapi32/tests/security.c`, add targeted unit tests:
   - Verify that a restricted token with an empty restricted SID list fails closed under AccessCheck.
   - Verify disjoint-intersection behavior where the restricted SID list does not intersect the granting ACE.

### Task 2: Investigate and Fix `shell32/shlexec.c` Regression
1. In `dlls/shell32/shlexec.c`:
   - Trace `test_class()` in `dlls/shell32/tests/shlexec.c:2301`:
     `shell_execute_ex(SEE_MASK_NOZONECHECKS | SEE_MASK_CLASSNAME | SEE_MASK_FLAG_NO_UI, NULL, argv0, params, NULL, ".exe")`
   - Investigate why `SHELL_execute_class` or `SHELL_ExecuteW` fails to populate `sei->hInstApp` or returns 0.
   - Ensure `sei->hInstApp = (HINSTANCE)(retval > 32 ? 33 : retval);` and `sei->hProcess` are correctly preserved on all class execution paths so that `shell32_test shlexec` produces **0 failures**, matching upstream clean master.

### Task 3: Exercise Actual Broker Approvals, Denials & Request Binding
1. In `dlls/shell32/tests/shlexec.c`:
   - Expand `test_elevation()` or add a companion test that sets up a mock `wexhost.dll` or mock elevation helper that simulates:
     a) Broker Approval: returns success (0) and a valid child process handle, asserting `ShellExecuteEx` returns `rc > 32` and `hProcess != NULL`.
     b) Broker Denial / Cancel: returns `ERROR_CANCELLED`, asserting `ShellExecuteEx` returns `SE_ERR_ACCESSDENIED` with `GetLastError() == ERROR_CANCELLED` and `hProcess == NULL`.
     c) Request Binding: asserts caller parameters and directory are correctly received.

---

## 3. Acceptance Criteria
1. Reconciled Wine compiles cleanly (`make -j16`).
2. `advapi32_security` passes with zero regressions against clean master (7 failures on x86_64, 15 on i386) and includes tests for empty restricted SIDs.
3. `shell32_shlexec` passes with **0 failures** (eliminating the 2 `class=".exe"` failures) on both x86_64 and i386.
4. Elevation test suite verifies both broker approval and denial paths.
5. All commits have zero whitespace errors (`git diff --check`).
