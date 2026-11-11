<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# U1-M1 / U1-M1R / U1-M1R2 / U1-M1R3 runner repair and fake-loader regression checks

Status (2026-11-11): `run-wine.sh` repaired per work orders U1-M1, U1-M1R,
U1-M1R2, and U1-M1R3 reviewer correction in `docs/uwp/EXECUTION_PLAN.md`; `test-run-wine.sh`
passes 142/142 checks under Git Bash with a fake loader and fake wineserver.
Codex reproduction script `unsloth-tmp/uwp-u1-20261111/review-runner-wait-failure.sh`
now passes with `RUNNER_EXIT=1` and `PREFIX_RETAINED`. No real Wine, no builds, no
WSL, no VM and no shared lab access were used. This is not a runtime gate: a
fake-loader pass does not establish packaged or unpackaged Wine behavior (see
the plan).

## What changed in run-wine.sh (U1-M1, U1-M1R, U1-M1R2, and U1-M1R3)

* **No more false successful exits.** A failure of either architecture,
  malformed or extra output, a nonzero probe exit, a timeout, or an oracle
  HRESULT mismatch makes the overall exit nonzero (1). The per-arch
  verdict in `actual-wine-<arch>.txt` is PASS only when the case shape
  (exactly two lines, one `HRESULT unpackaged 0x........` line, one
  `PASS negative_activation` line, exit 0) and the oracle HRESULT
  (`0x80040154`) both match; `compare-<arch>.txt` records which part failed.
  CRLF normalization before matching is preserved, and the package-activation
  cases remain explicitly NOT-RUN.
* **Wineboot failure/timeout fails immediately even if marker exists.**
  A nonzero exit or timeout from `wineboot` fails the run with exit 2 even if
  the `syswow64/kernel32.dll` marker file was created on disk before failure.
  The prefix is retained for diagnostics.
* **Bounded Wine commands with kill-after and validated bounds.** `wineboot`,
  the feature-gate `reg add`, probe runs, and wineserver cleanup commands go
  through `timeout` with `-k` (kill-after deadline, defaults 10 s, overridable
  via `WEX_U1_KILL_AFTER`). Every timeout bound (`WEX_U1_WINEBOOT_TIMEOUT`,
  `WEX_U1_REG_TIMEOUT`, `WEX_U1_PROBE_TIMEOUT`, `WEX_U1_WINESERVER_TIMEOUT`,
  `WEX_U1_KILL_AFTER`) is strictly validated to be a finite positive integer
  before any mutation; invalid or zero values exit 2. A command that traps or
  ignores `SIGTERM` is forcefully killed with `SIGKILL` after the kill-after
  bound.
* **Matching wineserver binary required before mutation.** The runner resolves
  the matching `wineserver` binary at `<build>/server/wineserver` (standard Wine
  build tree layout) or `<build>/wineserver` before creating the prefix,
  results dir, or ownership lock. If neither exists or is executable, the runner
  exits 2 without mutating the environment.
* **Scoped wineserver cleanup without global process scans.** The runner never
  scans global processes via `pgrep` or `/proc`. Cleanup is strictly scoped to
  the fresh owned prefix using the matching binary with bounded `-k` and `-w`.
* **Strict wineserver wait verification (U1-M1R2).** In Wine's `server/main.c`,
  `kill_lock_owner` returns whether a server lock owner existed (exiting `!ret`,
  meaning exit status 1 when no server was running), but the `-w` handler executes
  `wait_for_lock()` and exits 0 on successful wait. ANY nonzero `-w` status
  indicates wait failure and triggers cleanup failure (forcing exit 1 and prefix
  retention). `-k` status 1 remains permitted as a benign indicator that no server
  was running.
* **Missing/nonexecutable matching server during cleanup is a failure (U1-M1R2).**
  If the matching wineserver binary disappears or loses executable permission after
  the probe run, `cleanup_server` returns 1 (failure) rather than 0. The runner
  retains the prefix and lock, logs diagnostics, and returns exit 1.
* **Prefix deletion errors never silently succeed (U1-M1R2).** The runner does not
  suppress prefix removal errors (`rm -rf || true` removed). If prefix deletion fails,
  cleanup failure is flagged, exit 1 is returned, diagnostics are logged, and prefix
  state is retained.
