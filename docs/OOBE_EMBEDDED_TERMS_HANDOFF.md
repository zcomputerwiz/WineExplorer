# OOBE Embedded Terms — Work Handoff (post Round 3, 2026-10-24)

For an agent or human taking over this milestone. The authoritative status lives in
`docs/OOBE_EMBEDDED_TERMS_ACCEPTANCE.md`; this document adds the operational context that
document does not carry: environments, evidence locations, what was found in Round 3,
and exactly what remains.

## 1. Status in one paragraph

The OOBE embedded-terms milestone (Q1–Q6 repairs, Review Rounds 1–2, and the Round-3 native
verification round) is **complete on this branch and stopped before merge**, awaiting
independent Round-3 review. All executed suites pass; the remaining NOT-RUN cells in §6 of
the acceptance report are prerequisite-gated live-network / desktop evidence, not code
defects. During Round 3, an independent re-review of the R2 diff found and fixed one
substantive bug: the new op-id validator used the prefix `review_` while the production
coordinator has generated `op_<verb>_<32 lowercase hex>` ids since before Q1–Q6 (the R2
review document misstated the grammar; see §4 below).

## 2. Branch & commit state

Branch: **`gemini/oobe-embedded-terms`** (remote `github.com/zcomputerwiz/WineExplorer`).
Working tree clean; HEAD pushed as of 2026-10-24.

| Commit | Content |
|---|---|
| `f10d5171` | Q1–Q6 repairs with acceptance evidence (baseline) |
| `2bc4555b` | Review Round 1 repairs F1–F3 (bridge hardening, damaged-doc isolation, dialog reflow) |
| `1df4688d` | **Round 2**: F1 review op-id validation + zero-total RUNNING reject (`unixlib.c`); F2 lone-surrogate catalog fix (`wex-components.py`); tests; report |
| `06559739` | **Round 3 (docs)**: fresh-clone VM native rebuild + both-PE welcome-page execution, recorded in the acceptance report |
| `765dd336` | **Round 3 correction**: op-id grammar prefix `review_`→`op_` to match the production coordinator; test fixtures; rebuilt `wexhost.so`; re-execution; report updates |

Related branch (reviewer artifacts, not merged): `codex/oobe-embedded-terms-r2-review`
(`fbb7a315`) holds `docs/reviews/qwen-oobe-20261022-r2.md` — the R2 review doc. **Note: that
doc's statement of the coordinator id grammar is wrong** (it says `review_<verb>_<hex>`);
the code in `wex-components.py` is the source of truth (`op_{verb}_{uuid.uuid4().hex}`).

## 3. Current binary state (VM-verified, 2026-10-24)

- `build/dlls/wexhost/wexhost.so` = `77ef90c2a6b316cf436b857aed60fb035d0542f8ba09ea14d2505fdbe585a0be`
  (contains the R2 delta **and** the Round-3 grammar correction)
- `build/programs/oobe/i386-windows/oobe.exe` = `c1421b7a…`, `x86_64-windows/oobe.exe` =
  `f6f8edad…` — byte-identical to the Round-1 POST-state PEs; no PE-target source has changed
  since, so this is by construction, not an omission.
- Both PEs executed to fully rendered welcome pages against the current `wexhost.so`
  (Xvfb :95/:96, fresh prefix): pre-fix run per `r3_exec_oobe.log`, post-fix run per
  `r3b_exec_oobe.log`.
- Full test matrix last verified at 180 tests / all rc=0 in the **original** environment
  (`logs/r2_matrix.log`); see §6 for why it must be re-run before merge.

## 4. What Round 3 found (read this before touching the bridge code)

