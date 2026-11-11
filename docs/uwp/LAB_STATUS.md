<!--
SPDX-License-Identifier: GPL-2.0-or-later
Copyright 2026 WineExplorer contributors
-->
# U1 Lab Status Report

**Date**: 2026-11-11
**Status**: clean baseline and isolated R0B both-ABI build/version tests passed; guest running
**Scope**: Codex setup; the earlier Antigravity recovery report is retained below.

## Current Codex-owned lab

* Reliable entry: direct subprocess argv to `wsl.exe --distribution Debian
  --user root --exec ...`. Expected sentinel stdout was verified. The prior
  empty-output report was not a live access-denied verdict.
* The original D: disk is incomplete: `qemu-img compare` differs at offset
  13,421,772,800. It remains untouched and is not booted.
* New self-contained disk:
  `/var/tmp/wex-uwp-u1-images-20261111/standalone.qcow2`. A new conversion,
  read-only structural check and full virtual-content comparison all exited 0;
  comparison output was `Images are identical.` before the first boot. No
  shared backing file, forced locking bypass or foreign guest is used.
* Owner marker/control/run records:
  `/var/tmp/wex-uwp-u1-20261111`, owner `codex-uwp-u1-20261111`, mode 0700.
  `scripts/lab/uwp/owned-lab.py` verifies PID/start ticks/argv/disk before control.
* KVM, eight vCPUs, 16 GiB RAM; headless virtio VGA. SSH is loopback 2237 with
  a pinned ed25519 host key. QMP and serial log are private to this run.
  A, B, wexbuild and elevation fixtures were not accessed or changed.
* Guest: Debian x86_64, kernel `6.12.115+deb13.1-amd64`, user `debian` with
  noninteractive sudo. SSH readiness succeeded on the initial boot and after
  the final storage/boot-order correction.
* Dedicated empty 160 GiB disk `work.qcow2`, serial `WEX_U1_20261111`, was
  verified blank, formatted and mounted at
  `/home/debian/uwp-u1-20261111/build`; 149 GiB free after mounting. Root disk
  had only 9.5 GiB free and is not the Wine build destination.
* The clone inherited `/etc/wineexplorer`. Only inside this owned clone it was
  moved to `/home/debian/uwp-u1-20261111/inherited-system-role`, retaining it
  for inspection. `/etc/wineexplorer` is absent for the new baseline. No shared
  role configuration or account was reset.
* Build source pin: `c3f59e05a37134aee529a7d636af3ba67a9efc5c`, exported from
  the clean `codex/uwp-runtime` worktree. Source archive SHA-256 matches Windows
  and guest: `034464ACB2B36118C2DC951389570F79F7134D95C44D5791ACC5D609556439F4`.
* Build command: `configure --enable-archs=i386,x86_64`, initially `make -j4`.
  After checking host headroom (16 visible CPUs, >55 GiB available memory),
  only the owned make timeout/process group was interrupted, the guest was
  powered down normally, and it restarted with eight vCPUs/16 GiB. The same
  objects and source pin are retained, and compilation resumed with `make -j8`.
  The data mount persisted across restart using its exclusively owned UUID.
  Commands/logs/phase/PID live under `/home/debian/uwp-u1-20261111/`.
  Build completed with exit 0 and `Wine build complete.`. The baseline remains
  intact while candidates use separate source/build directories on the data disk.
  Configure reported no sound backend: this is the console/runtime baseline,
  not a full distribution/media or presentation acceptance build.

## Runtime baseline verification (2026-11-11)

`baseline-phase=PASSED`, `baseline-exit=0`. Installed Xvfb/xauth only inside
the owned guest and ran fresh owned prefixes with the exact baseline loader
and matching wineserver. Both system32 AMD64 and syswow64 x86 cmd smoke
markers/architecture checks passed. Both U1.0 unpackaged controls returned
`0x80040154`, matched the native oracle and exited 0. The accepted runner's
live cleanup and final summary report PASS. Packaged positive, missing-class
and missing-DLL activation remain explicitly NOT-RUN.

Evidence lives under `/home/debian/uwp-u1-20261111/`: `build.log`,
`baseline/run.log`, `baseline/uwp-results/`, phase/exit/coordinator logs.
Self-owned fixture archive from parent commit `ae21e619` has SHA-256
`95bceb2844d2a36d4efd7f80be72372fdba667b1c10ca104f4969014edbf1f8f`.
Built x64 ntdll SHA-256 is
`e8528476545e2c8ca94bb38d9b8a6a8f30c68f4dd0d8bf4192aa702589b7cb2d`;
x86 is `92ac53a0e7cfceafe0426c9d242c91e1703e047f7f773465b3a7823729496f89`.
This proves the scoped baseline controls, not package support or presentation.