* **Summary status coherence matching final exit (U1-M1R2).** Machine-readable
  `cleanup: PASS|FAIL` and `overall: PASS|FAIL` in `summary.txt` are updated in the
  `EXIT` trap to reflect the final exit status. `overall: PASS` is never reported
  after cleanup failure, prefix deletion error, or lost ownership; diagnostics are
  fully retained.
* **Coherent post-server ownership and missing prefix failure (U1-M1R3).** Cleanup
  executes bounded wineserver cleanup only when the prefix exists and ownership is
  verified, then performs a single coherent ownership and existence verification
  after server wait. If the ownership lock changes during `wineserver -k` or `-w`
  (late ownership loss), or if the prefix is unexpectedly missing at cleanup,
  cleanup failure is flagged (`cleanup_ok=false`), overall exit 1 is returned, any
  remaining state (including a changed lock) is retained without deletion, and
  `summary.txt` records `cleanup: FAIL` and `overall: FAIL`.
* **Provable exact invocation identity lock.** Prefix ownership uses an exact
  invocation token (`wex-u1-invoc:pid=$$:time=...:rand=...`) written with
  `noclobber`. Cleanup compares the token for exact equality rather than a
  pid-prefix grep. If the lock is altered, missing, or belongs to another
  invocation, the runner refuses to delete the lock, retains the prefix for
  diagnostics, and forces a failure exit. Preexisting prefixes or held locks are
  refused with exit 3.
* **Canonicalization before refusal.** Build and prefix paths are
  canonicalized with `realpath -m` (resolving symlinks and `..`) before the
  refusal checks, which cover the wexbuild test fixture paths
  (`/var/lib/wexsys-c2`, `/home/wexadm`, `/home/wexstd`, `/home/wexcar`,
  `/var/lib/wexgreeter`) and any involvement of `/etc/wineexplorer` or
  `/usr/local/libexec/wineexplorer` (the live installed system-role
  configuration and runtime), for both the prefix and the build dir.
* **Fresh atomic results directory creation.** An optional third positional
  argument selects a fresh results directory. The directory is created with
  an atomic `mkdir -- "$RESULTS"` (never check-then-`mkdir -p`), rejecting
  existing directories, symlink/reparse paths or parents, the caller's cwd,
  the tracked fixture directory, and paths inside the prefix. The default is
  a fresh temporary directory via `mktemp -d`. Per architecture the results dir
  keeps `raw-<arch>.txt` (exact probe output, CRLF intact), `exit-<arch>.txt`,
  `actual-wine-<arch>.txt`, `compare-<arch>.txt`, plus `summary.txt`.
* **Exact usage.** `usage: run-wine.sh <wine build dir> <prefix> [results
  dir]`; exit codes 0 (pass), 1 (case failure or cleanup failure),
  2 (usage/prerequisite error), 3 (refused).

## Fake-loader and fake-wineserver regression checks (test-run-wine.sh)

The test copies the fixture to a disposable temporary directory and runs the
copied `run-wine.sh` against a fake `wine` loader and fake `wineserver` that
emulate `wineboot` (creates the `syswow64/kernel32.dll` marker), `reg add`,
probe execution, and bounded `-k`/`-w` cleanup. No real Wine, no builds, no
WSL, no VM. Run with Git Bash at `C:/Program Files/Git/bin/bash.exe` (not the WSL
or system32 bash shim):

```
"C:/Program Files/Git/bin/bash.exe" tests/uwp/u1_0/test-run-wine.sh
```

