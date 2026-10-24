<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Native calibration refresh: PREPARED_NOT_RUN

Implemented the bounded offline refresh in the owned C: graphics checkout,
following CHROMIUM_GRAPHICS_G4_NATIVE_CALIBRATION_REFRESH_PLAN_20261024.md.
No VM boot, SSH connection, guest write, Wine/Windows PE/browser/debugger
execution, image creation, runtime transfer or rebuild occurred.

Fresh HOST preparation is
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/native-exit-calibration-r2-20261024.preflight/`.
Its immutable `preflight.json` SHA256 is
`45abc62d3156b22f1fb4971aa622a7c7c081cdbddf81d8370b654dc8ead3d063`;
`source-stage.tar` SHA256 is
`7e72ee0de1b58b8b01ac97da33cdc329385e73c2ddddcd6ca6411cc8fbe310d3`.
Exact host launch/run/80s stop and guest consumer/archive/absence commands are
recorded there. The host run remains absent.

GUEST stage/output deliberately remain the compiled ORIGINAL R1 paths:
`/home/debian/wex-g4/native-exit-calibration-r1-20261023-stage` and
`/home/debian/wex-g4/native-exit-calibration-r1-20261023`.
Both require a future canonical-path/lexists absence check before writes.
Host absence is not evidence of guest absence. The original runner, five
helper/contract test sources, worker build receipt/eight logs and original
PEs366c6928.../6087e3ef... are unchanged. No identity rule was relaxed.
Native schema remains mode=calibration, manifest=null, native attestation=false.

Only this explicit refreshed private branch now uses authenticated readiness
before guest preflight, durable credential, current protected storage and the
accepted self-contained ownership PROBE/strict decoder before staging.
Raw success/failure stdout is retained up to65536 bytes before interpretation.
Only validated COMPLETE+empty continues; busy/UNKNOWN/nonzero/timeout/malformed/
truncated responses and failed readiness/budget/space/hash/identity/absence gates
stop without staging or archive SSH. Legacy modes retain their original paths.
The runner's existing unprivileged resource sample remains secondary best effort;
the privileged pre-stage observation is not an atomic ownership lock.

The preparation contains exactly16 original source/PE/pins TAR inputs, eight
host helpers, three separately inventoried host tests, two seed sources/ISO,
19 expected environment hashes and the original build receipts. No vendor
payload is included. Expected guest hashes are not live observations. The
674/cap180/650/free1GiB/435,900/90/80,285/300/55,42/30/5/5 bounds and actual
VirGL/privateLO/unique-socket/native identity/reap/cleanup requirements remain.
Final shutdown still requires attributed QMP event plus acknowledgements,
QEMU0/no fallback/owned children absent and Windows controller0.

Read-only storage preparation validated the sealed37,879,808-byte safety top
SHA47b82682..., frozen three identities, normal-lock four-layer metadata and
full former-top SHAc2acebb9... in10.811896s. D: free was1,924,280,803,328 bytes.
No disk mutation/create command was added. Durable credential metadata/content
and seed-public matching passed without secret output. Old preparations remain.

Private changes/evidence are retained under
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/native-exit-calibration-r2-20261024.change/`:
`procedure.before.py`, `procedure.after.py`, `procedure.patch`, preparation logs,
`focused-tests.log`, `audit-preparation.py` and `audit.json`.
Before procedure SHA385dfaf0...; after SHA
`2502081ab63551add529a22518ac209e4196f6b813e99815a8de306183bf1fcd`.
The audit passed49 file hashes and all16 actual TAR members; audit SHA256
`3ce5fa1f53309e326994d4652f0645012fdba7f7db29ded8ab11d4dcfd1e79cf`.
The manifest's source_commit records the preceding HEAD70da11f6; current per-file
hashes pin the test/private-source work committed with this report.

Validation from the C: checkout's `scripts` directory through WSL Debian:

```text
python3 -B -m unittest test_g4_native_calibration_refresh test_g4_native_exit_calibration test_g4_native_exit test_g4_prefix_ownership_integration test_g4_prefix_ownership test_g4_ready_boot_integration test_g4_guest_readiness test_g4_qmp_shutdown test_g4_virgl_launcher test_g4_virgl_budget test_g4_remote_failure test_g4_idle_diagnostic -q
Ran 129 tests in 20.525s: OK, no skips.
```

This includes prior102 controls,19 native calibration/shared-C controls and eight
new refresh tests. Tests exercise the actual private branch with fake guest
callbacks, original schema/parser/reap failures, lossless capture, pre-stage
gates, current-storage negatives, exact fixed paths and actual offline preparation
without network/create/native dispatch. An original host C harness and local
Python controls execute; Windows APIs/native calibration remain unproven.
Parent review is required before any later live calibration. No browser/graphics
acceptance is claimed. D: master, other workers and existing M wine are untouched.
