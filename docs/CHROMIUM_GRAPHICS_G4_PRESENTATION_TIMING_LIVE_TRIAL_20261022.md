<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# G4 no-fault presentation timing live trial — 2026-10-22

## Result and limits

Exactly one approved `--presentation-timing --seconds 45` measurement ran on
reviewed source `4cf5d059`. Consumer/SSH, host, diagnostic and evidence archive
exit codes were all 0. Collection validity and experiment health were true;
classification was `correct before page readback`, with `acceptance=NOT_PROVEN`.
All five front marker/background/rim verdicts and the one page verdict passed.
This run did not reproduce stale fronts at its measured sample times. It does
not explain the earlier discrepancy or establish a production cause or G4
acceptance. No fault, extra healthy control, tracing arm or second run occurred.

The earliest actual PNG capture began **64.986 ms after observer request
receipt**, rather than at the nominal zero offset. Native-query overhead and
capture lateness are retained, not relabeled as on-time measurement. The first
capture offset is not a DOM-freeze-to-presentation latency measurement. The first
and final fronts and the page PNG were also visually inspected: frozen green
client, blue marker and, in fronts, complete caption/menu/window are present.

## Evidence paths

All retained host evidence is on D:

`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/presentation-timing-r1-20261022/`

- `timing-evidence/`: all six raw PNGs, decoded `results.json`, both ACKs,
  host/Chromium/Wine stdout/stderr, `capture.log`, `wexcomp.log`, native EGL,
  all ten per-sample native-window query stdout/stderr pairs, compiled native
  helper, Wayland information and process records.
- `execution.json`: exact consumer argv, validated Linux identities, all 12
  deployed source hashes, all 17 observed known binary hashes, exits and hashes
  of every archived consumer file. An offline recheck matched every file hash.
- `preflight.json` and adjacent
  `presentation-timing-r1-20261022.preflight/`: prepared before boot; include
  source manifest, source-stage tar, known binary hashes and exact commands.
- `results.json`, `launcher-identity.json`, serial/QEMU/Xvfb/host-GL logs,
  stopper stdout/stderr, and `preflight-notes.md` preserve startup and shutdown.
- `host-procedure/`: copies of the actual private launcher, session controller,
  stopper and one-run staging/evidence script, plus final Windows WSL session
  JSON and its separate stdout/stderr. The original session/log files also
  remain adjacent to the run directory with `.wsl-*` suffixes.

Images for parent inspection are `timing-evidence/presentation-front_0.png`,
`presentation-front_250.png`, `presentation-front_1000.png`,
`presentation-front_3000.png`, `presentation-page.png` and
`presentation-front_after_page_250.png`. Exactly six PNGs were archived; there
are no generic `screen-*.png` or `electron-capture.png` files. All front PNGs
have SHA-256 `d8ad24ad86d0bfc4f7044ddd70d7748eed5171d8e6b1b6094e65ab12c1c48b2c`;
the page hash is `4c2d75b9d57c214966c527a2f1844a6aafeb4ac36b63a41d7edf56548fb8e215`.
Generated prefixes, profiles and Wayland runtime directories were excluded
from the D: consumer archive. Vendor binaries and private credentials are not
in Git. The raw guest prefix/output remains in the powered-off overlay at
`/home/debian/wex-g4/presentation-timing-r1-20261022/`.

## Commands and preparation

Only the existing session controller/launcher/stopper booted or stopped the
existing owned overlay. A private one-run staging/evidence script prepared the
archive/manifest before boot, checked binary/deployed hashes, invoked the
existing consumer and copied its evidence; it did not change measurement code.
Actual host commands:

```powershell
wsl -d Debian --exec python3 -B /mnt/d/DeepThought/Projects/WineExplorer/build/g4-virgl-20261022/presentation-timing-trial.py prepare
pwsh -NoProfile -File D:/DeepThought/Projects/WineExplorer/scripts/g4-virgl-launcher-session.ps1 -RunName presentation-timing-r1-20261022
wsl -d Debian --exec python3 -B /mnt/d/DeepThought/Projects/WineExplorer/build/g4-virgl-20261022/presentation-timing-trial.py run
wsl -d Debian --exec python3 -B /mnt/d/DeepThought/Projects/WineExplorer/scripts/stop_g4_virgl_launcher.py --identity /mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/presentation-timing-r1-20261022/launcher-identity.json --timeout 60
```

