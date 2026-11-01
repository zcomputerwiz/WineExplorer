# Chromium Graphics G4 recovery repeat live trial — 2026-10-22

## Outcome

One fresh independent healthy control passed its renderer/progression checks and
showed the expected full frozen fixture in the retained front screenshot. The
single approved recovery repeat then stopped at the strict `pre_fault` gate:
the page pixels passed, but the front-image blue-marker sample failed. The
diagnostic exited 4 before fault injection. No recovery result is claimed.
Because the gate failed, neither DComp regression controls nor WebView2 runs
were started. This is the only guest boot in this slice.

## Run and provenance

Run directory: `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/recovery-r1-independent-repeat-20261022/`.
The existing VirGL overlay was launched with the reviewed persistent
`scripts/g4-virgl-launcher-session.ps1` controller, private WSL mount
namespace, and bounded `build/g4-virgl-20261022/launch.py` launcher. The run
recorded Linux launcher PID 289/start ticks 10396 and Windows WSL PID 33868.
The existing dedicated SSH key and known-hosts file were used with strict host
verification. No unrelated VM, Wine checkout, or compositor submodule was
changed. No source edits were made during the live run.

The hashed stage is
`/home/debian/wex-g4/recovery-r1-independent-repeat-stage/`. SHA-256 values
observed on the workspace and staged guest copies matched:

| File | SHA-256 |
|---|---|
| `build/g4-virgl-20261022/launch.py` | `b6ea8870863d778b01d34fb663787b9d02ff917e1987d84e9b23faddc0aa581b` |
| `scripts/check-g4-webview2.py` | `66f9aba9d28ea5be93978d7793267118348e01b72af8b2a78890d4a1e9d19302` |
| `scripts/check-g3-correctness.py` | `48ee9788c31c2d35b800133e91f97b3c7ffe44ac609bab8cc35d8791a39af207` |
| `scripts/graphics_resources.py` | `110ca088e5b1505e54fdfd1f84fe06fa0cd95086bfbbeff7aeb7a585d14a929a` |
| `scripts/graphics_frame_pixels.py` | `03e6dcbbcd44f7bbb33ee9a26cb0dee23cf6355b9d92d384ff34520e95d775a0` |
| `scripts/graphics_wexcomp.py` | `9a09a1f41e9a8036a0fa6597c9050da81fc1d75e0a6cf45b9991f503d4e77f78` |
| `scripts/analyze-webview2-run.py` | `d4b7649550aedc99cdf39581959eb4ab4724d6495c7fb42f22fa24efa74e9c28` |
| `scripts/test_graphics_probes.py` | `329ae04bbab141b5d541661ad3f708608f6f613656fce47c219b10240ebcf11d` |
| `scripts/test_g4_native_window_state.cpp` | `e55c831c7e24e3d8969859183890f6b7fd146754d20f2922146a43f9391ff60a` |
| `scripts/snapshot-graphics-x11.py` | `0024e6c86e8bf5b1f1cdffb7acc307282c75078d53c82dc58846cf3287db2063` |
| `scripts/run-isolated-graphics.py` | `82a21d9bc378249f1602af989c53f8ed9603af632b678a041a041789f12a5e1c` |
| `scripts/g4-electron-probe/main.js` | `a05610638fb40e5078ed4c8495b99e86699795a92f7df57d6ab12b3874c8a50f` |
| `scripts/g4-electron-probe/package.json` | `0bc224c4b50d715fec113dff98fad0d901e9a738ad087f290949c04aa931e860` |
| `scripts/g4-electron-probe/recovery-frame-wait.js` | `5fa053c6912b67dc11fb42a564cbef10398287a4f0bbce0459ad06a753384149` |

The run result also records the Electron runtime and Wine stack hashes. The
healthy and recovery results record outer GLES2 renderer and inner Wayland EGL
renderer as `virgl (D3D12 (Intel(R) UHD Graphics 750))`, both passed.

## Commands and results

Persistent controller:

```powershell
pwsh -NoProfile -File D:/DeepThought/Projects/WineExplorer/scripts/g4-virgl-launcher-session.ps1 -RunName recovery-r1-independent-repeat-20261022
```

Healthy frozen control:

