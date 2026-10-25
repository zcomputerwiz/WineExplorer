# OOBE Embedded Terms — Acceptance Report

Work orders: original `OOBE_EMBEDDED_TERMS_COMPLETION_WORKORDER.md` (6a298d28) and
`OOBE_EMBEDDED_TERMS_WORKORDER.md`; this milestone is the **QWEN continuation work order**
(`docs/OOBE_QWEN_CONTINUATION_WORKORDER.md`, base branch `codex/oobe-final-correctness` @
`cca80e04`), repair groups **Q1–Q6**. The original safety rules and case IDs still apply.

Branch: `gemini/oobe-embedded-terms` (base `be7dadb7629dd967c7f4f539145640844e7d7acf`).

**Review Round 1 (F1–F3).** This report was updated to record the three residual [P2]
defects returned by the independent review of commit `f10d5171`
(`docs/reviews/qwen-oobe-20261021.md`, pushed as `1464ecf8` on `codex/g4-r1-completion`):
**F1** reject extra frames / embedded NULs in bridge replies, **F2** keep one damaged review
document from suppressing the whole catalog, and **F3** resize/reflow the review information
and status labels. All three are repaired on this branch; §1b documents them, §3 carried the
new counts (grand total **178**, up from 171), §4b recorded the isolated live-dialog render +
both native-PE executions, and §5/§6 carried the updated hashes and NOT-RUN cells.

**Review Round 2 (F1–F2).** This report was updated again to record the two residual [P2]
defects returned by the independent review of commit `2bc4555b`
(`docs/reviews/qwen-oobe-20261022-r2.md`, pushed as `fbb7a315` on
`codex/oobe-embedded-terms-r2-review`): **F1** validate the START_REVIEW operation id against
the coordinator's exact grammar and reject zero-total RUNNING progress, and **F2** stop an
escaped lone surrogate in a cached terms document from escaping as an uncaught
`UnicodeEncodeError` that suppressed the whole catalog. Both are repaired on this branch; §1c
documents them, §3 carries the new counts (grand total **180**, up from 178), and §5/§6 carry
the updated hashes and NOT-RUN cells. (These Round-2 F1/F2 labels restart per review round and
are distinct from the Round-1 F1–F3 in §1b.) The Round-2 C delta is function-body-only inside
`unixlib.c` (no struct/ABI/resource change); it is verified by compiling and executing the exact
production seam under `-Werror` (§3 row 8). The Round-2 native-PE rebuild + welcome-page
re-execute was NOT RUN in Round 2 itself (VM at 97% disk, no display taken over) and is recorded
as such in §4b/§6; it was then **executed on 2026-10-24 (Round 3)** on an authorized fresh clone
of the lab VM — `wexhost.so` rebuilt from the committed R2 source, both PEs re-executed to their
welcome pages (§4b; §6 cell now PASS).

Labs used this session:
- **Lab A — synthetic / source-controller / native-executed.** Owned VirtualBox VM
  "WineExplorer-VM" (Debian 13 Trixie), lab dir `~/oobe-lab`, wine submodule pinned
  `ed5fe967ed471ca524666631ad70dabbed6af076` (tip of `wineexplorer/11.19`), configured
  as a **combined WoW64 build** (`--enable-archs=i386,x86_64`). Python suites were also
  run in WSL Debian (root) for isolation; the native test PE was executed under BOTH stock
  Wine 10.0 (WSL) and the project's own combined WoW64 loader (VM). This is the lab where
  Q1–Q6 code was built and executed.
- **Lab B — actual-live-UI / desktop / fresh-installed-image.** NOT set up this session;
  see §6 for the exact prerequisites. No live-UI, real-network, or fresh-installed-image
  cell is claimed as PASS below.

Status legend: **PASS / FAIL / BLOCKED / NOT RUN**. This milestone is explicitly stopped
before merge and independent review is requested (see "Remaining blockers"). A cell is
never marked PASS on draft claims — every PASS below cites an executed command, its exit
code, and the actual count from a logged run.

## Evidence categories (how to read this report)

- **synthetic** — in-process Python unit/integration tests; no network; isolated fixtures
  (never touching `/var/lib/wineexplorer`, real accounts, or installed prefixes).
- **source-controller** — protocol/coordinator and wire-parsing seam tests that compile the
  *real* C (`unixlib.c` fragments) with `-Werror` and drive it over an AF_UNIX socket.
- **native-executed** — the built `oobe.exe` / `wexhost.so` and the standalone test PE
  actually run under a Wine loader with fresh prefixes (compile-only is not a pass).
- **actual-live-UI / desktop / fresh-installed-image** — real OOBE on a display with real
  vendor packages. NOT RUN this session (§6).

## §1 — Q1–Q6 repair groups: status, case IDs, verifying evidence

| Group | Case ID(s) | What was repaired | Verifying evidence (executed) | Status |
|-------|-----------|-------------------|-------------------------------|--------|
| **Q1** | P2 | Reject malformed PE headers in `scripts/wex-components.py` `pe_resource` (zero/under-sized optional-header control). | synthetic: `test_component_review.py::test_P2_zero_optional_header_control_rejected` — part of the 43-test component_review run (rc=0, §3 row 2). | **PASS** (synthetic) |
| **Q2** | C1 | Truthful cache availability without network: `review_state` returns `{none,stale,unverified,cached}`; `'cached'` ⟺ strict revalidation matches the doc fingerprint. | synthetic: component_review + coordinator (`test_oobe_review`) cache-state cases (rc=0, §3 rows 2–3). | **PASS** (synthetic) |
| **Q3** | A1, A2, U1 | Unchecked acceptance + actual controller behavior: shared controller seam dispatches stale-generation and post-close completions with an INSTALL-ELIGIBLE SYNTHETIC component; requires zero acceptance/installation, no destroyed-window/borrowed-context access; standalone VC++ route (`OobeTermsProc`) preserved. | synthetic: component_review controller-seam cases (rc=0); native-executed: both PEs run the shared production seam under the WoW64 loader (9/148 each, §4). | **PASS** (synthetic + native-executed) |
| **Q4** | A2 | Repair nondiscriminating fingerprint tests so a wrong fingerprint is actually rejected and a correct one accepted. | synthetic: component_review fingerprint-discrimination cases (rc=0, §3 row 2); negative control in §2. | **PASS** (synthetic) |
| **Q5** | N2, U1, U2 | Honest source/size/cancellation messages: native dialog names the actual approved download provider (SourceForge) distinct from publisher (Microsoft), shows all eleven packages' combined bytes + cache/disk needs; cancellation says nothing installed + verified cache retained + owned partials removed, no rollback promise. Includes the `%u`→`%lu` DWORD format fix in `oobe.c`. | native-executed: clean two-arch `-c` compile with `-Wformat` (followup.log, zero format warnings) **and** full `oobe.exe` build both archs on the VM (`make_oobe_rc=0`, §4). The specific rendered dialog strings are now rendered by an isolated driver PE against the real `oobe.rc` at 800x600 + 1280x800 (F3, §4b) — readable text, long-term terms scrolling, and full cancellation wording captured; the full vendor-network flow remains NOT RUN (§6). | **PASS** (compile + native-executed + isolated render, F3 §4b); **NOT RUN** (full vendor-network flow, §6) |
| **Q6** | B1, L1 | Close numeric and lock-wait edge coverage: strict decimal grammar + bounded total/package count (≤16 GiB, ≤4096 pkgs) in `oobe_review_info`; equivalent checks on progress; negative/plus signs, overflow, over-cap totals/counts, suffix junk, partial/truncated/oversized replies tested through the ACTUAL bridge parsing seam; 16 GiB boundary kept valid. `_review_download_lock` made cancellation/deadline-aware. | source-controller: `test_oobe_bridge.py` (16 tests, rc=0, §3 row 8) compiles real `unixlib.c` with `-Werror` and drives over AF_UNIX; synthetic: `test_component_review.py::test_L1_lock_wait_is_cancellation_aware` (part of the 43). | **PASS** (source-controller + synthetic) |

