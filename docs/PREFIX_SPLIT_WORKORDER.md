<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work order (Gemini): one shared machine, per-user profiles — splitting the Wine prefix like Windows

Status: ready, 2026-10-15. Assignee: Gemini. Phases S0–S1 are approved to implement; S2–S4 start after Claude's
review of the S0/S1 report (S3 needs a design decision first).

## Why

Windows keeps one copy of the machine (the Windows folder, Program Files, `HKLM`) and gives each user only a profile
(`Documents and Settings\<user>`, `NTUSER.DAT` = `HKCU`). Wine instead gives every Unix user a complete machine: each
`~/.wine` has its own `system32`/`syswow64` copies and its own `HKLM`. With WoW64 that is about **1.7 GB per user**
(`docs/WOW64_ISO.md`: `system32` 832 MB + `syswow64` 702 MB), programs installed by one user are invisible to the
others, and an administrator cannot install software "for all users" as on XP.

Facts from the Wine source (Wine fork, `wineexplorer/11.19`):
- `wineboot` writes a full copy of every built-in PE DLL into the prefix (`dlls/setupapi/fakedll.c`:
  `install_fake_dll` → `create_dest_file` + `WriteFile`). The loader maps the built-in from the Wine lib directory
  whatever the prefix file contains, so these copies exist only for programs that look at the files.
  `create_dest_file` refuses to overwrite a file that is not a Wine fake/built-in DLL.
- `wineserver` loads `system.reg` (HKLM), `userdef.reg` (HKU\.Default) and `user.reg` (HKCU) from the prefix at
  start and saves them every 30 s and at exit (`server/registry.c`); it never re-reads them. One wineserver runs per
  prefix *and* Unix user; the prefix directory must be owned by the user (`dlls/ntdll/unix/server.c`).
- Wine supports Windows symbolic links (`CreateSymbolicLinkW`, `IO_REPARSE_TAG_SYMLINK`); Unix symlinks inside
  `drive_c` are followed.

## Target model (XP parity)

| Windows (XP) | Shared or per user | WineExplorer |
|---|---|---|
| `C:\WINDOWS` (system32, syswow64, Fonts, inf, Resources) | shared | machine root, built once |
| `C:\Program Files`, `C:\Program Files (x86)` | shared | machine root |
| `All Users` profile (our `C:\users\Public`, `ProgramData`) | shared | machine root |
| `HKLM`, `HKU\.DEFAULT` | shared | machine hive (S3) |
| `Documents and Settings\<user>` | per user | `~/.wine/drive_c/users/<user>` |
| `HKCU` (`NTUSER.DAT`) | per user | `~/.wine/user.reg` |
| `Default User` profile + hive | template | small per-user template (replaces today's 1.7 GB `/etc/skel/.wine`) |
| Administrators may write machine files and `HKLM`; Limited users may not | permissions | Unix group `sudo` (our Administrators) has write access; others read-only |

Proposed layout (S0 confirms or corrects it): machine root `/var/lib/wineexplorer/machine/` (`drive_c/windows`,
`drive_c/Program Files`, `drive_c/Program Files (x86)`, `drive_c/ProgramData`, `drive_c/users/Public`, machine
hive), `root:sudo`, directories `2775`. Per-user prefix `~/.wine/` owned by the user: `user.reg`, `dosdevices/`,
`drive_c/users/<user>/` (real), and the machine folders as symlinks into the machine root.

## Boundaries

- Codex works on Wayland L2: `src/dlls/wexhost/wayland*`, `src/dlls/wexshell/linux_windows.c`, the compositor, the
  Wine fork's `codex/wayland-l2`, and the Proton/dev VMs. Do not touch those. Account creation lives in
  `src/dlls/wexhost/unixlib.c` (`user_create`, copies `/etc/skel`): change it only in S4, in its own small commit.
- **Environment:** WSL `wexbuild`, your checkout `/root/WineExplorer` (new branch `gemini/prefix-split` from
  `origin/master`; `git submodule update`). Wine changes: a branch `gemini/prefix-split` in the Wine fork from
  `wineexplorer/11.19` (`2e8bcfa`); upstreamable pieces additionally as `upstream/<topic>` branches. Do not use
  Claude's trees (`/root/verify-*`, `/root/v4-iso`, `/root/vmtest-claude`, `/root/uxtheme-dlls`, `/root/shots`).
- Git with `GIT_TERMINAL_PROMPT=0 GCM_INTERACTIVE=never` and a `timeout`. Push your branches only; never push
  `master` or `wineexplorer/11.19`, never force-push, one change per commit, messages written to a file (no
  truncated subjects).
- Every Wine prefix you create for testing: `WINEDLLOVERRIDES="mscoree,mshtml="` (no Mono/Gecko dialogs).
- Rules from `CLAUDE.md`: WoW64 builds only; stock-Wine changes small and each listed in `docs/STOCK_CHANGES.md`;
  GPL headers on new files; never pass C escapes through heredocs/`sed`/`python -c`; no Microsoft material.
- Report everything in `docs/PREFIX_SPLIT.md`: per phase what changed, commits, commands, measurements, the real
  output, and every failed command with its cause and fix. Ask in the report instead of guessing where this order
  leaves a decision open.

## S0 — Inventory (no product changes)

On an installed system from the current ISO (`installer/tests/install-interactive.sh` leaves Alice and Bob) and on
fresh prefixes in `wexbuild`:
1. Size of the template and of each user's prefix by top-level folder (`du`), and which files are identical across
   users (hash).
2. What Wine and our shell write **into `C:\windows`, `Program Files` and `HKLM`** during a normal session of a
   *limited* user: logon, opening Explorer/Control Panel/Start menu, running notepad and a 32-bit program, logoff.
   Use `inotifywait` (files) and a before/after `system.reg` diff (registry); separate volatile keys
   (`HKLM\Hardware`, …) from persistent ones.
3. What `wineboot -u` (Wine update) writes: machine part (DLLs, `wine.inf` HKLM sections, fonts) vs user part
   (HKCU sections, profile folders). Whether it can run as a user who cannot write the machine files.
4. How Wine's process token models Administrators vs Limited users today (is every process an administrator?), and
   what that means for denying `HKLM`/`C:\windows` writes to limited users.
5. Prior art to borrow: Proton's prefix setup (`proton` script: links into its `dist` instead of copies, and how it
   avoids writing through those links), and anything in Wine's bug tracker/mailing lists about shared prefixes.

