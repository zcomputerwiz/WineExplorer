<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# 8 GiB Chromium control trial — result (single-variable RAM comparison)

One fresh normal headful Chromium control at **8192 MiB** guest RAM, the only
change from the 4 GiB control-channel baseline that OOM-killed its renderer.
Candidate, browser arguments, HTTP fixture, private loopback, VirGL runtime and
security are identical to that baseline; no Wine rebuild, no new trace
deployment. The renderer OOM **does not persist at 8 GiB**: the guest kernel
never invoked the oom-killer, no Chromium fatal/crash/OOM appears in the log,
and the browser child was still alive at the 45-second observation deadline — a
browser-lifetime / native held-handle timeout, not a page failure.

## Single-variable change

Owned QEMU launcher `build/g4-virgl-20261022/launch.py`: RAM `-m 4096` → `-m
8192`. Nothing else on the argv changed (machine q35, smp 4, virtio-vga-gl, gtk
gl=on, R4 disk, seed.iso, hostfwd 2235→22, serial log, QMP socket, -no-reboot).

No swap installed. No browser memory-capping / renderer-limit flags. No feature
change, sandbox bypass or image replacement. Fresh stage/output/profile/helper
paths under a new `chromium-memory-8g-20261028` run name; no evidence or
candidate deletion, no path reuse from the 4 GiB run.

## Candidate reuse (no rebuild)

Reused the last actually deployed matched full combined Wine candidate from the
control-channel run — **02d4ef2 diagnostic lineage over fffa7de** — at
`/home/debian/wex-g4/wine-control-channel-20261027/build/wow64`, with its client,
server, modules and browser runtime unchanged. Availability was confirmed first;
the four out-of-trial matched DLLs (userenv/kernelbase, x86_64 + i386) are
present and byte-identical before and after the run (see Cleanup). No Wine
rebuild or new trace deployment occurred.

## Actual QEMU argv (recorded in execution.json)

```text
qemu-system-x86_64 -accel kvm -machine q35 -cpu host -smp 4 -m 8192
  -device virtio-vga-gl -display gtk,gl=on
  -drive file=.../system-native-identity-r4-20261024.qcow2,format=qcow2,if=virtio
  -drive file=.../seed.iso,format=raw,media=cdrom,readonly=on
  -nic user,model=virtio-net-pci,hostfwd=tcp:127.0.0.1:2235-:22
  -serial file:.../chromium-memory-8g-20261028/serial.log
  -qmp unix:.../isolated-sockets/virgl-runtime-338v5ygj/qmp.sock,server=on,wait=off
  -no-reboot
```

`-m 8192` is present in the live argv. Full argv retained in the private run dir.

## Guest memory (before and after the consumer)

Compact meminfo sample at start/end of the browser interval (execution.json):

| field | before consumer | after consumer |
|---|---|---|
| MemTotal | 8138032 kB (~7.76 GiB) | 8138032 kB |
| MemFree | 6986348 kB | 6543344 kB |
| MemAvailable | 7763464 kB | 7775540 kB |
| SwapTotal | 0 kB | 0 kB |
| SwapFree | 0 kB | 0 kB |

`guest_memtotal_bytes = 8333344768`. MemFree fell by ~433 MiB (443,004 kB) across
the whole consumer interval — modest, with no exhaustion and no swap (SwapTotal
stayed 0).

## Kernel OOM: none at 8 GiB (vs the 4 GiB baseline)

The 4 GiB control-channel run's retained `serial.log` shows a real global guest
OOM (parent-reviewed): oom-killer invoked at kernel 84.055919, normal-zone free
33248 kB below min 33656 kB with all_unreclaimable yes and total swap 0, then
CrRendererMain Linux PID 1183 selected and killed (anon-rss 748648 kB) at kernel
87.986467.

This 8 GiB run's retained `serial.log` contains **no** oom-killer invocation, no
"Killed process", and no "out of memory". The renderer OOM does not reproduce at
double the RAM.

## Chromium log: no fatal/crash/OOM

`chromium-native-evidence/chromium.log` shows only one benign self-recovering
line (132): `Network service crashed or was terminated, restarting service`. No
OOM, FATAL, SIGSEGV, GPU crash or renderer death appears in the log.

## Consumer outcome: browser-lifetime timeout, not a page failure

`chromium-native-evidence/results.json` journal_outcome:

