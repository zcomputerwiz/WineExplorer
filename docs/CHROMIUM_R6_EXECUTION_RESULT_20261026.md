<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium R6 Startup Diagnostic Execution Result

**Date:** 2026-10-26
**Status:** `DIAGNOSTIC_NONACCEPTANCE_STOPPED` (Exit 1 Expected Diagnostic; Live Guards & Clean QMP Shutdown Verified)
**Branch:** `codex/chromium-r6-execution`
**Worktree:** `D:/DeepThought/Worktrees/chromium-r6-execution/WineExplorer`
**Parent Work Order:** `docs/CHROMIUM_R6_EXECUTION_WORK_ORDER_20261026.md` (commit `1c47306f`)
**Parent Review Reference:** `docs/CHROMIUM_R6_AND_SHADER_PARENT_REVIEW_20261026.md` (commit `1c47306f`)

---

## 1. Executive Summary

As authorized by the parent work order, Antigravity executed the single, reviewed R6 Chromium startup diagnostic using Luna's accepted preparation in `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/chromium-registration-r6-20261026.preflight/preflight.json` (SHA `a52cd9740ead19ee084ec27c0b38b325a82f032664ba149f38933a41185454f8`) against existing disk `system-native-identity-r4-20261024.qcow2`.

- **Live Guards Passed:** Preflight hash, free disk space (1.73 TiB on D:, 11.97 GB in guest), idle prefix ownership probe, clean path absence for STAGE/OUTPUT, and 27-member source archive deployment integrity (all 27 SHA256 checksums matched 100%).
- **Diagnostic Finding:** Pre-browser runtime guard in `check-g4-chromium-native-exit.py --runtime-probe` tripped with `ValueError: Existing partial/extra runtime; never overwrite` (`set(actual) != set(expected)` against `runtime-manifest.json`).
- **Protected Safety Invariant:** Zero browser launch, zero live probe launch, zero network/HTTP requests, and zero prefix corruption occurred.
- **Graceful Shutdown Verified:** Stopper executed via verified QMP powerdown protocol: `capabilities_ack: true`, `powerdown_ack: true`, `guest_shutdown_observed: true`, `qemu_exit_status: 0`, `xvfb_stopped: true`, `fallback_used: false` in 7.05s. Windows launcher process exited 0.
- **No Retries / No Speculative Fixes:** The trial stopped immediately as instructed.

---

## 2. Command Execution & Timeline

| Phase | Exact Command | Exit Code | Elapsed / Monotonic |
|---|---|---|---|
| **Launcher** | `powershell.exe -ExecutionPolicy Bypass -NoProfile -File D:\DeepThought\Worktrees\chromium-r5-preparation\WineExplorer\scripts\g4-virgl-launcher-session.ps1 -RunName chromium-registration-r6-20261026` | `0` | 57.53s total session |
| **Run** | `wsl -d Debian --exec python3 -B /mnt/d/DeepThought/Worktrees/chromium-r5-preparation/WineExplorer/build/g4-virgl-20261022/chromium-registration-r6-trial.py run` | `1` (expected nonacceptance) | Started 22:20:23Z, failed 22:21:09Z |
| **Stop** | `wsl -d Debian --exec python3 -B /mnt/d/DeepThought/Worktrees/chromium-r5-preparation/WineExplorer/scripts/stop_g4_virgl_launcher.py --identity /mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/chromium-registration-r6-20261026/launcher-identity.json --timeout 80` | `0` | 7.05s shutdown |

