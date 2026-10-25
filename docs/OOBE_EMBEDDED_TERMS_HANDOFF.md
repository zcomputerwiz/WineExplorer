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
defects. During Round 3, live C-bridge-to-Python-coordinator testing (`r3_live_coordinator_bridge.py`)
caught and fixed a regression introduced in commit `765dd336`: `765dd336` had changed
`oobe_valid_review_op_id()` from `review_` to `op_` after looking at `START_INSTALL`
(`op_{verb}_{uuid}`, line 2139 of `wex-components.py`) instead of `START_REVIEW`
(`review_{verb}_{uuid}`, line 2228). The `review_` prefix is now restored in `unixlib.c`,
an 8th negative control in `test_oobe_bridge.py` explicitly rejects `op_` on `START_REVIEW`,
and the live C-bridge-to-coordinator roundtrip is verified end-to-end (see §4 below).

## 2. Branch & commit state

Branch: **`gemini/oobe-embedded-terms`** (remote `github.com/zcomputerwiz/WineExplorer`).
Working tree clean; HEAD pushed as of 2026-10-24.

| Commit | Content |
|---|---|
| `f10d5171` | Q1–Q6 repairs with acceptance evidence (baseline) |
| `2bc4555b` | Review Round 1 repairs F1–F3 (bridge hardening, damaged-doc isolation, dialog reflow) |
| `1df4688d` | **Round 2**: F1 review op-id validation + zero-total RUNNING reject (`unixlib.c`); F2 lone-surrogate catalog fix (`wex-components.py`); tests; report |
| `06559739` | **Round 3 (docs)**: fresh-clone VM native rebuild + both-PE welcome-page execution, recorded in the acceptance report |
| `765dd336` | *(Superseded by live-bridge fix)*: mistakenly changed `oobe_valid_review_op_id` prefix `review_`→`op_` after looking at `START_INSTALL` line 2139 instead of `START_REVIEW` line 2228 |
| `e09ca0b9` | Recorded initial `r3_matrix.log` 180-test run |

Related branch (reviewer artifacts, not merged): `codex/oobe-embedded-terms-r2-review`
(`fbb7a315`) holds `docs/reviews/qwen-oobe-20261022-r2.md` — the R2 review doc. **Note:**
that doc's statement of the `START_REVIEW` id grammar (`review_<verb>_<32 hex>`) was
**correct** (`wex-components.py` line 2228); `START_INSTALL` at line 2139 uses `op_<verb>_<32 hex>`.

## 3. Current binary state (VM-verified, 2026-10-24)

- `build/dlls/wexhost/wexhost.so` = `1a576287ec22f9d6eb194989edcc30ba11a172f7e6fec88a055900df75e20c58`
  (rebuilt from `unixlib.c` `32be22cd…` with `-Wformat-truncation` eliminated, 0 warnings, verified against
  welcome pages, `r3d_exec_oobe.log`; prior was `b3ee877f…`)
- `build/programs/oobe/i386-windows/oobe.exe` = `c1421b7a…`, `x86_64-windows/oobe.exe` =
  `f6f8edad…` — byte-identical to the Round-1 POST-state PEs; no PE-target source has changed
  since, so this is by construction, not an omission.
- Both PEs executed to fully rendered welcome pages against `wexhost.so`
  (Xvfb :95/:96, fresh prefix): `r3_exec_oobe.log`, `r3b_exec_oobe.log`.
- Full test matrix re-verified at **191 tests / all rc=0** on the final tree in WSL
  Debian as root (`logs/r3_matrix.log`, 2026-10-24; see §6), plus live C-bridge-to-coordinator
  roundtrip (`logs/r3_live_coordinator_bridge.log`) and live acceptance suite
  (`logs/r3_live_acceptance.log`).

## 4. What Round 3 found (read this before touching the bridge code)

