<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# ONE idle diagnostic attempt — transport failure, probe NOT REACHED

Executed the exact [accepted preparation](CHROMIUM_GRAPHICS_G4_IDLE_DIAGNOSTIC_PREP_20261023.md)
once. Fresh launch usage92% five-hour/14%weekly met <=92 authorization.
Host audit matched manifest `b084c73b...`, five helpers/three sources/two seeds/
ISO/top size+mtime,19 expected pins, key mode600, absent fresh run and no owned
launcher/QEMU. No source changes, retries or further SSH after failure.

Evidence root:
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/native-idle-diagnostic-r1-20261023/`.
Exact launch/observe/stop argv remain in immutable preparation. Controller
started21:23:53.8894015Z; read-only procedure21:24:14–21:24:19Z, exit1.
650 guard passed with879.515397s remaining. Launcher/child identities and actual
NEW80GiB drive were validated. TCP2235 accepted a connection, which established
ONLY forwarded TCP availability, NOT SSH banner/authentication or guest readiness.

FIRST command `df -B1 /home/debian/wex-g4` failed SSH255. `execution.json`
retains actual argv, empty stdout and all88 stderr bytes (text reencoded,
base64, no truncation), decoded exactly:

```text
Connection timed out during banner exchange
Connection to 127.0.0.1 port 2235 timed out
```

No guest free-space result,19 actual binary hashes,435 guard or idle probe was
reached. No guest stage/transfer/output/profile/Wine/helper/browser/compositor
consumer or archive SSH occurred. No calibration/rendering/G4 acceptance.
Host D3D12 GL check passed and QEMU virtio-vga-gl was configured; guest inner/
outer VirGL were NOT verified. Serial log records boot progress, root mounted
with expected filesystem UUID, GROWROOT NOCHANGE, and later systemd/cloud-init
startup; this does not establish SSH readiness or explain the previous idle
probe's exit1. That original cause remains unknown. Live output retention worked.

## Shutdown — stopped, but graceful criterion FAILED

Immediately used the exact-identity SIGINT stopper, no further guest command.
Launcher stop_reason=`signal`; existing QMP system_powerdown path timed out after
45s and entered its existing terminate fallback. `results.json` records QEMU0,
both children stopped, but **no graceful_powerdown=true**. Stopper therefore
correctly exited1. Do NOT infer graceful guest shutdown from QEMU0/controller0
or claim a clean filesystem. Full `powerdown_error` TimeoutExpired (actual QEMU
argv and45s bound) remains in raw `results.json`; stopper's terminal error was
`RuntimeError: Launcher final result does not attest signal-driven graceful cleanup`.
Raw `qemu.stderr.log` confirms `qemu: terminating on signal 15 from pid 453919 (python3)`.

Persistent controller session was held through exit: WindowsWSL exit0 at
21:25:13.0917622Z (PowerShell session exit0). Independent host checks confirmed
Linux launcher453919/start5935022, QEMU453952/start5935136 and Xvfb453927/
start5935075 PIDs absent; Windows31472 absent. No cleanup retry or repair.

`shutdown-and-evidence-audit.json` retains these statuses and SHA256s of15 saved
files, including execution/guard/identity/results/serial/QEMU/Xvfb/host GL logs,
copied preparation and adjacent `.wsl-session.json`/stdout/stderr logs. The audit
itself is additive; preparation and original calibration R1 remain untouched.
No unit reruns or implementation changes during this execution-only handoff.
Only this report is committed; M wine/D:master/other workers remain untouched.

STOP for parent review. The intended idle observation and graceful-shutdown
criterion were not achieved; no retry, readiness fix or subsequent phase is
authorized. Any next boot/check must be separately planned by parent.
