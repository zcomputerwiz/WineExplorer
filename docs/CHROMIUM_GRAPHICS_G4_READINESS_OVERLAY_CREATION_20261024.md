<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# ONE safety-overlay creation — CREATED_VERIFIED_NOT_BOOTED

Executed only the creation authorized in
[READY_BOOT_PARENT_REVIEW](CHROMIUM_GRAPHICS_G4_READY_BOOT_PARENT_REVIEW_20261024.md).
No boot/SSH/guest/Wine/helper/browser/consumer execution, resize, repair, rebase,
mount, commit or deletion. Only this report is tracked; private helpers,
immutable preparation, original layers/evidence, M wine and D:master unchanged.

Fresh gates passed: immutable preflight SHA ce85b091..., all27 parent-audited
files (7 helpers/9 sources/2 seeds/ISO/prior+6 changes/qemu-img),19 expected pins,
exact original probe, canonical absent target/run, normal-lock original3-layer
metadata, unchanged original identities and idle owner/open-file snapshots.
D: free1924321660928bytes exceeded80GiB reserve. No65GB full rescan.

## Actual command / result

Exactly once, with30s bound:

```text
/usr/bin/qemu-img create -f qcow2 -F qcow2 -b /mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/system-capacity-r1-20261023.qcow2 /mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/system-readiness-r1-20261024.qcow2
```

Exit0 in0.068481765s; stderr empty. No size/force option. Complete observation
01:58:22–01:58:52UTC, exit0. Normal-lock post-info validated exact canonical
qcow2 chain80/80/64/3GiB, exact backings/formats and all dirty/corrupt=false.
Original3 device/inode/size/mtime/ctime identities remained equal before/after;
owners/open-chain snapshots remained empty. These are point-in-time evidence,
not an atomic cross-file lock or guest filesystem cleanliness proof.

Frozen former-top full SHA256 before AND after:
`c2acebb986bab6e3c6cd6bdd0c7efbe4c4d3b4f7f69ed358a7d53598ba1f9936`.
Both hashed3353083904bytes:14.932994051s /14.774410706s, each within120s.
All27 input hashes were rechecked immediately before create and afterwards.

New disk `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/system-readiness-r1-20261024.qcow2`:
physical197888bytes; SHA256
`8db102978344a22d6f0815f9f93f19e9b4a55c8b1ce22020e65a19360e98d869`.
New identity stable across checks; no guest writes or journal replay observed
because it was never booted. Future run remains absent.

## Preserved evidence

Directory `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/readiness-overlay-creation-r1-20261024/`:

- `result.json`: all argv/exit/timing/inventory/identities/metadata/full hashes;
  SHA256 `5fe511dd6e78baa05ad69b000406a3998379be43e5f8165174516dc398c212af`.
- `create.stdout.log`/`create.stderr.log`, `info-before.*`/`info-after.*`,
  original one-shot `create-once.py` and outer `observation.*` logs retained.
- `final-evidence-sha256.json`: sealed hashes of10 final files, SHA256
  `1924ecf686e14c929a92c2fbd1898c235633978fc3706acc44f3f447b2d39d53`.
  Use this final map: result.json's preliminary outer stdout hash was sampled
  before the final print; original record is preserved without overwrite.

No source tests rerun for execution-only creation. Parent-reviewed69 controls
remain the prior implementation evidence, not a live guest result. STOP for
parent review BEFORE any boot/SSH/helper/browser. No automatic calibration or
filesystem-clean/readiness/shutdown/G4 claim follows creation success.
