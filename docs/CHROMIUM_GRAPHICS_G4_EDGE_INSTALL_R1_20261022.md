<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Edge MSI r1: installed prerequisite only

Executed **one** approved ordinary installation attempt on 2026-10-23 UTC using
the reviewed `edge-install-r1-v2-20261022.preflight` unchanged. Result:
`INSTALLED_PREREQUISITE_ONLY`, passed=true. No explicit browser invocation,
retry, extra component, administrative extraction, bypass or source change.
This is not browser/graphics/sandbox/G4 acceptance.

## Provenance and actual execution

The seven archive members/size/hash, five current sources and five host helpers
were rechecked against the v2 manifest before boot. Archive SHA256 remains
`35150687282dd716801a7ab4c55ae9c70169c966d136d43a9d098696f2072f63`.
Runner source is `9348b4be`; no implementation changed. Public lab key matched
the seed; its prepared private copy used mode600 without displaying key bytes.
No QEMU process was running and output/session paths were absent before launch.

Exact host commands (from the manifest):

```powershell
pwsh -NoProfile -File D:/DeepThought/Projects/WineExplorer/scripts/g4-virgl-launcher-session.ps1 -RunName edge-install-r1-v2-20261022
wsl -d Debian --exec env WEX_G4_CONTROL=edge-install WEX_TIMING_RUN_NAME=edge-install-r1-v2-20261022 WEX_TIMING_BOOT_NAME=edge-install-r1-v2-20261022 python3 -B /mnt/d/DeepThought/Projects/WineExplorer/build/g4-virgl-20261022/presentation-timing-trial.py run
wsl -d Debian --exec python3 -B /mnt/d/DeepThought/Projects/WineExplorer/scripts/stop_g4_virgl_launcher.py --identity /mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/edge-install-r1-v2-20261022/launcher-identity.json --timeout 60
```

Controller started 00:22:12.2989996Z. An initial SSH readiness probe timed out
during boot; the next read-only readiness check succeeded. No consumer retry.
650s preflight guard passed with 855.895590s remaining; immediate 435s consumer
guard passed with 853.152968s remaining. Guest free bytes **7561863168** before
transfer/creation, above 5 GiB. All 16 approved Wine/compositor binary hashes
and all seven deployed source/vendor hashes matched. Exact consumer argv and
observed hashes are in `execution.json`; only owned Wine msiexec received
`/i APPROVED_PACKAGE /qn /norestart /l*v PRIVATE_LOG` through offline isolation.

Host D3D12 acceleration and both inner EGL/outer compositor VirGL checks passed:
`virgl (D3D12 (Intel(R) UHD Graphics 750))`. Wineboot, installer, cleanup-kill,
cleanup-wait and consumer SSH exits were all 0. Complete runner duration:
115.401983s (maximum 285s). Process monitor stopped; remaining servers=[] and
remaining prefix processes={}. Archive exit 0; all **17** archived file hashes
independently rechecked, zero mismatches. No unchanged unit tests rerun.

Installed executable:
`/home/debian/wex-g4/edge-install-r1-v2-20261022/prefix/drive_c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe`.
COFF machine **0x8664** despite the directory name; SHA256
`bb74db4c753133706f4ab0a483a87aa28a90ddd6eeb305fe001376ba73a40fc3`.
Both EdgeUpdate Clients and uninstall registration identify Microsoft Edge
**156.0.4314.42**. `version_source=registration`; no independent PE version claim.

## Preserved diagnostics and limits

Evidence root:
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/edge-install-r1-v2-20261022/`.
See `edge-install-evidence/results.json`, `install-msi.log`, full Wineboot/
installer stdout/stderr, `installer-processes.jsonl`, `wexcomp.log`, native EGL/
Wayland logs and two copied setup logs. `execution.json` retains hashes/guards/
commands; `review-summary.json` records actual execution separately from the
unchanged historical PREPARED_NOT_RUN preparation. Archive:
`edge-install-evidence.tar.gz`; prefix/profile/vendor payload excluded from Git.
Prefix retained at the guest path above on the D:-backed disk; no files deleted.

Logs are **not warning/error-free**: setup reports unsupported storage
preallocation, activation-factory/registration warnings; updater reports failed
network requests inside the offline namespace. Setup nevertheless records
`Installation complete, returning: 0`. It also logs a scheduled deletion of
`C:\Program Files (x86)\Microsoft\Edge` and a helper NonInstall exit2. Preserve
these for parent review: restart/pending-operation persistence is NOT tested.
Sampled installer processes include setup.exe, MicrosoftEdgeUpdate and the
embedded installer, with no sampled msedge.exe; sampling does not prove absence
of short-lived unsampled processes. No separate browser/graphics attempt occurred.

## Verified shutdown

Separate exact-identity SIGINT stopper exit **0**. Launcher PID362/start_ticks1436,
QEMU PID377/start_ticks1484, Xvfb PID364/start_ticks1449 matched before execution.
Final `results.json`: stop_reason=signal, graceful_powerdown=true, QEMU exit0,
both children stopped. Serial log ends in clean filesystem detach/ACPI S5/power
down. Controller session record is
`../edge-install-r1-v2-20261022.wsl-session.json`: Windows WSL PID26880,
state=exited, exit0 at 00:25:12.3983636Z; persistent controller exec also exit0.
Post-stop read-only /proc checks found none of the three owned Linux PIDs.
Shutdown completed about 180s after controller start, well inside 900s and with
the 90s cleanup reserve intact. No unrelated VM was touched.

Return for parent evidence review before any next boot/browser/production work.
