# OOBE Embedded Terms — Acceptance Report

Work orders: original `OOBE_EMBEDDED_TERMS_COMPLETION_WORKORDER.md` (6a298d28) and
`OOBE_EMBEDDED_TERMS_WORKORDER.md`; this milestone is the **QWEN continuation work order**
(`docs/OOBE_QWEN_CONTINUATION_WORKORDER.md`, base branch `codex/oobe-final-correctness` @
`cca80e04`), repair groups **Q1–Q6**. The original safety rules and case IDs still apply.

Branch: `gemini/oobe-embedded-terms` (base `be7dadb7629dd967c7f4f539145640844e7d7acf`).

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
| **Q5** | N2, U1, U2 | Honest source/size/cancellation messages: native dialog names the actual approved download provider (SourceForge) distinct from publisher (Microsoft), shows all eleven packages' combined bytes + cache/disk needs; cancellation says nothing installed + verified cache retained + owned partials removed, no rollback promise. Includes the `%u`→`%lu` DWORD format fix in `oobe.c`. | native-executed: clean two-arch `-c` compile with `-Wformat` (followup.log, zero format warnings) **and** full `oobe.exe` build both archs on the VM (`make_oobe_rc=0`, §4). The specific rendered dialog strings are NOT covered by an automated UI test — that cell is NOT RUN (§6). | **PASS** (compile + native-executed); **NOT RUN** (live-UI rendering) |
| **Q6** | B1, L1 | Close numeric and lock-wait edge coverage: strict decimal grammar + bounded total/package count (≤16 GiB, ≤4096 pkgs) in `oobe_review_info`; equivalent checks on progress; negative/plus signs, overflow, over-cap totals/counts, suffix junk, partial/truncated/oversized replies tested through the ACTUAL bridge parsing seam; 16 GiB boundary kept valid. `_review_download_lock` made cancellation/deadline-aware. | source-controller: `test_oobe_bridge.py` (15 tests, rc=0, §3 row 8) compiles real `unixlib.c` with `-Werror` and drives over AF_UNIX; synthetic: `test_component_review.py::test_L1_lock_wait_is_cancellation_aware` (part of the 43). | **PASS** (source-controller + synthetic) |

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

## §3 — Owned lab source/test matrix (ACTUAL executed counts)

Commands run from the repo root, WSL Debian as **root** per suite requirement, isolated
fixtures, bounded timeouts. Counts below are the *actual* totals from the logged runs
(`matrix.log` 2026-10-21T07:21Z; `followup.log` 2026-10-21T07:45Z), not draft claims.

| # | Suite (case IDs) | Command (working invocation) | Exit | Actual result | Skips |
|---|------------------|------------------------------|------|---------------|-------|
| 1 | — | `python3 -m py_compile scripts/wex-components.py` | 0 | OK | — |
| 2 | component_review (P2, C1, A1/A2/U1, Q4, L1) | `python3 -m unittest discover -s scripts -p test_component_review.py -v` | 0 | **Ran 43**, OK | 0 |
| 3 | coordinator / review_backend (C1, protocol) | `PYTHONPATH=scripts python3 -m unittest test_oobe_review` (top-level module) | 0 | **Ran 13**, OK | 0 |
| 4 | runtime repairs | `python3 scripts/test_runtime_repairs.py` | 0 | **Ran 57**, OK | 0 (Wine on PATH → native seam executed, not skipped) |
| 5 | terms | `python3 scripts/test_component_terms.py` | 0 | **Ran 11**, OK | 0 |
| 6 | runtime artifacts | `python3 scripts/test_component_runtime_artifacts.py` | 0 | **Ran 14**, OK | 0 |
| 7 | recovery (Gate E protocol) | `python3 scripts/test_component_recovery.py` | 0 | **Ran 14**, OK | 0 |
| 8 | bridge — ACTUAL parsing seam (B1) | `python3 scripts/test_oobe_bridge.py` | 0 | **Ran 15**, OK | 0 |
| 9 | capture audit | `python3 scripts/test_oobe_capture_audit.py` | 0 | **Ran 4**, OK | 0 |
| 10 | Gate E synthetic (standalone script) | `python3 scripts/test_gate_e_synthetic.py` | 0 | ALL GATE E SYNTHETIC RECOVERY CHECKS PASSED (3 checks) | — |

**Total Python unittest: 43+13+57+11+14+14+15+4 = 171 tests, all OK (rc=0).** Plus Gate E
standalone (3 checks). Zero skips across the new review suites.

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

