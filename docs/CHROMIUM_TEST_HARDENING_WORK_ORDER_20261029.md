<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work Order: Wine userenv AppContainer Test Hardening Verification — 2026-10-29

- **Work Order ID:** `WO-20261029-CHROMIUM-TEST-HARDENING-VERIFICATION`
- **Lead Architect / Manager:** Antigravity (Gemini)
- **Implementer / Worker Agent:** Claude Code (`mailbox: "claude"`)
- **Parent Reviewer:** Codex (`mailbox: "codex"`)
- **Superproject Worktree:** `D:\DeepThought\Worktrees\chromium-token-integration\WineExplorer`
- **Root Branch:** `codex/chromium-boundary-diagnostic`
- **Wine Child Branch:** `codex/g4-appcontainer-correctness`
- **Wine Commit:** `b222f7f2977663ca9c680fb15363bc0e17b3053f` (pushed to `https://github.com/zcomputerwiz/wine.git`)
- **Target Task:** Rebuild Wine `userenv_test.exe` and execute unit test suite in isolated test prefixes on both `x86_64` and `i386`.

---

## 1. Context & Scope

Codex issued Parent Review `docs/CHROMIUM_R5_CANDIDATE_PARENT_REVIEW_20261029.md` (commit `a2dc103ac9371d116075ee166197fdb3921bfe66`), noting:

### Finding P1: Destructive Fixed-Name Tests in `dlls/userenv/tests/userenv.c`
- **Issue:** Fixed test names `WineExplorerUnregTest_12345` and `WineExplorerFileCollTest_12345` with unconditional pre-test profile deletion and `CREATE_ALWAYS` risked deleting or truncating pre-existing user profile data or files.
- **Resolution in commit `b222f7f2`:**
  1. Unique test profile and package names generated dynamically using `GetCurrentProcessId()` and `GetTickCount()` (`WexUnreg_%lu_%lu`, `WexColl_%lu_%lu`).
  2. Removed unconditional `pDeleteAppContainerProfile` before ownership is established.
  3. Pre-flight non-existence checks with `skip` (failing closed if the target directory already exists to protect any existing data).
  4. Changed `CREATE_ALWAYS` to `CREATE_NEW` so existing files cannot be overwritten or truncated.
  5. Added explicit boolean ownership tracking flags (`created_pkg`, `created_ac`, `created_sentinel`, `created_coll_file`, `profile_registered`).
  6. Clean up only positively owned artifacts created in the test, on every exit path.

---

## 2. Assigned Worker Tasks

### Task 1: Fetch and Rebuild Wine Unit Tests
1. Fetch latest `codex/g4-appcontainer-correctness` in the Wine build tree, pointing to commit `b222f7f2977663ca9c680fb15363bc0e17b3053f`.
2. Recompile `dlls/userenv/tests/userenv_test.exe` for both `x86_64` and `i386`.

### Task 2: Execute Unit Tests in Isolated Prefixes
1. Run `userenv_test.exe` on `x86_64-windows` in a fresh, isolated temporary prefix (`WINEPREFIX=/tmp/wine-test-userenv-x64`).
2. Run `userenv_test.exe` on `i386-windows` in a fresh, isolated temporary prefix (`WINEPREFIX=/tmp/wine-test-userenv-x86`).
3. Also execute `dlls/kernelbase/tests/security_test.exe` (or `kernelbase_test.exe security`) on both architectures to confirm zero regressions.
4. Verify that all suites complete with **0 failures**. Record exact pass counts and report output.

### Constraints & Invariants
- **NO BROWSER RUN**: Do not execute a new Chromium browser trial. The r5 candidate trial evidence (`chromium-browser-verify-r5-20261029`, QEMU 0, HTTP 200 index/scene, visual marker squares) is preserved and authoritative.
- Clean up any temporary test prefixes created during testing.
- Reply to `antigravity` via bridge message with the full test results.
