<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Capacity Stage A checkpoint — layout proved, NOT_READY_FOR_STAGE_B

No image created/resized, VM/Wine/browser boot, block attach/mount, launcher edit,
deletion or whole-disk copy. Original chain/evidence preserved; tracked work only
in C: graphics checkout, existing `M wine` unstaged. No unrelated tests repeated.

## Actual bounded extraction and preserved initial refusal

Read installed qemu-img/qemu-io help (10.0.13), exact two-layer chain and normal
input locks; no force-share/unlock. No launcher/QEMU/open chain FD observed before
reads or final replay. Host D free1,860,803,162,112 bytes at feasibility check.
Chain: system.qcow264GiB -> exact Debian image-source base3GiB; canonical paths,
no symlink surprise. Before/after chain JSON and original file size/inode/device/
mtime/ctime identities unchanged. This is NOT full-content-hash attestation.

Fresh private `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/capacity-r1-20261023.preflight/`
retains help, argv/exits, chain JSON and ONLY34,304 raw virtual GPT bytes:
primary34x512 + backup33x512. Installed dd has no skip; backup used its accepted
--image-opts raw offset filter68719459840 over exact qcow2 input, count33.
Both dd operations exit0, outputs exact sizes; no filesystem/payload extraction.

Initial validator expected generic Linux filesystem type0fc63daf..., so refused
actual architecture-specific root type4f68bce3... with FAILED_PRESERVED. That
failed preflight remains immutable, SHA256
`c01f9f22f701d1e0fc348da4db2f553629310a4d1f1bdf6fe02e98003c6a6f59`.
Parent independently decoded the table and approved exact x86-64 root type fix,
not a broad allowlist. Corrected validator replayed RETAINED sectors only: PASS;
no second extraction/prep overwrite. Root PARTUUID below is from actual GPT and
matches historical boot PARTUUID; ext4/filesystem UUID remain historical serial
evidence, not newly read filesystem metadata.

## Verified actual layout (512-byte sectors)

Primary/backup signatures, both header CRCs and array CRCs, identical arrays,
reciprocal headers, inclusive usable bounds, populated GUID uniqueness, extents/
no overlaps all pass.128x128 GPT entries; unused entries do not duplicate GUID0.
Disk GUID7c10b6c9-9bb4-46dd-9744-9ce537598b22, usable2048..134217694.

| Partition | Start LBA | End LBA | Type GUID |
| --- | ---: | ---: | --- |
|14|2048|8191|21686148-6449-6e6f-744e-656564454649|
|15|8192|262143|c12a7328-f81f-11d2-ba4b-00a0c93ec93b|
|1 (root, physically last)|262144|134217694|4f68bce3-e8cd-4db1-96e7-fbcaf984b709|

All attributes0. Root GUID624cfa91-935d-4cc7-9a3f-f29b41cac2a0;14GUID
74a0a387-4f48-4802-b9e5-55cf7f5fadc4;15GUIDaa6be279-f5d5-4c22-b99d-4cf440ef3b5a.
At proposed80GiB, projected last usable167772126 permits33,554,432 additional
root sectors (+16GiB), keeping starts/other extents unchanged. Projection ONLY;
no GPT/rootfs growth performed. First larger writable boot remains expansion.

Raw primary SHA256 `0e9d0adfe3e2f5d8aa168062b3f7fa1a258d776e426376b150d7ba29b7612f9f`;
backup `15966b074969c8afd128357c8f277f6dc47a77106d791ae99714daed045e7f6e`.
No binary sectors published in Git.

## Code/tests and incomplete execution preparation

New narrow `scripts/g4_capacity.py` has read-only Stage A dispatch only; create/
inspect/launcher-diff/remote strings are future templates, NEVER executed. Fake
creation outcome gate preserves partial failures; no retry/delete. Focused:
`wsl -d Debian --exec sh -lc 'cd /mnt/c/Users/Owner/.codex/worktrees/chromium-graphics-plan/WineExplorer/scripts && python3 -B -m unittest test_g4_capacity -q'`
13 tests PASS/no skips after exact-type correction (initial12 also passed).
Includes generic/other-architecture root rejection, CRC/signature/backup/bounds/
overflow/nonfinite/boolean/overlap/GUID/non-last-root negatives, command bounds,
active owner/FD, existing/wrong output/missing tools/space/changed-chain refusal
and fake partial-create outcomes. Mocks are not mutation execution proof.

**NOT_READY_FOR_STAGE_B:** failed prep is not a runnable maintenance manifest.
Exact19 binary expected values/remote hash argv, proper Windows C:\ boot-command
conversion and complete execution inventory still need a separately reviewed
offline continuation. Current source labels its future manifest NOT_READY and
the boot string as a template. Full original system+backing content SHA256
measurement/unchanged pre-post gate remains MANDATORY before/after Stage B;
not measured this window. No permission to rerun preparation or create/boot
follows from the corrected replay. Return promptly for parent review/checkpoint.
