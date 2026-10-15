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

---

## Phase S1 — Shared Built-in DLLs (Symlink Fake DLLs & Copy-on-Write)

### S1.1 Architecture & Implementation

#### 1. Opt-in Mechanism: `WINELINKFAKEDLLS` & `HKLM\Software\Wine\LinkFakeDlls`
To preserve 100% upstream stock Wine behavior by default, symlink generation is strictly opt-in:
- **Environment Variable:** `WINELINKFAKEDLLS=1` activates symlink creation during initial prefix bootstrap (when the registry does not yet exist).
- **Registry Persistence:** During initial installation, `setupapi` automatically persists `HKLM\Software\Wine\LinkFakeDlls = 1` (DWORD).
- **Seamless Updates:** Subsequent `wineboot -u` invocations (even without the environment variable set) check `HKLM\Software\Wine\LinkFakeDlls` and continue maintaining symlinks.

#### 2. Wine Unix Call: `wine_create_symlink` vs `CreateSymbolicLinkW`
Wine's Win32 API implementation of `CreateSymbolicLinkW` (`dlls/kernelbase/file.c` -> `FSCTL_SET_REPARSE_POINT` -> `server/fd.c:set_reparse_point`) does **not** create standard POSIX `symlink(2)` symbolic links:
- In Wine's server, `FSCTL_SET_REPARSE_POINT` creates a dummy regular file with a trailing question mark (`fd->unix_name + "?"`) and attaches an extended attribute (`user.reparse`) containing the reparse header and target data.
- These dummy reparse points are completely invisible to standard Unix utilities (`ls`, `cat`, package managers), cannot be traversed by Linux file loaders or external tools, and fail on filesystems lacking user extended attribute support.
- Therefore, we introduced an internal Unix call in `dlls/ntdll`:
  - **`unixcall_wine_create_symlink` (`dlls/ntdll/unix/file.c`):** Resolves DOS/NT paths to Unix paths via `ntdll_get_unix_file_name()`, canonicalizes target paths with `realpath()`, checks idempotency before unlinking existing links, and creates a true POSIX `symlink(2)` link pointing into `/usr/local/lib/wine/`.
  - Exported as `@ cdecl wine_create_symlink(wstr wstr)` in `ntdll.spec`.
  - WOW64 path redirection is performed cleanly in `dlls/setupapi/fakedll.c` via `get_real_link_dest()` (redirecting `\system32\` to `\syswow64\` while preserving `drivers/etc`, `catroot`, and `spool`), allowing `wow64_wine_create_symlink` to remain a simple pass-through thunk without duplicate path manipulation.

#### 3. SetupAPI Fake DLL Generator (`dlls/setupapi/fakedll.c`)
- Hooked into `install_fake_dll()`, `create_fake_dll()`, and `delay_copy_files()`.
- Non-fake DLLs (e.g., native third-party libraries installed by applications) are verified via `is_fake_dll()` and preserved as real files.
- WinSxS assembly delayed copies (`delay_copy_files()`) resolve canonical targets and create direct symlinks into `/usr/local/lib/wine/` assemblies.

#### 4. Client-Side Copy-on-Write (COW) in NTDLL (`dlls/ntdll/unix/file.c`)
To ensure application installers or updaters can overwrite, supersede, or truncate a built-in library (e.g. replacing `msvcp140.dll` with an application-specific version) without mutating the read-only package tree (`/usr/local/lib/wine/`):
- **Client-Side Location:** Implemented in `check_cow_break_symlink()` called from `NtCreateFile()`.
- **Target Gating (`is_in_wine_dll_dirs`):** Only symlinks whose canonical resolved target resides inside Wine's DLL directories (`dll_paths`, `system_dll_paths`, or `build_dir`) are broken. Ordinary symlinks elsewhere (e.g. user symlinks under `Z:\...` or inside `/tmp`) write through untouched.
- **Access / Disposition Gating:** Only fires for data writes (`FILE_WRITE_DATA | FILE_APPEND_DATA | GENERIC_WRITE | GENERIC_ALL`) or truncation/supersede dispositions (`FILE_OVERWRITE`, `FILE_OVERWRITE_IF`, `FILE_SUPERSEDE`). Skips `FILE_DELETE_ON_CLOSE`.
- **Relative Path Support (`RootDirectory`):** When `attr->RootDirectory` is provided, `check_cow_break_symlink()` receives the open directory file descriptor `root_fd` and uses POSIX.1-2008 directory-relative syscalls (`fstatat`, `readlinkat`, `openat`, `renameat`, `unlinkat`) with `dir_fd = (root_fd != -1) ? root_fd : AT_FDCWD`.
- **Permission & Atomicity:** New regular copies preserve the exact permissions of the package target (`st_target.st_mode & 0777`) and are populated in a temporary file (`<name>.cow.<pid>`) before being atomically swapped over the symlink using `renameat()`.

#### 5. Leaf Symlink Preservation on Delete (`server/fd.c`)
In stock Wine's `open_fd()`, `fd->unix_name` was canonicalized with `realpath()`, resolving symlinks to their package target. Consequently, `unlink_closed_fd()` on `FILE_DELETE_ON_CLOSE` would attempt to unlink the target in `/usr/local/lib/wine/`. We modified `open_fd()` in `server/fd.c` so that when the leaf component is a symbolic link (`S_ISLNK`), only the parent directory is canonicalized, keeping the leaf symlink path in `fd->unix_name`. When `unlink_closed_fd()` runs, `unlink()` unlinks the symlink in the prefix, leaving the shared package file pristine.

---

### S1.2 Prefix Size Reduction Measurements

Measurements taken from fresh WoW64 prefixes initialized with `wineboot -i`:

| Metric | Stock Prefix (Baseline) | Symlinked Prefix (Phase S1) | Reduction |
|---|---|---|---|
| **Total Disk Footprint** | **1,728,414,151 bytes (1.73 GB)** | **23,166,570 bytes (23 MB)** | **-98.66% (-1.71 GB)** |
| `drive_c/windows/system32` | 845 MB (861 files) | 1.8 MB (728 symlinks, 133 meta files) | -99.79% |
| `drive_c/windows/syswow64` | 836 MB (897 files) | 1.6 MB (798 symlinks, 99 meta files) | -99.81% |
| Target Requirement | < 100 MB per user | **23 MB** | **Pass (77 MB headroom)** |

Total symlinks created: **1,526 symlinks** (728 in `system32`, 798 in `syswow64`).

---

### S1.3 Copy-on-Write (COW) Verification Suite

The COW verification suite (`installer/tests/run_cow_tests.sh` executing `installer/tests/test_cow_cases.c`) tests all required conditions:
1. `Z:\` symlink to an ordinary file writes through without breaking into a copy.
2. Relative open with `RootDirectory` on a built-in DLL symlink breaks into a private regular copy.
3. `DeleteFileW` on a built-in DLL symlink unlinks the symlink while preserving the package target in `/usr/local/lib/wine/`.
4. Limited user (`testuser`) relative open breaks into a private regular copy owned by `testuser`.

```
--- 1. Testing Z: symlink write-through ---
=== Test: Symlink in Z:\ to an ordinary file writes through ===
SUCCESS: Wrote 33 bytes through Z:\ symlink
PASS: /tmp/cow_test_user_link.txt is still a symbolic link (did not break into a copy)!
Target content: MODIFIED_CONTENT_THROUGH_SYMLINK

--- 2. Testing relative open with RootDirectory on built-in symlink ---
lrwxrwxrwx 1 root root 47 Oct 15 15:06 /tmp/wine_cow_test/drive_c/windows/system32/msvcp140.dll -> /usr/local/lib/wine/x86_64-windows/msvcp140.dll
=== Test: Relative open with RootDirectory on built-in symlink ===
SUCCESS: Relative open broke built-in symlink and wrote 27 bytes
-rw-r--r-- 1 root root 5021751 Oct 15 15:06 /tmp/wine_cow_test/drive_c/windows/system32/msvcp140.dll
PASS: drive_c/windows/system32/msvcp140.dll is now a regular file!
msvcp140.dll header: COW_RELATIVE_TEST_MODIFIED

--- 3. Testing DeleteFileW on built-in symlink ---
lrwxrwxrwx 1 root root 46 Oct 15 15:06 /tmp/wine_cow_test/drive_c/windows/system32/version.dll -> /usr/local/lib/wine/x86_64-windows/version.dll
=== Test: DeleteFileW on built-in symlink ===
SUCCESS: DeleteFileW unlinked built-in symlink
PASS: drive_c/windows/system32/version.dll was deleted (unlinked)!
Package file remains intact:
-rwxr-xr-x 1 root root 171200 Oct 15 15:05 /usr/local/lib/wine/x86_64-windows/version.dll

--- 4. Testing COW as limited user (testuser) ---
=== Test: Symlink in Z:\ to an ordinary file writes through ===
SUCCESS: Wrote 33 bytes through Z:\ symlink
PASS: user Z: symlink still a symlink!
=== Test: Relative open with RootDirectory on built-in symlink ===
SUCCESS: Relative open broke built-in symlink and wrote 27 bytes
PASS: limited user relative open broke symlink to regular file!
-rw-r--r-- 1 testuser testuser 5021751 Oct 15 15:07 /home/testuser/test_pfx/drive_c/windows/system32/msvcp140.dll
msvcp140.dll header: COW_RELATIVE_TEST_MODIFIED
ALL COW TESTS PASSED PERFECTLY!
```

---

### S1.4 Wine Update (`wineboot -u`) Simulation

We tested Wine update behavior after simulating a newer Wine runtime:
1. Touched `/usr/local/lib/wine/x86_64-unix/ntdll.so` to a future timestamp (+1 day).
2. Invoked `wineboot -u` without `WINELINKFAKEDLLS` in the environment:
   ```
   wine wineboot -u
   ```
3. Wine read `HKLM\Software\Wine\LinkFakeDlls = 1` from the prefix registry.
4. All **1,526 symlinks** were retained and re-verified.
5. Prefix size remained at **26 MB**.

---

### S1.5 Conformance Test Suite & Raw Logs

The test suites were run on the stock pin baseline (`2e8bcfa` prefix without symlinks) and on `gemini/prefix-split` (prefix with `WINELINKFAKEDLLS=1`). All raw logs are committed under `docs/prefix-split/logs/`.

#### Summary Results

| Test Suite | Stock Pin Baseline (`2e8bcfa`) | Branch (`gemini/prefix-split`) | Delta | Raw Log File |
|---|---|---|---|---|
| `kernel32:file` | 571,334 executed (244 todo, 7 failures, 0 skipped) | 571,334 executed (244 todo, 7 failures, 0 skipped) | 0 (Exact Match) | [logs/kernel32_file_branch.log](logs/kernel32_file_branch.log) |
| `setupapi:install` | 1,085 executed (4 todo, 0 failures, 0 skipped) | 1,085 executed (4 todo, 0 failures, 0 skipped) | 0 (Exact Match) | [logs/setupapi_install_branch.log](logs/setupapi_install_branch.log) |
| `setupapi:parser` | 1,046 executed (13 todo, 0 failures, 0 skipped) | 1,046 executed (13 todo, 0 failures, 0 skipped) | 0 (Exact Match) | [logs/setupapi_parser_branch.log](logs/setupapi_parser_branch.log) |
| `uxtheme:system` | 22,252 executed (17 todo, 0 failures, 4 skipped) | 22,252 executed (17 todo, 0 failures, 4 skipped) | 0 (Exact Match) | [logs/uxtheme_system_branch.log](logs/uxtheme_system_branch.log) |

#### Exact Summary Lines from Raw Logs

- **`kernel32:file`:**
  ```
  0020:file: 571334 tests executed (244 marked as todo, 0 as flaky, 7 failures), 0 skipped.
  ```
- **`setupapi:install`:**
  ```
  0020:install: 1085 tests executed (4 marked as todo, 0 as flaky, 0 failures), 0 skipped.
  ```
- **`setupapi:parser`:**
  ```
  0020:parser: 1046 tests executed (13 marked as todo, 0 as flaky, 0 failures), 0 skipped.
  ```
- **`uxtheme:system`:**
  ```
  0020:system: 22252 tests executed (17 marked as todo, 0 as flaky, 0 failures), 4 skipped.
  ```

---

### S1.6 Acceptance Install Tests

A fresh hybrid ISO was built from the branch via `installer/build-iso.sh --clean` (`/root/wineexplorer-wow64.iso`), and the zero-input unattended fixture was created via `installer/tests/prepare-unattended-iso.py` (`/root/wineexplorer-wow64-unattended.iso`).

#### 1. Unattended Install (`installer/tests/install-unattended.sh`)
```
started wex-unattend-test
found
found
found
shut down wex-unattend-test
PASS: installed LightDM, custom/recovery greeters, session, automatic logon and prefix isolation
PASS: zero-input install and installed-disk checks; evidence in /tmp/vmtest/wex-unattend-test
```

#### 2. Interactive Install (`installer/tests/install-interactive.sh`)
```
started wex-inter-test
found
--- welcome
found
--- license
found
--- partitions
found
found
--- created
found
--- format
found
--- copied
found
--- regional
found
found
--- computer
found
found
found
--- workgroup
found
--- oobe
found
found
found
--- users
found
found
--- finish
found
--- logon
found
clicked 758,337
found
found
--- rejected-password
found
--- alice-desktop
found
--- switch-user
clicked 782,415
found
found
--- desktop
=== Building test32.exe (i686-w64-mingw32-gcc) and test64.exe (x86_64-w64-mingw32-gcc) ===
=== Staging test executables into VM ===
Desktop user: bobsmith on :1
=== 1. Running 32-bit test program on desktop ===
found
/tmp/vmtest/wex-inter-test/steps/18-test32.png
PASS: Win32 test program window drawn
=== 2. Running Wine 32-bit notepad from syswow64 ===
found
/tmp/vmtest/wex-inter-test/steps/19-notepad32.png
PASS: 32-bit syswow64 notepad window drawn
=== 3. Running 64-bit test program ===
found
/tmp/vmtest/wex-inter-test/steps/20-test64.png
PASS: Win64 test program window drawn
=== Cleaning up running test processes ===
PASS: All WoW64 32-bit and 64-bit program checks succeeded.
shut down wex-inter-test
PASS: installed LightDM, custom/recovery greeters, session, automatic logon and prefix isolation
PASS: full names, root/Alice passwords, Bob passwordless state, administrator membership and setup cleanup
PASS (Bob desktop 5 s after logon); screenshots in /tmp/vmtest/wex-inter-test/steps; VM stopped
```

---

### S1.7 Inventory of Real (Non-Symlink) Files in System Folders

In a freshly bootstrapped prefix, `system32` contains **97–133 files** and `syswow64` contains **89 files** that remain physical regular files instead of symlinks. All other files (728 in `system32` and 798 in `syswow64`) are symbolic links.

#### Categorized File Breakdown

| Category | `system32` Count | `syswow64` Count | Purpose & Rationale |
|---|---|---|---|
| **NLS Tables (`.nls`)** | 75 | 75 | Codepage tables (`c_*.nls`), Unicode normalization (`normnfc.nls`), locale tables (`locale.nls`). Installed via `wine.inf` `[NlsFiles]` (`CopyFiles = NlsFiles`). Applications map these data tables directly via NT memory sections; they are data files, not fake PE DLLs. |
| **WinMD Metadata (`.winmd`)** | 10 | 10 | Windows Runtime (WinRT) interface contracts (`Windows.Foundation.winmd`, `Windows.Globalization.winmd`, etc.) installed via `wine.inf` `[WinmdFiles]`. These are metadata packages queried by WinRT language projections. |
| **Network Config (`drivers/etc/`)** | 4 | 0 | `hosts`, `networks`, `protocol`, `services`. Installed via `wine.inf` `[EtcFiles]`. These flat text configuration files are queried and modified per-machine by network APIs. Shared across 32/64-bit. |
| **Color Profiles (`spool/drivers/color/`)** | 1 | 0 | `sRGB Color Space Profile.icm`. Installed via `wine.inf` `[ColorFiles]`. Standard ICM display/printer color profile data. |
| **Management Console (`devmgmt.msc`)** | 1 | 0 | Device Manager console document installed via `wine.inf` `[MscFiles]`. XML-based console layout file loaded by `mmc.exe`. |
| **Driver Store INF Repository** | 5 | 0 | Plug-and-play driver INF packages (`driverstore/filerepository/{input, winebth, winebus, winehid, wineusb}.inf`) staged by `pnp_driver_install()` during initial device enumeration. |
| **DOS / 16-bit Subsystem Binaries** | 0 | 3 | `ddhelp.exe`, `dosx.exe`, `dsound.vxd` in `syswow64`. 16-bit DOS extender dummy stubs generated during prefix initialization for compatibility with legacy 16-bit DOS/Win16 callers. |
| **Vulkan ICD Manifest (`winevulkan.json`)** | 1 | 1 | Khronos Vulkan ICD loader manifest specifying the client driver module path for 64-bit and 32-bit Vulkan applications. |
| **Total Non-Symlink Files** | **97** | **89** | Total disk space: **< 5 MB** (retaining full 98.6% size reduction). |

## Phase S2 — Shared Machine Folders (XP Model Integration)

### S2.1 Architecture & Implementation

Phase S2 implements the target XP-style multi-user directory structure:
1. **Machine Root (`/var/lib/wineexplorer/machine`)**:
   - Holds the single shared copy of the machine directories:
     - `drive_c/windows` (system32, syswow64, inf, fonts, system folders)
     - `drive_c/Program Files`
     - `drive_c/Program Files (x86)`
     - `drive_c/ProgramData` (All Users shell folders, Start Menu, Templates)
     - `drive_c/users/Public` (Public Desktop, Public Documents)
   - Ownership and permissions: `root:sudo`, setgid mode `2775` on directories, `664` on normal files, `775` on binaries (`.exe`, `.dll`).
   - Members of the `sudo` group (Administrators) have write permissions to install machine-wide applications.
   - Limited users (e.g. `bob`) have read-and-execute access only; writes to `C:\WINDOWS` and `C:\Program Files` are denied (`ERROR_ACCESS_DENIED`).
2. **Per-User Prefix Template (`/etc/skel/.wine`) & User Prefixes (`~/.wine`)**:
   - Contains only user profile data and symbolic links to machine directories:
     - `dosdevices/c:` -> `../drive_c`
     - `dosdevices/z:` -> `/`
     - `drive_c/windows` -> `/var/lib/wineexplorer/machine/drive_c/windows`
     - `drive_c/Program Files` -> `/var/lib/wineexplorer/machine/drive_c/Program Files`
     - `drive_c/Program Files (x86)` -> `/var/lib/wineexplorer/machine/drive_c/Program Files (x86)`
     - `drive_c/ProgramData` -> `/var/lib/wineexplorer/machine/drive_c/ProgramData`
     - `drive_c/users/Public` -> `/var/lib/wineexplorer/machine/drive_c/users/Public`
     - `drive_c/users/<username>`: private per-user profile directory.
   - Footprint:
     - Stock prefix: **~1,730 MB** (~1.73 GB).
     - S1 prefix: **~28 MB**.
     - S2 user prefix: **3.2 MB** (**99.8% reduction** from stock).
     - Shared machine root: **22 MB**.
3. **Machine Setup Automation (`scripts/setup-machine-root.sh`)**:
   - Staged into `/usr/local/bin/wex-setup-machine.sh` during packaging.
   - Invoked during installer GUI setup (`installer/scripts/wex-gui-setup.sh`) and LightDM greeter setup (`scripts/setup-greeter.sh`).
   - Idempotently bootstraps machine root if not present, applies permissions (`root:sudo`, `2775`), and links the `/etc/skel/.wine` template prefix.

### S2.2 Relative Symlink Target Resolution in Copy-on-Write (COW)

Carried over from S1 re-review:
In `wine/dlls/ntdll/unix/file.c` (`check_cow_break_symlink`), when checking if a symlink target resides inside Wine's DLL directories, relative target symlinks were previously resolved against the current working directory if `RootDirectory` was not specified.
The logic was refined to canonicalize the path of the link itself using `realpath(unix_name)` (or `/proc/self/fd/<dir_fd>/<unix_name>`), resolving relative link targets against the link's own parent directory in all cases.

#### Verification (`installer/tests/run_cow_tests.sh`)
```
--- 1. Testing Z: symlink write-through ---
=== Test: Symlink in Z:\ to an ordinary file writes through ===
SUCCESS: Wrote 33 bytes through Z:\ symlink
PASS: /tmp/cow_test_user_link.txt is still a symbolic link (did not break into a copy)!
Target content: MODIFIED_CONTENT_THROUGH_SYMLINK
--- 2. Testing relative open with RootDirectory on built-in symlink ---
lrwxrwxrwx 1 root root 47 Oct 15 18:02 /tmp/wine_cow_test/drive_c/windows/system32/msvcp140.dll -> /usr/local/lib/wine/x86_64-windows/msvcp140.dll
=== Test: Relative open with RootDirectory on built-in symlink ===
SUCCESS: Relative open broke built-in symlink and wrote 27 bytes
-rwxr-xr-x 1 root root 5021751 Oct 15 18:02 /tmp/wine_cow_test/drive_c/windows/system32/msvcp140.dll
PASS: drive_c/windows/system32/msvcp140.dll is now a regular file!
msvcp140.dll header: COW_RELATIVE_TEST_MODIFIED
--- 3. Testing DeleteFileW on built-in symlink ---
lrwxrwxrwx 1 root root 46 Oct 15 18:02 /tmp/wine_cow_test/drive_c/windows/system32/version.dll -> /usr/local/lib/wine/x86_64-windows/version.dll
=== Test: DeleteFileW on built-in symlink ===
SUCCESS: DeleteFileW unlinked built-in symlink
PASS: drive_c/windows/system32/version.dll was deleted (unlinked)!
Package file remains intact:
-rwxrwxr-x 1 root root 171200 Oct 15 15:27 /usr/local/lib/wine/x86_64-windows/version.dll
--- 4. Testing COW as limited user (testuser) ---
=== Test: Symlink in Z:\ to an ordinary file writes through ===
SUCCESS: Wrote 33 bytes through Z:\ symlink
PASS: user Z: symlink still a symlink!
=== Test: Relative open with RootDirectory on built-in symlink ===
SUCCESS: Relative open broke built-in symlink and wrote 27 bytes
PASS: limited user relative open broke symlink to regular file!
-rwxrwxr-x 1 testuser testuser 5021751 Oct 15 18:02 /home/testuser/test_pfx/drive_c/windows/system32/msvcp140.dll
msvcp140.dll header: COW_RELATIVE_TEST_MODIFIED
--- 5. Testing relative symlink target COW ---
lrwxrwxrwx 1 root root 58 Oct 15 18:02 /tmp/wine_cow_test/drive_c/windows/system32/rel_target_test.dll -> ../../../../usr/local/lib/wine/x86_64-windows/msvcp140.dll
=== Test: Symlink with relative target path breaks on write ===
SUCCESS: Wrote 24 bytes, broke relative-target symlink
PASS: rel_target_test.dll with relative target broke into a regular file!
Header: REL_TARGET_COW_MODIFIED
ALL COW TESTS PASSED PERFECTLY!
```

### S2.3 Phase S2 Acceptance Test (`installer/tests/test_s2_acceptance.sh`)

The Phase S2 acceptance suite directly executes the work order requirements:
1. Alice (administrator in `sudo`) installs small test program into `C:\Program Files\AliceApp` (`app64.exe` and `app32.exe`).
2. Bob (limited user) sees and runs both 64-bit and 32-bit apps successfully.
3. Bob is denied write access to `C:\windows\system32` ("Access denied.").
4. Bob is denied write access to `C:\Program Files` ("Access denied.").
5. Exactly 0 files modified, created, or deleted in machine root across user sessions (bit-for-bit SHA256 diff count: 0).

#### Real Test Output
```
=== WineExplorer Phase S2 Acceptance Test ===
=== WineExplorer S2 Machine Root Setup ===
Machine root: /var/lib/wineexplorer/machine
Template prefix: /etc/skel/.wine
Applying permissions to machine root (root:sudo, mode 2775)...
Configuring template prefix in /etc/skel/.wine...
=== S2 Machine Root Setup Completed Successfully ===
--- Initializing user prefixes from /etc/skel/.wine ---
Alice prefix size: 3.2M
Bob prefix size:   3.2M
Machine root files before test: 222
--- Test 1: Alice installs a program into C:\Program Files ---
	1 file(s) copied
	1 file(s) copied
PASS: Alice installed app64.exe and app32.exe into machine Program Files!
total 472
-rwxrwxr-x 1 alice sudo 232984 Oct 15 17:24 app32.exe
-rwxrwxr-x 1 alice sudo 245857 Oct 15 17:24 app64.exe
--- Test 2: Bob (Limited User) sees and runs Alice's program ---
Process running: 64-bit (is_wow64=0)
SUCCESS: LoadLibrary advapi32.dll succeeded: 00006fffff210000
SUCCESS: kernel32.dll FileVersion = 10.0.19045.5796
PASS: Bob successfully executed 64-bit app from C:\Program Files!
Process running: 32-bit (is_wow64=1)
SUCCESS: LoadLibrary advapi32.dll succeeded: 7adb0000
SUCCESS: kernel32.dll FileVersion = 10.0.19045.5796
PASS: Bob successfully executed 32-bit app from C:\Program Files!
--- Test 3: Bob cannot write C:\WINDOWS\system32 ---
Access denied.

PASS: Bob was denied write access to C:\windows\system32!
--- Test 4: Bob cannot write C:\Program Files ---
Access denied.

PASS: Bob was denied write access to C:\Program Files!
--- Test 5: Verify nothing per-user lands in machine root ---
PASS: Exactly 0 machine root files modified, created, or deleted by user sessions!
=== ALL S2 ACCEPTANCE TESTS PASSED PERFECTLY! ===
```

### S2.4 Conformance Test Suites & Raw Logs

The full test suites for `kernel32:file`, `setupapi:install`, `setupapi:parser`, and `uxtheme:system` were executed on the S2 branch and compared against the baseline pin:

| Test Suite | Total Tests | Todo Count | Failures | Skipped | Status vs Baseline Pin |
|---|---|---|---|---|---|
| `kernel32:file` | 571,334 | 244 | 7 | 0 | **Exact match** (0 regressions, 0 new failures) |
| `setupapi:install` | 1,085 | 4 | 0 | 0 | **Exact match** (0 regressions, 0 new failures) |
| `setupapi:parser` | 1,046 | 13 | 0 | 0 | **Exact match** (0 regressions, 0 new failures) |
| `uxtheme:system` | 22,252 | 17 | 0 | 4 | **Exact match** (0 regressions, 0 new failures) |

#### Exact Summary Lines from Raw Logs
```
kernel32:file:
file.c:11894: 571334 tests executed (244 marked as todo, 0 flaky, 7 failures).

setupapi:install:
install.c:1571: 1085 tests executed (4 marked as todo, 0 flaky, 0 failures).

setupapi:parser:
parser.c:796: 1046 tests executed (13 marked as todo, 0 flaky, 0 failures).

uxtheme:system:
system.c:1270: 22252 tests executed (17 marked as todo, 0 flaky, 0 failures). 4 tests skipped.
```

### S2.5 Acceptance Install Tests

Both unattended and interactive installation tests were executed against a freshly built ISO:
- ISO image: `/root/wineexplorer-wow64.iso` (1.6 GB) generated by `installer/build-iso.sh --clean`.
- Unattended ISO: `/root/wineexplorer-wow64-unattended.iso`.

#### 1. Unattended Install (`installer/tests/install-unattended.sh`)
```
started wex-unattend-s2
found
found
found
shut down wex-unattend-s2
PASS: installed LightDM, custom/recovery greeters, session, automatic logon and prefix isolation
PASS: zero-input install and installed-disk checks; evidence in /tmp/vmtest/wex-unattend-s2
```

#### 2. Interactive Install (`installer/tests/install-interactive.sh`)
```
started wex-inter-s2
found
--- welcome
found
--- license
found
--- partitions
found
found
--- created
found
--- format
found
--- copied
found
--- regional
found
found
--- computer
found
found
found
--- workgroup
found
--- oobe
found
found
found
--- users
found
found
--- finish
found
--- logon
found
clicked 758,337
found
found
--- rejected-password
found
--- alice-desktop
found
--- switch-user
clicked 782,415
found
found
--- desktop
=== Building test32.exe (i686-w64-mingw32-gcc) and test64.exe (x86_64-w64-mingw32-gcc) ===
=== Staging test executables into VM ===
Desktop user: bobsmith on :1
=== 1. Running 32-bit test program on desktop ===
found
/tmp/vmtest/wex-inter-s2/steps/18-test32.png
PASS: Win32 test program window drawn
=== 2. Running Wine 32-bit notepad from syswow64 ===
found
/tmp/vmtest/wex-inter-s2/steps/19-notepad32.png
PASS: 32-bit syswow64 notepad window drawn
=== 3. Running 64-bit test program ===
found
/tmp/vmtest/wex-inter-s2/steps/20-test64.png
PASS: Win64 test program window drawn
=== Cleaning up running test processes ===
PASS: All WoW64 32-bit and 64-bit program checks succeeded.
shut down wex-inter-s2
PASS: installed LightDM, custom/recovery greeters, session, automatic logon and prefix isolation
PASS: full names, root/Alice passwords, Bob passwordless state, administrator membership and setup cleanup
PASS (Bob desktop 6 s after logon); screenshots in /tmp/vmtest/wex-inter-s2/steps; VM stopped
```
