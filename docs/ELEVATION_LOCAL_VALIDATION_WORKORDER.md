<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work order: local validation of the elevation boundary (Windows host, WSL, installed system)

Date: 2026-10-23. Status: ready. For a local Claude session on the user's Windows machine with WSL (Debian) and an
Openbox desktop. Results go back into this file ("Results") on a branch, through a pull request.

## Why

The elevation boundary work (`ELEVATION_BOUNDARY_PLAN.md`, `ELEVATION_SYSTEM_ROLE_WORKORDER.md`) was built and tested
in a cloud container that has no installed WineExplorer system, no desktop session and no Windows. Three things
could not be checked there, and the launcher (`wex-sysrun`) stays disabled by default until they are:

1. Native Windows values for the SYSTEM, LocalService and NetworkService tokens (privileges with their states,
   groups with attributes, integrity), which the Wine side currently uses as provisional lists.
2. The S2, S3, S4 and WoW64 acceptance tests after the `wexsys` migration. They were only syntax-checked.
3. A real desktop session with the launcher enabled: does anything a user does every day break.

## Rules

- Sources: `zcomputerwiz/WineExplorer` and `zcomputerwiz/wine`, branch `gemini/token-security-b` of each, at or after
  WineExplorer `0a9f83bb` and Wine `cc8a92f6`. Build Wine as WoW64 (`--enable-archs=i386,x86_64`), as `CLAUDE.md`
  requires. Work on a new branch of your own; never push to `gemini/*`, `codex/*` or `master`.
- Read `CLAUDE.md`, `docs/HANDOFF.md`, `docs/CODEX_HANDOFF.md` (how this machine's WSL, build VM and test VMs are
  used; follow its "do not enter" rules about other agents' WSL distributions), and the two elevation work orders
  above, before starting.
- Never bypass or weaken UAC on the Windows host. Steps that need an administrator prompt are done by the user;
  ask them and wait.
- Proprietary Microsoft material never enters the repository (`CLAUDE.md`). Measured values (SIDs, privilege names,
  attribute flags, status codes) are facts and may be recorded; no copied binaries, screenshots of Windows, or
  Windows source.
- The launcher is installed only for this validation (`WEX_SYSRUN_ENABLE=1`). Do not change its default.
- The setgid launcher must live on a native Linux filesystem (ext4 inside WSL or the test VM), not on `/mnt/c`
  (drvfs ignores setgid). Record `uname -r`, `/proc/sys/kernel/yama/ptrace_scope` and
  `/proc/sys/fs/suid_dumpable` for every Linux environment you test in.
- Commit after each working step; push your branch after each commit. Open one pull request at the end against
  `gemini/token-security-b` with this file's Results filled in.

## Part 1: native token measurements (Windows host, administrator prompt by the user)

Make a scratch directory `C:\wex-tokens` that Everyone may write (`icacls C:\wex-tokens /grant *S-1-1-0:(OI)(CI)M`).
Then have the user, in an elevated PowerShell, create and run three one-shot scheduled tasks that each run
`cmd /c whoami /all > C:\wex-tokens\<name>.txt`:

| Task | Run as (`schtasks /ru`) | Output |
| :--- | :--- | :--- |
| `wex-system` | `SYSTEM` | `system.txt` |
| `wex-localservice` | `"NT AUTHORITY\LOCALSERVICE"` | `localservice.txt` |
| `wex-networkservice` | `"NT AUTHORITY\NETWORKSERVICE"` | `networkservice.txt` |

(`schtasks /create /tn <task> /ru <account> /sc once /st 00:00 /f /tr "cmd /c whoami /all > C:\wex-tokens\<file>"`,
then `schtasks /run /tn <task>`, then `schtasks /delete /tn <task> /f`.) Also record
`[Environment]::OSVersion.Version` and the build number. Delete the three tasks and the directory afterwards.

Record in "Results": for each account, the user SID, every group with its attributes, every privilege with its
state, and the integrity level. Then compare with the provisional lists in `docs/TOKEN_SECURITY_REFERENCES.md`
(section "SYSTEM, LocalService and NetworkService tokens (C3, 2026-10-22): not measured") and Wine's
`server/token.c` (`token_create_service`) and list every difference. Do not change the Wine code in this part; list the differences.

Optional, only if the user agrees and it takes no extra admin steps: whether a non-elevated process can raise a
duplicate of its own token's integrity level (`SetTokenInformation(TokenIntegrityLevel)` to High), and the error it
gets. The Wine side assumes `ERROR_PRIVILEGE_NOT_HELD` (1314).

## Part 2: install and acceptance tests (installed system: test VM or WSL, per CODEX_HANDOFF)

1. Build Wine and WineExplorer from the sources above the way this machine normally does, and install a fresh
   system (the interactive or unattended installer test, whichever `CODEX_HANDOFF.md` names as current).
2. Run `setup-machine-root.sh` (a fresh setup) and also `--update` on a copy of a pre-migration `root:sudo`
   machine root if one exists. Record ownership and modes before and after.
3. Run, as their scripts require (root-prepared, user-run):
   - `scripts/test_b3_machine_root.sh`
   - `installer/tests/test_s2_acceptance.sh`, `test_s3_acceptance.sh`, `test_s4_acceptance.sh`
   - `installer/tests/check-wow64.sh`
   - with `WEX_SYSRUN_ENABLE=1`: `scripts/test_wex_sysrun.sh` and `scripts/test_sysrun_server.sh`
   - `scripts/check-token-security.py --all` as an unprivileged `sudo` member, from a directory that user can write
     (see the C1 Results in the system role work order for the test-exe ownership caveat), helper unset; then
     `--focused` with a helper configured, as a `sudo` member and as a standard user.
   Every failure: quote it verbatim, find the cause, and say whether it is the test's assumption (fix the test,
   say how) or a product bug (do not fix; describe it with a file:line and a proposed patch).
   The `WAITING` lines (assertions that need the broker) are expected; list them.