| Case | Fake behavior | Expected |
|---|---|---|
| pass | correct two-line CRLF output, exit 0 | exit 0; PASS lines; HRESULT MATCH; raw CRLF retained; prefix removed; summary cleanup & overall PASS (no FAIL); tracked oracle file untouched |
| wrong-hr | correct shape, HRESULT `0x80004005`, exit 0 | exit 1; FAIL verdict; HRESULT MISMATCH; summary overall FAIL |
| fail-line | `FAIL negative_activation`, exit 1 | exit 1; FAIL |
| malformed | three lines (extra output), exit 0 | exit 1; FAIL; raw evidence kept |
| nonzero | correct output, exit 1 | exit 1; FAIL; exit status recorded |
| timeout | probe hangs without ignoring TERM | exit 1; exit status 124; FAIL |
| foreign-prefix | prefix with preexisting content | exit 3; state retained |
| alias-dotdot | `..` path to an existing dir | exit 3 after canonicalization; state retained |
| alias-symlink | symlink alias (host-dependent, see limitations) | exit 3 |
| lock | prefix holding `.wex-u1-lock` | exit 3; lock retained |
| no-parent | prefix parent missing | exit 3 |
| usage | 0 or 4 arguments | exit 2; exact usage printed |
| results-reuse | results directory already exists | exit 2 |
| results-no-parent | results dir parent does not exist | exit 2 |
| results-symlink | results dir is a symlink/reparse | exit 2 |
| results-cwd / results-fixdir | cwd or fixture results dir | exit 2 |
| default-results | no third argument | exit 0; fresh temp results dir with evidence |
| build-etc | build dir under `/etc/wineexplorer` | exit 3 |
| missing-probe | probe absent from the copy | exit 2 |
| missing-server | wineserver binary missing from build dir | exit 2; rejected before mutation; no prefix created |
| server-subdir | wineserver in `server/` subdirectory | exit 0; PASS; resolves normal Wine build layout |
| boot-fail-marker | wineboot creates marker then exits 1 | exit 2; failure logged; marker exists; prefix retained |
| boot-timeout | wineboot ignores TERM and hangs | exit 2; timeout logged; killed by kill-after; prefix retained |
| reg-timeout | reg add ignores TERM and hangs | exit 2; timeout logged; killed by kill-after; prefix retained |
| probe-timeout | probe ignores TERM and hangs | exit 1; probe exit 137; killed by kill-after; prefix retained |
| cleanup-timeout | wineserver ignores TERM on shutdown | exit 1; cleanup timeout warning logged; prefix retained; summary cleanup & overall FAIL (no PASS) |
| cleanup-fail | wineserver exits nonzero on shutdown | exit 1; cleanup failure logged; prefix retained; summary cleanup & overall FAIL (no PASS) |
| lock-changed | ownership lock tampered during run | exit 1; ownership lost logged; tampered lock retained; prefix retained; summary cleanup & overall FAIL (no PASS) |
| invalid-timeout | timeout bound set to non-integer | exit 2; positive integer error logged |
| zero-timeout | timeout bound set to 0 | exit 2; positive integer error logged |
| wait-fail-1 | wineserver -k exits 0, -w exits 1 | exit 1; wait failure logged; prefix & lock retained; summary cleanup & overall FAIL (no PASS) |
| server-disappear | wineserver binary disappears after probe | exit 1; missing server logged; prefix & lock retained; summary cleanup & overall FAIL (no PASS) |
| k-exit1-pass | wineserver -k exits 1, -w exits 0 | exit 0; clean pass; prefix removed; summary cleanup & overall PASS (no FAIL) |
| lock-changed-w | ownership lock changed during wineserver -w | exit 1; ownership lost logged; tampered lock & prefix retained; summary cleanup & overall FAIL (no PASS) |
| prefix-removed | prefix removed before cleanup | exit 1; missing prefix logged; summary cleanup & overall FAIL (no PASS) |

The suite also checks `bash -n` syntax of both scripts. On failure it
retains the work directory and prints its path.

## Executed and outcome

* `bash -n run-wine.sh`, `bash -n test-run-wine.sh` (Git Bash): OK.
* `"C:/Program Files/Git/bin/bash.exe" tests/uwp/u1_0/test-run-wine.sh`
  (2026-11-11): **142/142 checks PASS, exit 0**.
* `"C:/Program Files/Git/bin/bash.exe" unsloth-tmp/uwp-u1-20261111/review-runner-wait-failure.sh`
  (2026-11-11): **RUNNER_EXIT=1, PREFIX_RETAINED, exit 0 (defect reproduced and verified resolved)**.

## Limitations

* Native host is Windows (Git Bash); it lacks native Linux `/proc` and `pgrep`.
  Do not claim Linux validation: the runner's scoped wineserver `-k`/`-w` cleanup
  path and timeout kill-after escalation are verified deterministically against
  the fake loader/wineserver, but live execution against a real Wine server and
  guest environment remains a separate downstream gate.
* No real Wine runtime was run; the fake loader only emulates commands the
  runner issues (`wineboot`, `reg add`, probe, and `wineserver`).
* Symlink creation on Windows Git Bash depends on Developer Mode / host privileges;
  when unprivileged, Git Bash emulates links, which `run-wine.sh` catches and
  refuses.
* A fake-loader pass is not a runtime gate and does not replace the U1
  baseline runs on a clean pinned WoW64 build (see `EXECUTION_PLAN.md`).
