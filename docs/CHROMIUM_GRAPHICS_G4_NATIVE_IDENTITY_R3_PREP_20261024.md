<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Native identity R3: PREPARED_NOT_RUN, target absent

Completed only NATIVE_IDENTITY_ISOLATION_PLAN_20261024 offline work. No image
creation, boot, SSH/guest access, Windows/Wine/browser/debugger execution, PE
build, download or runtime extraction. Creation and boot need separate parent
authorizations after this review. Existing failed R2 paths/layer remain preserved.

Fresh immutable preparation:
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/native-exit-calibration-r3-20261024.preflight/`.
Preflight SHA256
`f9edf5af01e21807640f44b77160af33ab97253e6b08a10a7b85d6c75ab3fe4a`;
16-member source TAR SHA256
`84570cc0671bb977e5b916edf1c8bc18316605b36fc2942e9598d56f4fd777d1`.
All members are original project sources/tests, two original366c/6087 worker PEs
and pins; no vendor payload. Original five C/helper test build sources, receipt
and eight logs are unchanged. Eight host helpers, six separate host tests, seeds/
ISO, durable key metadata/public matching and19 EXPECTED environment pins are
recorded. Expected pins and historical guest paths are not live guest proof.

Only new target `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/system-native-identity-r3-20261024.qcow2`
is selected by the private launcher. It remains ABSENT. Missing/symlink/
noncanonical target fails before Linux output/socket/child creation; no fallback.
Preparation records ONE future qemu-img create argv, no size/force option,
backing frozen `system-capacity-r1-20261023.qcow2`; it was NOT executed. Exact
Windows/WSL create, launch/run/80s stop commands and future normal-lock inspection
argv are in the manifest. Create first needs reviewed fresh gates and its own
exit/stdout/stderr/chain record; later boot needs separately accepted creation.

Actual offline checks: normal-lock OLD four-chain, frozen three identities,
historical unchanged lower hashes, no observed owners/open chain and>=80GiB D:
reserve passed. Frozen parent full SHAc2acebb9... matched in10.418934s; preserved
failed layer identity/full SHAda698bec... matched in0.143191s. Future chain is
new80/frozen80/old64/Debian3GiB, exact backings/no dirty/corrupt flags. Failed R2
layer is a preserved sibling, never the new backing. Future before/after gates
require that failed layer and frozen originals unchanged. No65GB rescan or disk
mutation occurred.

Explicit R3 Python approval is exactly parent-reviewed9f86add0 runner477846d5...
and readerac97a7ea..., not obsolete R1 approval. HOST name is R3; compiled GUEST
stage/output stay ORIGINAL R1. Both future canonical/lexists absence checks remain
mandatory before writes, even though history says the frozen parent was not
staged. Authenticated674/cap180/650, free1GiB/19actual/435, strict COMPLETE+empty
ownership/64KiB raw capture,16 deployed hashes and unchanged normal calibration
are retained. Busy/UNKNOWN/nonzero/truncation/errors stop before staging. Original
900/90/80,285/300/45/55,42-30-5-5 bounds, privateLO/unique sockets/actual innerouter
VirGL, strict matching and rejected evidence-before-reap remain. Shutdown needs
attributed QMP event+ACKs/QEMU0/no fallback/owned absence/Windows0.

Validation through WSL Debian from the C: checkout's scripts directory:

```text
python3 -B -m unittest test_g4_native_identity_isolation test_g4_rejected_identity test_g4_native_calibration_refresh test_g4_native_exit_calibration test_g4_native_exit test_g4_prefix_ownership_integration test_g4_prefix_ownership test_g4_ready_boot_integration test_g4_guest_readiness test_g4_qmp_shutdown test_g4_virgl_launcher test_g4_virgl_budget test_g4_remote_failure test_g4_idle_diagnostic test_g4_edge_owned_threads -q
Ran 164 tests in 26.418s: OK, no skips.
```

Prior158 assertions retained plus six R3 controls: actual guarded launcher prefix,
explicit source approval, actual fake-dispatch R3 branch/failed gates, distinct
parent/failed-layer identity/content/metadata/reserve negatives and actual
preparation with synthetic storage/no process dispatch. Old overlay-selection
test now checks the new fixed name. The legacy R2 positive fixture explicitly
supplies its historical launcher approval; real stale-source refusal remains.
No source/PE acceptance was weakened. Local Linux/shared-C controls are not native
API/browser/G4 proof.

Before/after launcher/procedure, patches, logs and audit are retained under
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/native-exit-calibration-r3-20261024.change/`.
Launcher895f37a6... ->7d88ba5d...; procedure2502081a... ->e5bd0131....
Audit checks56 file hashes/all16 regular TAR members; `audit.json` SHA256
`05829677bc78cb922bb740dbddc023fe15bf3843f6b8be188e00bca3969d880f`.
Manifest source_commit precedes this test/report commit; actual final per-file
hashes pin WIP inputs. C: branch only, existing M wine/main D: master/other workers
untouched. STOPPED for parent review; no execution authority implied.