Exact sole guest consumer command (through existing key-only SSH2235 with the
dedicated known-hosts file and `StrictHostKeyChecking=yes`):

```sh
sudo -n /usr/bin/python3 -B /home/debian/wex-g4/presentation-timing-r1-stage-20261022/scripts/run-isolated-graphics.py --socket-root /home/debian/wex-g4/isolated-sockets -- runuser -u debian -- python3 -B /home/debian/wex-g4/presentation-timing-r1-stage-20261022/scripts/check-g4-webview2.py --wine-build /var/tmp/wex-g3-repair-20261018/clean-source/build/wow64 --runtime /home/debian/wex-g4/private-electron-44.9.2/runtime --host /home/debian/wex-g4/private-electron-44.9.2/runtime/electron.exe --electron-app /home/debian/wex-g4/presentation-timing-r1-stage-20261022/scripts/g4-electron-probe --presentation-timing --seconds 45 --renderer gl --gl-driver virgl --wexcomp-bin /home/debian/wex-g4/labwc --wexcomp-config /home/debian/wex-g4/wayland --compositor-renderer gles2 --output-dir /home/debian/wex-g4/presentation-timing-r1-20261022
```

Host D: free space was 1,882,351,128,576 bytes; guest space before staging/run
was 25,032,282,112 bytes. SSH readiness initially failed while booting; the
private Linux credential copy also needed recreation after WSL session loss.
It was restored from the existing authorized lab key with mode 600 after the
public key matched the seed. Readiness errors and exact messages are preserved
in `preflight-notes.md`. No measurement had started, and bounds were unchanged.

## Acceleration, health and pixels

Launcher host GL verification passed with accelerated D3D12 on Intel UHD 750.
Both outer GLES2 compositor and inner Wayland EGL independently passed VirGL
verification with `virgl (D3D12 (Intel(R) UHD Graphics 750))`. Native Wayland was
observed with no X11 driver in the owned process map. No GPU child exit,
renderer exit, malformed process event or classified analyzer error occurred.
All eight analyzer counters were zero. Owned Wine cleanup recorded no remaining
servers. No `--trace-native` or recovery flag was supplied.

Window geometry stayed `{x:25,y:25,width:740,height:520}`, content bounds
`{x:29,y:79,width:732,height:462}`, viewport `732x462`, observed scale 1.0.
Marker geometry was `{x:192,y:177.59375,width:40,height:40}` in client CSS
coordinates. All front samples matched blue `[21,101,192]` at `(241,277)` and
green `[232,245,233]` at the background sample, with 400/400 blue marker pixels
and left/right/bottom rim fractions 1.0. The page also sampled 400/400 blue
pixels, at client point `(212,198)`, and matched the green background.
Compositor identity stayed PID 889/start ticks 5642 across every sample.

## Actual timing

Observer monotonic request receipt was `115.564874216` seconds. The table uses
that origin for the first four rows; the last row uses page-notification receipt
`118.993593645` seconds. Values below are milliseconds, rounded only here; full
precision starts/ends/durations are in `results.json`.

| Front | Target offset | Observation start offset | Observation duration | PNG start offset | PNG duration | PNG lateness |
|---|---:|---:|---:|---:|---:|---:|
| front_0 | 0 | 0.100 | 213.737 | 64.986 | 64.855 | 64.986 |
| front_250 | 250 | 250.138 | 596.836 | 314.590 | 462.187 | 64.590 |
| front_1000 | 1000 | 1000.130 | 165.555 | 1064.511 | 32.127 | 64.511 |
| front_3000 | 3000 | 3000.189 | 166.196 | 3064.893 | 32.060 | 64.893 |
| front_after_page_250 | 250 | 250.256 | 250.777 | 315.762 | 114.231 | 65.762 |

The bounded two-rAF wait logged two callbacks with renderer timestamps 5763.7
and 5800.4 ms, elapsed 51 ms. Host-relative log times were request 7324 ms,
four-front completion ACK 10579 ms, page event 10618 ms, page/post completion
ACK 11333 ms and result 11335 ms. Nonce was
`f653e1ff96e24bc47407cd2589e93703`. Fixture collection elapsed 4414.5453 ms.
Browser-main page capture started at 11134.8479 ms and ended at 11169.0865 ms
(34.2386 ms). Observer page decode ended at `118.995361007` seconds. Clock
domains differ and must not be subtracted from one another. Post-page timing
is relative to observer notification receipt, not an exact 250 ms after the
browser capture completion; actual notification/IPC overhead remains a limit.