### Execution Timeline (UTC)
- `2026-10-26T22:20:19Z`: Wrapper validation started; D: drive free space verified (1,855,900,815,360 bytes / 1.73 TiB).
- `2026-10-26T22:20:21Z`: Windows launcher controller process launched (`PID 41180`), starting WSL `unshare --mount` session (`PID 356`).
- `2026-10-26T22:20:22Z`: `launcher-identity.json` established (`QEMU PID 371`, `Xvfb PID 358`, display `:0`, Host GL: D3D12 Intel UHD 750 direct rendering).
- `2026-10-26T22:20:23Z`: Trial run started. Readiness budget verified (897.9s remaining).
- `2026-10-26T22:21:08Z`: Guest SSH readiness achieved on port 2235 (8 probe attempts, 45.0s elapsed).
- `2026-10-26T22:21:08Z`: Guest free space before stage verified: 11,966,550,016 bytes (11.97 GB).
- `2026-10-26T22:21:09Z`: Prefix ownership probe passed (`busy: false`). STAGE `/home/debian/wex-g4/chromium-registration-r6-20261026-stage` created.
- `2026-10-26T22:21:09Z`: Source stage archive (`source-stage.tar`, 27 members) extracted and rehashed via `sha256sum`; 100% matched `transfer_files`.
- `2026-10-26T22:21:09Z`: Pre-browser runtime guard `check-g4-chromium-native-exit.py --userenv-r6 --runtime-probe` executed; failed with exit 1 (`ValueError: Existing partial/extra runtime; never overwrite`).
- `2026-10-26T22:21:10Z`: Trial aborted with exit 1; `consumer_started: false`, `archive_skipped: "No consumer; no further SSH"`.
- `2026-10-26T22:21:10Z`: Stopper script invoked (`stop_g4_virgl_launcher.py`).
- `2026-10-26T22:21:17Z`: QMP graceful powerdown completed; QEMU exited 0; Windows launcher exited 0; wrapper execution summary saved.

---

## 3. Verified Inputs and Provenance

- **Preflight Record:** `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/chromium-registration-r6-20261026.preflight/preflight.json`
  - SHA256: `a52cd9740ead19ee084ec27c0b38b325a82f032664ba149f38933a41185454f8` (exact match).
- **Source Stage Archive:** `source-stage.tar`
  - SHA256: `2c6e0dc51b1ccff231062708ea079532bb5f7b509006aee4d72af6315e3acce5` (exact match, 27 members).
- **Host Trial Script:** `D:/DeepThought/Worktrees/chromium-r5-preparation/WineExplorer/build/g4-virgl-20261022/chromium-registration-r6-trial.py`
  - SHA256: `eec22829d90fff7ca8a0e748c9988fccfb98d9ba1a76db18f67a5d095bf74548` (exact match).
- **Disk Image:** `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/system-native-identity-r4-20261024.qcow2`
  - Size: 945,881,088 bytes.
- **Accepted Modules Staged:**
  - `kernelbase.dll` (x86_64): `147fb181ff8ce1eb13aa2bb166e053f40c26191662d1b447247fdfd1e720aa24`
  - `kernelbase.dll` (i386): `261a2fd0dfa4b04f6b3c16a122f3efbc3a703bb0d078cea8ab552d1476c5f0ed`
  - `userenv.dll` (x86_64): `71de0bea2e3b85a9afcddf7dcdcc40a31b204605530b4d83e0e2cf08e2bf04ba`
  - `userenv.dll` (i386): `02f1f5b914b8aa0ba846cf5c04fb616c8aaf6a2563cfeeb5a162b4c151db8313`
- **Native PEs Staged:**
  - `native-exit.exe` (parent helper): `433841b75e11b82035ec9f5602e1af4dcd2e165f39819da3e4f20c8135df5ad7`
  - `userenv-r6-probe.exe`: `8558081c1211a7d0298bf33b2fa0a9ef62d4b3b3079439fbb4efa398ea962c4d`

---

## 4. Private Artifact & Evidence Inventory

All raw files remain strictly D:-resident under `D:\VirtualBox_VMs\WEX-G4-VirGL-20261022\chromium-registration-r6-20261026\`:

| File Name | Size (Bytes) | Description |
|---|---|---|
| `wrapper-execution-summary.json` | 1,677 | High-level wrapper timeline, PIDs, exit codes, and stop evidence. |
| `execution.json` | 110,008 | Detailed trial ledger: budgets, readiness, 14 remote records, checksum verifications, and error report. |
| `results.json` | 7,555 | QEMU/QMP shutdown ledger: command line, capabilities, powerdown ACKs, guest SHUTDOWN event, exit 0. |
| `readiness.json` | 26,742 | SSH port 2235 poll history (8 attempts, 45.0s to readiness). |
| `launcher-identity.json` | 724 | Process identity record: launcher PID 356, QEMU PID 371, Xvfb PID 358. |
| `serial.log` | 72,317 | Guest kernel boot and systemd console log. |
| `gl-driver.stdout.log` | 1,756 | Host GL diagnostic: D3D12 Intel UHD 750, core profile 4.1 Mesa 25.0.7. |
| `gl-driver.stderr.log` | 0 | Clean host GL stderr. |
| `qemu.stdout.log` / `stderr.log` | 0 / 0 | Clean QEMU stdout/stderr. |
| `xvfb.log` | 0 | Clean Xvfb log. |
| `trial-run.stdout.log` | 110,008 | Python trial stdout (redirected large JSON ledger). |
| `trial-run.stderr.log` | 1,150 | Python trial traceback confirming `ValueError: Existing partial/extra runtime`. |
| `pid` | 4 | Host PID file. |
| `chromium-native-evidence/` | Directory | Extracted evidence dir (unpopulated because consumer was not started). |

---

## 5. Detailed Diagnostic Analysis

### Failure Point
During pre-consumer phase in `chromium-registration-r6-trial.py`:
```python
native = ['python3', '-B', STAGE + '/scripts/check-g4-chromium-native-exit.py', '--userenv-r6']
state = json.loads(remote([*native, '--runtime-probe'], 40).stdout)
```
Inside `check-g4-chromium-native-exit.py`:
```python
def runtime_state(manifest=None, *, zip_path=None, extract=False, ...):
    ...
    paths = list(RUNTIME.rglob('*'))
    if any(p.is_symlink() for p in paths): raise ValueError('Runtime link not allowed')
    actual = {p.relative_to(RUNTIME.parent).as_posix(): p for p in paths if p.is_file()}
    if set(actual) != set(expected):
        raise ValueError('Existing partial/extra runtime; never overwrite')