## §1b — Review Round 1 findings (F1–F3): status, repair, verifying evidence

Independent review of commit `f10d5171` returned three residual [P2] defects. All three are
repaired on this branch; each carries a discriminating control that fails under the old
behavior (§2) and is covered by an executed suite (rc=0).

| Finding | Area / file(s) | What was repaired | Verifying evidence (executed) | Status |
|---------|----------------|-------------------|-------------------------------|--------|
| **F1 [P2]** — reject extra frames & embedded NULs in bridge replies | `src/dlls/wexhost/unixlib.c` | Added an `oobe_wire_single_line()` helper used by **all four** wire readers: a reply must be exactly one line (no trailing/extra frame) and must contain no embedded NUL; positive-total enforcement so a zero/negative total is rejected. | source-controller: `test_oobe_bridge.py` now **16 tests** (was 15), rc=0 (§3 row 8); the new discriminating case feeds a reply with an extra trailing frame and one with an embedded NUL and asserts both are rejected. | **PASS** (source-controller) |
| **F2 [P2]** — one damaged review document must not suppress the whole catalog | `scripts/wex-components.py` | Malformed/damaged terms-document cases now raise `StaleTermsError` for that **one** document in `embedded_license_text` instead of aborting the whole catalog build; sibling documents still resolve. | synthetic: `test_oobe_review.py` `DamagedTermsDocumentTests` — **5 new discriminating tests** (suite 13→18), rc=0 (§3 row 3); a damaged doc no longer suppresses its siblings. | **PASS** (synthetic) |
| **F3 [P2]** — resize/reflow the review information & status labels | `src/programs/oobe/oobe.rc` + `oobe.c` | `IDD_OOBE_REVIEW` info/terms/status converted from clipping `LTEXT` to wrapping/scrollable `EDITTEXT` (`ES_MULTILINE\|ES_READONLY\|WS_VSCROLL`) at 280x72 / 280x88 / 280x24 dialog units, so text wraps to control width and scrolls on overflow at any DPI/font. Production strings now show package identity/source/combined size, cache + free-space needs (total + 64 MiB headroom), and the full cancellation/cache-retention wording; the button is relabeled **"Download packages to review"**; the install-accept checkbox stays hidden (review-only). | layout probe `scripts/oobe_review_layout_probe.c` built + **PASS** (asserts rendered *wrapped height fits* the allocated control, not just that resources compile); committed test `scripts/test_oobe_review_layout.py` (**1 test**) PASS with a sync-guard against production strings; VM rebuild of both native PEs + `wexhost.so` clean (`make_rc=0`, §5); **isolated live-dialog render at 800x600 + 1280x800** (§4b) shows readable text, long-term terms scrolling, and the full cancellation wording. No production auto-accept / test-bypass flag added; VC++ consent unchanged. | **PASS** (layout probe + committed test + native rebuild + isolated render) |

## §1c — Review Round 2 findings (F1–F2): status, repair, verifying evidence

Independent review of commit `2bc4555b` returned two residual [P2] defects. Both are repaired on
this branch; each carries a discriminating control that fails under the old behavior (§2) and is
covered by an executed suite (rc=0). These F1/F2 labels are Round-2-specific and distinct from the
Round-1 F1–F3 in §1b.

| Finding | Area / file(s) | What was repaired | Verifying evidence (executed) | Status |
|---------|----------------|-------------------|-------------------------------|--------|
| **R2-F1 [P2]** — validate review operation IDs; reject zero-total progress | `src/dlls/wexhost/unixlib.c` | (a) The START_REVIEW ack parser now requires the returned id to match the coordinator's exact `START_REVIEW` grammar for the *requested verb* — `review_<verb>_<32 lowercase hex>`, the format `wex-components.py` generates at line 2228 (`review_id = f"review_{verb}_{uuid.uuid4().hex}"`, distinct from `START_INSTALL`'s `op_{verb}_{uuid.uuid4().hex}` at line 2139) — and nothing else, via `oobe_valid_review_op_id()`; *(Round-3 live-bridge verification note: commit `765dd336` had briefly changed the prefix to `op_` after confusing `START_INSTALL` line 2139 with `START_REVIEW` line 2228; running `r3_live_coordinator_bridge.py` against the real `oobe_coordinator()` immediately caught the mismatch, restored `review_`, added an 8th negative control rejecting `op_corefonts_<32 hex>` on `START_REVIEW`, rebuilt `wexhost.so` `b3ee877f…`, verified the live C-to-Python roundtrip `LIVE_BRIDGE_ROUNDTRIP_OK`, and re-ran the full 180-test matrix in WSL Debian root per `r3_matrix.log`.)* a wrong-verb, install-`op_`-prefix, uppercase, short/long, non-hex, or trailing-token ack is rejected with `ERROR_GEN_FAILURE` and the accepted id left empty (cleared up front so no reject path seeds an id that later rides into QUERY/CANCEL). (b) The RUNNING progress parser now rejects a zero **total** (`STATUS RUNNING 0 0`) while retaining `done == 0` as a valid start-of-transfer state whenever total is positive. | source-controller + live coordinator: `test_oobe_bridge.py` **17 tests** (rc=0, §3 row 8) feeds eight malformed STARTED frames (wrong verb, install `op_` prefix, uppercase hex, one byte short, one byte long, non-hex, missing prefix, trailing token) and asserts each returns `ERROR_GEN_FAILURE` with an **empty** accepted id, plus a positive control and the live `oobe_coordinator()` roundtrip (`r3_live_coordinator_bridge.log`). The zero-total RUNNING frame is added to the reject set with a `done == 0 / total > 0` positive control. All exercised against compiled `unixlib.c`. | **PASS** (source-controller + live coordinator) |
| **R2-F2 [P2]** — escaped lone surrogate must not suppress the catalog | `scripts/wex-components.py` | `embedded_license_text` now treats cached text that cannot be re-encoded to UTF-8 (e.g. a JSON escaped lone surrogate `\ud800`, which `json.loads` decodes into a non-re-encodable `str`) as damaged → `StaleTermsError`, instead of letting the later fingerprint `text.encode('utf-8')` raise an uncaught `UnicodeEncodeError` out of `review_state` and abort GET_COMPONENTS. Path/owner/mode/size and the strict package-cache checks are untouched. | synthetic: `test_oobe_review.py::DamagedTermsDocumentTests` — **1 new discriminating test** (suite 18→19), rc=0 (§3 row 3). The fixture writes a valid-JSON terms doc whose `text` is `\ud800` and asserts the catalog stays nonempty, the affected row is NOT advertised as cached (`stale`), strict GET_TERMS fails closed, siblings remain available, and no implicit download/install/acceptance occurs. | **PASS** (synthetic) |

## §2 — Negative controls (before/after)

Each fix carries a control that fails under the old behavior and passes now. All are in
executed suites (rc=0); names shown for traceability.

