<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium Renderer Unblock — Mode-Family Wiring Correction & Narrowed Causal Analysis

**Status:** `OFFLINE_PREP_COMPLETE` (Mode-family wiring corrected; focused execution-path tests added; causal claims narrowed. No VM/Wine/browser executed in this step.)
**Branch:** `codex/chromium-renderer-unblock`
**Parent Work Order Reference:** `docs/CHROMIUM_RENDERER_TERMINATION_WORK_ORDER_20261026.md` (commit `dc8e9b99`)
**Parent Review Reference:** `msg_1793060145336_ce144d6c`
**Prior Runtime-Unblock Commit (imported):** `95f26352` (originally `0206d03c`)
**Prior Live Report:** `docs/CHROMIUM_RUNTIME_UNBLOCK_20261026.md`

---

## 1. Executive Summary

Parent reviewed the runtime-unblock commit and live report, confirmed Chromium now renders its
"Aw, Snap!" `RESULT_CODE_KILLED` page (real UI progress), and identified two concrete defects:

1. **A real mode-family wiring omission** in the prior runtime-unblock change: `runtime-unblock`
   was added to `configure()` and the module-view `selected()`, but was **omitted from both** the
   compositor `strip_wineprefix` option selection and the pre-browser `g4_userenv_r6_probe`
   dispatch branch in `run()`. The live consumer result consequently had **no `api_probe` record** —
   four mounted DLLs did not mean the 21-check probe actually ran.
2. **Over-strong causal claims** in the prior report that attributed the kill to the performance
   manager and treated nearby Wine stubs as a fatal path.

This change makes the smallest functional correction for (1) — extending both selection sites to
`runtime-unblock` and to the new `renderer-unblock` identity (`chromium-renderer-unblock-20261026`)
across the runner and module view — adds a focused execution-path test that proves the compositor
option and probe dispatch are selected per mode, and records the narrowed causal analysis for (2).

---

## 2. Mode-Family Wiring Correction

### 2.1 What was wrong

In `scripts/check-g4-chromium-native-exit.py`, `run()` selected two behaviors by mode:

- Compositor option: `{'strip_wineprefix':True} if MODE in ('userenv-r4','userenv-r6') else {}`
- Pre-browser probe dispatch: `elif MODE=='userenv-r6':` → `g4_userenv_r6_probe.phase(...)`

The prior runtime-unblock commit set `MODE='runtime-unblock'` but left both sites unchanged, so a
`--runtime-unblock` consumer ran the compositor **without** prefix stripping and **never dispatched**
the metadata probe. This is a small functional comparison correction, not a harness redesign.

### 2.2 The fix

`scripts/check-g4-chromium-native-exit.py`:
- `configure(mode)`: allowed modes now include `'renderer-unblock'`; it maps to
  `HOST='chromium-renderer-unblock-20261026'` (STAGE/OUTPUT/PROFILE derive as for the other fresh
  identities).
- `run()` compositor option: `MODE in ('userenv-r4','userenv-r6','runtime-unblock','renderer-unblock')`.
- `run()` probe dispatch: `elif MODE in ('userenv-r6','runtime-unblock','renderer-unblock'):` so the
  R6 registration/probe phase runs for all three scoped modes. The `('userenv-r3','userenv-r4')`
  branch still dispatches `g4_userenv_probe.phase`.
- `main()`: added `--renderer-unblock` to the mutually-exclusive selection group and wired it into
  `configure(...)`.

`scripts/g4_userenv_view.py`:
- `configure(mode)`: allowed modes now include `'renderer-unblock'`, mapped to
  `chromium-renderer-unblock-20261026`.
- `selected()`: returns both `userenv` and `kernelbase` for `x86_64` (`0x8664`) and `i386` (`0x14c`)
  when `MODE in ('userenv-r6','runtime-unblock','renderer-unblock')`.
- `__main__`: added the `--renderer-unblock` CLI flag.

---

## 3. Exact Chromium Process Mapping (observed in the prior live control)

| Role | Windows PID | Type flag / detail |
|---|---|---|
| Main Browser | 220 (`0x00dc`) | main; policy service, screen setup, display 1280x720 detected |
| Crashpad Handler | 256 (`0x0100`) | `--type=crashpad-handler` |
| GPU Process | 568 (`0x0238`) | `--type=gpu-process`; ANGLE / VirGL (D3D12, Intel UHD 750) |
| Storage | 600 | storage utility process |
| Renderer (content) | 708 | `--type=renderer` — never crashed; remained in memory |
| Renderer (content) | 716 | `--type=renderer` — never crashed; remained in memory |
| Network Service | 724 (`0x02d4`) | out-of-process network service, spawned after the in-process one crashed |
| Extension Renderer | 916 (`0x0394`) | `--type=renderer` for extensions; continued running, initialized V8/Blink contexts |
| Metrics | 932 (`0x03a4`) | `--type=utility --utility-sub-type=chrome.mojom.ProcessorMetrics` |

