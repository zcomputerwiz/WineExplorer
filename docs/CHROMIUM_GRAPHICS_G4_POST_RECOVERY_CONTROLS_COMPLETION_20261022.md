<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Remaining post-recovery controls — completed once each

Both remaining controls passed in separate fresh boots/prefixes, with the actual
private raw-UTC guards enforced. No x64 repeat, GPU fault, retry, download,
cleanup, diagnostic/source change or unchanged unit-test rerun. Preserve the
original x64 graphics pass AND procedure failure in 9cb47a12; guard repair/tests
are 75bba089/748482c6. This report does not erase or relabel that failure.

| Cell | Result | Correct remaining seconds: 650 guard / 435 guard | Guest free bytes | Archive hash recheck |
|---|---|---:|---:|---:|
| i686 v2 | own-dcomp + foreign-full-dcomp each producer exit 0, submitted, exactly 4096 pixels | 845.831458 / 843.728582 | 11,536,265,216 | 14 files, 0 mismatches |
| strict WebView2 v2 | host/diagnostic 0; WebGL1/2/FBO/readbacks/errors passed | 851.354480 / 849.216197 | 9,615,544,320 | 16 files, 0 mismatches |

Both guards precede their actual operations; raw session UTC origins, elapsed,
required and remaining are retained in `budget-before-guest-preflight.json` and
`budget-before-consumer.json` and execution metadata. No PowerShell date
reformatting was used. Exact Linux launcher/child ownership validation remained
separate and mandatory. No guard/procedure/archive errors occurred.

## Evidence and observations

Under `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/`:

- `post-recovery-i686-budgetfix-v2-20261022/foreign-evidence/`
- `post-recovery-webview2-budgetfix-v2-20261022/webview2-evidence/`

Each root retains actual command/exits/deployed hashes in `execution.json`, guard
logs, persistent-session identity/results and stopper logs. Full stdout/stderr,
native EGL/compositor/Wine logs, results and PNGs retained; WebView2 additionally
has full Chromium/host/process-map logs. Final Windows records/logs copied under
each `host-procedure/`. Current `live-execution-status.json` says completed/pass/
verified shutdown; preparation markers remain historical. Prefix/profile/runtime
excluded from archives, retained guest state untouched.

Launch HEAD `748482c6`; all source/archive and five host-helper hashes matched
before each boot; all 8/6 deployed sources and 16/19 expected binaries matched
before respective consumers, including approved real WebView2 host/loader/browser.
Full source/dependency/binary provenance tables remain in the accepted v2
manifests; guest archives unchanged from preparation. I686 compiled EXE SHA:
`6a29f5fa3b7fbe37d7de84d6f8aa6de7ea01fe85399afe6010518c83858495ec`.

Inspected both i686 PNGs: actual frame/swatch present; their hashes equal the
original x64 PNGs. Producer-completion and both exact 4096 counts passed. Path
counters report CPU readbacks/compositions, not a GPU-only-copy claim.
Inspected actual WebView2 `screen-18.png`: full window, heading, blue marker and
controls. SHA `a0e3681802a2b1cfb6e50c56d70d7f5ebbee91ec12d3014c4d918ea5571eba95`.
Both WebGL APIs available/not lost, default AND texture `[255,0,0,255]` as this
original host requires, FBO 36053 and all GL errors 0. Animation progressed;
all analyzer classified counters 0. No process-failed/unexpected GPU exit/
renderer-crash/FATAL signature found in complete host/Chromium logs.

Raw logs are not error-free: retain network/unsupported LLM/task-manager/
optional-video/geolocation Chromium diagnostics and Wine COM/token/icon/USB/
setup/stub diagnostics. They are outside existing classified failure signatures;
details in WebView2 `host-procedure/error-review.md`, not a production-safety
claim. Both controls verified inner Wayland EGL AND outer GLES2 VirGL/D3D12 Intel
UHD 750; compositor identities `875/4556` and `863/3904`, alive at end. Owned
Wine cleanup found no remaining servers.

## Verified separate shutdowns

| Cell | Windows WSL PID, UTC start → exit (exit 0) | Linux launcher / QEMU / Xvfb PID:start ticks |
|---|---|---|
| i686 | 33812, 19:54:33.569 → 19:57:08.789 | 295:6328 / 362:6519 / 349:6368 |
| WebView2 | 3184, 19:57:55.418 → 20:00:39.005 | 361:26433 / 376:26565 / 363:26449 |

Each separate exact-identity SIGINT stopper exited 0; final launcher result
records signal-driven graceful QMP powerdown, QEMU exit 0 and both children
stopped. Explicit post-stop owned PIDs absent/SSH2235 closed. Durations ~155/164
seconds inside 900, preserving >90 seconds cleanup reserve. No Ctrl-C or process-
absence-only shutdown inference. Usage before boots 47%/7%, then 53%/8% (5h/week).

Fresh processes/prefixes and rebooted guest prove these bounded regression cells,
NOT resource survival in the faulted process or absence of pre-reboot GPU-global
corruption. Shared overlay/cache/build/runtime context remains. Historical stale/
blanking uncertainty stays open; no full G4/Edge/installed-shell/efficiency/merge
claim. Only this report tracked. Return for parent evidence/image review.
