<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Presentation timing repeats r2/r3 — 2026-10-22

Two additional serial fresh-prefix/profile/output controls completed in one
owned VirGL boot. Both collection/experiment/health verdicts and all five front
plus page pixel verdicts passed; both classify `correct before page readback`.
This is a bounded non-reproduction at the measured times, with
`acceptance=NOT_PROVEN`. It does not explain or fix the earlier stale fronts.
No further arm, GPU fault, trace, measurement change or unit-test rerun occurred.

## Evidence and provenance

All paths below are under `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/`:

- `presentation-timing-r2-20261022/timing-evidence/`
- `presentation-timing-r3-20261022/timing-evidence/`
- `presentation-timing-repeat-20261022/`: shared launcher/serial/QEMU/Xvfb/GL
  logs, identities, final shutdown result, stopper logs, second-run gate records
  and `host-procedure/` copies including the final Windows WSL exit record.

Each run retains `preflight.json`, `execution.json`, all six PNGs and their
decoded results, host/Chromium/Wine/compositor/capture/process logs, both ACKs,
native EGL and every before/after native-window query log. Matching sibling
`.preflight/` directories retain the pre-boot source tar/manifests. Prefixes,
profiles and runtime directories were excluded from the host consumer archives;
raw guest state remains in the stopped overlay. No private/vendor payload is
in Git. Offline checks matched every archived file hash and exactly six PNGs
per run, with no generic captures. Both first fronts were independently decoded
with nonblack bounds `(25,25,765,545)` and blue at `(241,277)`.

Repository HEAD at preparation was `41c4990e` (plan/docs only since reviewed
measurement source `4cf5d059`). All 12 source hashes and 17 known binary hashes
matched before each launch. The stage tar SHA-256 was unchanged from r1:
`b2e459dcfd41c507909c0ae2f7f4c3c0187dbdacadc821afea8c9f735091ca91`.
See [r1](CHROMIUM_GRAPHICS_G4_PRESENTATION_TIMING_LIVE_TRIAL_20261022.md) for
the unchanged source/runtime hash table and setup. Both optional JS helpers,
native C++ source and actual isolation wrapper were staged/hashed. The private
host procedure only gained run-name/shared-boot identity parameters; its hash
and actual copy are archived. Compiled native probes were retained and hashed:
r2 `ec41f27e49bf42dc6185c140f234396a9c5cf64d8bd5e5220b89543888d36e71`,
r3 `daeac52781b4f770f0084cfa78f83a7518316cf015409a0611093a96a72d266e`.

These controls share the existing guest, kernel, cached runtime/build and cache
context. They are not independent cold boots. Each had its own fresh state,
isolated wrapper invocation, compositor and nonce:

| Run | UTC procedure start → archive complete | Compositor PID/start ticks | Nonce | SSH/host/diagnostic/archive exits |
|---|---|---|---|---|
| r2 | 15:25:48 → 15:27:23 | 881 / 4923 | `af8c697b3db9ed0e3dbf1acb499a1826` | 0 / 0 / 0 / 0 |
| r3 | 15:28:22 → 15:29:54 | 1275 / 20234 | `36495a7e6a7577e6cbb330ab74394fa3` | 0 / 0 / 0 / 0 |

Both outer GLES2 and inner EGL independently verified
`virgl (D3D12 (Intel(R) UHD Graphics 750))`; native Wayland was observed. No GPU
or renderer exit, malformed process event or classified Chromium error occurred.
Owned Wine cleanup found no remaining servers. Guest space before r2/r3 was
23,105,204,224 / 21,175,734,272 bytes. Geometry/scale and pixel checks match r1.

All ten front PNGs have the same hash as r1:
`d8ad24ad86d0bfc4f7044ddd70d7748eed5171d8e6b1b6094e65ab12c1c48b2c`.
Both page PNG hashes match r1:
`4c2d75b9d57c214966c527a2f1844a6aafeb4ac36b63a41d7edf56548fb8e215`.

## Measured times

Milliseconds, rounded here; actual monotonic starts/ends and full precision are
in each `results.json`. Pre-page offsets use observer request receipt, **not
DOM freeze**; the final row uses observer page-notification receipt. The first
actual PNG starts were 64.627 and 67.244 ms after observer request receipt.
All PNG lateness, including native-query overhead, is retained.