R0B candidate source is the same clean base plus only reviewed
`kernelbase/version.c`, its `Makefile.in` and `kernel32/tests/version.c`.
Its fresh build is `/home/debian/uwp-u1-20261111/build/candidates/r0b-1/`,
also configured for both architectures. Build exit 0, Wine build complete;
kernel32 version tests passed 200 checks per ABI, zero failures/two skips.
`test-phase=PASSED`, `test-exit=0`, cleanup wait succeeded. Runtime committed
as isolated Wine `c7be1378`; the parent native harness is still under review.

During the Raven review the coordinator found the owned QEMU stopped; cause
was not established. Both self-contained disks passed fresh read-only
`qemu-img check --output=json` with zero errors before recovery. Preserved
`serial-before-recovery-2.log` and previous process identity; restarted only
this owned guest after headroom/port checks. Pinned-host-key readiness and
data mount succeeded again; retained candidate build already reported BUILT/0.
No shared guest or image was controlled, and no image lock was bypassed.

## Setup failures and recovery

1. QMP `device_add` on the Q35 root bus failed with
   `Bus 'pcie.0' does not support hotplugging`. The empty owned disk was
   retained, verified blank with `qemu-img map`, and attached at restart.
2. A first restart used the unsupported drive-level `serial` form and exited
   1; the initial exception omitted native stderr. The helper now retains
   stderr, and the disk uses a separate `-device virtio-blk-pci,...,serial=...`.
3. The new PCI device changed firmware disk enumeration; SSH banner attempts
   timed out. QMP screendump showed iPXE / `No bootable device`. Only this
   unbooted owned QEMU was quit via its private QMP endpoint. Giving the real
   root disk explicit `bootindex=1` fixed the boot; SSH was reverified.

Current state left behind: owned guest running, baseline and tested R0B candidate retained;
both original partial disk and setup diagnostics retained. Use the owned
controller's `status`/`stop`, never A/B wrappers or global process kills.
Headless boot/SSH is not graphics or UWP runtime proof.

## Historical Antigravity recovery report

---

## 1. Prior Job Summary

* **Job ID**: `9622abe3` (conversation `268719d2-82f9-4ac1-b74b-9aa67a08d69c`)
* **Failure Mode**: Hit print timeout after approximately 9 minutes of execution.
* **Completion Evidence**: No boot or SSH completion evidence exists. Provisioning was incomplete.

---

## 2. Disk State & Validity

* **Owned Disk Path**: `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/uwp-u1-lab/uwp-u1-disk.qcow2`
* **File Size**: `12768443672` bytes (matches parent observation).
* **Timestamps**:
  * Created: 2026-11-11 09:07:30 UTC-6
  * Last Modified: 2026-11-11 09:09:06 UTC-6
* **Verification Status**:
  * **Validity / completion is NOT established**.
  * **Do NOT reuse based on existence**.
  * **Distinction**: Header metadata is readable (magic bytes `51-46-49-FB` indicating QCOW2 v3, cluster bits 16 [64 KiB cluster], 80 GiB virtual size, no backing file). However, **image integrity is NOT checked** (no `qemu-img check` or repair was performed or permitted).

---

## 3. Helper Script Status

* `scripts/lab/uwp/g-u1.sh`: **Draft only**. Must NOT be run until ownership and host identity are verified.
* `scripts/lab/uwp/wait-ssh-u1.sh`: **Draft only**. Must NOT be run until ownership and host identity are verified.

---

## 4. Inspection Results & Observed Commands

All inspection commands were strictly read-only and bounded within a 15-second subprocess limit.

### A. Windows Host Process Check
* **Command**: `Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'qemu|convert|u1' }` and `Get-Process *qemu*`
* **Outcome**: Exit code 0. No active U1 `qemu-img convert`, `qemu`, or related processes running.

### B. Linux / WSL Environment Check
* **Commands**:
  * `wsl.exe -e sh -c "ps aux | grep -E 'qemu|convert'"`
  * `wsl.exe -d Debian -e echo "Debian OK"`
* **Outcome**: Subsystem returned empty output / non-responsive standard streams.
* **Disposition**: Per rule (*"Wsl/Service/E_ACCESSDENIED or timeout means stop that path, document and return; do not try another distro or repeated WSL workarounds"*), the WSL inspection path was immediately halted without retries, workarounds, or distro switching.

### C. Direct Header Metadata Inspection
* **Command**: .NET `System.IO.File::OpenRead` of the first 72 bytes.
* **Outcome**: Successful read of header bytes `51-46-49-FB-00-00-00-03-...`. Confirms basic file existence and readable QCOW2 v3 header, but does not verify logical or filesystem integrity.

---

## 5. State Left Behind & Constraints Preserved

* **No Operations Performed**:
  * No `qemu-img check` or repair.
  * No clone, boot, or SSH attempts.
  * No deletion or kill operations.
  * No edits to helper scripts or scratch scripts.
  * No shared guest or disk modifications.
  * No build, Wine runtime execution, tests, commits, staging, push, or subagent delegation.
* **Repository State**:
  * Main checkout foreign elevation edits are strictly preserved.
  * The only repository change introduced by this recovery job is this documentation file (`docs/uwp/LAB_STATUS.md`).
