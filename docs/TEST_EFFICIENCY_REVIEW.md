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
| 3 | Wine `web_core.c:902`, `dwmapi/tests/dwm_attr.c:72`, 129 | a timed-out child is not ended; `dwm_attr` ignores a wait result and its restart loop can start 25 readers (~255 s worst case) | fail the phase and reap the children; one bounded readiness phase | web_core fixed (Wine 66321a8b); dwm_attr: Integration |
| 4 | `scripts/test_broker_e1s18.sh:95` | compares against `/tmp/main_prod*_baseline.o` when present and passes when absent; production `main.c` changed since (4386e0d0) | bind the reference to reviewed source and flags, or report it missing | partly: one baseline per worktree, a missing one reported (GPO hit the shared file) |
| 5 | `installer/tests/run-chain.sh:121` | one failed firmware install aborts the other firmware's stages | continue on the firmware that installed; keep the overall failure | fixed, 18fe3efb (also a failed baseline) |
| 6 | `scripts/lab/system-tier-t1.sh:119`, 172–175, 235 | four probes run twice | capture once, check fields together | fixed, b9c97049 |
| 7 | `test_sysrun_server.sh:781` (4 s per `el_up`), `system-tier-t1.sh:66` (13 s), `safe-mode.sh:1978` (up to 14 s per firmware) | fixed sleeps | bounded readiness waits | T1 fixed (b9c97049); el_up fixed, ee10b849 (waits for the holder's ready line); `safe-mode.sh` fixed, 244731fa (a closed window is waited for) |
| 8 | `system-tier-t1.sh:146`, 28/130 | picks the first session 0 server on the host; a marker path ignores `WORK` | the script's own server PID; path from `$W` | fixed, b9c97049 |
| 9 | `tests/wam/run-broker-tests.sh:55`, 33, 16–23 | cache checks after both architectures (the first one's cache is gone); logs deleted on cleanup; fixed X display | check per architecture, keep logs on failure, own display | fixed, 5a3d8068 |
| 10 | `scripts/test_safemode.py:546`, 1230 | word bans from the GRUB removal (2eb9f72f) fail on comments | semantic checks; keep `check-vintner.py:121` | fixed, 18fe3efb: the check and the menu run with GRUB's flags and must act as without (no GRUB tool run) |


## Needs a decision (owner, 2026-11-23: do them; #1 and #3 in session suite-sections, #2 and #4 in session vm-chain-rework)

| # | Where | Finding | Change | Risk | Status |
|---|-------|---------|--------|------|--------|
| 1 | `test_e1s6_transport.sh:9`, `wex-run.sh:223` | the lab suite reruns most of the normal suite (~11 min per full run, measured 2026-11-14) | lab prerequisites apart, lab checks, a small common smoke set | lab flags changing ordinary behaviour would go unseen | done for the shell suites, ee10b849: lab sections and a smoke set on the paths the lab flags touch (TOOLING "Sections of the system tier suites"); `WEX_LAB_FULL=1` runs all. Verified 2026-11-23 (`wex-run.sh full` on 2661de03, Wine c82a1353): lab 7 min, 249 checks, 16 sections (was 25 min, 451 checks); full 37 min (was 57) |
| 2 | `run-chain.sh:59`, `safe-mode.sh:2033`, 2098–2149 | each of 12 stage jobs provisions accounts and prefixes and boots once | one baseline per firmware, stage overlays forked from it | cold provisioning must stay covered once; Vintner counters (`safe-mode.sh:2131`) | done (claude/vm-chain-rework): `NAME-base-FW` provisioned once and read-only, inputs recorded; stages fork from the disk it left (counts and generation unchanged). Verified 2026-11-23 (chain vmc1 on b1123a.iso): 41 min, was 53 |
| 3 | `test_sysrun_server.sh:73`, 87; `safe-mode.sh:1010`, 1971; `test_safemode.py:58` | the first failure ends the whole suite (b1123a lost most of both suites at minute 4) | sections with verified restoration, named results, skips for dependants; `subTest` in the static tests | damaged shared state must stay fatal | done: the shell suites, ee10b849 (75 sections, snapshot restore compared, setup and unverifiable restores fatal; full run 37 min, was 57; the restore checked on the real fixture with WEX_SECTION_FORCE_FAIL, 911d8aff); the VM side, claude/vm-chain-rework (startrec and rundialog subresults, `subTest` in `test_safemode.py`) |
| 4 | `safe-mode.sh:619`, 1039–1046; `vmtest.py:184` | OCR of "Recycle Bin" is the only desktop readiness signal (600 s timeouts); b1123a's lastgood failed on a misread title | structured window or probe readiness; keep screenshots | the visible interface needs some appearance checks | done (claude/vm-chain-rework): window probes over the serial console (`session-run.py --wait-desktop`, titles); OCR for images without them, for Wayland sessions (no X window to probe) and for appearance checks |


## Not obsolete

No current behavioural test was found obsolete: R8 coverage still applies (R9 keeps the R8 route, 5377caf3); the DWM
tests were rewritten for the pipe (01bf6a72); standalone WAM and the normal suite test different enforcement settings.