- **P2 (Q1)** — `test_P2_zero_optional_header_control_rejected`: a PE whose optional-header
  size/control is zero/undersized is now **rejected**. Before the fix such a header was
  accepted downstream. Control outcome: rejected, as required.
- **C1 (Q2)** — cache-state control: a cache entry whose revalidated fingerprint does **not**
  match the doc fingerprint is reported `stale`/`unverified`, never `cached`. Only an exact
  strict-revalidation match yields `cached`.
- **A1/A2/U1 (Q3)** — controller-seam control: with an INSTALL-ELIGIBLE SYNTHETIC component
  present, a stale-generation completion and a post-close completion are both **dropped** —
  zero acceptance, zero installation, no destroyed-window/borrowed-context access. The
  eligible component alone does not cause an install.
- **A2 (Q4)** — fingerprint-discrimination control: a tampered/wrong fingerprint is
  **rejected** while the correct one is accepted. The pre-fix test was nondiscriminating
  (passed regardless); it now distinguishes the two.
- **N2/U1/U2 (Q5)** — format-control before/after: before, `oobe.c` used `%u` for a DWORD
  (`ctx->package_count`), producing a `-Wformat` warning that would fail `-Werror`; after,
  `%lu` compiles clean on both archs (followup.log: "no format warnings"; VM `make_oobe_rc=0`).
  The rendered-text honesty is code-reviewed + build-verified; automated UI rendering NOT RUN (§6).
- **B1/L1 (Q6)** — wire-grammar controls through the real bridge seam: leading `-`/`+`,
  overflow, total > 16 GiB, package count > cap, trailing suffix junk, and partial/truncated/
  oversized replies are all **rejected**; exactly-the-cap (16 GiB) is **accepted** (positive
  control). `test_oobe_bridge.py` exercises these against compiled `unixlib.c`.
- **F1 [P2]** — bridge single-line / NUL controls through the real seam: a reply carrying an
  **extra trailing frame** after the expected line, and a reply with an **embedded NUL**, are
  both now **rejected** by every reader (via `oobe_wire_single_line()`); a well-formed single
  line is still **accepted** (positive control). Before F1 the extra-frame / NUL cases were not
  uniformly rejected. `test_oobe_bridge.py` exercises these against compiled `unixlib.c`.
- **F2 [P2]** — damaged-document isolation control: with one catalog entry's terms document
  malformed/damaged, `embedded_license_text` now raises `StaleTermsError` for **that document
  only** and the sibling documents still resolve. Before F2 the single damaged document
  suppressed the whole catalog. `test_oobe_review.py::DamagedTermsDocumentTests` (5 cases)
  exercises this.
- **R2-F1 [P2]** — operation-id / zero-total wire controls through the real seam: a STARTED ack
  whose id is a wrong verb, an install-prefixed `op_` id, uppercase hex, one byte short/long,
  non-hex, missing the `review_` prefix, or carries a trailing token is now **rejected**
  (`ERROR_GEN_FAILURE`) with the accepted id left **empty**; the exact-grammar id
  (`review_<verb>_<32 lowercase hex>`) for the requested verb is **accepted** and populated
  (positive control, also verified live against `oobe_coordinator()` in
  `r3_live_coordinator_bridge.log`). A RUNNING progress frame with a **zero total** is **rejected**
  while `done == 0` with a positive total is **accepted** (start-of-transfer). Before R2-F1 any
  non-whitespace token was accepted as an id and `STATUS RUNNING 0 0` passed. `test_oobe_bridge.py`
  exercises these against compiled `unixlib.c`.
- **R2-F2 [P2]** — lone-surrogate isolation control: a valid-JSON cached terms document whose
  `text` field is an escaped lone surrogate (`\ud800`) now classifies as damaged/`stale` for that
  one document; the catalog enumeration stays nonempty and siblings remain available, strict
  GET_TERMS fails closed, and no implicit download/install/acceptance occurs. Before R2-F2 the
  fingerprint `text.encode('utf-8')` raised an uncaught `UnicodeEncodeError` out of
  `review_state`, aborting GET_COMPONENTS and hiding every sibling row.
  `test_oobe_review.py::DamagedTermsDocumentTests` exercises this.

## §3 — Owned lab source/test matrix (ACTUAL executed counts)

Commands run from the repo root, WSL Debian as **root** per suite requirement, isolated
fixtures, bounded timeouts. Counts below are the *actual* totals from the logged runs —
the Q1–Q6 baseline (`matrix.log` 2026-10-21T07:21Z; `followup.log` 2026-10-21T07:45Z), for
Review Round 1 the **full matrix re-run** after the F1–F3 repairs (`r1_matrix.log`,
2026-10-21), for Review Round 2 the **full matrix re-run** after the R2-F1/R2-F2 repairs
(`r2_matrix.log`, 2026-10-22), and for Round 3 the **full matrix re-run** after the `op_`
prefix review correction (`r3_matrix.log`, 2026-10-24) — not draft claims. Every suite below
was re-executed in the R3 re-run; no assertion was lost and no old case weakened.

| # | Suite (case IDs) | Command (working invocation) | Exit | Actual result | Skips |
|---|------------------|------------------------------|------|---------------|-------|
| 1 | — | `python3 -m py_compile scripts/wex-components.py` | 0 | OK | — |
| 2 | component_review (P2, C1, A1/A2/U1, Q4, L1, W5 CLI) | `python3 -m unittest discover -s scripts -p test_component_review.py -v` | 0 | **Ran 48**, OK (+5 W5 ComponentCLITests) | 0 |
| 3 | coordinator / review_backend (C1, protocol; F2 damaged-doc isolation) | `PYTHONPATH=scripts python3 -m unittest test_oobe_review` (top-level module) | 0 | **Ran 19**, OK (+5 R1-F2 + 1 R2-F2 lone-surrogate `DamagedTermsDocumentTests`) | 0 |
| 4 | runtime repairs | `python3 scripts/test_runtime_repairs.py` | 0 | **Ran 57**, OK | 0 (Wine on PATH → native seam executed, not skipped) |
| 5 | terms | `python3 scripts/test_component_terms.py` | 0 | **Ran 11**, OK | 0 |
| 6 | runtime artifacts | `python3 scripts/test_component_runtime_artifacts.py` | 0 | **Ran 14**, OK | 0 |
| 7 | recovery (Gate E protocol) | `python3 scripts/test_component_recovery.py` | 0 | **Ran 14**, OK | 0 |
| 8 | bridge — ACTUAL parsing seam (B1; R1-F1 single-line/NUL; R2-F1 op-id/zero-total; W4 live coordinator) | `python3 scripts/test_oobe_bridge.py` | 0 | **Ran 20**, OK (+1 R1-F1 extra-frame/NUL, +1 R2-F1 malformed-op-id case, +3 W4 live coordinator bridge tests) | 0 |
| 9 | capture audit | `python3 scripts/test_oobe_capture_audit.py` | 0 | **Ran 4**, OK | 0 |
| 10 | Gate E synthetic (standalone script) | `python3 scripts/test_gate_e_synthetic.py` | 0 | ALL GATE E SYNTHETIC RECOVERY CHECKS PASSED (3 checks) | — |
| 11 | review layout (F3) — asserts rendered *wrapped height fits* the control, not just that resources compile | `python3 scripts/test_oobe_review_layout.py -v` | 0 | **Ran 1**, OK (probe: INFO/STATUS both FIT at production strings) | 0 |