```
The guest path `/home/debian/wex-g4/chromium-headful-r1-20261023-stage/vendor/extracted/chrome-win` on the overlay disk `system-native-identity-r4-20261024.qcow2` has a different file set from the 258 files listed in `runtime-manifest.json`.

### Invariants Observed
1. **No Fail-Open:** When `set(actual) != set(expected)` failed, `runtime_state` aborted cleanly rather than silently proceeding with a mismatched binary runtime.
2. **No Overwrite:** The runner did not attempt to overwrite, download, or clobber the directory.
3. **Consumer Gated:** Because the runtime could not be verified, the browser was **not launched**, the pre-browser probe was **not launched**, and no WINEPREFIX mutations occurred.
4. **Diagnostic Exit 1:** As defined by contract `limits['diagnostic_exit'] = 1`, this exit is an expected diagnostic nonacceptance signal, not an unhandled crash or dirty hang.

---

## 6. Shutdown Invariant Verification

The VM shutdown conformed 100% to the strict requirements:
- **Exact Identity Stopper:** `stop_g4_virgl_launcher.py` verified launcher PID `356` and start ticks `10295` before issuing SIGTERM.
- **QMP Protocol Complete:**
  - `capabilities_ack`: `true`
  - `powerdown_requested`: `true`
  - `powerdown_ack`: `true`
  - `guest_shutdown_observed`: `true` (`event: SHUTDOWN`, `reason: guest-shutdown` at monotonic `158.786s`)
- **QEMU Exit Status:** `0` (clean exit).
- **Fallback Avoidance:** `fallback_used: false` (SIGKILL / SIGTERM fallback to QEMU was NOT needed).
- **Process Cleanup:** Both `qemu_stopped: true` and `xvfb_stopped: true`. Zero orphaned processes.
- **Windows Controller Exit:** `0`.

---

## 7. Distinguishing Prior Offline Units from this Run

- **Prior Units (Offline):**
  - Luna independent clean-build units: 49/49 security, 260/260 userenv passing on both x86_64 and i386.
  - Parent native Windows probe: 21/21 passing.
  - Antigravity offline unit suites: 31/31 passing in WSL Debian.
- **This Run (Live Guest VM):**
  - Exactly ONE diagnostic execution.
  - Stopped at pre-browser runtime integrity guard (`ValueError: Existing partial/extra runtime; never overwrite`).
  - Zero live browser, probe, or token execution occurred.

---

## 8. Proposed Next Experiment (Documented Only, Not Executed)

To enable Chromium to proceed past the runtime probe:
1. Audit the exact file set discrepancy between `/home/debian/wex-g4/chromium-headful-r1-20261023-stage/vendor/extracted/chrome-win` on the guest disk and `runtime-manifest.json` (e.g. checking if previous runs created cache/profile files inside the extracted directory).
2. If authorized by Parent, either re-extract the clean `chrome-win.zip` into an absent path or align the runtime manifest with the frozen overlay image.
3. Once the runtime probe succeeds, execute the pre-browser metadata sanity probe followed by native Chromium launch.

*No further commands or trials have been executed. Antigravity is stopped and standing by for Parent review.*
