<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Capacity maintenance r1 — actual one-boot execution

Executed the ONE maintenance authorized in CAPACITY_EXECUTION_PREP; immutable
preflight SHA256 `f5fef641fce0a7e0d34cf74c85c58f307cd58478ebb001584699c59f983fc2bf`.
That preparation's PREPARED_NOT_RUN marker remains historical, not current status.
Actual evidence is private D: `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/`
`capacity-maintenance-r1-20261023/`, plus adjacent same-stem creation, prehash,
overlay-inspection and Windows-controller records. Original failed Stage A remains
untouched. No browser, Wine command, download, second boot or manual repair/growth.

## Actual mutation and commands

Precreate inventory/owners/absent-target/space gates passed; host free
1,860,751,851,520 bytes. `g4_capacity_maintenance.py verify-originals` exit0,
full original-chain hashing214.609054s, matched both accepted hashes/identities.
Actual creation at2026-10-23T16:00:32Z, exit0:

```text
/usr/bin/qemu-img create -f qcow2 -F qcow2 -b /mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/system.qcow2 /mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/system-capacity-r1-20261023.qcow2 85899345920
```

`verify-overlay` and `qemu-img info --backing-chain --output=json NEW` exit0:
exact new80GiB -> original64GiB -> Debian3GiB chain. No original image resize.
Only reviewed ignored C: launcher drive line was changed via apply_patch to NEW;
actual bytes equal proposal, SHA256
`d96f6854f0360cfb03c24ea6092aa8b3930cc1075af5cfb8aadfdeac65efaf82`.
Original launcher text/diff remain in immutable prep. Old consumer manifests
pinning the previous launcher are now stale; future work needs fresh reviewed prep.

One persistent Windows controller command:

```powershell
pwsh -NoProfile -File C:/Users/Owner/.codex/worktrees/chromium-graphics-plan/WineExplorer/scripts/g4-virgl-launcher-session.ps1 -RunName capacity-maintenance-r1-20261023
```

Controller start16:01:02.6841456Z. RawUTC650/435 guards passed with
871.726817/796.648013 seconds remaining respectively; source/tool/seed inventory,
actual NEW drive argv and Linux PID/start identities separately validated.
Empty startup child-cmdline placeholders were not treated as identity proof.

Approved key-only strict-known-hosts SSH `debian@127.0.0.1:2235 'python3 -B -'`
received exact source stdin, SHA256
`94736cb7c73b47bb740d53a34d884a299c5a89d3b7b6e9867452bf964b820296`.
Full argv retained in guest-execution.json; SSH bounded150s, fixed nine read-only
commands bounded120s total/15s each. Execution16:02:46Z–16:02:48Z, exit0;
all nine commands exit0/reaped. No staging/profile creation or additional probe.

## Actual capacity result

`validate-guest` exit0. Disk80GiB; root start262144/end167772126,
PARTUUID624cfa91-935d-4cc7-9a3f-f29b41cac2a0 and ext4 UUID
fe2c0409-10e4-4cd2-949b-a61b19c5a757 preserved. BIOS2048..8191 and
EFI8192..262143 unchanged, with expected types/GUIDs/attributes.
Serial explicitly records automatic GROWROOT and ext4 growth16744443 ->20938747
blocks (4096 bytes/block). Free16,947,539,968 bytes; cloud-init done/no errors,
services running, exact19 accepted binary hashes unchanged (including Edge/labwc;
not21). Automatic first-boot growth mutated only the NEW top layer.

## Shutdown and original-content gate

Separate identity-validated `stop_g4_virgl_launcher.py --identity RUN/launcher-identity.json --timeout60`
sent SIGINT to launcher429139/start3997884, not Ctrl-C. Final QMP graceful_powerdown
true, stop_reason signal; QEMU429154/start3997928 exit0 and stopped;
Xvfb429141/start3997896 stopped. Windows WSL controller9836 exited0 at
16:02:57.0376294Z; stopper0, maintenance primaryError null. Final gate independently
requires QEMU0 AND Windows0 and all three recorded Linux identities absent.

`verify-finished` exit0: shutdown_clean0 true, all three recorded processes absent;
full post-hash217.325020s within600s. Both original file identities and complete
physical contents match precreate and preparation:

| Original | SHA256 before AND after |
| --- | --- |
| system.qcow2 | 89da0f350f487d569d6eefb54c5393c6e25f4e67e7169bcbcd2d99ab5a0e8900 |
| Debian backing QCOW2 | 0062fb66565560d03bc67df298196d0c960f9db55bdb0d00fef67fbb895b1e62 |

Results retained in verify-finished.json/posthash-exit.json. BOTH validation gates
passed, not merely stopper0. Hashing was outside the stopped VM's900s lifetime/
90s cleanup reserve. This is file-content/identity continuity, not an atomic
cross-file snapshot. Separate execution-status.json records actual completion;
evidence-hashes.json inventories retained run and adjacent execution records.

## Scope and handoff

Report-only tracked change; no unchanged unit tests rerun during live maintenance.
Existing reviewed30-method capacity coverage remains prior evidence, not growth
proof. Actual guest/provenance/cleanup results above provide maintenance evidence
only, not filesystem-wide integrity, graphics acceptance or Chromium readiness.
All disks/evidence preserved, D: master/other workers/submodules untouched;
pre-existing C: `M wine` unstaged. Stop for parent review before Chromium prep.

## Independent parent acceptance

Parent independently rehashed all42 listed run/adjacent evidence records with
zero discrepancies. Revalidated actual guest stdout against the manifest:
exact grown root/UUIDs/other extents/attributes,16,947,539,968 free bytes and19
accepted hashes. All nine exact commands exit0/reaped/within bounds; actual stdin
matches pinned source. Checked actual650/435 rawUTC guards, source/seed/tool/
proposed launcher hashes, creation exit/three-layer chain, QMP/QEMU/Windows0
and all owned Linux identities absent. Both original full post-hash records
match preparation AND precreate contents/identities; current original file
identities still match. No capacity or cleanup waiver; no extra boot/probe.

Capacity maintenance is accepted. The only future writable root is the NEW
system-capacity-r1-20261023.qcow2; keep every backing file intact, never open
the old root writable. New Chromium preparation MUST use current launcher hash
d96f6854... and fresh inventory, not old consumer preflights. Parent authorizes
only the offline acquisition/adapter/tests/preparation in CHROMIUM_PREP_HANDOFF,
VANILLA_CHROMIUM_PLAN and CHROMIUM_CANDIDATE_20261023. No guest staging/boot,
browser execution, symbols or alternate runtime until separate parent review.
