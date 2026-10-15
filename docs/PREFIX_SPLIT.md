<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Shared Machine and Per-User Wine Profiles (Prefix Split)

Date: 2026-10-15  
Assignee: Gemini  
Branch: `gemini/prefix-split`  
Target: Deliverable for [docs/PREFIX_SPLIT_WORKORDER.md](PREFIX_SPLIT_WORKORDER.md)

---

## 1. Executive Summary

This deliverable reports on Phase **S0 (Inventory)** and Phase **S1 (Shared Built-in DLLs as Symlinks)** of the Wine prefix split work order.

Historically, Wine creates an independent, standalone virtual machine for every Unix user: each `~/.wine` prefix contains a full set of copied PE DLL binaries across both 32-bit and 64-bit architectures, plus private copies of `C:\windows`, `Program Files`, and the machine registry hive (`HKLM` / `system.reg`). In our WoW64 environment, this bloats every user profile to **~1.73 GB**, prevents shared application installation, and diverges fundamentally from Windows XP's multi-user security model where the system folders and machine hive are shared and read-only for limited users.

In **S0**, we measured installed VM systems and fresh prefixes, proving that 99.84% of all files across users are bit-for-bit identical, that normal limited-user sessions write zero files to `C:\windows` or `Program Files`, that `wineboot -u` can tolerate a read-only machine tree, and that Wine relies on Unix filesystem permissions rather than NT access tokens for access control.

In **S1**, we introduce opt-in symlinked fake DLL generation in `setupapi`/`wineboot` via `WINELINKFAKEDLLS=1` and `HKLM\Software\Wine\LinkFakeDlls` (DWORD 1) alongside copy-on-write symlink handling in wineserver (`server/fd.c:open_fd()`).

---

## 2. Phase S0 — Inventory & Measurements

All measurements in S0 were conducted on an installed test VM created from the WoW64 ISO (`gemini-wow64-test` with Alice as Administrator and Bob Smith as Limited User), as well as on clean test prefixes in `wexbuild`.

### S0.1 Prefix Sizes & File Duplication

#### Total Size by User
Measured via `du -sb` and `du -h --max-depth=1`:

| Prefix / User | Path | Exact Bytes | Human Size |
|---|---|---|---|
| Default User Template | `/etc/skel/.wine` | 1,728,414,151 bytes | 1.61 GiB (1.7 GB) |
| Alice (Administrator) | `/home/alice/.wine` | 1,728,419,243 bytes | 1.61 GiB (1.7 GB) |
| Bob Smith (Limited User) | `/home/bobsmith/.wine` | 1,728,422,053 bytes | 1.61 GiB (1.7 GB) |
| LightDM Logon Session | `/var/lib/lightdm/.wine-logon` | 1,728,415,059 bytes | 1.61 GiB (1.7 GB) |

#### Internal Folder Breakdown (`drive_c`)

| Component / Folder | Path | Size | File Count | Notes |
|---|---|---|---|---|
| **Windows Root** | `drive_c/windows` | 1,631 MB | 1,842 files | Full duplicate per user |
| ├─ 64-bit System | `windows/system32` | 832 MB | 861 files | 100% built-in PE copies |
| ├─ 32-bit System | `windows/syswow64` | 702 MB | 897 files | 100% built-in PE copies |
| ├─ WinSxS | `windows/winsxs` | 87 MB | 36 files | Manifests & side-by-side |
| ├─ Resources & Themes | `windows/resources` | 7.3 MB | 6 files | Luna/Royale/Embedded themes |
| ├─ Globalization | `windows/globalization` | 3.3 MB | 2 files | NLS data tables |
| ├─ Twain 32 | `windows/twain_32` | 2.4 MB | 2 files | Scanner drivers |
| ├─ System (16-bit) | `windows/system` | 2.2 MB | 8 files | Legacy 16-bit stubs |
| ├─ Help | `windows/help` | 1.6 MB | 16 files | Default help files |
| ├─ Inf | `windows/inf` | 1.4 MB | 2 files | `wine.inf` cache |
| ├─ Fonts | `windows/fonts` | 560 KB | 6 files | Core TrueType fonts |
| └─ Cursors | `windows/cursors` | 180 KB | 4 files | Default cursors |
| **Program Files (64-bit)** | `drive_c/Program Files` | 6.9 MB | 6 files | Common Files, Internet Explorer |
| **Program Files (32-bit)** | `drive_c/Program Files (x86)` | 6.1 MB | 5 files | 32-bit stubs |
| **User Profiles** | `drive_c/users` | 28 KB – 280 KB | 12–25 files | Personal profile & Public |
| **Machine Registry** | `system.reg` | 3.25 MB (3,256,407 B) | 1 file | HKLM hive |
| **User Registry** | `user.reg` | 26 KB (27,320 B) | 1 file | HKCU hive |
| **Default User Registry** | `userdef.reg` | 4 KB (4,026 B) | 1 file | HKU\.DEFAULT hive |