**The op-id grammar bug (fixed in `765dd336`).** R2 added `oobe_valid_review_op_id()` to
`src/dlls/wexhost/unixlib.c`, requiring START_REVIEW ack ids to match `review_<verb>_<32 hex>`.
The production coordinator (`scripts/wex-components.py`, `oobe_coordinator()`) generates
`op_{verb}_{uuid.uuid4().hex}` — prefix **`op_`** — a format stable since before Q1–Q6
(introduced `e954c78c`, uuid-ified in `3d70da25`). The R2 review doc misstated the grammar
and the implementation followed it, so with R2's `wexhost.so` deployed **every legitimate
START_REVIEW acknowledgement would have been rejected** ("Malformed coordinator
acknowledgement") — the native review-download flow fails closed end-to-end. It went unseen
because none of the executed evidence ever drives START_REVIEW: welcome-page executions stop
before it, and the full vendor-network flow is a NOT-RUN cell.

Fix: prefix corrected to `op_` (function-body-only), all `test_oobe_bridge.py` fixtures moved
to the production grammar, `.so` rebuilt, bridge suite 17/17 OK. Lesson for future rounds:
**validate protocol assumptions against the actual coordinator code, not against review-doc
prose.** The validator itself is otherwise sound (no over-reads; short strings hit `\0` in the
hex loop; exact full-verb match also handles the underscore-bearing verb `d3dcompiler_47`).

## 5. Environment map

**Lab VM — `WEX-OOBE-Fresh-20261024`** (VirtualBox, on this Windows host):
- SSH: port **2223**, user `wineuser`, key `D:\Projects\VirtualBox_VMs\vm_key`
  (`ssh -i <key> -p 2223 wineuser@127.0.0.1`). Passwordless sudo available.
- It is a **flattened clone** of the original lab VM (`WEX-OOBE-Lab`, kept as-is for history).
  Other workstream data was removed from the clone only (originals untouched): disk went
  97% → ~34% used (~38 G free). Home is essentially just `~/oobe-lab`.
- Lab layout: `~/oobe-lab/wine/` = Wine source tree (pin `ed5fe967`) with project dirs
  **symlinked in** (`wine/dlls/wexhost -> ../../src/dlls/wexhost`, same for the oobe program
  dir) — so syncing a file to `src/…` updates the build-tree copy automatically. `build/` is
  the combined WoW64 build tree (`--enable-archs=i386,x86_64`).
- Rebuild: `make -C ~/oobe-lab/build/dlls/wexhost` (unixlib.c → wexhost.so); the oobe program
  targets only rebuild when `src/programs/oobe/*` changes.
- Exec harness scripts live in `~/oobe-lab/LIVE/sync-r3/` (`r3_rebuild.sh`,
  `r3_exec_oobe.sh`, `r3b_exec_oobe.sh`, `sync_r3_vm.sh`) and write evidence to
  `~/oobe-lab/LIVE/evidence/`. Xvfb + import are installed; each exec run creates a fresh
  prefix under the evidence dir and removes it at the end.
- Source sync pattern: tar the changed repo files (repo-relative paths) with a sha256
  manifest, scp to `$LAB/LIVE/sync-r3/`, then run the sync script which verifies pre-deploy,
  deploys, and re-verifies in place (`sha256sum -c --strict`).

**Host evidence — `D:\Projects\oobe-terms-lab\live\`** (private, NOT in public Git):
- `logs/`: `matrix.log`, `followup.log`, `r1_matrix.log`, `r2_matrix.log`, `vm_rebuild.log`,
  `vm_i386_fix.log`, `r3_pre_deploy_hashes.txt`, `r3_rebuild.log`, `r3_exec_oobe.log`,
  `oobe_*_r3*.wine.log`, `r3b_exec_oobe.log`
- `evidence-ui/`: rendered dialogs (R1 F3) + welcome-page screenshots
  (`oobe_{i386,x64}_welcome.png` R1, `_r3_` Round-3 pre-fix, `_r3b_` post-fix)
- `r3-deployed/`: copies of the VM-deployed R2 sources (byte-identical to the repo at the
  time; kept as sync evidence)

**Gone / not reproducible here:** the WSL-root environment where the full Python matrix was
originally run (`/root/WineExplorer` no longer exists in any WSL distro on this host). See §6.

## 6. Re-running verifications

- **Bridge suite (covers the C seam; self-contained):** needs only `gcc` + `python3`.
  `cd <repo> && python3 scripts/test_oobe_bridge.py` — compiles the current `unixlib.c`
  production functions under `-Wall -Wextra -Werror` and drives them over a live AF_UNIX
  socket. Last run: **17/17 OK** (2026-10-24, both on the lab VM and post-fix).
- **Full matrix (180 tests, 10 entries):** several suites (`test_oobe_review.py`,
  `test_component_review.py`, `test_runtime_repairs.py`, `test_gate_e_synthetic.py`, the
  `test_fluent_*` pair) **require root** and/or repo-root paths — running them as a normal
  user fails by design ("OOBE coordinator review tests require root"). The original run was
  WSL + root against a full checkout. To re-verify: recreate that environment (any root Linux
  with python3 + gcc, full repo checkout), then run each `scripts/test_*.py` plus the Gate E
  entry and compare per-file `Ran N … OK` lines against `logs/r2_matrix.log`. **This is a
  required gate before merge** — the 180-test claim currently rests on `r2_matrix.log` alone.
- **VM rebuild + welcome-page execution:** sync changed files (pattern in §5),
  `make -C ~/oobe-lab/build/dlls/wexhost`, then run an exec harness from
  `~/oobe-lab/LIVE/sync-r3/` (copy one and rename its log/png suffixes so evidence never
  overwrites earlier rounds').

## 7. Outstanding items (for the reviewer / next agent)

1. **Round-3 review of `1df4688d` → `765dd336`** is requested; do not treat the branch as
   merge-ready.
2. **Re-run the full matrix** in a root environment (§6) and record it as `r3_matrix.log`
   (or equivalent) before merge.
3. **NOT-RUN cells in acceptance-report §6 remain prerequisite-gated**, not defects: the full
   vendor-network download flow (needs private vendor packages + SourceForge reachability),
   live-desktop / fresh-installed-image cells, and offline cached-terms reopen. The op-id fix
   makes the native START_REVIEW path *correct*, but it is still only exercised by the compiled
   seam test, not by a live end-to-end download.
4. **Pre-existing `-Wformat-truncation` warning** at `unixlib.c:1171` (`nm_primary_path`) —
   not from this milestone; left to keep diffs scoped. Reviewer may want it fixed separately.
5. **The R2 review doc's grammar statement is wrong** (`review_` vs the actual `op_`). Flag it
   so future rounds quote coordinator code, not prose. (Commit history is not rewritten;
   `765dd336`'s message documents the correction.)
6. The original lab VM (`WEX-OOBE-Lab`) and its 97%-full disk are untouched; the clone
   (`WEX-OOBE-Fresh-20261024`) is the working environment and has ~38 G free.

## 8. Key files

| What | Where |
|---|---|
| Acceptance report (status of record) | `docs/OOBE_EMBEDDED_TERMS_ACCEPTANCE.md` |
| This handoff | `docs/OOBE_EMBEDDED_TERMS_HANDOFF.md` |
| R2 review doc (contains the grammar error) | branch `codex/oobe-embedded-terms-r2-review`, `docs/reviews/qwen-oobe-20261022-r2.md` |
| Bridge C seam + validator | `src/dlls/wexhost/unixlib.c` (`oobe_valid_review_op_id`, `oobe_review_start`, `oobe_parse_review_running`) |
| Coordinator (protocol source of truth) | `scripts/wex-components.py` (`oobe_coordinator()`, op-id at the `op_{verb}_{uuid…}` line) |
| Bridge seam tests | `scripts/test_oobe_bridge.py` (17 tests) |
| OOBE program + resources | `src/programs/oobe/` (`oobe.c`, `oobe.rc`, `oobe_catalog.h`) |
| Private evidence | `D:\Projects\oobe-terms-lab\live\` (§5) |