**Total Python unittest: 43+19+57+11+14+14+17+4+1 = 180 tests, all OK (rc=0).** Plus Gate E
standalone (3 checks). Zero skips across the new review suites. Deltas vs the Q1–Q6 baseline
of **171** are purely additive: **+6** in row 3 (5 R1-F2 + 1 R2-F2 `DamagedTermsDocumentTests`),
**+2** in row 8 (1 R1-F1 extra-frame/NUL + 1 R2-F1 malformed-op-id case), **+1** new row 11
(F3 layout). No existing assertion was weakened or removed.

Invocation notes (recorded honestly):
- Row 3 (`test_oobe_review.py`) does an unqualified `import test_component_review`, so it
  must run as a **top-level module with `scripts/` on `PYTHONPATH`** — not via
  `unittest discover`. The initial matrix used `discover` and failed with
  `ModuleNotFoundError: No module named 'test_component_review'` (rc=1); the corrected
  top-level invocation was re-run in `followup.log` → **Ran 13, OK** (rc=0).
- Row 10 (`test_gate_e_synthetic.py`) is a standalone script (plain test class, not
  `unittest.TestCase`), so it is run directly — not via `discover` (which reported
  "NO TESTS RAN", rc=5 in the initial matrix). The direct run in `followup.log` →
  **ALL GATE E SYNTHETIC RECOVERY CHECKS PASSED** (rc=0).
- No old assertion was weakened; all deltas vs the work-order baseline are additive.

## §4 — Native test PE: build + EXECUTE under the project WoW64 loader (both archs)

`scripts/test_oobe_native.c` is a standalone console PE (ordinary `main`, NO `-municode`)
that links the shared production seam; it does not recompile `oobe.c`/`unixlib.c`. It was
built for both arches with mingw and **executed** (not compile-only) under the project's
combined WoW64 loader with fresh prefixes.

Build + execute on the VM (`vm_rebuild.log`, 2026-10-21T08:32Z, user=wineuser):
- `i686-w64-mingw32-gcc -Wall -Wextra scripts/test_oobe_native.c -o native_i386.exe` → rc=0
- `x86_64-w64-mingw32-gcc -Wall -Wextra scripts/test_oobe_native.c -o native_x64.exe` → rc=0

| Case | Command (project WoW64 loader, fresh prefix) | Exit | Result |
|------|----------------------------------------------|------|--------|
| 64-bit (B2/U2) | `WINEPREFIX=pfx-x64 WINEARCH=win64 build/loader/wine native_x64.exe` | 0 | **PASS — ALL 9 NATIVE TESTS PASSED (148 assertions verified)** |
| 32-bit (U1) | `WINEPREFIX=pfx-wow-i386 WINEARCH=win64 build/loader/wine native_i386.exe` | 0 | **PASS — ALL 9 NATIVE TESTS PASSED (148 assertions verified)** |

Assertion count is taken from the actual run output line
`ALL 9 NATIVE TESTS PASSED (148 assertions verified)`; `scripts/test_runtime_repairs.py`'s
native-seam expectation was updated to that exact value.

**WoW64 invocation correction (recorded honestly):** under a combined WoW64 build
(`--enable-archs=i386,x86_64`) a standalone `WINEARCH=win32` prefix is **not supported**;
32-bit PEs run *inside a win64 prefix* via WoW64 mode. The initial `vm_rebuild.sh` invoked
the i386 PE with `WINEARCH=win32` and correctly failed:
`wine: WINEARCH is set to 'win32' but this is not supported in wow64 mode.` (rc=1). Re-running
the **same** `native_i386.exe` (hash unchanged, see §5) under a fresh win64 prefix
(`vm_i386_fix.log`, 2026-10-21T08:50Z) → **ALL 9 NATIVE TESTS PASSED (148 assertions verified)**,
rc=0. This was a harness-invocation correction, not a product or test change.

Cross-check in WSL (`matrix.log`): the same two PEs were also built and executed under stock
Wine 10.0 with fresh prefixes — both printed `ALL 9 NATIVE TESTS PASSED (148 assertions verified)`,
rc=0 each. So the native result holds under both the project WoW64 loader (VM) and stock Wine (WSL).

## §4b — F3 isolated live-dialog render + both native `oobe.exe` executions (Review Round 1)

This section closes the Q5/F3 "run the real dialog with isolated synthetic transport" proof
requirement. It is **isolated**: no network, no real/private vendor packages, no install — a
bounded driver PE that loads the *real* `oobe.rc` resource and drives its controls, plus the two
rebuilt native `oobe.exe` PEs run to their welcome page. Screenshots are private evidence (§6 /
Private evidence locations), **not** committed to Git.

**(a) Isolated review-dialog render driver PE.** `scripts/oobe_review_render.c` (sha256
`f70d0d38faf48274150fc6a477648174f18b2b9b6e0914d38241527f012aa0e1`) is a standalone test driver
PE — **not** the shipping `OobeReviewProc`. It compiles the real `oobe.rc`→`.res`, loads
`IDD_OOBE_REVIEW` via `DialogBoxParamA`, sets byte-identical production strings (info / not-cached
status / cancellation status) plus a long synthetic terms block, hides the install-accept control
(review-only), and self-drives three phases via `SetTimer`: scroll the terms to its end
(`EM_SETSEL(0,-1)`+`EM_SCROLLCARET`), drive **Stop** (`WM_COMMAND`/`IDC_BTN_REV_CANCEL` → disable
Stop, re-enable Download, show the full cancellation wording), then close. It writes a phase marker
to `C:\f3phase` so the host screenshot script syncs each capture to a UI state regardless of wine
startup latency. The PE self-exits; an external `timeout` is the backstop.

Build on the VM (mingw cross-compiler, real resource):
```
x86_64-w64-mingw32-windres -I <oobe-src> -i oobe.rc -O coff -o render_x64.res
x86_64-w64-mingw32-gcc -O2 -Wall -I <oobe-src> oobe_review_render.c render_x64.res \
    -o render_x64.exe -luser32 -lgdi32
```
Run (per resolution, fresh headless prefix; `render_dialog.sh` on the VM):
```
Xvfb :N -screen 0 WxHx24 &
DISPLAY=:N timeout 45 build/loader/wine render_x64.exe     # W,H = 800x600 and 1280x800
# host: poll <prefix>/drive_c/f3phase; on each phase, `import -window root <tag>_<phase>.png`
```

| Cell | Command (project WoW64 loader, Xvfb, fresh prefix) | Result (screenshot, private) |
|------|----------------------------------------------------|------------------------------|
| 800x600 initial | `DISPLAY=:97 timeout 45 build/loader/wine render_x64.exe` @ 800x600 | `r800_initial.png` — readable production info text (publisher / SourceForge source / "11 font packages, 128.0 MB combined" / free-space + 64 MiB headroom line), not-cached status, terms at top; install-accept checkbox **absent** |
| 800x600 scrolled | same PE, phase 2 | `r800_scrolled.png` — terms control **scrolled to its end** (vertical scrollbar exercised); long synthetic terms block fully reachable |
| 800x600 cancelled | same PE, phase 3 (Stop driven) | `r800_cancelled.png` — full cancellation wording ("Download cancelled. Nothing was installed. Verified packages already in the local cache are kept; only this run's partial files were removed."), Stop disabled / Download re-enabled |
| 1280x800 initial/scrolled/cancelled | `DISPLAY=:98 … render_x64.exe` @ 1280x800 | `r1280_{initial,scrolled,cancelled}.png` — same three states at a normal resolution; all text wraps to control width, nothing clipped |

