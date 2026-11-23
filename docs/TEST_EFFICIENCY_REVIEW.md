# Test efficiency review (2026-11-23)

The owner asked for a review of the tests for efficiency: a test still needed after code changes, enough diagnostic
resolution for the behaviour without excessive point checks. A Codex review, read-only, of WE-testmod 52054a82 and the
Wine batch tree f044c289; no runs. Savings are bounds read from the source or earlier measurements, not new timings.
Status column: what has been done since.

## Safe now

| # | Where | Finding | Change | Status |
|---|-------|---------|--------|--------|
| 1 | `scripts/lab/wex-test.sh` | a suite's status went through a `grep` pipe: a failed suite could read as a pass | the suite's own status decides | fixed, c957ad4c |
| 2 | `scripts/test_sysrun_server.sh:292`, 4519 | holding `wex-accounts.path`/`.timer` leaves a running `wex-accounts.service` able to publish a map (hypothesis from the source) | stop the service too and wait for it; restore unit states on failure as well | fixed, ee10b849 (`watch_hold`/`watch_release`, also in the restore and cleanup) |
| 3 | Wine `web_core.c:902`, `dwmapi/tests/dwm_attr.c:72`, 129 | a timed-out child is not ended; `dwm_attr` ignores a wait result and its restart loop can start 25 readers (~255 s worst case) | fail the phase and reap the children; one bounded readiness phase | open (web_core: mine; dwm_attr: Integration) |
| 4 | `scripts/test_broker_e1s18.sh:95` | compares against `/tmp/main_prod*_baseline.o` when present and passes when absent; production `main.c` changed since (4386e0d0) | bind the reference to reviewed source and flags, or report it missing | partly: one baseline per worktree, a missing one reported (GPO hit the shared file) |
| 5 | `installer/tests/run-chain.sh:121` | one failed firmware install aborts the other firmware's stages | continue on the firmware that installed; keep the overall failure | open |
| 6 | `scripts/lab/system-tier-t1.sh:119`, 172–175, 235 | four probes run twice | capture once, check fields together | open |
| 7 | `test_sysrun_server.sh:781` (4 s per `el_up`), `system-tier-t1.sh:66` (13 s), `safe-mode.sh:1978` (up to 14 s per firmware) | fixed sleeps | bounded readiness waits | partly: `el_up` waits for the holder's ready line, ee10b849; T1 and Safe Mode open |
| 8 | `system-tier-t1.sh:146`, 28/130 | picks the first session 0 server on the host; a marker path ignores `WORK` | the script's own server PID; path from `$W` | open |
| 9 | `tests/wam/run-broker-tests.sh:55`, 33, 16–23 | cache checks after both architectures (the first one's cache is gone); logs deleted on cleanup; fixed X display | check per architecture, keep logs on failure, own display | open |
| 10 | `scripts/test_safemode.py:546`, 1230 | word bans from the GRUB removal (2eb9f72f) fail on comments | semantic checks; keep `check-vintner.py:121` | open |

## Needs a decision

| # | Where | Finding | Change | Risk | Status |
|---|-------|---------|--------|------|--------|
| 1 | `test_e1s6_transport.sh:9`, `wex-run.sh:223` | the lab suite reruns most of the normal suite (~11 min per full run, measured 2026-11-14) | lab prerequisites apart, lab checks, a small common smoke set | lab flags changing ordinary behaviour would go unseen | done for the shell suites, ee10b849: lab sections and a smoke set on the paths the lab flags touch (TOOLING "Sections of the system tier suites"); `WEX_LAB_FULL=1` runs all |
| 2 | `run-chain.sh:59`, `safe-mode.sh:2033`, 2098–2149 | each of 12 stage jobs provisions accounts and prefixes and boots once | one baseline per firmware, stage overlays forked from it | cold provisioning must stay covered once; Vintner counters (`safe-mode.sh:2131`) | open |
| 3 | `test_sysrun_server.sh:73`, 87; `safe-mode.sh:1010`, 1971; `test_safemode.py:58` | the first failure ends the whole suite (b1123a lost most of both suites at minute 4) | sections with verified restoration, named results, skips for dependants; `subTest` in the static tests | damaged shared state must stay fatal | done for `test_sysrun_server.sh`/`test_e1s6_transport.sh`, ee10b849 (74 sections, snapshot restore compared, setup and unverifiable restores fatal); Safe Mode and `test_safemode.py` open (VM chain session) |
| 4 | `safe-mode.sh:619`, 1039–1046; `vmtest.py:184` | OCR of "Recycle Bin" is the only desktop readiness signal (600 s timeouts); b1123a's lastgood failed on a misread title | structured window or probe readiness; keep screenshots | the visible interface needs some appearance checks | open |

## Not obsolete

No current behavioural test was found obsolete: R8 coverage still applies (R9 keeps the R8 route, 5377caf3); the DWM
tests were rewritten for the pipe (01bf6a72); standalone WAM and the normal suite test different enforcement settings.
