<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Readiness/QMP integration — PREPARED_NOT_RUN, creation NOT authorized

Implements only [READY_BOOT_INTEGRATION_PLAN](CHROMIUM_GRAPHICS_G4_READY_BOOT_INTEGRATION_PLAN_20261024.md).
No VM/QEMU-system/SSH/Wine/helper/browser execution, image creation, resize,
repair, mount or guest access. Only allowed host read-only qemu-img info/full
former-top hash and local host tests ran. Old evidence/preparations/layers,
D:master, M wine and other workers remain unchanged.

Ignored launcher now selects ONLY `system-readiness-r1-20261024.qcow2`, rejects
missing/noncanonical/symlink target before outputs/sockets/children, no former-
top fallback. Shutdown retains reviewed QMP evidence; graceful requires all
protocol/guest-event/ACK gates, owned QEMU0, no fallback/error and stopped
children. Normal maxima50+5+10+5+5=75s inside total90; fresh stopper80s.
ACK-only/exit0 or forced terminate/kill never becomes graceful.

New explicit `native-idle-ready` procedure uses authenticated readiness BEFORE
df. Raw controller remaining must allow674s (650+24 two-reply headroom); gate
cap=min(180,remaining-650). Attempts/errors are preserved; failed/exceptional
gate causes NO further SSH. Then650 guard, free>=1GiB,19 hashes,435 guard and
EXACT original probe d36b5d1d24f0964c98491263a034e6477f7f6af26a5079cc1244e2b39327aac0.
Empty/busy/error never enters staging/consumer/archive. Fresh byte-origin label
now covers subprocess success/failure without inventing exceptions. Existing
read-only/calibration historical artifacts and consumed authority stay intact.

## Immutable private evidence

Under `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/`:

- `native-idle-ready-r1-20261024.preflight/preflight.json` SHA256
  `ce85b091eedd02e217f36898f4841ba0da54bae52e075ec94ef27a497ef219b3`.
  Includes7 actual helper pins,9 source inventories (7 tests+2 host sources),
  19 accepted guest pins,2 seeds/ISO, credential/public-seed identity, exact
  readonly/launch/run/stop argv, storage inspection/create argv, limits and
  expected four-layer paths/sizes/backing/dirty/corrupt metadata.
- `native-idle-ready-r1-20261024.change/` retains before/after files, patches
  and successful preparation stdout/stderr (stderr empty). Hashes below.

| Private artifact | SHA256 |
| --- | --- |
| launch.before.py | d96f6854f0360cfb03c24ea6092aa8b3930cc1075af5cfb8aadfdeac65efaf82 |
| launch.after.py / actual launch.py | 895f37a6f60d1ea0a4cadf779fff406eef1ff75851bc95f88f257ede8dadc1c1 |
| launch.patch | d70b3483c5348027e2027dbbecd2fa24a2d0d912cc043f26a6698c772c076693 |
| procedure.before.py | e663e8f5bf6924b069738acac39dd677705de9399c120c114bce4c62230b4445 |
| procedure.after.py / actual procedure | 4cd417eb01663b18c67f7adb81869afcc63e8cd3c38bdaeb42ea335241c74609 |
| procedure.patch | 3abbd59bcd90260eadccb3a4f4b2876ee87bf229a2fb14a5a8aeab436ba1cc56 |

Normal-lock info proved EXACT unchanged3 existing qcow2 layers, canonical paths,
no dirty/corrupt flags. Former top physical3353083904bytes full SHA256
c2acebb986bab6e3c6cd6bdd0c7efbe4c4d3b4f7f69ed358a7d53598ba1f9936 matched,
with identical device/inode/size/mtime/ctime before/after; hash/post-check11.485s
within120s bound. Lower identities match historical full-hash records; NO65GB
rescan. Owner/open-chain snapshots empty before/after (not an atomic lock).
D: free1924321910784bytes exceeds80GiB reserve. Pinned qemu-img hash verified.
New overlay/run absent. Full preparation audit rechecked all current inventory
hashes,19 pins, exact probe and four-layer FUTURE metadata. No fourth layer exists.

## Actual tests / next boundary

WSL Debian, owned C: `scripts`:

```text
python3 -B -m unittest test_g4_ready_boot_integration test_g4_guest_readiness test_g4_qmp_shutdown test_g4_virgl_launcher test_g4_virgl_budget test_g4_remote_failure test_g4_idle_diagnostic -q
Ran 69 tests in 4.792s — OK, exit0, no skips
```

Six new methods execute actual AST-compiled launcher functions and imported
private procedure with fakes: overlay missing/old-only/symlink, event+QEMU0,
ACK-only/error/nonzero/fallback, identity refusal, exact cleanup bounds,
readiness first/failure/exception/headroom, cap/650/435, helper/source/pin/drive
mismatch and unconditional no-stage/archive behavior. Remaining63 reviewed
controls also pass. New fixture/log paths use D:; plain test fixture bytes are
not formatted images. No integration tests execute the launcher module body.

STOP for parent review BEFORE separate creation authority. Future create is
`qemu-img create -f qcow2 -F qcow2 -b <exact former top> <exact new top>` with NO
size/force option. Fresh owners/reserve/absent target/inventory/full frozen-top
hash required before AND after creation, exact4-layer info and unchanged old
identities/content; no overwrite/repair fallback. Parent separately reviews
creation before ONE later boot. Rehash frozen former top after boot and check
lower identities. Journal replay may affect NEW layer only; no filesystem-clean,
live readiness, ACPI, guest19-pin, graphics or calibration claim is made here.
