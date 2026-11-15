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
| 2 | `ProPlusWW.msi` fails with 1603 after the 23 language packages installed | Custom action `CAInstallLicenses` calls `sppc.dll.SLInstallLicense`, a stub (63 of 67 exports of Wine's `sppc.dll` were stubs). | Fixed: `src/dlls/sppc`, a local license store that replaces the stock stub (`src/stock-replacements.txt`). Setup ends with return code 0; `WINWORD.EXE` and `EXCEL.EXE` are installed. |
| 3 | Word draws its start screen, then "Microsoft Office cannot verify the license for this product", and exits | Windows 10 mode. Word gives the licensing service a challenge (`SLSetAuthenticationData`, 288 bytes), consumes its right and reads the policy `*` and `SLGetAuthenticationResult`. Only the licensing service of Windows can make that answer; the store returns `E_NOTIMPL`. | Open. Not to be faked. Under test: Windows 7 mode, where Office 2016 installs and uses its own licensing service. |

| 4 | Windows 7 mode, this branch's Wine: `Office64WW.msi` fails with 1627 at `StartServices` | In Windows 7 mode setup installs Office's own licensing service (`osppsvc`, `Common Files\Microsoft Shared\OfficeSoftwareProtectionPlatform`, account `NT AUTHORITY\NetworkService`, 64-bit). It starts, reports running, then logs `0x80070005` and stops with exit code 5, without a failing file, registry or RPC call before it. The lab prefix is a plain user prefix: there the server gives services the user's token. Service-account tokens (SYSTEM, LocalService, NetworkService) exist only with the system role or session 0 (`docs/SYSTEM_TIER_WORK_ORDERS.md`), which the lab VM does not have. | Cause confirmed 2026-11-14: with the system tier set up in the lab VM (`sudo env WEX_SESSION0=1 bash scripts/setup-machine-root.sh` from the guest's copy of this branch) and the service's files and keys put into the machine root (`scripts/lab/office/start-ospp-session0.sh`, then `systemctl restart wex-session0.service` so the service manager reads the new entry), `osppsvc` starts in session 0 and stays running. Open: the setup itself has not run against the machine root yet (a per-machine install from a user's session, in Windows 7 mode). |

| 5 | Setup as the system account in session 0, Windows 7 mode (`scripts/lab/office/run-session0.sh`): `Office64WW.msi` now installs and the licensing service starts; `ProPlusWW.msi` fails with 1603 at `InstallFiles`, "Failed to extract cabinet ProPsWW2.CAB" | `CreateFileW` of `C:\windows\Fonts\SEGOEUISL.TTF` is refused (`c0000022`): the system account cannot create a file in the machine root's `windows\Fonts`. Cause: `setup-machine-root.sh` links open fonts into `windows\Fonts` under Windows names (`segoeui*.ttf` to Selawik, `calibri*`, `cambria*`; `src/resources/fonts/font-links.txt`), 13 links to root-owned files. Office ships the real fonts and overwrites those names; the open goes through the link and the target cannot be written. | Fix written, under test: in `server/fd.c` an overwrite (`O_CREAT` and `O_TRUNC` with write access) that fails with `EACCES` on a symbolic link removes the link and creates the file, as Windows replaces an existing file. It is a change to stock Wine, not yet committed to the fork: `scripts/lab/office/server-replace-link-patch.py` applies it to a tree. |

Session 0 runs, 2026-11-15 (`run-session0.sh`, `run-user.sh`):

* **The system account's Windows version is not kept.** `HKCU\Software\Wine\Version` set for session 0's account
  holds until session 0 restarts: `machine/user.reg` is `root:wexsys` mode 644, so the account cannot save its
  own hive. Set the version after the last restart and check it with `cmd /c ver` before every setup run. The
  first two session 0 setup runs were believed to be in Windows 7 mode and were in Windows 10 mode
  (`VersionNT = 603` in the setup log); what follows is corrected for that.
* **Windows 10 mode, setup as the system account, the font links set aside: return code 0.** The license store
  (`sppc`) took licenses and key in session 0; `WINWORD.EXE` and `EXCEL.EXE` are in the machine root. Office's own
  licensing service is not installed in this mode, so blocker 4's fix is not yet shown by a real setup run.