| Run/front | Target | Actual PNG start offset | PNG duration | PNG lateness | Whole observation duration |
|---|---:|---:|---:|---:|---:|
| r2 / 0 | 0 | 64.627 | 64.940 | 64.627 | 217.274 |
| r2 / 250 | 250 | 314.890 | 479.439 | 64.890 | 614.341 |
| r2 / 1000 | 1000 | 1064.649 | 32.166 | 64.649 | 167.205 |
| r2 / 3000 | 3000 | 3065.927 | 315.187 | 65.927 | 451.169 |
| r2 / post-page | 250 | 314.783 | 376.098 | 64.783 | 509.804 |
| r3 / 0 | 0 | 67.244 | 64.770 | 67.244 | 214.930 |
| r3 / 250 | 250 | 314.624 | 365.276 | 64.624 | 501.647 |
| r3 / 1000 | 1000 | 1066.356 | 31.993 | 66.356 | 168.468 |
| r3 / 3000 | 3000 | 3064.945 | 64.318 | 64.945 | 200.011 |
| r3 / post-page | 250 | 314.614 | 516.271 | 64.614 | 652.225 |

Observer request/page-notification origins were r2
`97.997301976 / 101.712328407` seconds and r3
`249.569156072 / 253.559611584` seconds. Page readback durations in the separate
browser-main clock were 38.6838 / 549.3233 ms. Host-relative request/front ACK/
page/post ACK/result log times were r2 `7144/10687/10732/11752/11754` ms and r3
`7022/10481/11037/12211/12214` ms. No cross-clock latency is inferred.

## Procedure, gate and verified shutdown

The existing controller launched `presentation-timing-repeat-20261022` once.
For each `N=2` then `N=3`, the prepared host commands were the following with
the literal respective run name; exact guest argv is retained in `execution.json`:

```powershell
wsl -d Debian --exec env WEX_TIMING_RUN_NAME=presentation-timing-r2-20261022 WEX_TIMING_BOOT_NAME=presentation-timing-repeat-20261022 python3 -B /mnt/d/DeepThought/Projects/WineExplorer/build/g4-virgl-20261022/presentation-timing-trial.py prepare
wsl -d Debian --exec env WEX_TIMING_RUN_NAME=presentation-timing-r3-20261022 WEX_TIMING_BOOT_NAME=presentation-timing-repeat-20261022 python3 -B /mnt/d/DeepThought/Projects/WineExplorer/build/g4-virgl-20261022/presentation-timing-trial.py prepare
pwsh -NoProfile -File D:/DeepThought/Projects/WineExplorer/scripts/g4-virgl-launcher-session.ps1 -RunName presentation-timing-repeat-20261022
wsl -d Debian --exec env WEX_TIMING_RUN_NAME=presentation-timing-r2-20261022 WEX_TIMING_BOOT_NAME=presentation-timing-repeat-20261022 python3 -B /mnt/d/DeepThought/Projects/WineExplorer/build/g4-virgl-20261022/presentation-timing-trial.py run
# Only after the complete pixel/health/time/usage gate passed:
wsl -d Debian --exec env WEX_TIMING_RUN_NAME=presentation-timing-r3-20261022 WEX_TIMING_BOOT_NAME=presentation-timing-repeat-20261022 python3 -B /mnt/d/DeepThought/Projects/WineExplorer/build/g4-virgl-20261022/presentation-timing-trial.py run
wsl -d Debian --exec python3 -B /mnt/d/DeepThought/Projects/WineExplorer/scripts/stop_g4_virgl_launcher.py --identity /mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/presentation-timing-repeat-20261022/launcher-identity.json --timeout 60
```

Both consumers used exactly r1's `--presentation-timing --seconds 45 --renderer
gl --gl-driver virgl --compositor-renderer gles2` flags, with only fresh paths.
The r2 gate explicitly required collection/experiment/health true, page pixels
true, exactly five fronts and all front pixels true. Corrected UTC budget check
at 15:28:12Z left 699.517 seconds, above the conservative 625-second
run-plus-cleanup allowance. An initial PowerShell date-conversion calculation
was wrong; it was preserved separately and corrected before r3 launched. The
one early SSH boot banner timeout and correction are in `procedure-notes.md`.

Windows WSL PID 17392 began at `15:24:51.7466838Z`; validated Linux launcher
PID/start ticks were 359/2146, QEMU 374/2231 and Xvfb 361/2160. The separate
stopper exited 0 with `stop_reason=signal`, `graceful_powerdown=true`, QEMU exit
0 and both children stopped. Windows WSL exited 0 at `15:30:09.9224202Z`, about
318 seconds after launch, preserving well over 90 seconds inside the 900-second
bound. Read-only checks found all owned PIDs absent and SSH2235 closed. No
Ctrl-C or guest process-absence-only shutdown inference was used.

Only this report is tracked. No production source/pins changed, no unchanged
unit tests were rerun, and no additional run or trace arm is authorized here.
Parent review chooses the next scope; visible WebGL composition/recovery
coverage remains a high-value TODO.
