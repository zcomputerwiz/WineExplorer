<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Restore disposable graphics guest capacity without deleting evidence

Parent plan after independently reviewed selected-stack R1, f41feae5. User
authorizes disposable D:-backed VMs. Guest free725,929,984 bytes is below1GiB;
no further browser/staging/profile may ignore that gate. This maintenance is
not graphics acceptance and must not execute Wine, Edge or a consumer probe.

## Scope and decisions

Use only D:/VirtualBox_VMs/WEX-G4-VirGL-20261022. Preserve system.qcow2,
its Debian image-source backing file, all profiles and every prior run.
No delete, compact, discard, downward commit, rebase, in-place resize or change
to another worker's disk. Main D: source checkout remains untouched. Tracked
work uses the C: graphics worktree; private artifacts remain D:.

Preferred new top layer: system-capacity-r1-20261023.qcow2,80GiB virtual,
explicit qcow2 backing format and exact absolute existing system.qcow2 backing
path. Never open that base writable after selecting the new layer. Do not use
an overlay if its path already exists; fail rather than overwrite/retry.
Preserve the complete chain and record rollback as selecting the old base ONLY
while everything is off, losing subsequent overlay-only changes. Do not switch
back casually after new tests. D: currently has~1.69TiB free; verify again.

IMPORTANT: recorded boot invokes GROWROOT/filesystem resize automatically.
The FIRST writable boot of the larger overlay is an expansion operation. Do
not label it metadata-only, disable unknown services speculatively, or assume
manual growpart must run. Historical root ext4/dev/vda1, vda14/vda15 and one
size do not prove partition starts/order/free trailing space.

## Stage A: offline implementation and layout proof only

Luna implements; parent reviews before ANY disk creation or VM boot.

1. Read installed qemu-img/qemu-io CLI help and exact backing-chain JSON. Verify
   all owned launcher/QEMU identities absent and no process opens the chain.
   Use normal exclusive image read locks; never force-share/force-unlock.
2. Extract ONLY bounded primary/backup GPT sectors read-only from the virtual
   disk into fresh D: metadata artifacts (qemu-img dd or documented equivalent;
   do not convert/copy the whole disk or attach an NBD/block device). Existing
   source disks are inputs only. Record commands, exits, hashes and image info.
3. Parse little-endian GPT with stdlib, verify protective MBR/header signatures,
   both header and partition-array CRCs, reciprocal primary/backup LBAs,
   sector size/capacity consistency, partition extents/unique GUIDs/no overlaps.
   Fail on ambiguous or unsupported tables. Retain raw GPT privately, never
   publish binary sectors or vendor files. Require root partition1 to be the
   physically last partition with expandable trailing space after enlargement;
   record all other starts/ends/types. Stop if root identity/layout not proved.
4. Write a narrow exact-path maintenance preparation (not a general VM manager):
   original full chain and file identities, host free space, exact new output,
   image creation/inspection commands, launcher change/diff/hash, boot/run/stop
   commands and bounded before/after remote checks. No shell-built destructive
   commands, globbed targets, credentials or arbitrary image selection.
5. Test parser with valid GPT and bad CRC/signature/bounds/overlap/backup/duplicate
   and non-last-root negatives. Test refusing existing outputs/wrong paths,
   changed base/chain, active owners, insufficient host space, missing tools,
   partial image-create failure and ambiguous guest layout. Fake subprocesses
   for mutation tests: do not create images or run a VM during offline tests.
6. Commit AND push code/tests/report and PREPARED_NOT_RUN private manifest.
   Parent independently reviews actual layout, code/tests and commands.

## Stage B: separately authorized maintenance execution

Only after parent review, create ONE new overlay, inspect its exact80GiB chain,
and point ONLY the owned private launcher at it. Preserve old launcher version
and record new helper hash; historical preflights stay immutable. No consumer
procedure can run while its helper inventory is stale. No new image download.

One bounded maintenance boot may automatically grow root; retain serial logs.
Use usual unique X sockets/owned identity/QMP/900s launcher/90s cleanup reserve,
raw UTC budget checks and key-only private SSH. Before remote commands, verify
the actual QEMU -drive points to the new overlay. No browser or Wine startup.
Capture lsblk JSON, findmnt root, sfdisk JSON/dump, filesystem type/UUID/block
counts, df bytes and service/cloud-init status. Require expected ext4/dev/vda1
and unchanged other partition starts/ends, root UUID/content markers, successful
growth and >=8GiB free. Retained installed Edge EXE,19 accepted core/tool binary
hashes and compositor hash must match. No payload download/reinstallation.

If automatic growth fails/requests repair or layout differs: preserve overlay,
logs and shut down; no guessed partition command, filesystem repair, retry or
manual resize. Return for parent review. Prove exact-identity graceful QMP,
QEMU/Xvfb/controller exit0 and no remaining owned processes. Confirm original
base/backing files unchanged by content hashes before/after, allowing metadata
access timestamps only. Report mutation precisely, never call this a G4 pass.

## Subsequent graphics work

Parent reviews maintenance evidence before a fresh browser preparation.
Selected-stack sample identifies own Wine message/IOCP/single-object waits,
not a cause. Existing browser logs show main/IO threads doing other work;
external network failures are expected under offline isolation. Next diagnostic
must target the local navigation/response-to-renderer boundary with a specific
hypothesis, not repeat generic white-window/stack controls or bypass security.
All actual Edge pixel/geometry/GPU/shader/tabbed/lifecycle gates remain open.