## §5 — Source & binary hashes (current code, VM-verified)

**Source (sha256, repo-relative).** These are the working-tree files packaged by
`make_sync.sh` into `oobe-sync.tgz`; the VM extracted them over `~/oobe-lab` and verified each
against the manifest — **all 16 reported `OK`** (`vm_rebuild.log`).

| File | sha256 |
|------|--------|
| `scripts/test_component_review.py` | `dc9e43ef766b8c748f7cffc5f941a23d14d53f25fd017cb7f2c2954ac993e251` |
| `scripts/test_oobe_bridge.py` | `cfb0bdec90d9d13f31111a5edb20268f1fd7fb55fdf3ad37cd8e3f55d077b5b0` |
| `scripts/test_oobe_native.c` | `4b278d0c4e51be768d77d31995e7a1ec3ac2fb63fa99c022b094966432537434` |
| `scripts/test_oobe_review.py` | `bda788b29d5bdf2624dffe3539bac706462f4778bf8099e367335de6d3354f96` |
| `scripts/test_runtime_repairs.py` | `6aa8a5dfdade75ec49d10447999b3aae8473658b82151dfc64f751edde698064` |
| `scripts/wex-components.py` | `30e84e2100bdff77c37c6a01a13ec8d8ae2366572bc3b9c5f82ba7a18c117b1c` |
| `src/dlls/wexhost/unixlib.c` | `de5d4b414bc8e661703c2dbbdb681d3414f767d6bfbb75aac0ae21b8d625b15a` |
| `src/dlls/wexhost/wexhost.c` | `8ca181b3201e012f48dbd0776b0ec91fa0ad0b82ccfcca2f6d2f53380748ca92` |
| `src/dlls/wexhost/wexhost.spec` | `cbb3be53e7647af46b220e16c94d3f9de018f371bb10a0073afdd33d0bafe598` |
| `src/dlls/wexhost/wexhost_unix.h` | `29ea7d6e59184a59f666f51ca262c082174dcedb69d90b019fa1f8b58bf6cb35` |
| `src/include/wexhost.h` | `1af2d3a258ab67252acb9a8213afb6cf73d817edc6efd8a3a877e978eb97438d` |
| `src/programs/oobe/oobe.c` | `ff273e89746093ef409ecaab15dae0bc86613083f0ecc0eecc07787624be633a` |
| `src/programs/oobe/oobe_catalog.h` | `e82b24f2e78165703289ff1454faade3ad54375e7d6cf8bccef8b75f82940f6e` |
| `src/programs/oobe/oobe.rc` | `f512c8245ca46e1cdd4bde110bf5ac90542072d36efda05ba1eb46b7a956920a` |
| `src/programs/oobe/oobe_worker.h` | `491bf03fa91252c4ded002321c62471b9b4360563fb414ff68eed8ab631cdc41` |
| `src/programs/oobe/resource.h` | `135185122a41dabbdee449565e3381553f54b62728cd9194e9fce894ca7466cf` |

**Binaries (sha256), built from the above on the VM — POST-state, current code**
(`vm_rebuild.log`):
- `wine/configure` args: `--enable-archs=i386,x86_64 --prefix=/home/wineuser/oobe-lab/install`
  (combined WoW64). wine submodule pin: `ed5fe967ed471ca524666631ad70dabbed6af076`.
- `make -C build/dlls/wexhost` → `make_wexhost_rc=0`; `make -C build/programs/oobe` →
  `make_oobe_rc=0` (no format warnings on either arch).
- `build/loader/wine` = `a8e56871b5c728c68699b79d5f6eba0c5e549a705c267d7e8f87052eeed4e4b9`
- `build/loader/wine-preloader` = `6161143796133a42db09df2e3810b27dc3499caa48fd000d24a9e834f5636231`
- `build/programs/oobe/i386-windows/oobe.exe` = `978da0fac2295c94415f43fa1f43686e8eee7ab171743ff8c4265a5f9e09e988`
- `build/programs/oobe/x86_64-windows/oobe.exe` = `ccd32f54b59c66ede2a8478cfdae362a772e3489919daddec5a084999380315b`
- `build/dlls/wexhost/wexhost.so` = `24e7dfcf552fce8a8ba8f9cf2ad10c002a63e1e9695ec36e8cea090c56520071`
- native PE: `native_i386.exe` = `a0d72bfa5c7571ad92cedf02c1537bbd27597fe2988f09238cbef4b4550ded37`
- native PE: `native_x64.exe` = `9adfc6a9b99a5c35a357a6b91d23f83a2aeaae1a4fb4367198cc1167145bd845`

