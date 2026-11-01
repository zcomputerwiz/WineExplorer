<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Visible WebGL timing r1 — one no-fault collection

One authorized fresh-prefix/profile run completed on reviewed `eecf8bb3`.
All five front and one page scene/canvas pixel verdicts passed; collection,
experiment validity and health were true. SSH/host/diagnostic/archive exits
were all 0. No fault, trace, retry, additional control or source change occurred.
The first/front-last/page images were visually inspected: full frozen scene,
intact frame on fronts, separate magenta WebGL1/cyan WebGL2 surfaces.

Classification remains **invalid/inconclusive**: the first whole observation
took 713.703 ms and crossed the 250 ms target. This is the existing conservative
timeline gate, not a health/pixel failure. Do not relabel it an on-time result.
`acceptance=NOT_PROVEN`; no recovery, historical blanking fix or G4 claim.

## Evidence and provenance

Root: `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/visible-webgl-timing-r1-20261022/`.
`timing-evidence/` contains exactly six PNGs, decoded per-canvas verdicts in
`results.json`, host/Chromium/Wine stdout+stderr, compositor/capture/process logs,
both ACKs, native EGL and all per-stage native-window logs. All 43 archived file
hashes were rechecked against `execution.json`: zero mismatches. Prefix/profile/
runtime directories were excluded from the archive; retained guest state remains
in the stopped overlay. No private/vendor payload is tracked.

Sibling `visible-webgl-timing-r1-20261022.preflight/` holds the preboot source
tar, manifest, private procedure and launcher/controller/stopper copies. Root
`preflight.json` and `execution.json` retain exact commands, all 12 source hashes,
17 expected/observed binary hashes and verified deployed hashes. This includes
both JS helpers, native C++ source and actual isolation wrapper. Binaries match
the prior approved controls. Key current SHA-256 values:

```text
check-g4-webview2.py bf4eac20f2b6598e05a81d5acbd441f2f8409116deb558ddc2fa1e971523601f
main.js             59cb7448c18facf305b5ba89ce94bfd568508d0e294c65de34dec84c3f95a486
graphics_frame_pixels.py 444a6deb140e52d713f35c04cefb65aa29a2ce07fba895423ef4ac281024c685
source-stage.tar    dda447bee4bfd670e7e2e0163262811e810b15a2f7d8022e720b70bdb11501e9
native probe EXE    1eedd0da9cc2da00a4734a96803003ad9f0daa6588be2af17a5eda188a94f748
all five fronts    0767ab837dfcf9f53a4db1ee432787c017ed3313794f41dd767c860fb270c67d
page                325de74c4598fa525e3fd4ba66d24c627bc28cc4985e5a3ca3e30896d18c46a4
```

Each image had WebGL1 and WebGL2 inset counts **1024/1024** matching independently.
Native outer bounds `(25,25,740,520)`, client `(29,79,732,462)`, viewport
`732x462`, scale 1. Canvas CSS rectangles `(360,300,64,64)` and `(456,300,64,64)`
were consistent across observations. Nonce `5f124c768ae57fd5f0a820db48cb1d29`;
compositor PID/start ticks `873/4612`. Inner EGL and outer GLES2 both verified
`virgl (D3D12 (Intel(R) UHD Graphics 750))`, native Wayland true. Both WebGL
API probes retained exact red/green readbacks, framebuffer complete, errors 0.
No GPU/renderer exit or classified Chromium error. No Wine servers remained.

## Actual timing

Milliseconds rounded below. Pre-page offsets originate at observer request
receipt `95.751085771` seconds, NOT DOM freeze. Post-page origin is notification
receipt `99.706912665`. Full monotonic start/end records are preserved.

| Front | Target | PNG start offset | PNG duration | PNG lateness | Whole observation |
|---|---:|---:|---:|---:|---:|
| 0 | 0 | 64.618 | 562.431 | 64.618 | 713.703 |
| 250 | 250 | 778.717 | 64.849 | 528.717 | 207.422 |
| 1000 | 1000 | 1064.723 | 31.990 | 64.723 | 168.747 |
| 3000 | 3000 | 3064.617 | 32.004 | 64.617 | 168.489 |
| post-page | 250 | 314.658 | 465.688 | 64.658 | 602.726 |

Host-relative request/front ACK/page notification/post ACK/result were
`6169/9480/9983/11101/11103` ms. Browser-main page readback duration was
498.3474 ms in its separate clock; no cross-clock latency is inferred.
Two renderer rAF opportunities completed in 18.9 ms, not presentation proof.

## Procedure and shutdown

Reused the existing private procedure, changing only its explicit visible flag
and manifest variant. With `WEX_TIMING_RUN_NAME` and `WEX_TIMING_BOOT_NAME` both
`visible-webgl-timing-r1-20261022`, ran its `prepare` before boot and `run` once
through WSL. Exact guest argv is in both manifests; key flags were
`--presentation-timing --visible-webgl --seconds 45 --renderer gl --gl-driver
virgl --compositor-renderer gles2`, existing offline isolation wrapper and cached
owned runtime/build paths unchanged. Generic front/page readbacks suppressed.

```powershell
pwsh -NoProfile -File D:/DeepThought/Projects/WineExplorer/scripts/g4-virgl-launcher-session.ps1 -RunName visible-webgl-timing-r1-20261022
wsl -d Debian --exec python3 -B /mnt/d/DeepThought/Projects/WineExplorer/scripts/stop_g4_virgl_launcher.py --identity /mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/visible-webgl-timing-r1-20261022/launcher-identity.json --timeout 60
```

Preboot usage 84% five-hour/13% weekly, D: free ~1.88 TB; guest free
19,248,619,520 bytes. Consumer procedure UTC `15:54:14–15:55:51`.
Windows WSL PID 1572 ran `15:53:23.663–15:56:18.802Z`, exit 0 (~175 seconds,
well within 900 with 90-second reserve). Validated Linux launcher `360/29893`,
QEMU `375/29980`, Xvfb `362/29905`. Separate identity-validated SIGINT stopper
exit 0; final result says signal/graceful powerdown true, QEMU exit 0 and both
children stopped. Explicit corrected `/proc` checks found owned PIDs absent and
SSH2235 closed. Windows final record/logs are copied under `host-procedure/`.

Startup SSH/temporary-key readiness failures and minor read-only shell-check
corrections are preserved in `host-procedure/procedure-notes.md`; no measurement
was retried. Temporary credential path was already absent after shutdown.
Only this report is tracked; no unchanged tests rerun, VM pins or production
source changed. Return for parent image/evidence review before ANY fault.