## Source/deployed hashes

All 12 staged source files matched their pre-boot workspace hashes before the
consumer command ran. Full known/observed binary maps are in the manifests.

| Source relative path | SHA-256 |
|---|---|
| `scripts/check-g4-webview2.py` | `3531098e12a6f9708335d35bb171d4071ca9bbbfad5d6e33b3d85da5034f7b6e` |
| `scripts/check-g3-correctness.py` | `48ee9788c31c2d35b800133e91f97b3c7ffe44ac609bab8cc35d8791a39af207` |
| `scripts/analyze-webview2-run.py` | `d4b7649550aedc99cdf39581959eb4ab4724d6495c7fb42f22fa24efa74e9c28` |
| `scripts/graphics_resources.py` | `110ca088e5b1505e54fdfd1f84fe06fa0cd95086bfbbeff7aeb7a585d14a929a` |
| `scripts/graphics_frame_pixels.py` | `03e6dcbbcd44f7bbb33ee9a26cb0dee23cf6355b9d92d384ff34520e95d775a0` |
| `scripts/graphics_wexcomp.py` | `9a09a1f41e9a8036a0fa6597c9050da81fc1d75e0a6cf45b9991f503d4e77f78` |
| `scripts/run-isolated-graphics.py` | `82a21d9bc378249f1602af989c53f8ed9603af632b678a041a041789f12a5e1c` |
| `scripts/test_g4_native_window_state.cpp` | `e55c831c7e24e3d8969859183890f6b7fd146754d20f2922146a43f9391ff60a` |
| `scripts/g4-electron-probe/main.js` | `dac520b62c2b4c2519f7d9dd0794ade493d805639b935c5019a59eadbf842048` |
| `scripts/g4-electron-probe/package.json` | `0bc224c4b50d715fec113dff98fad0d901e9a738ad087f290949c04aa931e860` |
| `scripts/g4-electron-probe/recovery-frame-wait.js` | `5fa053c6912b67dc11fb42a564cbef10398287a4f0bbce0459ad06a753384149` |
| `scripts/g4-electron-probe/presentation-timing.js` | `ed67ed51c77cde3892a66af48ec1547d8ddb66e5e10fb407b831b848d334334d` |

Electron executable hash was
`448da7f0a402b7299f87dc7dc896d48fd58a726ccbcf65b69820745f1bd38bf2`;
Wine `477df57788ca7cc54d3297cf7a455cd4220133cb4ffa845de24c2ab5c9c30867`;
wineserver `a9ce0193c5b32cbe760fe6e4b66c02f747de775c30e914fa87e7687d1f31c6e5`;
compositor `4a24dd13fed3d733d4edd6f213e4db284ff0dee9229e5df898d694d4446e0ec8`;
compiled native probe
`cb6094f9cd91d171ff07cf24b37cbba84d07b82dea33d47b766db863d9060818`.
Private launcher hash was
`b6ea8870863d778b01d34fb663787b9d02ff917e1987d84e9b23fadcc0aa581b`.
The wrapper, native source and both optional JS helpers were all staged and
hashed. No helper repair was needed after launching the consumer.

## Verified shutdown and continuation

Windows WSL PID 30656 started `2026-10-22T15:09:38.4000578Z`; Linux launcher
PID 286/start ticks 9743, QEMU PID 353/start ticks 9939 and Xvfb PID 340/start
ticks 9783 were validated before consumer work and again by the separate
stopper. The test/archive completed at 15:12:33Z. Stopper exit was 0, final
launcher `stop_reason=signal`, `graceful_powerdown=true`, QEMU exit 0, and both
children stopped. Windows controller/WSL exit was 0 at
`2026-10-22T15:12:56.6384865Z`. Its PID was absent on read-only confirmation;
all three recorded Linux PIDs were absent and SSH2235 closed. Shutdown finished
under four minutes after launch, well inside 900 seconds with over 90 seconds
of reserve. No Ctrl-C, session timeout termination or process-absence-only
shutdown inference was used. Other VMs were not operated.

This report is the only tracked change for the live milestone. No production
code, Wine/compositor pin or measurement bound changed. The previously reviewed
67 Linux tests and four Node controls were not repeated for this report-only
run; the actual live diagnostic and offline archive/hash/count checks passed.
Parent image/log review is next. Any next measurement/trace arm, visible WebGL
recovery or postfault matrix awaits separate scope.
