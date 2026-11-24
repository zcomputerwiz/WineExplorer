# Lab machines and disks

Where the project's virtual machines live, who owns each one, how long it is kept, and how disks are layered. Every
session that keeps a lab machine lists it here and documents how to drive it (its own lab doc, linked below). Tooling
for labs goes into `docs/TOOLING.md`. Measured 2026-11-24: wexbuild's root file system was 92 percent full (871 of
1007 GB), 525 GB of it the install chain's old test machines and 108 GB of old test images on `D:\WSL\wex-iso`.

## Layers

| Layer | What | Lifetime | Example |
|---|---|---|---|
| Base | an installed system at a known build, never changed after it is made (`chmod a-w` it) | as long as a lab or a run is built on it | `b1132a-bios` (a batch's install), `wex-b1122m-uefi` |
| Lab disk | a qcow2 overlay on a base, holding one lab's setup (Office installed, a domain joined, a lab build) | the lab's life; listed below with its owner | `sppc-r3` on `wex-b1122m-uefi` |
| Scratch | an overlay on a lab disk or base for one spike or test run | deleted when the run ends | `c2r-split` on `sppc-r3`, `spp-lab7` (deleted 2026-11-24) |

Rules:
- Make an overlay instead of a copy: `qemu-img create -f qcow2 -F qcow2 -b BASE/disk.qcow2 NEW/disk.qcow2`. Never boot or
  change a disk something else is built on: a change to a base corrupts every overlay on it, and nothing stops it but
  the base's file permissions.
- Keep chains at most three deep (base, lab disk, scratch). When a lab disk grows past about half of its base, or its
  base must go, flatten it: `qemu-img convert -O qcow2 LAB/disk.qcow2 LAB/flat.qcow2` and replace.
- A base belongs to one Wine and WineExplorer build. A lab that needs a newer build either installs it inside its own
  overlay (which then grows) or is rebuilt on a newer base; overlays cannot be moved to another base.
- Before deleting a disk, check that nothing is built on it (`vm-prune.sh` does this for the chain's runs; for lab
  disks, `qemu-img info -U --backing-chain` on every overlay of the machine).

## Retention of the install chain's runs

`scripts/lab/wex-batch.sh` and `installer/tests/run-chain.sh` leave one directory per run in
`VMTEST_DIR=/var/tmp/vmtest-gpt` (an installed disk of about 10 GB per firmware, its base overlay, one overlay per
stage, screenshots and journals) and one ISO per run in `D:\WSL\wex-iso` (3.3 GB). About 25 GB per batch.

`scripts/lab/vm-prune.sh` (root, in wexbuild) keeps the runs of the newest two batches (the last pushed and the one
running), runs named on a line of `/var/tmp/vmtest-gpt/.keep` or with `--keep PREFIX` (a failure still being looked
at), runs changed in the last two hours, runs a running qemu uses, and every disk a kept disk is built on. Of the
other runs it deletes only the disk images and the ISO; screenshots, journals, serial transcripts and logs stay, and a
file `PRUNED` in the run says when. Without `--apply` it only prints what it would delete.

When: after every pushed batch, by the orchestrator (who owns wexbuild). A failure that needs its disks later goes into
`.keep` first, with a comment saying why.

## Giving the space back to D:

Space freed inside a WSL distribution stays in its `ext4.vhdx` until the file is compacted: `vm-prune.sh --apply`
runs `fstrim`, which lets a sparse VHD shrink. To make a distribution's VHD sparse (once, by the owner; the distribution
must be stopped): `wsl --manage wexbuild --set-sparse true`. Without that, compact it while stopped with
`Optimize-VHD -Path D:\WSL\wexbuild\ext4.vhdx -Mode Full` (Hyper-V module) or `diskpart` (`select vdisk`, `compact
vdisk`).

## Inventory

Long-lived lab machines. Chain runs in `/var/tmp/vmtest-gpt` are covered by the retention above and not listed.

| Machine | Where | Base | Size | Owner | Kept until | How to drive it |
|---|---|---|---|---|---|---|
| `wex-b1122m-uefi` | wexbuild `/root/vmtest-spp` | (base: batch b1122m's UEFI install) | 9.2 GB | Session 0 | `sppc-r3` is gone | base only, never booted |
| `sppc-r3` | wexbuild `/root/vmtest-spp` | `wex-b1122m-uefi` | 7.3 GB | SPP service spec review | B76 is closed (the reviewer's request, 2026-11-24) | `D:\DeepThought\SPP` docs of the router work |
| `c2r-split` | wexbuild `/root/vmtest-spp` | `sppc-r3` | 9.5 GB | Session 0 (B76, Word under C2R; lab kernelbase, msxml3 and shell32 builds installed, originals in `/var/tmp/*-orig-*.dll`) | B76 is closed; restore the originals first | `scripts/vmtest.py` with `VMTEST_DIR=/root/vmtest-spp` (`put`, `sh`), `docs/KNOWN_BUGS.md` B76 |
| `spp-lab`, `spp-lab2` to `spp-lab6` (and `spp-lab2-eltest`, `spp-lab3-eltest`) | wexbuild `/root/vmtest-spp` | standalone (the eltest ones overlay lab2 and lab3) | 128 GB together | SPP and Office licensing work (sessions archived 2026-11-24) | to be decided by the SPP spec review | `D:\DeepThought\SPP\blackbox\wine-trace\LAB.md` (lab5 to lab7), `LOG.md` there |
| `wex-el` (+ `-eltest`, `-s0test`) | wexbuild `/root/vmtest-el` | standalone, overlays on it | 8.8 GB | elevation work (R8 and earlier); owner to confirm | to confirm | `docs/TOOLING.md`, "R8 lab chain" |
| `wex-s0` (+ `-adtest`, `-s0test`) | wexbuild `/root/vmtest-s0` | standalone, overlays on it | 9.5 GB | system tier (session 0) work; owner to confirm | to confirm | `docs/SYSTEM_TIER_RESUME.md` |
| `ra4` | wexbuild `/var/tmp/vmtest-gpt/ra4` | `b1118d-bios`, **which is gone: it cannot boot** | 6.8 GB | unknown (2026-11-21) | delete once its owner is found or nobody claims it | none |
| `wex-gpo` and its guests | wexgpo `/root/vmtest-gpo` | base disk, guests as overlays | 14 GB | GPO | the GPO lab's life | `docs/GPO_LAB.md` |
| VM B (ChromiumLab guest) | Debian distribution | qcow2 overlay | | Chromium lab | the lab's life | `docs/TOOLING.md`, "Lab disk hygiene (VM B and WSL)"; `D:\DeepThought\ChromiumLab` |
| UWP U1 guest | Debian distribution, `/home/debian/uwp-u1-*` | | | UWP agent | the UWP track's life | `docs/uwp/RESUME.md`, `docs/uwp/HANDOFF_NOTEPAD.md` |
| `Copilot-Win10-B` | VirtualBox on the Windows host | snapshots (`bb-office-cold` the standard one) | | SPP black-box oracle (session archived 2026-11-24) | while Windows comparisons may be needed | `D:\DeepThought\SPP\blackbox\LAB.md` |
| `SPP-Win10` | VirtualBox on the Windows host | | | **not ours: never touch** | | |

The `spp-lab*` disks are standalone copies of similar installs (9.8 to 34 GB each). Rebuilt as overlays on one base
they would take perhaps 60 to 90 GB less, but only when the SPP work needs them again; until then their owner decides
whether they are kept at all.