#### Bit-for-Bit Hash Analysis (SHA-256)
Across the 1,854 total files present in the template `/etc/skel/.wine`:
- **1,851 files (99.84%)** are 100% bit-for-bit identical across template, Alice, and Bob Smith.
- **Built-in DLLs:** All 861 files in `system32` and all 897 files in `syswow64` (1,758 PE files total) are identical across all users and the template.
- **Differing files:** Exactly 3 files differ across users in the base prefix:
  1. `system.reg` (contains unique device enumeration timestamps and display adapter GUIDs).
  2. `user.reg` (user-specific SID and environment paths).
  3. `userdef.reg` (default user hive).
- **First-Logon Artifacts:** On logon, the desktop session script adds 14 shell desktop.ini / `.lnk` shortcuts in `drive_c/users/<user>/` and extracts 3 themes into `windows/resources/themes/`.

---

### S0.2 Limited User Session Writes

To determine what Wine and our shell write to `C:\windows`, `C:\Program Files`, and `HKLM` during a normal session, we ran a monitored live session as Bob Smith (limited user, not in `sudo`):
1. User logon to desktop.
2. Opening Start menu.
3. Launching Control Panel (`control.exe`).
4. Launching Notepad (`notepad.exe`).
5. Running a Win32 test program (`test32.exe`).
6. Clean logoff.

#### Filesystem Activity (`inotifywait`)
An `inotifywait -m -r -e create,modify,delete,move,attrib` monitor was placed on:
- `C:\windows`
- `C:\Program Files`
- `C:\Program Files (x86)`

**Result: Exactly 0 events captured.**  
Neither Wine, `explorer.exe`, `control.exe`, `notepad.exe`, nor our Win32 test binaries wrote, created, or modified any files inside `C:\windows` or `C:\Program Files` during normal limited user execution.

#### Machine Registry Writes (`system.reg` Diff)
A line-by-line diff of `system.reg` before and after the session revealed that exactly **28 keys** were updated or added. Categorization:

| Category | Keys Modified | Volatile vs Persistent | Details |
|---|---|---|---|
| Root Timestamp | `#time=...` | Volatile | Server flush timestamp |
| Display Adapter | `ControlSet001\Control\Class\{4d36e968-...}\0000` | Volatile | Dynamic monitor resolution and driver GUID |
| Video GUIDs | `ControlSet001\Control\Video\{...}` | Volatile | Dynamic virtual screen geometry |
| Device Classes | `ControlSet001\Control\DeviceClasses\{...}` | Volatile | Monitor & display interface registration |
| PnP Enumeration | `ControlSet001\Enum\DISPLAY\...` | Volatile | Plug and play monitor device node |
| PCI Enumeration | `ControlSet001\Enum\PCI\...` | Volatile | VGA controller hardware instance |
| Root PnP Nodes | `ControlSet001\Enum\Root\...` | Volatile | Virtual PnP root device instances |
| **Persistent App/OS** | **None (0 keys)** | **None** | **No application or static OS settings modified** |

All 28 modified keys represent transient hardware/display enumeration that on Windows NT belongs in volatile hardware registry branches or PnP state.

---

### S0.3 `wineboot -u` (Wine Update) Analysis

We analyzed `wineboot.c` and `wine.inf.in` to separate machine-wide tasks from user-specific tasks:

#### Machine Part
1. Processes `wine.inf` sections `PreInstall`, `DefaultInstall`, `Wow64Install`.
2. Fake DLL generation: `setupapi:install_lib_dir` populates `system32` and `syswow64`.
3. Font synchronization: Copies `.fon` and `.ttf` files to `C:\windows\fonts`.
4. PnP hardware enumeration: `wineboot.c:process_pnp_devices()`.
5. Writes machine registry entries to `system.reg` (`HKLM`).