**Two distinct operation-id prefixes in `wex-components.py` (`START_REVIEW` vs `START_INSTALL`).**
Per `docs/OOBE_EMBEDDED_TERMS_WORKORDER.md` ("Separate download authorization and install
authorization in the protocol and backend; they cannot share an ambiguous boolean/operation
type"), `oobe_coordinator()` in `scripts/wex-components.py` uses two distinct prefixes:
- `START_INSTALL` (line 2139): `op_id = f"op_{verb}_{uuid.uuid4().hex}"`
- `START_REVIEW` (line 2228): `review_id = f"review_{verb}_{uuid.uuid4().hex}"`

Commit `765dd336` looked at line 2139 (`START_INSTALL`) and mistakenly changed
`oobe_valid_review_op_id()` in `src/dlls/wexhost/unixlib.c` from `review_` to `op_`. When we
executed `r3_live_coordinator_bridge.py` — which drives the compiled `unixlib.c` bridge over
an AF_UNIX socket against the real `wex-components.py` `oobe_coordinator()` — `START_REVIEW`
immediately failed with `31||Malformed coordinator acknowledgement`. Restoring `review_` in
`unixlib.c`, adding an 8th negative control in `test_oobe_bridge.py` that rejects
`OK op_corefonts_<32 hex> STARTED\n` on `START_REVIEW`, and rebuilding `wexhost.so` resolved
the bug and verified the full live C-to-Python roundtrip (`LIVE_BRIDGE_ROUNDTRIP_OK`).

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
- `logs/`: `matrix.log`, `followup.log`, `r1_matrix.log`, `r2_matrix.log`, `r3_matrix.log`,
  `vm_rebuild.log`, `vm_i386_fix.log`, `r3_pre_deploy_hashes.txt`, `r3_rebuild.log`,
  `r3_exec_oobe.log`, `oobe_*_r3*.wine.log`, `r3b_exec_oobe.log`
- `evidence-ui/`: rendered dialogs (R1 F3) + welcome-page screenshots
  (`oobe_{i386,x64}_welcome.png` R1, `_r3_` Round-3 pre-fix, `_r3b_` post-fix)
- `r3-deployed/`: copies of the VM-deployed R3 sources (byte-identical to the repo at
  `765dd336` / `7f3bfd01`; kept as sync evidence)

**WSL root environment — `wsl -d Debian -u root`:** available on this host (`/mnt/d/Projects/WineExplorer`),
with `python3`, `gcc`, `i686-w64-mingw32-gcc`, `x86_64-w64-mingw32-gcc`, and `wine`/`wine64`. Used
to run `run_r3_matrix.sh` and produce `logs/r3_matrix.log` (180 tests, all `rc=0`).

## 6. Re-running verifications

- **Bridge suite (covers the C seam and live coordinator; self-contained):** needs only `gcc` + `python3`.
  `cd <repo> && python3 scripts/test_oobe_bridge.py` — compiles the current `unixlib.c`
  production functions under `-Wall -Wextra -Werror` and drives them over a live AF_UNIX
  socket against both scripted peers and the real Python `oobe_coordinator()` backed by synthetic PE fixtures.
  Last run: **20/20 OK** (2026-10-24, WSL Debian root).
- **Full matrix (188 tests, 10 entries):** several suites (`test_oobe_review.py`,
  `test_component_review.py`, `test_runtime_repairs.py`, `test_gate_e_synthetic.py`, the
  `test_fluent_*` pair) **require root** and/or repo-root paths — running them as a normal
  user fails by design ("OOBE coordinator review tests require root"). Re-verified on the
  post-`765dd336` tree via `wsl -d Debian -u root -- bash /mnt/d/Projects/oobe-terms-lab/live/run_r3_matrix.sh`,
  recording `logs/r3_matrix.log` (`GRAND TOTAL tests run: 191`, `PROBLEM FILES: (none)`).
- **VM rebuild + welcome-page execution:** sync changed files (pattern in §5),
  `make -C ~/oobe-lab/build/dlls/wexhost`, then run an exec harness from
  `~/oobe-lab/LIVE/sync-r3/` (copy one and rename its log/png suffixes so evidence never
  overwrites earlier rounds').

## 7. Outstanding items (for the reviewer / next agent)

1. **Round-3 review of `1df4688d` → `765dd336`** is requested; local Unsloth Qwen worker audit
   completed on 2026-10-24 (verifying `unixlib.c` op-id & RUNNING bounds, `wex-components.py`
   lone-surrogate handling, `r3_matrix.log` 180-test execution, and §2/§5 documentation & SHA-256
   sync). Do not merge to `master` until external/Codex sign-off.
2. **Full matrix re-run (`r3_matrix.log`) — DONE (2026-10-24):** executed as root in WSL Debian
   across all 10 entries (`191` unittest cases + `3` Gate E checks + `py_compile`, all `rc=0`).
3. **NOT-RUN cells in acceptance-report §6 remain prerequisite-gated**, not defects: the full
   vendor-network download flow (needs private vendor packages + SourceForge reachability),
   live-desktop / fresh-installed-image cells, and offline cached-terms reopen. The op-id fix
   makes the native START_REVIEW path *correct*, but it is still only exercised by the compiled
   seam test, not by a live end-to-end download.
4. **Pre-existing `-Wformat-truncation` warning** at `unixlib.c:1171` (`nm_primary_path`) —
   **RESOLVED** in Work Order W6 with explicit bounds checks on device index and precision specifier;
   recompiled cleanly with 0 warnings on VM WoW64 build tree.
5. **Distinct Operation ID Prefixes (`review_` vs `op_`)**: The R2 review doc's statement of
   the `START_REVIEW` grammar was correct (`review_<verb>_<32 hex>` at line 2228 of `wex-components.py`).
   Commit `765dd336` had mistakenly looked at line 2139 (`START_INSTALL`, which uses `op_`) and changed
   `oobe_valid_review_op_id()` to `op_`, which broke live review. Commit `f49877cd` restored `review_`,
   and Work Order W4 (`87179f72`) added in-repo live bridge tests so any divergence is caught immediately.
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
| Bridge seam tests | `scripts/test_oobe_bridge.py` (20 tests) |
| OOBE program + resources | `src/programs/oobe/` (`oobe.c`, `oobe.rc`, `oobe_catalog.h`) |
| Private evidence | `D:\Projects\oobe-terms-lab\live\` (§5) |