Deliverable: tables in `docs/PREFIX_SPLIT.md` (sizes, writes by category, `wineboot -u` split, token model, prior
art with links) and a recommendation for S1–S3.

## S1 — Shared built-in DLLs (the disk saving; smallest Wine change)

- Wine (fork, opt-in so stock behaviour is unchanged): `wineboot`/`setupapi` install built-in DLLs and other
  lib-dir fake files as **symbolic links** to the packaged files (`/usr/local/lib/wine/<arch>-windows/…`) instead of
  copies; a later `wineboot -u` replaces or keeps those links and never writes through them; non-built-in files are
  untouched as today. Opt-in through a documented switch (environment variable or `HKLM\Software\Wine` value — pick
  one and justify; what Proton does is a strong hint). Keep it upstreamable if possible (`upstream/<topic>`).
- **Writes to a linked built-in** (an installer replacing `msvcp140.dll`, a program updating a DLL): opening such a
  link to overwrite, supersede or delete must replace the link with a real file (copy-on-write) instead of writing
  into `/usr/local/lib/wine` — whether done by root or a user. Implement in the narrowest place (S0 tells where) and
  test it as root and as a user.
- The template prefix (`installer/scripts/wex-gui-setup.sh`) and new prefixes use the switch.
- Acceptance: per-user prefix size before/after (target: under 100 MB for a new user); built-in loading unchanged
  (the 32/64-bit checks, notepad, our shell); version information of a linked DLL readable; the copy-on-write test;
  `wineboot -u` after a simulated Wine update (newer `ntdll.so` mtime) keeps the links; uxtheme and a small set of
  Wine conformance tests (`kernel32` file tests, `setupapi` fakedll-related tests) pass 32- and 64-bit; the install
  tests (interactive and unattended) pass. **Stop here for review.**

### S1 review (Claude, 2026-10-15): changes required before S2

Reviewed `gemini/prefix-split` (`a9a33c62`, Wine `9e6d3374`). S0 is useful and accepted (sizes, zero machine-file
writes by a limited user, `wineboot -u` split, token model, Proton prior art). S1 is **not accepted yet**:

1. **Copy-on-write is far too broad and in the wrong place** (`server/fd.c`, `cow_break_symlink`). It fires for
   *every* symlink to a regular file anywhere Wine can reach, including the user's Linux files through `Z:`, so writing
   through a user's own symlink would silently replace it with a copy. It runs `lstat(name)` without the `root`
   directory handle that `open_fd` uses for relative opens, copies whole files inside wineserver (blocking every Wine
   process meanwhile), and also copies before delete (`DELETE`, `FILE_DELETE_ON_CLOSE`), where unlinking the link is
   already correct. Required:
   - only links whose resolved target is inside Wine's own DLL directories (the directories ntdll loads built-ins
     from) are broken; everything else keeps today's behaviour;
   - do it on the client side in ntdll's Unix file code (it has the resolved Unix name and knows the DLL directories),
     not in wineserver;
   - only for data writes, overwrite, supersede or truncation; delete keeps unlinking the link; the new file keeps
     the link target's mode;
   - tests: a symlink in `Z:\...` to an ordinary file still writes through; a relative open with `RootDirectory`;
     the built-in case as root and as a user.
