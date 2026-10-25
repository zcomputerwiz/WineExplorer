<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# API probe deadline float admission fix (R4 rework)

Branch `codex/chromium-api-probe-deadline`, worktree D:/DeepThought/Worktrees/chromium-api-probe-deadline/WineExplorer, base fec7fdd3815b432c3f6fb1f7da7c4d39d9ae8b6e.

## Defect (before)

`scripts/g4_userenv_probe.py:phase` admitted the 45-second phase by reconstructing a local duration:

```python
start = now(); end = min(deadline, start + 45)
if end - start < 45: raise TimeoutError('Need full45s API probe/reap/intermediate cleanup allowance')
```

When the caller allowance is ample, `end` equals `start + 45`, and the check recomputes `(start + 45) - start`. IEEE-754 does not guarantee that round-trips to exactly 45: with `now = lambda: 31.1` and `deadline = 200`, `(31.1 + 45) - 31.1 == 44.99999999999999 < 45`, so the phase raised the admission TimeoutError before touching output even though the caller allowed 168.9s. The same arithmetic occurs at 40.1, 45.1, 50.1, 60.1, 61.1, 63.1 (parent reproduction). The R4 trial died on exactly this false positive; the API was never probed.

## Fix (after)

Compare the supplied absolute deadline directly against the computed absolute phase end; no subtraction or reconstruction of a local duration:

```python
start = now(); child = None; primary = None
result['probe_started'] = False; result['probe_reaped'] = False
if deadline < start + 45: raise TimeoutError('Need full45s API probe/reap/intermediate cleanup allowance')
end = start + 45
```

- Phase stays capped at exactly 45s (`end = start + 45`); once admission passes, the old `min(deadline, ...)` could never be smaller, so admitted behavior is unchanged.
- Error message and all run25/reap5/intermediate-cleanup15 reservations (`run_end = end - 20`, `left(1)`, `left(5)`, `left(8)`), quota, decode, timeout, and cleanup logic are untouched.
- `probe_started`/`probe_reaped` are initialized to False before admission, so a short-allowance rejection leaves explicit `False` evidence in `result` (previously the keys did not exist at all).
- No broad tolerance added, no budget expansion, no Wine API, prefix-idleness, or process-ownership changes.

## Tests (scripts/test_g4_userenv_view.py, ProbeTests)

All use injected monotonic clocks (`now=lambda: clock[0]`) and synthetic popen/helpers; no sleeps and no real Wine process.

1. `test_fractional_clock_starts_admit_and_complete_full_phase` — starts 31.1 (parent reproduction), 15.9/16.1, 31.9/32.1, 63.9/64.1, 127.9/128.1 (binary magnitude boundaries), deadline `start + 100`. Asserts the phase reaches and completes the existing synthetic API/cleanup path: child spawned exactly once, `probe_started` and `probe_reaped` True, `api_probe.passed` True, `linux_exit` 0, `started_monotonic == start`, no `cleanup_error`, clock never exceeds `start + 45`.
2. `test_exact_45s_allowance_admits_and_completes` — deadline consistently constructed as the absolute `start + 45.0`; admits and completes with the same full-path assertions.
3. `test_short_44p99s_allowance_rejects_before_child` — deadline `start + 44.99` at starts 0.0, 31.1, 64.1; asserts TimeoutError with the exact message 'Need full45s API probe/reap/intermediate cleanup allowance', zero popen calls (no child started), no cleanup invoked, and `result.get('probe_started') is False` / `result.get('probe_reaped') is False`.
4. `test_deadline_exhaustion_during_run_kills_reaps_and_cleans` — ample deadline, but the injected clock jumps so the run loop hits `run_end = end - 20` with the child still running; asserts TimeoutError('API probe25s timeout') propagates as primary, the child is killed and reaped within its reserved `left(5)`, wineserver `-k`/`-w` intermediate cleanup runs to completion (no `cleanup_error`), and the clock never exceeds `start + 45`.

Existing reservation, fault-matrix, and decode negative tests are unchanged and still pass.

## Verification (Debian WSL, worktree scripts/, branch source)

Command:

```bash
python3 -B -m unittest test_g4_compositor_scope test_g4_wexcomp_harness test_graphics_resources test_g4_userenv_view test_g4_chromium_userenv_r4_adapter test_g4_chromium_userenv_adapter test_g4_chromium_module_adapter test_g4_chromium_native_adapter test_g4_chromium_native_exit test_g4_native_exit test_g4_native_exit_calibration test_g4_edge_http_fixture -v
```

Result: Ran 95 tests — OK, 0 errors, 0 failures, 16 skipped, ~30s. The 16 skips are pre-existing private-adapter cases in the four chromium adapter modules (UserenvAdapterTests/ModuleAdapterTests/AdapterTests guard, diagnostic1-archive, fixed-root-view, transitive-inventory, contract-reader and fixed-r2-contract tests) that require parent-only fixtures; they are reported as skipped, not passed. Python -B, no package installs or builds.

## Limitations

- Deterministic clock tests exercise the real `phase` admission/run/reap/cleanup control flow with synthetic children; they do not run a real Wine API probe and cannot prove wall-clock behavior against a live guest.
- The fix changes only the admission comparison; any future code that reconstructs local durations from absolute clocks reintroduces the same bug class.

## Independent parent acceptance

Parent reviewed e3ec82e0 code and new clock assertions, passed14 focused probe/
scope tests in2.507s, and imported the change as832da342 on the graphics branch.
Then independently ran the full listed focused group with the parent's private
adapter inputs: **95 tests in48.340s, all pass, no skips**. This supplements the
worker's honestly reported16 private-fixture skips; it does not relabel that
worker run. No guest was launched. Fresh R5 preparation is separately assigned
under CHROMIUM_R5_OFFLINE_PREPARATION_WORK_ORDER_20261025; R4 remains consumed.