(For reference, the PRE-sync stale binaries were `oobe.exe`(i386)=`d4169a82…`,
`oobe.exe`(x64)=`a8d604ad…`, `wexhost.so`=`a2beea8f…`; all three changed after the rebuild,
confirming the current code was actually compiled in.)

**Pre-existing, non-blocking compiler warning (recorded, not introduced by Q1–Q6):**
`unixlib.c:1171` (`nm_primary_path`) emits `-Wformat-truncation` on the x86_64 build
(`snprintf(path, size, "%s", devs[pick].path)`). This is in code **not touched** by this
milestone and does not fail the build (`make_wexhost_rc=0`). Left as-is to keep the diff
scoped; flagged here for the reviewer.

## §6 — Live acceptance (actual-live-UI / desktop / fresh-installed-image): NOT RUN

The following cells require a live display, real/private vendor packages, and network access
to the approved provider. They were **NOT set up or executed this session** and are recorded
as NOT RUN with prerequisites — not PASS, not "pending". None of Q1–Q6's code is blocked on
them; they remain outstanding acceptance evidence for a later milestone.

| Cell | Category | Status | Prerequisites to run it |
|------|----------|--------|-------------------------|
| Before-state hashes/receipts/defaults + network trace; decline → zero requests | actual-live-UI | **NOT RUN** | Disposable cloned VM (authorized), private vendor packages, SourceForge reachability, interactive display (Xvfb or the VM's LightDM session), full OOBE launch. |
| Explicit download with truthful source/size/progress/cancel/retry; eleven exact package/resource hashes + full rendered terms | actual-live-UI | **NOT RUN** | Same as above + real network fetch of all eleven packages (excluded from synthetic suites by design). |
| No PE execution before acceptance; cached-terms reopen offline | actual-live-UI | **NOT RUN** | Prior download complete, then network cut; fresh prefix. |
| CoreFonts review-only (UI/CLI/protocol — no install path) | actual-live-UI + source-controller | **NOT RUN** (UI); source-controller side covered by synthetic suites (§3) | Live OOBE UI to confirm no install-accept checkbox / no CLI/protocol install verb. |
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
  - The Q5 rendered dialog text is verified by clean two-arch compile + full `oobe.exe` build,
    **not** by an automated UI test (live-UI cell NOT RUN, §6).
  - One pre-existing `-Wformat-truncation` warning in `unixlib.c:1171` (`nm_primary_path`) —
    not introduced here, non-blocking (§5).
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
2. **Q5 rendered-text honesty** is compile- + build-verified but has no automated UI assertion;
   it should be confirmed against the live dialog when §6 is run.
3. **Pre-existing `unixlib.c:1171` `-Wformat-truncation` warning** (not from Q1–Q6) — left to
   keep the diff scoped; reviewer may want it fixed separately.
4. Independent review of the Q1–Q6 diffs and this report is outstanding; **do not treat as
   merge-ready.**

## Private evidence locations (not in public Git)

Host (`D:\Projects\oobe-terms-lab\live\`):
- `logs/matrix.log` — full Python matrix + native build/run (WSL, root), 2026-10-21T07:21Z.
- `logs/followup.log` — post-`%lu` recompile (no format warnings) + corrected review_backend
  (Ran 13 OK) + Gate E direct run (PASSED), 2026-10-21T07:45Z.
- `logs/vm_rebuild.log` — VM incremental rebuild of wexhost.so + oobe.exe (both archs, rc=0),
  source-hash verification (all OK), native x64 PE under WoW64 loader (9/148), hashes/configure,
  2026-10-21T08:32Z.
- `logs/vm_i386_fix.log` — i386 native PE re-run under fresh win64 prefix / WoW64 mode (9/148),
  2026-10-21T08:50Z.
- `make_sync.sh`, `vm_rebuild.sh`, `vm_i386_fix.sh` — the reproducible scripts;
  `oobe-sync.tgz` + `local_manifest.txt` — the synced source set + sha256 manifest.

VM (`~/oobe-lab/LIVE/evidence/`): `vm_rebuild.log`, `vm_i386_fix.log`; built artifacts under
`build/` and native PEs/prefixes under `LAB/native-verify/`.