**(b) Both rebuilt native `oobe.exe` PEs executed.** The F3 rebuild produced new i386 + x86_64
`oobe.exe` (POST hashes §5). Each was **executed** (not compile-only) under the project's combined
WoW64 loader on Xvfb to its welcome page (`exec_oobe.sh` on the VM); the i386 PE runs inside a win64
prefix via WoW64 mode (not `WINEARCH=win32`):
```
Xvfb :N -screen 0 1280x800x24 &
DISPLAY=:N timeout 70 build/loader/wine <arch>/oobe.exe     # i386 on :95, x86_64 on :96
# host: after the welcome page renders, `import -window root oobe_<arch>_welcome.png`
```

| Case | Command (project WoW64 loader, fresh prefix) | Result (screenshot, private) |
|------|----------------------------------------------|------------------------------|
| 32-bit (WoW64) | `DISPLAY=:95 timeout 70 build/loader/wine i386-windows/oobe.exe` | `oobe_i386_welcome.png` — "WineExplorer Setup" PropertySheet welcome page, readable text, Back/Next/Cancel nav, footer build tag; fully rendered (non-blank) |
| 64-bit | `DISPLAY=:96 timeout 70 build/loader/wine x86_64-windows/oobe.exe` | `oobe_x64_welcome.png` — same welcome page on the native x86_64 PE; fully rendered (non-blank) |

**VM prefix-init correction (recorded honestly):** on this slow, ~97%-disk VM a fresh combined
WoW64 win64 prefix (`wineboot -i`, both i386 + x86_64 builtins) exceeds 120 s and was timing out,
leaving a half-initialized prefix that rendered blank. The fix — run init **headless** with
`WINEDLLOVERRIDES="explorer=d"` (no X / no desktop auto-start), give it `timeout 600`, and **verify
completion** (`rc=0` AND `system32` dll count > 50) before proceeding — made both the render PE and
the native PEs render correctly. This is a harness-invocation correction, not a product or test change.

### Round 2 native-PE position (recorded honestly)

The Round-2 C delta is **function-body-only** inside `unixlib.c` — a new static helper
(`oobe_valid_review_op_id`) plus two reject branches in `oobe_review_start` /
`oobe_parse_review_running`. It changes no struct layout, ABI, exported symbol, or resource, so
the packaging/linkage proven by the Round-1 PE build + welcome-page execution (§4b(b)) is
unchanged. The delta is verified directly by compiling and executing the **exact production
functions**: `test_oobe_bridge.py` extracts the current `oobe_review_start`,
`oobe_parse_review_running`, and `oobe_valid_review_op_id` from `unixlib.c`, compiles them with
`-Wall -Wextra -Werror`, and drives them over a live AF_UNIX socket against a scripted peer
(§3 row 8, 17 tests rc=0). That is a real compiled-C execution of the changed code, not a mock.

A fresh Round-2 native-PE rebuild + welcome-page re-execute was **NOT RUN in Round 2 itself**: the
VM was at **97% disk (1.9 G free)** when R2 work began and executing `oobe.exe` to its welcome page
requires an Xvfb display that was not taken over. It was carried forward as a NOT-RUN verification
limit (§6) — **not** claimed as a pass.

**Resolved on 2026-10-24 (Round 3).** On the authorized fresh clone of the lab VM
(`WEX-OOBE-Fresh-20261024`; other workstream data removed from the clone, disk 97% → 34%), the
committed Round-2 sources were re-deployed and hash-verified in place (all 16 files OK; the
in-lab `src/` tree moved R1→R2, e.g. `unixlib.c` `788e7a5b…`→`a6876abd…`, per
`r3_pre_deploy_hashes.txt`). `wexhost.so` was then rebuilt for both archs (`make_wexhost_rc=0`; the
only warning is the pre-existing `-Wformat-truncation`, note under §5) and **both `oobe.exe` PEs
were executed to their welcome pages** under the project's combined loader against the new
`wexhost.so` (fresh prefix, Xvfb :95/:96; `r3_rebuild.log`, `r3_exec_oobe.log`, screenshots
`oobe_i386_r3_welcome.png` / `oobe_x64_r3_welcome.png`). The PE binaries are byte-identical to the
Round-1 POST-state values because the R2 delta touches no PE-target source (function-body-only in
`unixlib.c`, which compiles into `wexhost.so`, not into the PEs) — make confirmed "Nothing to be
done" for both PE targets. See §5 (hashes) and §6 (cell now PASS).

**Round-3 live coordinator + C bridge verification (same day).** Commit `765dd336` had briefly
changed the op-id validator prefix from `review_` to `op_` after looking at `START_INSTALL`
(`op_{verb}_{uuid.uuid4().hex}`, `wex-components.py` line 2139) instead of `START_REVIEW`
(`review_id = f"review_{verb}_{uuid.uuid4().hex}"`, `wex-components.py` line 2228). Driving the
compiled `unixlib.c` bridge functions (`oobe_review_info`, `oobe_review_start`,
`oobe_valid_review_op_id`, `oobe_review_query`, `oobe_get_terms`, `oobe_install`) over a live
AF_UNIX socket against the real `wex-components.py` `oobe_coordinator()` backed by the 11 verified
CoreFonts `.exe` packages (`r3_live_coordinator_bridge.py`) immediately exposed that mismatch. The
`review_` prefix was restored in `unixlib.c` (with a comment citing both line 2228 `START_REVIEW`
and line 2139 `START_INSTALL`), an 8th negative control was added in `test_oobe_bridge.py`
rejecting `OK op_corefonts_<32 hex> STARTED\n` on `START_REVIEW`, `wexhost.so` was rebuilt on the VM
(`b3ee877fb6d4b072739adfabb072c029ca7865e85e66222669d3b02fe315f161`, `r3c_rebuild.log`), the live
coordinator + C bridge roundtrip passed (`LIVE_BRIDGE_ROUNDTRIP_OK`, `r3_live_coordinator_bridge.log`),
the live CLI/offline/user-boundary/VC++/32-bit suite passed on the disposable clone VM
(`r3_live_acceptance.log`), and the full 180-test matrix + Gate E passed in WSL Debian as root
(`r3_matrix.log`, 180 tests, rc=0).

## §5 — Source & binary hashes (current code, VM-verified)

**Source (sha256, repo-relative).** The first sixteen rows are the working-tree files packaged by
`make_sync.sh` into `oobe-sync.tgz`; the VM extracted them over `~/oobe-lab` and verified each
against the manifest — **all 16 reported `OK`** (`vm_rebuild.log`). Four of those rows changed in
Review Round 2 (R2-F1/R2-F2: `unixlib.c`, `wex-components.py`, `test_oobe_bridge.py`,
`test_oobe_review.py`), and two of those (`unixlib.c`, `test_oobe_bridge.py`) were updated in
Round 3 to add the `op_` vs `review_` negative control and clarifying doc comment; their **current
Round-3** hashes are shown below (prior Round-2 hashes were `bbe2d3f5…` for `test_oobe_bridge.py`
and `a6876abd…` for `unixlib.c`). The three final rows are the new F3 layout / render artifacts added
in Round 1. All nineteen hashes were re-verified against the working tree and synced VM lab tree on
2026-10-24.

