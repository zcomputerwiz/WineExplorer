<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Durable-key r2 idle preparation — PREPARED_NOT_RUN

Only [DURABLE_IDLE_PREP_PLAN](CHROMIUM_GRAPHICS_G4_DURABLE_IDLE_PREP_PLAN_20261024.md)
implemented. No VM/guest/SSH connection, image creation, disk mutation, runtime
build or retry. Launcher and reviewed helpers unchanged; only ignored procedure
PREPARATION code and narrow integration tests adapted. Old snapshots/manifests/
evidence, M wine and D:master preserved. Source commit is provenance, not a
substitute for the final actual source hashes pinned below.

Fresh private manifest:
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/native-idle-ready-r2-20261024.preflight/preflight.json`
SHA256 `7469840a6289a2e532c238f87a2f49f364de7b96baecb3115076e547857fdc6f`.
Mode remains native-idle-ready; exact launch/run/stop commands use r2. BOTH
SSH argv[2] and credential path are `/var/tmp/wex-g4-lab-credentials/vm_key`.
Canonical native ext4, UID/modes0700/0600, readability/internal source-content
comparison and seed-public match passed; metadata/fingerprint only, no secret
bytes/private content hash. Reviewed credential observer source/seal pinned.

Current7 helpers,9 sources/tests,2 seeds/ISO/prior record/observer hashes audited;
readiness helper5232f407... and all19 EXPECTED pins retained. Exact original
probe SHA d36b5d1d24f0964c98491263a034e6477f7f6af26a5079cc1244e2b39327aac0.
900/90 lifetime/reserve,80s stopper,674 headroom/cap180/650/435, guest1GiB and
unconditional no-stage/no-archive path remain. No live guest binary hashes.

The SAME existing safety layer is explicitly previously booted, not absent or
never-used:27590656bytes, full SHA393308d2dcf343df1dac1d6a2f47a4f4f27d70e26a88596d0d3f97eacf14999c,
exact identity from sealed r1 after.json. No create argv/authority in new manifest.
Normal-lock exact4chain/backings/sizes/flags passed; all frozen3 identities and
historical lower identities unchanged. Former-top full SHA c2acebb986bab6e3c6cd6bdd0c7efbe4c4d3b4f7f69ed358a7d53598ba1f9936
matched in10.596054254s within120s; no65GB rescan. Empty owner/open-file snapshots,
D: free1924293476352bytes (>80GiB). Fresh r2 run absent. No filesystem-clean claim.

## Actual tests / retained change

WSL Debian, owned C: checkout `scripts`:

```text
python3 -B -m unittest test_g4_ready_boot_integration test_g4_guest_readiness test_g4_qmp_shutdown test_g4_virgl_launcher test_g4_virgl_budget test_g4_remote_failure test_g4_idle_diagnostic -q
Ran 75 tests in 5.058s — OK, exit0, no skips
```

Prior72 plus3 methods exercise actual preparation with missing/changed identity/
changed content/wrong backing, stale/missing durable credentials, both recorded
durable paths and no-create/network dispatch. Fake positive preparation cannot
dispatch any process; backing negative allows ONLY fixed normal-lock info.
Actual offline preparation additionally used local findmnt/ssh-keygen public
derivation and qemu-img INFO, never an SSH connection or image mutation.

Private `native-idle-ready-r2-20261024.change/` retains preparation stdout/stderr
(exit0, stderr empty), before/after and patch:

| Artifact | SHA256 |
| --- | --- |
| procedure.before.py | 4cd417eb01663b18c67f7adb81869afcc63e8cd3c38bdaeb42ea335241c74609 |
| procedure.after.py / actual private source | 8b54e69c60d4e874216b0061122431737e45538333b99036a79e4c47e17c8907 |
| procedure.patch | 74118654af96c2df6e2edf829466c6adc6716f6026026a81ea0b6df9b6ca9302 |

`sealed-evidence.json` in that directory hashes all7 final preparation/change
files. STOP parent review before any guest dispatch. No readiness, original
idle-failure cause, graphics, calibration or new live authority is implied.