- outcome: **native_timeout**
- bound: helper_pid 32 → child_pid **224**, image chrome.exe
  (chromium-headful-r1-20261023-stage/vendor/extracted/chrome-win), `--app=<fixture>`,
  tick_ms 43436 (~43.4 s)
- terminal: event **timeout**, child_pid 224, wait_result **258** (WAIT_TIMEOUT),
  wait_ms 45000
- accepted_observation false; manifest_marker_verified false

wait_result 258 is WAIT_TIMEOUT, not WAIT_OBJECT_0 (267): at the 45-second mark
the Chromium child was **still running**. Per the work order, a still-running
browser at 45 s is not itself evidence of a failed loaded page — this is a
browser-lifetime / native held-handle observation timeout, distinct from a page
failure. `consumer_ssh_exit` is 1 with `diagnostic_nonacceptance_expected: true`;
the diagnostic by design makes **no** rendering or page-consumption acceptance
claim (results.json classification: "ordinary CreateProcess held-handle Chromium
status ONLY; never rendering/security acceptance"; "HTTP writes only; browser
consumption NOT_PROVEN").

Fixture nonce `91672e92ad015ae9c33e089c8ef87d40`, URL
`http://127.0.0.1:45039/91672e92ad015ae9c33e089c8ef87d40/index.html#nonce=...`.
Renderer string `virgl (D3D12 (Intel(R) UHD Graphics 750))`. A visible front was
captured (`terminal-front.png`).

## Cleanup and shutdown

- front_exit 0; cleanup_kill 0; cleanup_wait 0
- remaining_servers []; remaining_prefix_processes {} — no leftover owned processes
- outside_modules_unchanged **true** (the four matched-candidate DLLs byte-identical)
- archive_exit 0
- Graceful QMP powerdown via the exact-identity stopper: `graceful_powerdown true`,
  `qemu_exit_status 0`, qemu_stopped + xvfb_stopped both true

## Acceptance mapping (work-order criteria)

- fixture index/scene.js and nonce readiness — present (nonce above, private loopback up)
- visible front — captured (terminal-front.png)
- native held-handle status — recorded (child pid 224; bound + timeout journal)
- kernel OOM count — **0**
- clean owned shutdown — yes (graceful QMP powerdown, QEMU exit 0, no leftovers)

Result: with the single variable RAM raised 4096 → 8192 MiB, the renderer OOM
that killed CrRendererMain at 4 GiB does not persist. This is a scoped Chromium
memory comparison; it claims no full Edge/G4/security acceptance and no rendering
acceptance.

## Harness development notes (pre-consumer aborts, context only)

Several earlier QEMU boots aborted **before** the consumer was ever dispatched —
all harness-side, none touching candidate/trace/security:

1. cmdline race on the live /proc scan for the qemu pid;
2. budget handoff timing (run() started too late after a fresh boot);
3. partial launcher-identity.json read (qemu_pid not yet written) → KeyError;
4. guest-MemTotal parse bug — `guest_meminfo` values are strings, not lists, so the
   first character was indexed and the pre-consumer RAM check raised despite 8 GiB
   being present;
5. an `ssh()` shell helper that recursed on its own name instead of invoking the
   binary, so the SSH-readiness wait never connected.

Each was fixed and verified on a subsequent fresh boot; the final boot reached and
completed the consumer. Exactly **one** consumer dispatch occurred (no identical
second run); the aborted boots dispatched no browser.

## Files changed and where they live

Committed on `codex/chromium-memory-8g`:

- `scripts/check-g4-chromium-native-exit.py` — added `--memory-8g` mode (host run
  name `chromium-memory-8g-20261028`; BUILD override to the reused candidate;
  browser-only consumer)
- `docs/CHROMIUM_MEMORY_8G_RESULT_20261028.md` — this report

Local recipes under gitignored `build/` (not committed, matching every prior g4
trial — nothing under `build/` is tracked in this repo):

- `build/g4-virgl-20261022/launch.py` — RAM `-m 4096` → `-m 8192` (the single-variable
  change; full argv recorded above)
- `build/g4-virgl-20261022/chromium-memory-8g-trial.py` — per-mode orchestrator
  (prepare/verify/deploy/consumer/archive + budget guards; guest-MemTotal parse fix
  at the pre-consumer RAM check)

The `wine` submodule working-tree pin change in this worktree is left untouched and
not committed (no Wine/compositor pin update). Private run evidence stays in
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/chromium-memory-8g-20261028/` (not committed).