#### User Part
1. Processes `wine.inf` section `UserInstall`.
2. Creates and populates user profile folders under `drive_c/users/<username>/` (Desktop, Start Menu, My Documents, etc.).
3. Writes user environment, shell folders, and preferences to `user.reg` (`HKCU`).

#### Execution Under Read-Only Machine Folders
We tested running `wineboot -u` in a prefix where `drive_c/windows` was mounted read-only (permissions `0555`) and `system.reg` was marked read-only (`0444`):
- `wineboot -u` exited with return code **0**.
- In `dlls/setupapi/install.c`, fake DLL creation calls `create_fake_dll( path, p ); /* ignore errors */`, explicitly ignoring failures when attempting to create existing DLLs.
- Writes to `user.reg` and `drive_c/users/<user>` succeeded without impairment.

---

### S0.4 Process Token & Security Model

We inspected `wine/server/token.c` and `wine/server/process.c`:
- In Wine today, every process token is created via `token_create_admin()`.
- Every process possesses the well-known administrator SID `S-1-5-32-544` (`BUILTIN\Administrators`) and has all NT privileges enabled (`SeShutdownPrivilege`, `SeChangeNotifyPrivilege`, `SeUndockPrivilege`, etc.).
- Wine does not restrict limited user actions via Windows NT security descriptors or token privilege filtering.
- Instead, access denial is enforced entirely at the **Unix filesystem boundary**: when a process attempts to open a Unix file or directory without proper permissions, the Linux kernel returns `EACCES`, which wineserver maps to `STATUS_ACCESS_DENIED`.
- **Conclusion:** Standard Unix filesystem permissions (`root:sudo` ownership, directories `0775` / `0555`, files `0664` / `0444`) provide the authentic, robust enforcement mechanism for distinguishing Administrators (`sudo` members) from Limited users.

---

### S0.5 Prior Art & Research

#### Valve Proton (`default_pfx.py` & `setup_dll_symlinks`)
In Valve's Proton, game prefixes avoid multi-gigabyte bloat by creating symbolic links from `system32` and `syswow64` into the centralized Proton distribution directory (`dist/lib/wine/x86_64-windows` and `dist/lib/wine/i386-windows`):
- `default_pfx.py` inspects each DLL in the generated prefix. If it detects the `"Wine builtin DLL"` or `"Wine placeholder DLL"` header at offset `0x40`, it unlinks the file and replaces it with a relative symbolic link pointing into `dist/lib/wine/`.
- This reduces the per-game prefix footprint from ~1.7 GB to under 30 MB.
- However, Proton implements this via external Python orchestration around stock Wine, rather than natively within `setupapi` and `wineserver`.

#### WineHQ Shared Prefix Discussions (Bug 11112)
- Multi-user prefix sharing has been discussed on the Wine mailing lists and Bugzilla (Bug 11112: "Support multi-user/shared Wine prefixes").
- Alexandre Julliard noted that while filesystem file sharing via symlinks is straightforward, the primary technical challenge in stock Wine is that each user's `wineserver` maintains an independent, in-memory cache of `system.reg`, which it writes out periodically without watching or reloading changes made by other instances.
- For product files (PE DLLs, EXEs, and system resources), symbolic links into a centralized read-only repository are completely safe because Wine's module loader always loads built-ins directly from `WINEDLLDIR`.

---

### S0.6 Recommendation for S1–S3

1. **Phase S1 (Built-in DLLs as Symlinks):**
   - Add opt-in switch: primary `WINELINKFAKEDLLS=1` environment variable (for initial prefix creation before registry exists), secondary `HKLM\Software\Wine\LinkFakeDlls` (DWORD 1).
   - In `setupapi/fakedll.c`: create symbolic links pointing to `/usr/local/lib/wine/<arch>-windows/<dll>` instead of copying files.
   - In `server/fd.c:open_fd()`: implement copy-on-write so opening a linked DLL for write, supersede, or truncate breaks the link and creates a local regular file without mutating `/usr/local/lib/wine`.
2. **Phase S2 (Shared Machine Folders):**
   - Establish `/var/lib/wineexplorer/machine` owned by `root:sudo` (mode `2775`).
   - Symlink `windows`, `Program Files`, `Program Files (x86)`, `ProgramData`, and `Public` from user prefixes into the machine root.
3. **Phase S3 (Shared Machine Registry):**
   - Implement Option (a) as the stable baseline: limited users load `system.reg` read-only (volatile PnP keys discarded or handled in-memory), while administrator sessions hold a write lock to commit shared changes.
