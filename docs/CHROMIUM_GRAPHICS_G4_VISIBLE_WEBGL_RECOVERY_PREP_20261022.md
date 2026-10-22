<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Visible recovery r1 — PREPARED_NOT_RUN

Offline preparation only, following parent acceptance of the visual baseline
in `CHROMIUM_GRAPHICS_G4_VISIBLE_WEBGL_TIMING_R1_20261022.md`. No new boot,
guest access, deployment, run invocation, fault or diagnostic/test change.
Usage before preparation: 92% five-hour / 14% weekly. Parent must review and
authorize live execution in the next window; no live authority is implied here.

Prepared directory:
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/visible-webgl-recovery-r1-20261022.preflight/`.
Contains `PREPARED_NOT_RUN`, `preflight.json`, `source-stage.tar` and copies of
the private procedure, launcher, persistent controller and exact-identity stopper.
Manifest records exact guest argv, launch/stop commands, stage colors, limits,
12 source/dependency hashes and 17 expected binary hashes. All 29 match the
reviewed visible baseline; tracked diagnostic source remains `eecf8bb3` (current
HEAD at preparation `69d6c22f` differs only by its report). Native C++/wrapper/
frame-wait helper are included; the unchanged full inventory also includes the
unused timing helper, so no dependency is omitted. Archive hash verified locally.
No binary/deployment verification in the powered-off guest is claimed.

Existing private `presentation-timing-trial.py` now prepares explicit
`--gpu-recovery --visible-webgl --seconds 45 --renderer gl --gl-driver virgl
--compositor-renderer gles2`, using the same approved build/runtime and isolated
offline wrapper. Only mode/variant metadata, credential-path-only offline
preparation and evidence naming were adjusted; no new orchestration framework.
The hashed private copy is retained in the preparation directory. It records
the future mode-600 key path `/tmp/wex-visible-recovery-r1/vm_key`; no credential
was copied or SSH connection made during this preparation.

Future output: `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/visible-webgl-recovery-r1-20261022/`,
consumer archive `recovery-evidence/`. That run directory does not exist yet.
Exact future controller/stopper commands (NOT executed):

```powershell
pwsh -NoProfile -File D:/DeepThought/Projects/WineExplorer/scripts/g4-virgl-launcher-session.ps1 -RunName visible-webgl-recovery-r1-20261022
wsl -d Debian --exec python3 -B /mnt/d/DeepThought/Projects/WineExplorer/scripts/stop_g4_virgl_launcher.py --identity /mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/visible-webgl-recovery-r1-20261022/launcher-identity.json --timeout 60
```

Pre-fault WebGL1/2 expected magenta/cyan; recovered, settled_2s and settled_4s
expected yellow/blue. No fault on bad pre-fault front OR page pixels or failed
backend/process/identity preconditions. At most one owned-GPU fault; no retry.
Preserve paired failed images and all logs. Consumer is 45 seconds; existing SSH
consumer/archive bounds 300/45 seconds, launcher 900, stopper 60; reserve at least
90 seconds for verified cleanup. Recheck usage, ownership, guest space, deployed
hashes and actual inner/outer VirGL before consumer launch; preserve Windows exit
and final QMP results. No unchanged tests rerun. This report alone is tracked.