## Part 3: desktop session with the launcher (Openbox, then the default session)

With the launcher enabled and a helper configured, log in as a `sudo` member and as a standard user and do, for
each, in the WineExplorer desktop:

- start Explorer, browse `C:\`, open `C:\Windows\System32` and `C:\Program Files`; try to create a folder in
  `C:\Program Files` (expected: access denied, no crash);
- open `regedit`, read `HKLM\Software`, try to create a key there (expected: denied), create one under `HKCU`;
- open `notepad`, save into the user's Documents; open `taskmgr` and check that `services.exe`, `winedevice.exe`
  and the service processes are listed and cannot be ended or have their priority changed by the user;
- run a 32-bit program (any 32-bit test program from `installer/tests/test-program.c` built with the i686 MinGW
  compiler);
- run `sc query`, `sc qc RpcSs`, `sc create wexprobe binPath= C:\windows\system32\cmd.exe` (expected: access
  denied);
- log out and in again; check `wineserver` processes: `ps -o pid,uid,gid,egid,sgid,args -C wineserver` (expected:
  egid the user's, sgid `wexsys`) and `ls -l /var/lib/wineexplorer/machine/system.reg` (still `root:wexsys`).

Record each step as pass/fail with the exact error text, and note anything slow (more than a few seconds) or noisy
(new `err:` lines compared with a session without the launcher). Keep logs under `build/elevation-validation/`
(not committed if large; summarise them in "Results").

## Report

Fill "Results" below with: the environment table (Windows build, WSL/VM kernel, ptrace_scope, suid_dumpable,
Wine and WineExplorer commits), the Part 1 tables and difference list, the Part 2 results per test, the Part 3
step list, and every product bug found with file:line. Then open the pull request and tell the user its link.

## Results

### Environment

| Item | Value |
| :--- | :--- |
| Windows host | 10.0.26300.0 (Windows 11 Pro) |
| WSL kernel (Debian) | 6.18.33.2-microsoft-standard-WSL2 |
| `yama/ptrace_scope` / `suid_dumpable` (Debian WSL) | 1 / 0 |
| WineExplorer | branch `claude/elev-local-validation` from `9ae0203c` |
| Wine (`gemini/token-security-b`) | `cc8a92f6` |

Parts 2 and 3: not run yet (see "Status").

### Part 1: native tokens (measured 2026-10-22, elevated by the user)

Method: one-shot scheduled tasks (`schtasks /ru`) running `whoami /all`. **Caveat:** these are Task Scheduler
tokens, not Service Control Manager service tokens. Task-specific groups (`NT SERVICE\Schedule`, `NT TASK\<task>`)
and the absence of SeImpersonate on LocalService/NetworkService may differ for a real service (SCM is documented to
add SeImpersonate); that was not measured. Raw output: `build/elevation-validation/tokens/*.txt` (not committed).
`whoami` does not print the primary group, owner or default DACL.

Integrity: **System Mandatory Level (S-1-16-16384)** for all three. Matches Wine (`system_label_sid`).

**Groups** (all `Mandatory group, Enabled by default, Enabled group` unless noted):

| Group | SYSTEM S-1-5-18 | LocalService S-1-5-19 | NetworkService S-1-5-20 |
| :--- | :---: | :---: | :---: |
| Everyone S-1-1-0 | yes | yes | yes |
| BUILTIN\Users S-1-5-32-545 | yes | yes | yes |
| NT AUTHORITY\SERVICE S-1-5-6 | yes | yes | yes |
| CONSOLE LOGON S-1-2-1 | yes | yes | yes |
| Authenticated Users S-1-5-11 | yes | yes | yes |
| This Organization S-1-5-15 | yes | yes | yes |
| LOCAL S-1-2-0 | yes | yes | yes |
| BUILTIN\Administrators S-1-5-32-544 | yes, not mandatory, Group owner | no | no |
| Per-task SID (not mandatory; owner) | `NT SERVICE\Schedule` S-1-5-80-... | `NT TASK\wex-localservice` S-1-5-87-... | `NT TASK\wex-networkservice` S-1-5-87-... |
| Unknown `S-1-5-32-<8 subauthorities>` (mandatory) | no | 8 of them | no |

**Privileges** (E = enabled, d = disabled; "-" = absent):

| Privilege | SYSTEM native | SYSTEM Wine | LocalService native | LS Wine | NetworkService native | NS Wine |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| SeAssignPrimaryToken | d | d | d | d | d | d |
| SeLockMemory | E | E | - | - | - | - |
| SeIncreaseQuota | d | d | - | - | d | d |
| SeTcb | E | E | - | - | - | - |
| SeSecurity | d | d | - | - | - | - |
| SeTakeOwnership | d | d | - | - | - | - |
| SeLoadDriver | d | d | - | - | - | - |
| SeSystemProfile | E | E | - | - | - | - |
| SeSystemtime | d | d | d | - | - | - |
| SeProfileSingleProcess | E | E | - | - | - | - |
| SeIncreaseBasePriority | E | E | - | - | - | - |
| SeCreatePagefile | E | E | - | - | - | - |
| SeCreatePermanent | E | E | - | - | - | - |
| SeBackup | d | **E** | - | - | - | - |
| SeRestore | d | **E** | - | - | - | - |
| SeShutdown | d | **E** | d | d | d | - |
| SeDebug | E | E | - | - | - | - |
| SeAudit | E | E | d | d | d | d |
| SeSystemEnvironment | d | **E** | - | - | - | - |
| SeChangeNotify | E | E | E | E | E | E |
| SeUndock | d | d | d | d | d | - |
| SeManageVolume | d | **E** | - | - | - | - |
| SeImpersonate | E | E | - | **E** | - | **E** |
| SeCreateGlobal | E | E | E | E | E | E |
| SeIncreaseWorkingSet | E | - | d | - | d | - |
| SeTimeZone | E | - | d | - | d | - |
| SeCreateSymbolicLink | E | - | - | - | - | - |
| SeDelegateSessionUserImpersonate | E | - | - | - | - | - |
| SeRemoteShutdown | - | d | - | - | - | - |

### Differences to fix (Wine `token_create_service`, `server/token.c:898`; not changed here)

SYSTEM:
1. Wine enables SeBackup, SeRestore, SeShutdown, SeSystemEnvironment, SeManageVolume; native has them **disabled**.
2. Wine has SeRemoteShutdown (disabled); native SYSTEM has none.
3. Wine lacks SeIncreaseWorkingSet, SeTimeZone, SeCreateSymbolicLink, SeDelegateSessionUserImpersonate (all enabled
   natively). Wine defines no LUID for them.
4. Groups: Wine has only Everyone, Authenticated Users, Administrators (owner, primary). Native also has BUILTIN\Users,
   SERVICE, CONSOLE LOGON, This Organization, LOCAL and a per-service SID. Administrators is not mandatory natively
   (Wine sets MANDATORY).

LocalService:
5. Wine has SeImpersonate enabled; absent in the measured token (see caveat: SCM services may differ).
6. Wine lacks SeIncreaseQuota, SeSystemtime, SeIncreaseWorkingSet, SeTimeZone (all disabled natively).
7. Groups: Wine lacks CONSOLE LOGON, This Organization, per-service/task SID, and the eight unknown S-1-5-32-* SIDs
   (these look service-specific; do not copy them).

NetworkService:
8. Wine has SeImpersonate enabled (absent natively, same caveat).
9. Wine lacks SeShutdown, SeUndock, SeIncreaseWorkingSet, SeTimeZone (all disabled natively).
10. Groups: same missing groups as LocalService, minus the eight unknown SIDs (NetworkService had none).

Integrity, user SIDs, SeChangeNotify and SeCreateGlobal (enabled), SeAssignPrimaryToken, SeAudit (LS/NS): match.

Cleanup: scheduled tasks deleted by the script; `C:\wex-tokens` removed after the copy.

Note on the Wine side: the Wine branch moved after the work order was written. This run used Wine `c2b4fb9`, which
contains `117b0a7` ("align SYSTEM, LocalService, NetworkService and filtered tokens with native measurements"). Against
that commit the remaining differences from the tables above are: LocalService and NetworkService still hold
SeImpersonate (enabled; absent in the measured scheduled-task tokens, possibly added by the SCM for real services,
not measured), LocalService has no SeIncreaseQuota, and the group lists (items 4, 7, 10) are as before. The test
expectations below were updated to what Wine now produces.

### Part 2: build, install, acceptance tests

Environment (all rows recorded in the same run):

| Item | Build and test host (Debian WSL) | Installed system (QEMU/KVM guest `elev-val`) |
| :--- | :--- | :--- |
| Kernel | 6.18.33.2-microsoft-standard-WSL2 | 6.12.113+deb13-amd64 |
| `yama/ptrace_scope` | 1 | 0 |
| `fs/suid_dumpable` | 0 | 0 |
| Filesystem for setgid | ext4 (`/var/tmp`, `/opt`) | ext4 root |
| Sources | WineExplorer `claude/elev-local-validation` (from `9ae0203c`), Wine `gemini/token-security-b` at `c2b4fb9`, wexcomp `acc46fe6` | same ISO |

Deviation: the Proton build VM (SSH 2223) has 1.3 GB free on `/` and 9 GB on `/srv`, too little for a WoW64 build, so
I powered it off unused (it is not running now) and built in Debian WSL under `/var/tmp/elev` (a shallow clone of the
Wine fork; the WineExplorer tree came from `git archive` because a GitHub clone of it stalled). Build: `assemble.sh`,
`configure --enable-archs=i386,x86_64 --prefix=/usr/local`, `make -j14`: ok. ISO: `installer/build-iso.sh --clean`:
ok, 1.7 GB. Install: `installer/tests/install-interactive.sh` (VM `elev-val`, 20 GB disk): **PASS**, including the
Alice/Bob logons and `check-wow64.sh` (32-bit and 64-bit test programs, 32-bit and 64-bit notepad, Alice denied
direct writes to the machine root, root install, Bob runs it and is denied, Fluent theme listed).

Machine root ownership, fresh install (identical after `setup-machine-root.sh --update`): `root:wexsys` 2775 for the
root, `drive_c`, `windows`, `system32`, `Program Files`; `system.reg` and `userdef.reg` 664, `user.reg` 644, all
`root:wexsys`; ACL `group:wexsys:rwx` plus the same default ACL; group `wexsys` (gid 987) has no members; Alice is
in `sudo`. No pre-migration `root:sudo` machine root existed; the migration is covered by `test_b3_machine_root.sh`
(`b3_migration_from_sudo`, `b3_migration_idempotent`) and by the setup output "Migrating machine root from group root
to wexsys" during the S2 and S3 runs.

| Test | Where | Result |
| :--- | :--- | :--- |
| `scripts/test_b3_machine_root.sh` | WSL | 13 of 13 PASS |
| `test_s2_acceptance.sh` | guest | Tests 0 to 5 PASS; **Test 6 FAIL** (product bug 1); WAITING lines: COW break of `msvcp140.dll` by an administrator's own session; Carol modifying Alice's files |
| `test_s3_acceptance.sh` | guest | PASS ("ALL PHASE S3 ACCEPTANCE TESTS PASSED"); WAITING: Case 1 (Alice registers COM class and Uninstall entry), Case 3 and 3b (HKLM persistence needs two administrator sessions), Case 4 (Alice's post-upgrade key) |
| `test_s4_acceptance.sh` | guest | PASS; WAITING: Alice's own session installing into Program Files |
| `check-wow64.sh` | guest (inside the install run) | PASS |
| `scripts/test_wex_sysrun.sh`, `WEX_SYSRUN_ENABLE=1` | WSL | 60 of 60 PASS after the test fix (test bug 2) |
| `scripts/test_sysrun_server.sh`, `WEX_SYSRUN_ENABLE=1` | WSL, build under `/opt/elev` | 123 of 123 PASS after the test fixes (test bugs 3 to 6) |
| `check-token-security.py --all`, helper unset | WSL, as `sudo` member and as standard user, both archs | exit 0 / exit 0 after test bug 7: limited/standard shape, registry, pipe and consent PASS |
| `check-token-security.py --focused` | WSL, as `sudo` member and as standard user | exit 0 / exit 0 |

Failures, with causes (test assumption = fixed in this branch; product bug = not fixed):

1. **Product bug: `--update` nests the themes.** S2 Test 6 compares the machine root before and after the root
   `--update` and reports `FAIL: 77 unexpected changes in machine root!`. The diff (now printed by the test) is new
   files `Themes/Fluent/Fluent/...`, `Themes/XP/XP/...` and `Themes/Luna/Luna/Luna.msstyles`.
   `scripts/setup-machine-root.sh:251` runs `cp -rf "$theme_dir" ".../Themes/$tname"` with `$theme_dir` ending in a
   slash; when the destination exists (every run after the first) `cp` copies into `.../$tname/$tname`. Proposed
   patch: `cp -rfT "$theme_dir" ".../Themes/$tname"` (or `mkdir -p` the destination and copy `"$theme_dir".`).
2. Test: `test_wex_sysrun.sh` check "setup installed the launcher without WEX_SYSRUN_ENABLE" fails when the caller
   exports `WEX_SYSRUN_ENABLE=1`, which is how this work order says to run it. Fix: `env -u WEX_SYSRUN_ENABLE` in
   `setup_run`.
3. Test: `test_sysrun_server.sh` cleanup used `$XV_PID` unset (`set -u`), so an early failure aborted the cleanup
   with "XV_PID: unbound variable" and left the group, users and `/var/lib/wexsys-c2` behind. Fixed with `${XV_PID:-}`.
4. Environment: WSLg's `/tmp/.X11-unix` has mode 0777, not 1777, so Xvfb refuses to start. Run that test under
   `unshare -m` with a tmpfs mounted over `/tmp/.X11-unix`.
5. Test assumption: the launcher refuses a build tree below `/var/tmp` (world-writable directory), so the server
   was not started through it and the test reported "server ids are not (user, user, saved wexsysc2)". The Wine build
   dir must sit under a root-owned chain such as `/opt/elev/we/build/wow64` (with its source tree beside it, since the
   build tree links into it).
6. Test: `test_sysrun_server.sh` expected the old provisional privilege lists for LocalService and NetworkService;
   updated to the lists from Wine `117b0a7`.
7. Test: `test_token_security_regressions.cpp` expected exactly three privileges in the filtered token; Wine now also
   gives SeIncreaseWorkingSet and SeTimeZone (disabled). Expected list updated.
8. Test prerequisite, not in the repo: S2 and S3 need `/usr/local/bin/check_s1_32.exe` and `check_s1_64.exe`
   (without them: `install: cannot stat '/usr/local/bin/check_s1_64.exe'`). Built from `installer/tests/test_s1.c`
   with `-lversion` (i686 and x86_64 MinGW) and installed. Not documented anywhere I could find.
9. Test cleanup leaves `winedevice.exe` of the test user running, so `userdel` of that user failed ("currently used
   by process"); I killed it by hand.

`XDG_RUNTIME_DIR is invalid or not set` is printed twice by `setup-machine-root.sh` when run as root from a script
(noise from the wine invocations, not a failure).

### Part 3: desktop session

Only the command-line part was done. The graphical steps (Explorer, regedit, notepad, taskmgr, logging out and in
through the display manager) were **not run**: driving them through OCR and screenshots in the guest was out of
reach here, and they need a person or a longer session. What was run in the installed guest, as `alice` (in `sudo`)
and `bobsmith` (standard), each with its own prefix and a fresh `wineserver`, in three phases: P0 no launcher, P1
launcher installed with `WEX_SYSRUN_ENABLE=1 setup-machine-root.sh --update` and the helper unset, P2 launcher and the
helper configured (`HKLM\Software\Wine\Elevation\Helper` written into the machine hive, offline, as root).

| Step | P0 | P1 | P2 (helper set) |
| :--- | :--- | :--- | :--- |
| `mkdir "C:\Program Files\WexProbe..."` | Access denied | Access denied | Access denied |
| `reg add HKLM\Software\WexProbe...` | succeeds | **succeeds** (writes system.reg) | **denied** ("Unable to access or create the specified registry key") |
| `reg add HKCU\Software\WexProbe...` | ok | ok | ok |
| `sc create` | succeeds | succeeds | **denied** (`failed to open service manager: 5`) |
| 32-bit and 64-bit console program (`check_s1_32/64.exe`) | ok | ok | ok |
| `tasklist` | ok | ok | ok (`services.exe` listed) |
| `wineserver` ids | n/a | uid=gid=egid=user, sgid 987 (`wexsys`) | same |
| `system.reg` | `root:wexsys` 664 | same | same |

So the launcher alone changes nothing about what a user may write (P1); enforcement needs the helper (P2), as the
plan says. The wineserver runs with the user's egid and `wexsys` as saved gid, and the machine root stays
`root:wexsys` after every phase. `sc query` and `sc qc` are not supported by this Wine's `sc.exe` (usage / "command not
supported"), so those steps could not be checked. `cd /d "C:\Program Files" && dir` printed "Path not found" in every
phase including P0; that is a cmd quoting artifact of my script, so listing `Program Files` is unverified.

Slowness: none; every command took 0.1 to 0.6 s. New `err:` lines compared with P0:
- With the launcher (P1 and P2), seven lines `err:virtual:kernel_writewatch_init Error opening /proc/self/pagemap.`
  per run (`dlls/ntdll/unix/virtual.c:301`). Hypothesis, not confirmed: processes started with the system group are
  non-dumpable, so `/proc/self/pagemap` is root-owned to them; Wine falls back, but logs at ERR level. Proposed
  patch: log it at TRACE/WARN, or open `/proc/self/pagemap` before the privilege drop.
- With the helper (P2) also `err:plugplay:load_function_driver Failed to load driver L"winehid", status 0xc0000001`
  twice, presumably because the user's own services cannot load drivers under enforcement; expected, but new noise.

### Product bugs found

1. `scripts/setup-machine-root.sh:251`: theme directories nested on `--update` (above).
2. `dlls/ntdll/unix/virtual.c:301` (Wine): `err:` noise for processes started through the launcher (above).

### Status

Done: Part 1 (except the optional integrity-raise test, which needs the user's consent), Part 2 (all listed scripts
run, one product bug), Part 3 command-line subset. Not done: Part 3 graphical steps; `--update` on a pre-migration
`root:sudo` machine root outside the B3 fixture (none existed); the `WAITING` assertions need the broker.
Leftovers: QEMU guest `elev-val` disk under `/var/tmp/wineexplorer-vmtest/elev-val` (stopped); build and ISO under
`/var/tmp/elev` in Debian WSL; test users `tkadm`, `tkstd` and `/opt/elev` in Debian WSL. Logs are under
`build/elevation-validation/` (not committed).

### Follow-up fixes

Session `ccr-08223ee4-p830bf`, Wine `ccr-work` (from `c2b4fb9`), WineExplorer from `a242f559`.

1. **Security: P1 persisted every user's HKLM write.** With the launcher and the helper unset, `save_branch()` raised the
   system group for the machine hive save, so any user's `reg add HKLM\...` reached the shared `system.reg`. Fix in
   `server/registry.c`: the group is raised only if the machine hive loaded at start enforced elevation
   (`machine_hive_saved`, set once after loading `system.reg`). The live helper value is deliberately not used: with
   it a user could set `Helper` in memory, and the next save would persist that write and everything else. Without
   enforcement the hive keeps the stock/S3 rule (real-user access), so nothing is written (the files are `root:wexsys`
   664 and no user is in `wexsys`). The load path is unchanged and still comes from the machine root either way.
   Fixtures in `scripts/test_sysrun_server.sh`: `c2_unenforced_hklm_write_not_persisted_wexstd` and `..._wexadm`
   (launcher on, helper unset, `reg add HKLM` works in memory but `system.reg` is unchanged),
   `c2_machine_hive_not_saved_without_enforcement` (replaces the old "saved with raised group" check, which asserted
   the bug), `c2_enforced_standard_user_hklm_write_denied_and_not_persisted` (helper set), and the existing
   `c3_service_descriptor_saved_in_the_machine_hive` (system role write persists). Not covered: a `wineboot --init`
   boot state key in `system.reg` (`ActiveComputerName` is volatile, not saved), so the persisting system-role write
   that is checked is the service descriptor.
2. **`--update` nested the themes.** `scripts/setup-machine-root.sh` uses `cp -rfT` and removes a `Themes/<name>/<name>`
   left by earlier runs. `test_b3_machine_root.sh` gained `b3_update_themes_not_nested` (two `--update` runs, same
   file list; fails with the old `cp`). This is the cause of S2 Test 6 ("77 unexpected changes"); S2 itself was not
   rerun here (needs the installed guest).
3. **`kernel_writewatch_init` ERR.** Cause as suspected: the launcher's processes run with the system group and are
   non-dumpable, so `/proc/self/pagemap` is owned by root and `open` fails with EACCES. `virtual.c` now logs at WARN
   when the open fails with EACCES/EPERM and `PR_GET_DUMPABLE` is 0; the fallback is unchanged. The ERR line is gone
   from a launcher run.
4. **`Failed to load driver L"winehid", status 0xc0000001`.** `winedevice.exe` (plugplay) does run as SYSTEM through the
   launcher. `ZwLoadDriver` -> `open_driver` calls `OpenServiceW(winehid, SERVICE_QUERY_CONFIG | SERVICE_SET_STATUS)`;
   that failed with `ERROR_ACCESS_DENIED`. Under enforcement `services.exe` checks the request against the default
   service descriptor, and the SYSTEM ACE (`0x201fd`) lacked Wine's `SERVICE_SET_STATUS` (0x8000). The only other way
   to get it is `own_process_access`, which requires the service's recorded process id to equal the client's; the
   driver service `winehid` is never started by `services.exe`, so it has no process record. So even SYSTEM was
   refused; it was not a missing privilege or a Services key read. Fix in `programs/services/rpc.c`: the default
   descriptor gives SY `0x2a1fd` (adds 0x8000). Standard users and Administrators are unchanged. Both errors are gone
   in a helper-set run; `winebth` still reports `c00000e5` (unrelated, no Bluetooth stack).
5. **Test hygiene.** `test_s2_acceptance.sh` and `test_s3_acceptance.sh` build `/usr/local/bin/check_s1_32.exe` and
   `check_s1_64.exe` from `installer/tests/test_s1.c` (`-lversion`, i686 and x86_64 MinGW) when missing. The cleanup of
   `test_sysrun_server.sh` now sends SIGKILL to the test users' remaining processes (the `winedevice.exe` that blocked
   `userdel`).

Acceptance after the fixes (Debian cloud container, Wine build `/home/user/build/dev`): `test_sysrun_server.sh`
`WEX_SYSRUN_ENABLE=1`: 141 checks passed (was 123 before the new checks); `test_wex_sysrun.sh`: 60 passed;
`test_b3_machine_root.sh`: 14 PASS lines; `check-token-security.py --all`, helper unset, as an unprivileged `sudo`
member: exit 0, advapi32 security 0 failures, shlexec 1805 tests 2 failures (the known baseline), ntdll info 0
failures; `--focused` (sets the helper itself) as a `sudo` member and as a standard user: exit 0 / exit 0. All fixture
users, groups, `/etc/wineexplorer` and `/usr/local/libexec/wineexplorer` were removed. Not rerun: S2, S3, S4 (need the
installed guest and the broker).

### Results, second validation run (2026-10-23, after the follow-up fixes)

Sources: WineExplorer `claude/elev-validation-2` from `1d3add0f`, Wine `gemini/token-security-b` at `1309a1db`
(WoW64: `configure --enable-archs=i386,x86_64`, `make -j14`), wexcomp `acc46fe6`. Built in Debian WSL (kernel
6.18.33.2-microsoft-standard-WSL2, `ptrace_scope` 1, `suid_dumpable` 0); the earlier leftovers (guest disk,
`/var/tmp/elev`, `/opt/elev`, users `tkadm` and `tkstd`) were removed first. Fresh ISO (1.7 GB), fresh QEMU guest
`elev-val2` (kernel 6.12.113+deb13-amd64).

Step 1, reinstall and acceptance (guest as root; `check_s1_32/64.exe` prebuilt, `gcc` and `acl` added to the guest):

| Test | Result |
| :--- | :--- |
| `install-interactive.sh` (incl. `check-wow64.sh`) | PASS |
| `test_s2_acceptance.sh` | **PASS** (Test 6 now passes); WAITING: COW break by an administrator's own session; Carol modifying Alice's files |
| `test_s3_acceptance.sh` | PASS; WAITING: Case 1 COM/Uninstall by Alice, Case 3 and 3b (HKLM persistence needs two administrator sessions), Case 4 |
| `test_s4_acceptance.sh` | PASS; WAITING: Alice's own session installing into Program Files |