The earlier report lumped several of these together and labeled the renderers generically as "the
killed page." The mapping above keeps roles and lifetimes distinct.

---

## 4. Narrowed Causal Analysis (correcting earlier claims)

Directly observed facts, kept separate from inference:

- **`trace:seh:signal (22, ...)` is not a SIGABRT crash.** Signal 22 is SIGABRT on Linux, but this
  trace line comes from Wine's `msvcrt/except.c` tracing an explicit call to
  `signal(SIGABRT, handler)` — i.e. **registering** a signal handler during base startup, not
  delivering an abort. The prior report over-read it as a crash.
- **`ProcessHandleTable` and `SetProcessMitigationPolicy` stubs are normal.** These are the calls
  Chromium makes from `sandbox::TargetServicesBase::LowerToken()` (token lowering / handle closer)
  during renderer startup; they are not evidence of a fatal path.
- **Renderers 708 and 716 did not crash or call `TerminateProcess`;** they remained running in
  memory at observation time.
- **The in-process Network Service crashed at 23:54:04.196** (`network_service_instance_impl.cc:652`),
  which caused the browser to spawn the out-of-process Network Service (PID 724). This is a real,
  separately-timestamped restart — not the same event as the later tab kill.
- **At ~16.8s post-launch the browser declared `Tab Killed: http://127.0.0.1:33071/`**
  (`RESULT_CODE_KILLED`) for that navigation, followed by a page-discard log line. Renderer 916
  (the extensions renderer) **continued running** and initialized V8/Blink script contexts afterward.

Interpretation (candidate inference, not yet confirmed): the observed terminal state is a killed
navigation result on the fixture URL with an earlier, independent in-process network-service crash.
The `sad_tab` / `page_discarding` messages do **not** by themselves prove the performance manager
caused the termination, and the nearby mitigation/handle-table stubs do **not** prove a fatal path.
Distinguishing the actual kill reason is the purpose of the next authorized live step; no speculative
patch is claimed here as a fix.

---

## 5. Focused Execution-Path Test Outcomes

`scripts/test_g4_renderer_unblock.py` (new) drives the real `run()` with all external effects mocked
and asserts the pure selection logic:

1. `test_strip_wineprefix_selected_for_scoped_modes_only` — `strip_wineprefix=True` for
   `userenv-r4`, `userenv-r6`, `runtime-unblock`, `renderer-unblock`; absent for `r1`,
   `module-warn-r2`, `userenv-r3`.
2. `test_probe_dispatch_r6_for_scoped_and_normal_for_r3_r4` — `g4_userenv_r6_probe.phase` dispatched
   for `userenv-r6`, `runtime-unblock`, `renderer-unblock`; `g4_userenv_probe.phase` for
   `userenv-r3`, `userenv-r4`.
3. `test_renderer_unblock_mode_paths_agree_across_scripts` — `configure('renderer-unblock')` agrees on
   `HOST`/`STAGE`/`OUTPUT`/`PROFILE` in both the runner and the module view.
4. `test_renderer_unblock_selected_modules_both_arches` — exactly 4 modules (`kernelbase.dll` +
   `userenv.dll` for `x86_64` and `i386`).
5. `test_renderer_unblock_cli_flag_selects_mode_and_rejects_combination` — `--renderer-unblock`
   selects the mode and is mutually exclusive with the other selection flags.

Run under WSL Debian Python 3.13.5 (from `scripts/`):

| Suite | Result |
|---|---|
| `test_g4_renderer_unblock.py` (new) | **5 passed** |
| `test_g4_runtime_unblock.py` | 7 passed |
| `test_g4_chromium_native_exit.py` | 11 passed |
| `test_g4_userenv_view.py` | 15 passed |

**Total: 38 passed, 0 failures, 0 errors.** `git diff --check` clean (0 whitespace errors). These
import-style suites must be run from the `scripts/` directory (bare sibling imports); the repo-root
`scripts/test_...py` form fails on module resolution for all of them (pre-existing design property).

---

## 6. Deliverable Commits & Next Steps

- **Branch:** `codex/chromium-renderer-unblock`
- **Changed in this commit:**
  - `scripts/check-g4-chromium-native-exit.py` — mode-family wiring (configure, run compositor +
    probe dispatch, main CLI).
  - `scripts/g4_userenv_view.py` — renderer-unblock identity, module-view selection, CLI flag.
  - `scripts/test_g4_renderer_unblock.py` — new focused execution-path suite.
  - `docs/CHROMIUM_RENDERER_UNBLOCK_20261026.md` — this report.
- **Next (Antigravity):** review, compile the native helper PE matching
  `chromium-renderer-unblock-20261026`, and prepare the next authorized live step to distinguish the
  actual kill reason (narrowly scoped process termination/status traces or existing Chromium logging;
  no debugger attach, sandbox/privilege bypass, or repeated blind trials).

Private screenshots/runtime/logs remain on D: private storage. Stopping for parent review.
