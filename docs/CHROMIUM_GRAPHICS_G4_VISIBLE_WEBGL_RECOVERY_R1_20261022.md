<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Visible WebGL recovery r1 — one bounded trial

One authorized `--gpu-recovery --visible-webgl --seconds 45` trial passed its
existing recovery and actual front/page checks. All four stage pairs were saved:
pre_fault magenta/cyan, recovered/settled_2s/settled_4s yellow/blue. Each canvas
matched **1024/1024** inset pixels in each image; marker/background/rim and
geometry checks passed. The full pre-fault/recovered fronts and corresponding
pages were visually inspected. All three recovered fronts are byte-identical,
as are all three recovered pages; decoded front bounds are `(25,25,765,545)`.

This is one scoped visible-consumer recovery pass, not a historical blanking fix,
root-cause conclusion, independent repeat, post-fault matrix or G4 acceptance.
No source/test/production change, retry, extra healthy arm or other VM operation.
Historical incidental-capture schedule caveat remains as documented in
VISIBLE_WEBGL_IMPLEMENTATION; this trial uses only its explicit stage captures.

## Evidence and identities

Root: `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/visible-webgl-recovery-r1-20261022/`.
`recovery-evidence/` holds all eight PNGs, results/per-canvas/CSS/rim verdicts,
host/Chromium/Wine stdout+stderr, wexcomp/capture/process logs, native-window
logs and stage ACKs. All **39** archived files were rehashed against execution
manifest: zero mismatches. Prefix/profile/runtime directories excluded; retained
guest state remains in the stopped overlay. No private/vendor payload in Git.

The sibling `.preflight/` archive/manifests and `PREPARED_NOT_RUN` are retained
as immutable preparation history. Separate `live-execution-status.json` now says
`COMPLETED_SINGLE_TRIAL_AND_VERIFIED_SHUTDOWN`; root `execution.json` records the
actual command, UTC execution, identities, deployed hashes and exits.

Current repository at launch `847d860d`, diagnostic source unchanged from reviewed
`eecf8bb3`. All 12 source/dependency and four host-helper hashes matched before
boot; all 12 deployed source and 17 approved binary hashes matched before the
consumer. Full tables remain in manifests; setup matches RECOVERY_PREP/TIMING_R1.
Source archive SHA-256 remains
`dda447bee4bfd670e7e2e0163262811e810b15a2f7d8022e720b70bdb11501e9`.
Compiled native probe: `311c6576fedaf7572928a7dc63711460d5bebf40b52bedadc3484e6e192aea75`.

Pre-fault PNG hashes match the visual baseline. Recovered PNG SHA-256 values:

```text
front (all 3): 903c7d76f7b6f5ac5264c26c1e64025f724a6a78f50ba1e4dfc62fbe5ed259fb
page  (all 3): 92e15e84c39549f21658e2dc46d6e47030771fb8c5b213e3c6536778e6021dc0
```

Nonce `10784334b9613e1559c87f60f05be114`; compositor PID/start ticks `886/5701`,
unchanged. Browser PID/creation time `420/1792696903593.526` and renderer
`616/1792696904999.451` stayed fixed. Exactly one owned GPU fault at host-relative
7403 ms: `528/1792696903991.41` → `912/1792696911332.393`. Healthy pre-fault page
ACK at 7401 ms preceded the fault; pre-fault front/page pixels both true.
Both APIs recorded one context-loss event and fresh valid contexts, red default/
green FBO readbacks, framebuffer complete, errors 0 and unchanged ANGLE backend.
Frames/visible transform progressed after recovery. One expected killed GPU exit,
no renderer exit; analyzer GPU-crash count 1, all other classified errors 0.
Inner EGL and outer GLES2 both verified VirGL/D3D12 Intel UHD 750, native Wayland
true. SSH/host/diagnostic/archive exits all **0**, no procedure error.

## Procedure and verified shutdown

Used the exact prepared command/private procedure once (no modifications):

```powershell
pwsh -NoProfile -File D:/DeepThought/Projects/WineExplorer/scripts/g4-virgl-launcher-session.ps1 -RunName visible-webgl-recovery-r1-20261022
wsl -d Debian --exec env WEX_TIMING_RUN_NAME=visible-webgl-recovery-r1-20261022 WEX_TIMING_BOOT_NAME=visible-webgl-recovery-r1-20261022 python3 -B /mnt/d/DeepThought/Projects/WineExplorer/build/g4-virgl-20261022/presentation-timing-trial.py run
wsl -d Debian --exec python3 -B /mnt/d/DeepThought/Projects/WineExplorer/scripts/stop_g4_virgl_launcher.py --identity /mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/visible-webgl-recovery-r1-20261022/launcher-identity.json --timeout 60
```

Public key matched seed; authorized temporary copy mode 600, no contents printed.
Fresh guest stage/output paths verified absent. Guest free 17,321,385,984 bytes;
D: free ~1.87 TB; prelaunch usage 1% five-hour / 0% weekly. Consumer procedure
UTC `19:20:58–19:22:32`. Persistent Windows WSL PID 18152 ran
`19:19:54.888–19:22:58.351Z`, exit 0 (~183 seconds of 900, over 90 reserved).
Validated launcher PID/start ticks `365/322`, QEMU `382/379`, Xvfb `367/338`.
Separate identity-validated SIGINT stopper exit 0; final result records signal,
graceful QMP powerdown true, QEMU exit 0 and both children stopped. No remaining
owned Wine servers. Explicit post-stop `/proc` checks found owned PIDs absent
and SSH2235 closed; copied Windows final record/logs under `host-procedure/`.
No Ctrl-C or process-absence-only shutdown inference. Return for parent review;
no additional experiment authorized or attempted.