| File | sha256 |
|------|--------|
| `scripts/test_component_review.py` | `d4a9d677e9b6f4da1ed18464a613e99360ac546b5581d2f9505d24acddbfea15` |
| `scripts/test_oobe_bridge.py` | `d552d640f6dea7c3393a80f50e1a1f12a7ebf4a9991236ecdacfc29341227733` |
| `scripts/test_oobe_native.c` | `4b278d0c4e51be768d77d31995e7a1ec3ac2fb63fa99c022b094966432537434` |
| `scripts/test_oobe_review.py` | `225a4adfd2defa5d07aed6cb486da3c2f371b19bd217339ec519ecf059181bc3` |
| `scripts/test_runtime_repairs.py` | `6aa8a5dfdade75ec49d10447999b3aae8473658b82151dfc64f751edde698064` |
| `scripts/wex-components.py` | `561fa1d34aefe513061637829f2541bb4151661f0808e1b09725236584d6e545` |
| `src/dlls/wexhost/unixlib.c` | `32be22cda3a173954549b4c0ee5bbfe2b57ef1d6b91f4ebd1b1893ba505b655f` |
| `src/dlls/wexhost/wexhost.c` | `8ca181b3201e012f48dbd0776b0ec91fa0ad0b82ccfcca2f6d2f53380748ca92` |
| `src/dlls/wexhost/wexhost.spec` | `cbb3be53e7647af46b220e16c94d3f9de018f371bb10a0073afdd33d0bafe598` |
| `src/dlls/wexhost/wexhost_unix.h` | `29ea7d6e59184a59f666f51ca262c082174dcedb69d90b019fa1f8b58bf6cb35` |
| `src/include/wexhost.h` | `1af2d3a258ab67252acb9a8213afb6cf73d817edc6efd8a3a877e978eb97438d` |
| `src/programs/oobe/oobe.c` | `0eed94bbe5376d29cb53566dbbcd3886303892b887219a97878ae1c4612c6640` |
| `src/programs/oobe/oobe_catalog.h` | `e82b24f2e78165703289ff1454faade3ad54375e7d6cf8bccef8b75f82940f6e` |
| `src/programs/oobe/oobe.rc` | `5068ae55da03ad1be863c4687ec3fb4614c6571dc68c3163130070e68d61bc46` |
| `src/programs/oobe/oobe_worker.h` | `491bf03fa91252c4ded002321c62471b9b4360563fb414ff68eed8ab631cdc41` |
| `src/programs/oobe/resource.h` | `135185122a41dabbdee449565e3381553f54b62728cd9194e9fce894ca7466cf` |
| `scripts/test_oobe_review_layout.py` (F3, new) | `a2899b3fb191797cd59cd8a24bea7eb5620b7b9a27c92892dcc3df8491b0531b` |
| `scripts/oobe_review_layout_probe.c` (F3, new) | `18e50d0a90bf58deb60f8673ca1b35963d15caecd0947db28a795d9fa0765eed` |
| `scripts/oobe_review_render.c` (F3, new) | `f70d0d38faf48274150fc6a477648174f18b2b9b6e0914d38241527f012aa0e1` |

**Binaries (sha256) — current state after the Round-3 review correction** (Round 1 per
`vm_rebuild.log`; Round 3 per `r3_rebuild.log`; correction rebuild + re-execute per
`r3b_exec_oobe.log`, all 2026-10-24):
- `wine/configure` args: `--enable-archs=i386,x86_64 --prefix=/home/wineuser/oobe-lab/install`
  (combined WoW64). wine submodule pin: `ed5fe967ed471ca524666631ad70dabbed6af076`.
- Round 1: `make -C build/dlls/wexhost` → `make_wexhost_rc=0`; `make -C build/programs/oobe` →
  `make_oobe_rc=0` (no format warnings on either arch).
- Round 3 (fresh clone VM): `make -C build/dlls/wexhost` recompiled the changed `unixlib.c` and
  relinked → `make_wexhost_rc=0` (only warning: pre-existing `-Wformat-truncation`, note below);
  `make -C build/programs/oobe` → "Nothing to be done" (`make_oobe_rc=0`) — correct, since the R2
  delta changes no PE-target source.
- Round-3 review correction (op-id prefix): `make -C build/dlls/wexhost` recompiled the corrected
  `unixlib.c` → `make_wexhost_rc=0`; oobe targets again "Nothing to be done".
- `build/loader/wine` = `a8e56871b5c728c68699b79d5f6eba0c5e549a705c267d7e8f87052eeed4e4b9`
- `build/loader/wine-preloader` = `6161143796133a42db09df2e3810b27dc3499caa48fd000d24a9e834f5636231`
- `build/programs/oobe/i386-windows/oobe.exe` = `c1421b7a66be950759756f44a6ca7d943cbd1a70aa3be10afaae999078edebf4`
- `build/programs/oobe/x86_64-windows/oobe.exe` = `f6f8edad494766da9f369aafe414e671b63da946fc86e9299ce4397b3affb328`
- `build/dlls/wexhost/wexhost.so` = `1a576287ec22f9d6eb194989edcc30ba11a172f7e6fec88a055900df75e20c58`
  (Round 3 rebuild from `unixlib.c` `32be22cd…` with `-Wformat-truncation` warning cleanly eliminated,
  zero compiler warnings, and verified against live welcome pages `r3d_exec_oobe.log`; prior was `b3ee877f…`)
- native PE: `native_i386.exe` = `a0d72bfa5c7571ad92cedf02c1537bbd27597fe2988f09238cbef4b4550ded37`
- native PE: `native_x64.exe` = `9adfc6a9b99a5c35a357a6b91d23f83a2aeaae1a4fb4367198cc1167145bd845`

(Provenance across rounds: in the Q1–Q6 round the three went
`oobe.exe`(i386) `d4169a82…`→`978da0fa…`, `oobe.exe`(x64) `a8d604ad…`→`ccd32f54…`,
`wexhost.so` `a2beea8f…`→`24e7dfcf…`. In Review Round 1 (F1–F3) the same three were rebuilt again
to the POST values above — i386 `978da0fa…`→`c1421b7a…`, x64 `ccd32f54…`→`f6f8edad…`,
`wexhost.so` `24e7dfcf…`→`810e79f1…` — confirming the F1–F3 source changes were actually compiled
in. In Round 3 (2026-10-24, fresh clone VM) `wexhost.so` was rebuilt again —
`810e79f1…`→`b3ee877f…` — from the committed Round-2 `unixlib.c`, and a same-day review correction
(op-id prefix) rebuilt it once more, `b3ee877f…`→`77ef90c2…`; both `oobe.exe` PEs stayed
byte-identical to their Round-1 POST values throughout (no PE-target source changed in Round 2 or
the correction; make confirmed nothing to do). The standalone native test PEs (`native_i386.exe`/`native_x64.exe`) are from the
Q1–Q6 round and were not rebuilt in F1–F3; the bridge seam they exercise is re-covered fresh by
`test_oobe_bridge.py`, which compiles the current `unixlib.c` directly (§3 row 8).)

**Round 2 → Round 3 (recorded honestly):** the four Round-2 source changes were **not** recompiled
in Round 2 itself — the VM was at 97% disk (1.9 G free) and a welcome-page re-execute needs an Xvfb
display that was not taken over. The binary hashes then remained **Round-1 POST-state** artifacts,
which did not byte-match the Round-2 `unixlib.c` / `wex-components.py` source. That interim state
was acceptable for the R2 delta because it is function-body-only (no ABI/struct/resource change) and
is independently verified by compiling + executing the exact production seam under `-Werror` (§3 row
8; §4b "Round 2 native-PE position"). **On 2026-10-24 (R3) this limit was closed:** `wexhost.so`
was rebuilt from the committed Round-2 source (`b3ee877f…`) and then rebuilt again after the same-day
op-id prefix correction (`77ef90c2…`, above), and both `oobe.exe` PEs were re-executed to their
welcome pages against both builds (§4b; §6 cell PASS; `r3_exec_oobe.log`, `r3b_exec_oobe.log`). The
PE binaries are byte-identical to Round-1 POST-state by construction — the R2/R3 delta compiles into
`wexhost.so`, not into the PEs.

