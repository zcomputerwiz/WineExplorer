<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# Microsoft Office (2016, 2021, 2024) on WineExplorer

Goal: the official installers run, and Word and Excel start and work. This file records the lab, each blocker
found and its state. Installer media are local only (`D:\Projects\Apps`) and never enter the repository; nothing
of the media's DLLs is copied or disassembled. Interfaces Wine lacks are learned by calling the original DLL as a
black box and comparing output (the probes below), from ReactOS, or from prior art with a compatible licence
and no circumvention in it.

## Media

| Version | File | Kind |
|---|---|---|
| 2016 | `SW_DVD5_Office_Professional_Plus_2016_W32_English_MLF_X20-41353.ISO` | MSI setup, 32-bit, volume |
| 2021 | `ProPlus2021Retail.img` | Click-to-Run, 32- and 64-bit, retail |
| 2024 | `Office2024-LTSC-x86.zip` | Click-to-Run (Office Deployment Tool, `Standard2024Volume`), 32-bit |

## Lab (2026-11-14)

* VirtualBox VM `WineExplorer-Office`: a linked clone of `WineExplorer-VM` at its snapshot `pre-safemode-test`
  (the original is not changed). 4 GB, **2 CPUs**, SSH `wineuser@127.0.0.1` port 2223, key
  `D:\Projects\VirtualBox_VMs\vm_key` (`VM_PORT=2223 bash scripts/vm.sh ssh ...`).
  * VirtualBox runs on the Hyper-V backend here (WSL 2 is on). With 4 CPUs the guest kernel stalled at boot
    (RCU stalls, systemd blocked); with 2 it boots. The guest clock runs slow under load.
  * The clone inherits the snapshot's saved state: `VBoxManage discardstate` before `modifyvm`.
  * `D:\Projects\Apps` is a transient read-only shared folder: `sudo mount -t vboxsf -o ro,uid=1000,gid=1000 apps
    /mnt/apps` after every boot. Extracted media are in `~/office/media/`.
  * The guest's source is `~/src` (this branch through `git archive`, the pinned `wine` commit through
    `scripts/prepare-wine.sh`), its WoW64 build `~/build/wow64`. The Windows account used has no Python, so
    `scripts/vm.sh sync` does not run from it.
* `scripts/lab/office/` (guest side, copied to `~/office/lab`):
  * `run.sh NAME DISPLAY TIMEOUT EXE [ARGS]`: one program in the prefix `~/office/pfx-NAME` on an owned Xvfb.
  * `config-2016.xml`: unattended Office 2016 setup, Word and Excel only (`setup.exe /config ...`).
  * `build-one-dll.sh`: builds one single-source 32-bit builtin DLL against the build tree without configure.
  * `stdcall-args-probe.c`: how many argument bytes a stdcall export takes (no disassembly).
  * `pidgenx2-probe.c`: calls `PidGenX2` as Office setup does and prints the output blocks.
* Host RAM is the limit (32 GB, shared with another session's WSL VM): check free memory before starting a VM or
  a build. A WSL build of Wine with `-j10` needed about 6 GB.

## Office 2016 (MSI)

| # | Symptom | Cause | State |
|---|---|---|---|
| 1 | Setup ends at once, `InvalidCDKey`, return code 30030 | Setup loads `system32\pidgenx.dll` and calls `PidGenX2` to identify its embedded volume key. Wine had no `pidgenx.dll`. | Fixed: `src/dlls/pidgenx`. Output equals the original's for the medium's key except the random and time fields. Checked with a 32-bit build of the DLL in a test prefix on the clone's older Wine: setup passes the key and installs packages. Not yet checked in a full build of this branch. |
| 2 | `ProPlusWW.msi` fails with 1603 after the 23 language packages installed | Custom action `CAInstallLicenses` calls `sppc.dll.SLInstallLicense`, a stub (63 of 67 exports of Wine's `sppc.dll` are stubs). | Open. Needs a license store in `sppc.dll` (a project module replacing the stock stub through `src/stock-replacements.txt`), and a decision on what licensing status it reports: nothing here may fake an activation. |

Open questions for blocker 2:

* Which `SL*` functions setup and the programs call, in order.
* The first-run state of a volume install on Windows is a grace period until the organization's KMS host
  activates it. KMS and online activation are not implemented in Wine.

Notes:

* `PidGenX2( key, pkeyconfig path, mpc, oem id, info, pid, dpid3, dpid4 )`: `info` is `{ 0x14, 0, DWORD *groups,
  count, ? }`; the original returns `E_INVALIDARG` for a count of 0, accepts a key whose group is not listed, and
  returns `0x8a010101` for a bad key.
* A `pkey2009` key is 25 base-24 digits (`BCDFGHJKMPQRTVWXY2346789`), the position of its `N` being the first:
  bits 0-19 group, 20-49 serial, 50-102 secret, 103-112 checksum (CRC-32, polynomial `0x04c11db7`, initial value
  all ones, most significant bit first, inverted, over the 16 bytes with the checksum bits clear).
* The first finding was made on the clone's older Wine (2026-10-18) with the medium's own `pidgenx.dll` put into
  a test prefix; that prefix is a throwaway.

## Office 2021 and 2024 (Click-to-Run)

Not started.