* **Word as a standard user** (`officeuser`, a new account, prefix from `/etc/skel/.wine` and
  `scripts/personalize-prefix.sh`): runs, full start screen with online templates.
  * "First things first": Accept does nothing and starts `DW20.EXE` each time; cause not looked at. The per-user
    value `HKCU\Software\Microsoft\Office\16.0\Common\General\ShownFirstRunOptin` = 1 skips the dialog.
  * With the user in Windows 7 mode and the install made in Windows 10 mode, Word is "Unlicensed Product" and asks
    for a product key; with the published KMS client key entered it waits on a progress bar. That is the mismatch
    (no `osppsvc` exists), not a finding about services.
* A service's control calls are forwarded from a user's session to session 0 (`SYSTEM_TIER_WORK_ORDERS.md`, T5.4).
  Whether a program in a user's session reaches a session 0 service's own RPC endpoint is not tested yet; Office's
  licensing client needs that.

Blocker 4 can be repeated in seconds: `scripts/lab/office/grab-ospp.sh` copies the service's files and registry keys
while setup runs (setup removes them when the service does not start), `start-ospp.sh` puts them back and starts
the service alone. Pitfalls met: `+relay` for a whole setup run wrote 6 GB (a `RelayFromInclude` value did not
limit it); a script that takes a line number from `grep` must stop when there is none (`sed -n "p"` prints
the whole log).

Checked 2026-11-14 for newer fixes: `origin/master` is 15 commits ahead of this branch's base (UWP XAML, test
harness, Debugging Mode, `wine` pinned to `1052fab3`); none touches services, the system role or tokens.

The license store (`src/dlls/sppc/sppc.c`):

* License files are kept as installed in `WINDOWS\spp\wine\{file id}.xrm-ms` (the id is made from the file's
  SHA-256), product keys and grace start times in `HKLM\Software\Wine\Licensing` (64-bit view).
* Values and id lists are read out of the stored files (`tm:infoStr`, `sl:policyInt`, `sl:policyStr`).
* A key is identified with `pidgenx.dll` and the key configuration among the installed licenses; the product is the
  activation id of the key's group.
* State: no key, not licensed (`SL_E_PKEY_NOT_INSTALLED`); from the product's first key on a grace period of 30
  days (`SL_I_OOB_GRACE_PERIOD`); after it, notification (`SL_E_GRACE_TIME_EXPIRED`). The start time stays when a
  key is removed. Nothing activates a product: that needs the licensing service to reach a KMS host or Microsoft.
* Error codes and the supported id-list queries follow Windows 11 (`scripts/lab/office/sppc-probe.c`, read-only,
  run on the host on 2026-11-14): an unknown product, license or application gives `SL_E_VALUE_NOT_FOUND`, an
  unknown key `0xc004f016`, an empty id list `SL_E_VALUE_NOT_FOUND` with the caller's variables untouched, a
  policy without a consumed right `SL_E_RIGHT_NOT_GRANTED`.
* What setup calls, in order: `SLInstallLicense` (7 issuance and key configuration files, then per product the
  `PL`, `OOB`, `PPDLIC`, `PHN` files), after each `SLGetSLIDList` (file to licenses) and `SLGetLicenseInformation`
  (`LicenseType`, `productSkuId`); `SLGetProductSkuInformation` (`BuildVersion`); `SLInstallProofOfPurchase`
  (`msft:rm/algorithm/pkey/2009`); `SLConsumeRight` (application `0ff1ce15-a989-479d-af46-f275c6370663`);
  `SLPersistApplicationPolicies`.
* Word calls `SLLoadApplicationPolicies`, `SLGetPolicyInformation` (`office-30CAC893-3CA4-494C-A5E9-A99141352216`,
  not in the licenses), then the challenge of blocker 3.
* A builtin DLL copied into a prefix is not what Wine loads when its own library directory has a module of that
  name: test builds go into `/usr/local/lib/wine/i386-windows`.

The guest runs this branch's Wine (`wine-11.21`, built in `~/build/wow64`, installed to `/usr/local`) since
2026-11-14 23:24 guest time; blockers 1 and 2 were first passed with 32-bit builds of the two DLLs in the clone's
older Wine.

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