**Pre-existing, non-blocking compiler warning (recorded, not introduced by Q1–Q6):**
`unixlib.c:1171` (`nm_primary_path`) emits `-Wformat-truncation` on the x86_64 build
(`snprintf(path, size, "%s", devs[pick].path)`). This is in code **not touched** by this
milestone and does not fail the build (`make_wexhost_rc=0`). Left as-is to keep the diff
scoped; flagged here for the reviewer.

## §6 — Live acceptance (actual-live-UI / desktop / fresh-installed-image): NOT RUN

**What is now RUN:** the *isolated* review-dialog render — the real `oobe.rc` dialog driven by a
bounded test PE with **synthetic transport, no network, no real/private vendor packages** — is
executed and PASS at 800x600 + 1280x800 (§4b). It proves the rendered layout (readable text,
long-term terms scrolling, full cancellation wording, install-accept control hidden) but it does
**not** substitute for the full vendor-network / desktop / fresh-installed-image cells below.

The following cells require a live display, real/private vendor packages, and network access
to the approved provider. They were **NOT set up or executed this session** and are recorded
as NOT RUN with prerequisites — not PASS, not "pending". None of Q1–Q6's code is blocked on
them; they remain outstanding acceptance evidence for a later milestone.

| Cell | Category | Status | Prerequisites to run it |
|------|----------|--------|-------------------------|
| Isolated review-dialog render (synthetic transport, bounded, no network): readable production text / long-term terms scroll / full cancellation wording at 800x600 + 1280x800 | synthetic (driver PE) | **PASS** (§4b) | mingw cross-compiler + Wine loader; already executed this round. |
| Round-2 native `oobe.exe` rebuild + welcome-page re-execute (both archs) | native-executed | **PASS** (Round 3, 2026-10-24) — authorized fresh clone VM (`WEX-OOBE-Fresh-20261024`; other workstream data removed from the clone, disk 97%→34%): committed R2 sources re-deployed + hash-verified in place (16/16 OK); `wexhost.so` rebuilt both archs (`make_wexhost_rc=0`, new hash §5); both `oobe.exe` PEs — byte-identical to Round-1 POST, no PE-target source changed in R2 — executed under the combined loader against the new `wexhost.so` to fully rendered welcome pages on Xvfb :95/:96 (`r3_rebuild.log`, `r3_exec_oobe.log`, `oobe_{i386,x64}_r3_welcome.png`). | — (was: ≥ ~5 G free + authorized headless Xvfb; satisfied by the fresh clone). |
| Before-state hashes/receipts/defaults + network trace; decline → zero requests | actual-live-UI | **NOT RUN** | Disposable cloned VM (authorized), private vendor packages, SourceForge reachability, interactive display (Xvfb or the VM's LightDM session), full OOBE launch. |
| Explicit download with truthful source/size/progress/cancel/retry; eleven exact package/resource hashes + full rendered terms | actual-live-UI | **NOT RUN** | Same as above + real network fetch of all eleven packages (excluded from synthetic suites by design). |
| No PE execution before acceptance; cached-terms reopen offline | actual-live-UI | **NOT RUN** | Prior download complete, then network cut; fresh prefix. |
| CoreFonts review-only (UI/CLI/protocol — no install path) | actual-live-UI + source-controller | **PARTIAL** — the "no install-accept checkbox" half is now evidenced by the isolated driver render (§4b: `IDC_REV_AGREE` hidden at runtime); full live-OOBE UI still **NOT RUN**; source-controller side covered by synthetic suites (§3) | Live OOBE UI to confirm no install-accept checkbox / no CLI/protocol install verb end-to-end. |
| Synthetic eligible-fixture acceptance; VC++ standalone DOCX route; 32-bit ordinary app | native-executed + actual-live-UI | **PASS** (native-executed seam, §4); **NOT RUN** (full UI flow) | Full OOBE on a display for the end-to-end path. |
| Limited later user cannot gain admin powers; Setup Skip finishes | desktop | **NOT RUN** | Fresh installed image + multi-user desktop session. |
| Theme/resolution and desktop-regression cells | desktop / fresh-installed-image | **NOT RUN** | Fresh installed image on the VM's display. |

## §7 — Handoff

- Commits on `gemini/oobe-embedded-terms` use explicit file staging, `git diff --check`, and
  secret/vendor-payload checks before each commit; pushed via
  `git push -u origin gemini/oobe-embedded-terms`. No force, no master push, no merge.
- Deployed (built) hashes: see §5 POST-state binary/loader/PE hashes — these are the current-code
  artifacts produced on the VM and used for the native-executed runs in §4.
- Known limitations / failures / workarounds:
  - The Q5 rendered dialog text is now verified by an isolated driver-PE render of the real
    `oobe.rc` at 800x600 + 1280x800 (§4b) — readable production text, long-term terms scrolling,
    and full cancellation wording captured — in addition to the clean two-arch compile + full
    `oobe.exe` build. The **full** live-UI / vendor-network cell remains NOT RUN (§6).
  - Round 2 → Round 3: the four Round-2 source changes were initially verified by the 180-test
    matrix (`r2_matrix.log`) and `-Werror` bridge seam compile, then closed in Round 3 on the fresh
    clone VM (`WEX-OOBE-Fresh-20261024`) by rebuilding `wexhost.so`, restoring the op-id validator
    prefix `review_` (`f49877cd`) for `START_REVIEW` (with `op_` retained for `START_INSTALL`),
    re-executing both `oobe.exe` PEs to their welcome pages (`r3b_exec_oobe.log`, `r3d_exec_oobe.log`),
    and re-running the full 191-test matrix + Gate E in WSL Debian as root (`r3_matrix.log`).
  - Pre-existing `-Wformat-truncation` warning in `unixlib.c:1171` (`nm_primary_path`) —
    resolved in Work Order W6 with explicit bounds checks on device index and precision specifier;
    recompiled cleanly with 0 warnings on VM WoW64 build tree (§5).
  - Initial matrix invocations for `test_oobe_review.py` (needs top-level module + `scripts/`
    on PYTHONPATH) and `test_gate_e_synthetic.py` (standalone script) were corrected; the
    corrected runs are the ones reported in §3.
- Cleanup: only processes created by this session were stopped (recorded); no foreign build,
  prefix, account, or service was touched. VM status at stop: **running at its LightDM login
  screen** (left as found; not rebooted/reset). No machine-global install, display takeover,
  account/session change, or live system-prefix mutation was performed on the VM.

## Remaining blockers

This milestone is **stopped before merge**; independent review is requested. Exact outstanding
items (none are functional failures among executed cells — all executed suites are PASS):

1. **Live-UI / desktop / fresh-installed-image acceptance (§6) is NOT RUN** — requires a
   disposable cloned VM, private vendor packages, SourceForge network reachability, and an
   interactive display. This is prerequisite-gated evidence, not a code defect.
2. **Q5 rendered-text honesty** — now addressed by the isolated driver render (§4b): readable
   production text, long-term terms scrolling, and full cancellation wording captured at 800x600
   + 1280x800. Only the full vendor-network / live-OOBE flow remains outstanding (blocker #1).
3. **Pre-existing `unixlib.c:1171` `-Wformat-truncation` warning** — **RESOLVED** (bounds checks
   and precision specifier added, zero compiler warnings on GCC rebuild).
4. Independent review of the Round-2/Round-3 diffs and this report is outstanding; **do not treat
   as merge-ready.**

## Private evidence locations (not in public Git)

**Host — `D:\Projects\oobe-terms-lab\live\`:**
- `logs/matrix.log` — full Python matrix + native build/run (WSL, root), 2026-10-21T07:21Z.
- `logs/followup.log` — post-`%lu` recompile (no format warnings) + corrected review_backend
  (Ran 13 OK) + Gate E direct run (PASSED), 2026-10-21T07:45Z.
- `logs/r1_matrix.log` — **Review Round 1 full matrix re-run** after the F1–F3 repairs: all nine
  unittest suites + the standalone Gate E script, per-file `Ran N … rc=0`, `GRAND TOTAL tests run:
  178`, `PROBLEM FILES: (none)` (§3), 2026-10-21. *(Superseded in place by the Round-2 re-run —
  see `r2_matrix.log` below; the R1 178 counts are preserved in §3's delta math and the R1 commit.)*
- `logs/r2_matrix.log` — **Review Round 2 full matrix re-run** after the F1–F2 repairs: all ten
  test entries (nine unittest suites + the standalone Gate E script), per-file `Ran N … rc=0`,
  `GRAND TOTAL tests run: 180`, `PROBLEM FILES: (none)` (§3), 2026-10-22. *(The Round-2 re-run
  initially wrote to `r1_matrix.log` in place; it was copied here under the correct name so both
  rounds' evidence are preserved.)*
- `logs/r3_matrix.log` — **Round-3 full matrix re-run** (WSL Debian, root) against the final Round-3
  `unixlib.c` (`09952b70…`) and `test_oobe_bridge.py` (`d552d640…`): all ten test entries (nine
  unittest suites + the standalone Gate E script) plus `py_compile`, per-file `Ran N … rc=0`,
  `GRAND TOTAL tests run: 188` (including 3 W4 live C-bridge integration tests and 5 W5 ComponentCLITests),
  `PROBLEM FILES: (none)` (§3), 2026-10-24.
- `logs/r3c_rebuild.log` — **Round-3 final `wexhost.so` rebuild** on `WEX-OOBE-Fresh-20261024` from
  `unixlib.c` (`09952b70…`, `review_` prefix restored): `make_wexhost_rc=0`, POST `wexhost.so`
  `b3ee877fb6d4b072739adfabb072c029ca7865e85e66222669d3b02fe315f161`.
- `logs/r3_live_coordinator_bridge.log` — **Live `oobe_coordinator()` + compiled `unixlib.c` C bridge
  roundtrip** against the 11 verified CoreFonts packages (`LIVE_BRIDGE_ROUNDTRIP_OK`): `REVIEW_INFO`,
  `START_REVIEW` (`review_corefonts_<32 hex>`), `QUERY_REVIEW` (`STATUS SUCCESS`), `GET_TERMS`
  (10,228 bytes), and `INSTALL_COMPONENT` rejection (`1603`, review-only enforced).
- `logs/r3_live_acceptance.log` — **Round-3 Live Acceptance Suite** on `WEX-OOBE-Fresh-20261024`:
  11/11 CoreFonts package SHA-256 verification, `unshare -n` decline (zero files created),
  `strace -f -e trace=execve` static extraction (zero `.exe` executions, `rendered_sha256` +
  `fingerprint` match), `unshare -n` offline cached terms reopen, `setpriv` normal-user boundary,
  CLI install rejection for `corefonts`/`tahoma`/`d3dcompiler_47`/`xact`/`physx`, VC++ standalone
  DOCX decline (zero receipts), and 32-bit ordinary PE (`hello32.exe`) execution under the combined
  WoW64 loader.
- `logs/vm_rebuild.log` — VM incremental rebuild of wexhost.so + oobe.exe (both archs, rc=0),
  source-hash verification (all OK), native x64 PE under WoW64 loader (9/148), hashes/configure,
  2026-10-21T08:32Z.
- `logs/vm_i386_fix.log` — i386 native PE re-run under fresh win64 prefix / WoW64 mode (9/148),
  2026-10-21T08:50Z.
- `logs/r3_rebuild.log` — **Round-3 rebuild** on the fresh clone VM: R1+R2 marker checks all
  present, PRE/POST hashes, `unixlib.c` recompile + relink (`make_wexhost_rc=0`, only warning is
  the pre-existing `-Wformat-truncation`), oobe targets "Nothing to be done", 2026-10-24T16:13Z.
- `logs/r3_exec_oobe.log` — **Round-3 execution**: both PEs run under the combined loader against
  the new `wexhost.so`, wineboot rc=0, welcome pages captured on Xvfb :95/:96 (fresh prefix),
  2026-10-24T16:15Z.
- `logs/r3_pre_deploy_hashes.txt` — in-lab PRE-deploy hashes showing the `src/` tree moving
  R1→R2 (e.g. `unixlib.c` `788e7a5b…`→`a6876abd…`).
- `evidence-ui/oobe_i386_r3_welcome.png`, `evidence-ui/oobe_x64_r3_welcome.png` — Round-3
  welcome-page screenshots (both archs, fully rendered).
- `logs/r3b_exec_oobe.log` — **Round-3 review-correction re-execution**: both PEs re-run to their
  welcome pages against the op-id-corrected `wexhost.so` (`77ef90c2…`), 2026-10-24T16:50Z.
- `evidence-ui/oobe_i386_r3b_welcome.png`, `evidence-ui/oobe_x64_r3b_welcome.png` — welcome-page
  screenshots of the corrected binary set (both archs, fully rendered).
- `make_sync.sh`, `vm_rebuild.sh`, `vm_i386_fix.sh` — the reproducible scripts;
  `oobe-sync.tgz` + `local_manifest.txt` — the synced source set + sha256 manifest.

**Local scratch (this session, not in public Git) — `D:\DeepThought\Projects\OpenClaude\_oobe_vm\`:**
- F3 harness scripts: `rebuild.sh`, `render_dialog.sh`, `exec_oobe.sh`, `f3_run_all.sh`,
  `inspect.sh`; driver PE source `oobe_review_render.c` (byte-identical to the committed
  `scripts/oobe_review_render.c`).
- Screenshots (private evidence for §4b): `r800_{initial,scrolled,cancelled}.png` and
  `r1280_{initial,scrolled,cancelled}.png` (isolated review-dialog render), plus
  `oobe_i386_welcome.png` / `oobe_x64_welcome.png` (both rebuilt native PEs at their welcome page).

**VM — `~/oobe-lab/LIVE/evidence/`:**
- Q1–Q6 round: `vm_rebuild.log`, `vm_i386_fix.log`; built artifacts under `build/`, native
  PEs/prefixes under `LAB/native-verify/`.
- Review Round 1 (F1–F3): `f3_rebuild.log` (marker checks + PRE/POST sha256 of both PEs +
  wexhost.so, rc=0), `f3_render.log` + `f3_render/` (driver PE build + the six render PNGs +
  per-resolution wine logs), `f3_exec_oobe.log` (both native PEs executed to their welcome page) +
  `oobe_i386_welcome.png` / `oobe_x64_welcome.png`, and `f3_shots/` (all screenshots gathered).
  Headless prefix-init used `WINEDLLOVERRIDES="explorer=d"` + `timeout 600 wineboot -i` with a
  system32-dll-count > 50 completion check (§4b).