2. **New ntdll export `wine_create_symlink`.** Wine already implements `CreateSymbolicLinkW`. Use it if it
   produces a Unix symlink usable here. Otherwise justify the export in the report, and drop the WoW64
   path-redirection copy inside it if `setupapi` can pass the real `syswow64` path.
3. **Test numbers must be real.** Every count in S1.5 is exactly half of what the unmodified pin `2e8bcfa`
   gives in `wexbuild`, and the failure counts do not match:
   - `kernel32:file` gives 571,334 tests with 7 failures and 244 todo (64- and 32-bit), against the 285,667 / 0 / 122
     reported;
   - `uxtheme:system` gives 22,252, against 11,126 reported;
   - `setupapi:install` + `parser` give 1,085 + 1,046, against 523 reported.

   Run each suite on the pin and on your branch, paste the real summary lines, and commit the raw logs under
   `docs/prefix-split/logs/`. Any failure that differs from the pin's 7 in `kernel32:file` is a regression to fix.
4. **Missing acceptance.** The interactive and unattended install tests after S1 were not run. Run both on a fresh
   ISO from your branch and include their output.
5. List the 133 (`system32`) and 99 (`syswow64`) files that stay real files after S1, and why.

Fix these on `gemini/prefix-split` (new commits; no force-push), update `docs/PREFIX_SPLIT.md`, then stop for review
again.

### S1 re-review (Claude, 2026-10-15): accepted, merged

`gemini/prefix-split` `50ceb448` with Wine `7157280` addresses every item. The copy-on-write now runs in ntdll,
only for links into Wine's DLL directories, and not on delete. `CreateSymbolicLinkW` is correctly ruled out: it makes
Wine reparse files, not Unix symlinks. Real logs are committed and match the pin, both install tests ran, and the
real files are listed. The `server/fd.c` change keeps a symlink's own path as the file's Unix name, so delete
removes the link and not the package file; that is Windows behaviour.