```sh
sudo -n /usr/bin/python3 -B /home/debian/wex-g4/recovery-r1-independent-repeat-stage/scripts/run-isolated-graphics.py --socket-root /home/debian/wex-g4/isolated-sockets -- runuser -u debian -- python3 -B /home/debian/wex-g4/recovery-r1-independent-repeat-stage/scripts/check-g4-webview2.py --wine-build /var/tmp/wex-g3-repair-20261018/clean-source/build/wow64 --runtime /home/debian/wex-g4/private-electron-44.9.2/runtime --host /home/debian/wex-g4/private-electron-44.9.2/runtime/electron.exe --electron-app /home/debian/wex-g4/recovery-r1-independent-repeat-stage/scripts/g4-electron-probe --freeze-scene --seconds 60 --gl-driver virgl --wexcomp-bin /home/debian/wex-g4/labwc --wexcomp-config /home/debian/wex-g4/wayland --compositor-renderer gles2 --output-dir /home/debian/wex-g4/recovery-r1-independent-repeat-20261022/healthy-control
```

Wrapper/host exit 0; result `diagnostic_exit=0`, `acceptance=NOT_PROVEN`.
Animation diagnostic passed (59 visible samples, frames 0 to 2801); the
retained `healthy-control-evidence/screen-24.png` shows the complete caption,
menu, frozen scene, and blue marker. This is a harness control, not product
acceptance.

Exactly one recovery command was then run:

```sh
sudo -n /usr/bin/python3 -B /home/debian/wex-g4/recovery-r1-independent-repeat-stage/scripts/run-isolated-graphics.py --socket-root /home/debian/wex-g4/isolated-sockets -- runuser -u debian -- python3 -B /home/debian/wex-g4/recovery-r1-independent-repeat-stage/scripts/check-g4-webview2.py --wine-build /var/tmp/wex-g3-repair-20261018/clean-source/build/wow64 --runtime /home/debian/wex-g4/private-electron-44.9.2/runtime --host /home/debian/wex-g4/private-electron-44.9.2/runtime/electron.exe --electron-app /home/debian/wex-g4/recovery-r1-independent-repeat-stage/scripts/g4-electron-probe --gpu-recovery --seconds 60 --gl-driver virgl --wexcomp-bin /home/debian/wex-g4/labwc --wexcomp-config /home/debian/wex-g4/wayland --compositor-renderer gles2 --output-dir /home/debian/wex-g4/recovery-r1-independent-repeat-20261022/single-fault
```

Wrapper/host exit 0; diagnostic exit 4. The gate recorded a valid nonempty
page capture and `recovery_page_pixel_verdicts.pre_fault.passed=true`, including
400/400 blue marker pixels. The front PNG was 1280x720 with observed window
`{x:25,y:25,width:740,height:520}`, client bounds
`{x:29,y:79,width:732,height:462}`, and scale 1.0. Its rim samples passed
(left/right/bottom fractions 1.0), and its background sample matched
`[232,245,233]`; however, the expected front marker point `(241,277)` was
`[232,245,233]` rather than blue `[21,101,192]`, with 0/400 blue pixels in the
marker region. The `pre_fault` front verdict and capture diagnostic failed;
the failure acknowledgement is retained with `pixelPassed=false`. The strict
gate refused injection, so there is no postfault or recovered-stage evidence.
This is a front/page disagreement requiring review, not evidence that recovery
blanking occurred.

The healthy capture, failed paired captures, dense `screen-*.png` sequence,
`results.json`, Chromium/host/Wine stdout and stderr, `capture.log`,
`wexcomp.log`, native EGL and per-stage native-window logs, process records,
ACKs, and compiled native helper are retained under `healthy-control-evidence/`
and `single-fault-evidence/`. Generated prefixes, profiles, and runtime
directories were excluded from the D: evidence copies.

## Shutdown and scope

The exact-identity stopper validated the per-run Linux launcher record before
SIGINT. Final launcher `results.json` reports `stop_reason=signal`,
`graceful_powerdown=true`, QEMU exit status 0, and QEMU/Xvfb stopped. The
persistent Windows controller exited 0; its session record is `exited` with
`windowsExitCode=0`. The unrelated VirtualBox VM was not touched. No repeated
fault, DComp PE matrix, or WebView2 matrix was run. Visible WebGL recovery
remains a high-value TODO for a separately scoped run. Do not infer a product
fix or acceptance from this healthy control or the pre-fault failure.