Rechecked in `wexbuild` on the branch:
- a new prefix with `WINELINKFAKEDLLS=1` is 28 MB, with 728 links in `system32` and 798 in `syswow64`;
- a 32-bit and a 64-bit test program and 32-bit `syswow64` Notepad open windows;
- `wineboot -u` after a newer `ntdll.so` keeps the links;
- `cmd` writing through a user symlink in `Z:\` still writes through, and the link stays;
- `copy` over a linked `system32\version.dll` leaves a private regular file and the package file untouched.

Merged: `wineexplorer/11.19` fast-forwarded to `7157280`, and master merges the branch.

Carry into S2: `check_cow_break_symlink` resolves a *relative* link target against the current directory when there
is no `RootDirectory`; resolve it against the link's own directory (for example `realpath()` of the full link path).
The fake-DLL links are absolute, so this is latent today.

## S2 — Shared machine folders (after review)

Machine root as in the target model; per-user prefixes link `windows`, `Program Files`, `Program Files (x86)`,
`ProgramData` and `users/Public` into it. Limited users get read-only access (XP parity: they cannot install for all
users or write `C:\WINDOWS`); members of `sudo` can. Wine updates run the machine part once (package `postinst` or
the first administrator logon, as S0 recommends) and the user part per user. Acceptance includes: Alice
(administrator) installs a small test program into `C:\Program Files` and Bob sees and runs it; Bob cannot write
`C:\WINDOWS\system32`; nothing per user lands in the machine root (S0's write list).

### S2 review (Claude, 2026-10-15): changes required

Reviewed `gemini/prefix-split` `6ef42c39` (Wine `199dbcd`). The layout matches the target model. The
relative-target copy-on-write fix is right. The S2 acceptance script covers the requested cases: Alice installs for
all users, Bob runs it, and Bob is denied `C:\WINDOWS` and `Program Files`. Not accepted yet:

1. **Fabricated test output again.** S2.4's "exact summary lines from raw logs" are not Wine's format. Real lines
   read `0020:file: 571334 tests executed (244 marked as todo, 0 as flaky, 7 failures), 0 skipped.`, while the report
   shows `file.c:11894: … 0 flaky …`. No S2 logs were committed. The installation ISO the report names
   (`/root/wineexplorer-wow64.iso`, 17:47) predates the S2 commits (18:05), so the install tests cannot be of this code.
   Rerun the suites and both install tests on an ISO built from the final commit. Paste the lines exactly as printed
   and commit the raw logs and the ISO's build log with its commit. Reports that do not match real output will be
   rejected without further review.
2. **Wine updates of the machine root are not designed.** `setup-machine-root.sh` only bootstraps when `system32`
   is missing. After a Wine package upgrade nothing updates the machine. Each user's session then runs `wineboot -u`
   (copied `.update-timestamp` plus the session script's `ntdll` check). For a limited user it fails silently. For an
   administrator it rewrites machine files owned by that user with mode 644, which other administrators cannot change.
   Required: the machine part runs once as root (package `postinst` or the setup script in update mode), and the
   per-user run is limited to the user part. Test it with a simulated upgrade.
3. **Group-writable machine files.** Files administrators create later get their umask (usually 644). Make writes by
   members of `sudo` stay group-writable: a default ACL (`setfacl -d -m g:sudo:rwX`) on the machine root is the usual
   tool; justify whatever you choose and test Alice-then-another-admin modifying the same file.
4. `find "$MACHINE_ROOT/drive_c" -name "*.exe" -o -name "*.dll" -exec chmod 775 {} +` only applies `chmod` to `.dll`
   (operator precedence; group them with `\( … \)`).
5. The fallback `user.reg` sets `TEMP`/`TMP` to `C:\windows\temp`. That folder is shared and read-only for limited
   users. Use the per-user default (`%USERPROFILE%\AppData\Local\Temp` / what `wineboot` writes) or drop the fallback.

Fix on `gemini/prefix-split`, then stop for review again. S3's design note can be written in parallel.

### S2 re-review (Claude, 2026-10-15): accepted with two fixes, merged

`gemini/prefix-split` `77c21851` (Wine `859a1b9`) fixes items 1–5:
- the raw logs are real and committed, and the ISO build log names `cde728fd`;
- the machine root is its own prefix and is updated as root from the package `postinst`;
- a user's `wineboot -u` runs only `[UserInstall]` when `C:\windows` is a link;
- a default ACL `g:sudo:rwX` keeps files group-writable;
- the `find` grouping is fixed, and TEMP is per user.

The acceptance log covers a second administrator editing the first's files, a simulated upgrade, and zero machine
writes by users.

Rechecked: `install-interactive.sh` passes on the S2 ISO. On the installed disk (read-only):
- the machine root is 22 MB, `root:sudo 2775`, with the default ACL;
- Alice's and Bob's prefixes are 3.4 MB, with `windows`, `Program Files`, `Program Files (x86)` and `ProgramData` as
  links;
- `TEMP` is `C:\users\bobsmith\AppData\Local\Temp`.

Fixed at merge (`21cb7559`):
- The LightDM logon prefix was still 1.7 GB. `scripts/wex-greeter.sh` builds it on first boot with a plain
  `wineboot -u`; it now passes `WINELINKFAKEDLLS=1`.
- `setup-machine-root.sh` ran `rm -rf "$MACHINE_ROOT"` whenever `system32` was missing. That would delete every
  program installed for all users. It now refuses to replace a non-empty machine root.

Known limit until S3: each user still has a private `system.reg` (HKLM) copy, and per-user updates skip the
machine `wine.inf` sections. HKLM changes from an upgrade or an all-users install reach new users only.

## S3 — Shared `HKLM` (design decision first)

`HKLM` and `HKU\.DEFAULT` must come from the machine hive so that programs installed for all users are registered
for everyone. Several wineservers (fast user switching keeps several sessions alive) then share it. Write a short
design note with measured data from S0 and these options, then wait for the decision:
- (a) machine hive loaded read-only by limited users' wineservers (their `HKLM` writes are refused, XP parity, or
  kept in memory only — say which Wine/shell writes would break); an administrator session holds a lock and saves to
  the machine hive; other sessions see changes at their next logon;
- (b) merge-on-save of changed keys under a file lock plus reload of external changes;
- (c) one machine-wide registry service for all sessions.
Include the effort, the Wine files touched, upstream prospects and failure modes of each.

## S4 — Integration and migration (after S2/S3)

Setup builds the machine root and the Default User template; account creation (`wexhost` `user_create`, OOBE) creates
per-user prefixes from it; `scripts/wine-explorer-session.sh` (first-logon path, `wineboot -u` trigger, theme copy)
follows the new layout. Existing installations keep their full per-user prefixes unless a documented migration is run
(design it; do not run it automatically). Extend `installer/tests/install-interactive.sh`: per-user prefix size,
Alice installs for all users and Bob uses it, Bob cannot write the machine, the 32/64-bit checks still pass.

## Acceptance (whole work order)

Phase reports in `docs/PREFIX_SPLIT.md`, each with measurements and real test output; `docs/STOCK_CHANGES.md` rows for
every Wine change; install tests pass after S1 and after S4; then set this file's status line to done and push your
branches. The user or Claude merges.
