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

Step 2, Part 3 phase P1 and P2 repeated in the new guest (alice in `sudo`, bobsmith standard; command line, own prefix
and fresh `wineserver` each):

| Check | P1 (launcher on, helper unset) | P2 (launcher on, helper set) |
| :--- | :--- | :--- |
| `reg add HKLM\Software\WexProbe...` | succeeds in the session | denied ("Unable to access or create the specified registry key"), key not visible afterwards |
| `system.reg` md5 before / after each session | `39c99d30...` before and after alice, and after bobsmith (unchanged); `WexProbe` lines in `system.reg`: 0 | `64f47250...` before and after both sessions (unchanged); 0 lines |
| `mkdir "C:\Program Files\..."` | Access denied | Access denied |
| `sc create` | succeeds in the session, not persisted | denied (`failed to open service manager: 5`) |
| 32-bit and 64-bit console program | ok | ok |
| `wineserver` ids | uid=gid=egid=user, sgid 987 | same |
| `winehid` or `kernel_writewatch_init`/`pagemap` lines | none | none (0 matches in all stderr files) |

The only `err:` lines left in these runs are the headless first start of a prefix without a display
(`nodrv_CreateWindow`, `systray`, `winemenubuilder.exe` 126). `system.reg` stayed `root:wexsys` 664 and the machine
root `root:wexsys` 2775.

Step 3, Part 3 graphical steps, driven by me through the guest's QEMU display (screenshots, mouse and keyboard through
the monitor; no human input), in the default WineExplorer (X11) session. The guest had the launcher installed and the
helper set (`HKLM\Software\Wine\Elevation\Helper`), as in P2. Note: this is the installed WineExplorer XP-style desktop
(logonui Welcome screen, Explorer shell), not Openbox; the guest has no Openbox session. Screenshots are in
`build/ev/` of the working tree (not committed).

As `bobsmith` (standard user, passwordless):

| Step | Result |
| :--- | :--- |
| Log on from the Welcome screen, desktop with icons | pass (about 5 s) |
| Explorer: My Computer, `C:\`, `C:\windows`, `C:\Program Files` | pass: listings shown (`Common Files`, `Internet Explorer`, `Windows Media Player`, `Windows NT` in Program Files); no crash |
| Create a folder in `C:\Program Files` ("Make a new folder") | no folder created, no dialog, no crash (still 4 objects; nothing new on disk) |
| `regedit` (Run): read `HKLM\Software` | pass (`Hardware`, `Software`, `System` listed) |
| `regedit`: create a key under `HKLM\Software` | denied: dialog "Unable to create a new registry key." |
| `regedit`: create a key under `HKCU` | pass |
| `notepad`: type text, Save As in `My Documents` | pass: `bobnote.txt` is at `/home/bobsmith/.wine/drive_c/users/bobsmith/Documents/` |
| `taskmgr` Processes: `services.exe`, `winedevice.exe` (two), `plugplay.exe`, `svchost.exe`, `rpcss.exe` listed | pass |
| `taskmgr`: End Process on `services.exe` and on `winedevice.exe` | denied, "Unable to Terminate: Access denied. (5)" |
| `taskmgr`: Set Priority to High on `services.exe` | denied, "Unable to Change Priority: Access denied. (5)" |
| 32-bit program | passed earlier on the same guest image in `check-wow64.sh` (window drawn, 32-bit and 64-bit notepad); not repeated in this session |
| Log off | pass; afterwards only the greeter's `wineserver` (uid 102) is left, none of `bobsmith`; `system.reg` unchanged |

During bobsmith's session, from the guest console: `ps -o pid,uid,gid,egid,sgid,args -C wineserver` showed
uid 1001, gid 1001, egid 1001, sgid 987 (`wexsys`) for the user's `wineserver`;
`ls -l system.reg` = `root:wexsys` 664, md5 `64f47250...` as before the session, no `WexGui` string in it.

As `alice` (in `sudo`, password logon): the desktop appeared but **without the desktop icons** (only the taskbar), see
open observation 1. `regedit`: creating a key under `HKLM\Software` is denied with the same dialog; `taskmgr`:
End Process on `services.exe` denied ("Access denied. (5)"). Not done as alice: Explorer browsing, notepad, folder
creation in `Program Files`, `sc`; the Alice session was a shortened pass.

Not done: `sc query`, `sc qc`, `sc create` in the GUI session (the Wine `sc.exe` does not support them; the command-line
`sc create` denial is recorded in step 2), the log-out-and-in of Alice, the display-manager restart.

Open observations (no verdict): 
1. Alice's desktop has no icons after the test sequence (S2, S3, S4 reset her prefix; launcher on and helper set).
   Right after the install, before the tests, Alice's desktop had them (`15-alice-desktop.png`), and Bob's after the same
   tests did. I did not find out whether the test resets, the helper or the administrator token causes it.
2. The `kernel_writewatch_init` and `winehid` lines are gone in the command-line runs (step 2); I did not read the
   session logs of the graphical runs.

Step 4, cleanup: done at the start of this run for the earlier leftovers (guest `elev-val` disk, `/var/tmp/elev`,
`/opt/elev`, `/opt/wexbuild`, users `tkadm` and `tkstd`, group `tkadmg`) in Debian WSL; the Proton build VM is not
running. The leftovers of this run (guest `elev-val2` under `/var/tmp/wineexplorer-vmtest`, `/var/tmp/elev`, the
`/tmp/.X11-unix` tmpfs mounts were private to `unshare`) are kept until the pull request is reviewed.

### Results, why Alice's desktop has no icons (2026-10-23)

Same guest (`elev-val2`, Wine `1309a1db`, WineExplorer `1d3add0f` plus PR 29), one change at a time, each time a
cold boot, a logon from the Welcome screen and a collector run as root in the live session. The launcher default was
not touched; the launcher and the helper were switched only in this disposable guest (launcher: the
`/usr/local/libexec/wineexplorer` directory and `/etc/wineexplorer/sysrun.conf` moved away; helper:
`HKLM\Software\Wine\Elevation\Helper` removed from or added to `system.reg` offline). Before every Alice run her
prefix was reset exactly the way `reset_all_users()` of `installer/tests/test_s3_acceptance.sh` does it (copy of
`/etc/skel/.wine`, `mkdir users/alice`, `chown`). Explorer ran with
`WINEDEBUG=-all,err+all,fixme+all,+shell,+file` (via `~/.xsessionrc`); the logs (about 3 MB each, `+file` is noisy) are
in `build/al/out/<scenario>/` of the working tree, not committed.

**Cause.** The first logon of an account runs `"$WINE" regsvr32.exe /s shell32.dll` as that user
(`scripts/wine-explorer-session-common.sh:43`, block "First logon of an account made by Setup", line 27 onward).
`DllRegisterServer` of shell32 (`dlls/shell32/shell32_main.c:1122`) first runs `__wine_register_resources()`, which
writes `HKLM\Software\Classes\Typelib\...`; with the elevation helper set the server denies that write to the limited
token (`NtSetValueKey` -> 5, `warn:atl:do_process_key Could set value of key: 00000005`,
`regsvr32.exe` exit 5, trace in `build/al/regsvr32-shell32.trace`). The function returns the error, so
`SHELL_RegisterShellFolders()` (`dlls/shell32/shellpath.c:4731`: per-user shell folders, `create_extra_folders()`)
never runs and the profile folders (`Desktop`, `Documents`, `Downloads`, `Favorites`, `Links`, `Music`, `Pictures`,
`Videos`, `Searches`, `Contacts`, `Saved Games`) are never created. The `/s` switch hides the failure; the session
continues with `users/<name>/` holding only `AppData`, and the shell shows the taskbar but no desktop icons
(`C:\users\alice\Desktop` does not exist: `GetFileAttributesW` fails in the `+file` trace). The registry values are
identical with and without the failure (`"Desktop"="C:\\users\\alice\\Desktop"` in `user.reg`, from the sed in the
same script); only the directories are missing.

It is **not** specific to Alice, to the administrator token, to the launcher or to the S2/S3/S4 tests: it happens
at the first logon of any account whose prefix is a template copy when the helper is set.

| Scenario | Launcher | Helper | Account, prefix | `regsvr32 shell32.dll` | `Desktop` folder (owner, mode) | Icons |
| :--- | :---: | :---: | :--- | :--- | :--- | :---: |
| s1 | on | set | alice (`sudo`), reset | **Failed** | absent (profile: `AppData` only) | no |
| s2 | on | unset | alice, reset | Successfully registered | alice:alice 755, empty | yes |
| s3 | off | set | alice, reset | **Failed** | absent | no |
| s4 | off | off | alice, reset | Successfully registered | alice:alice 775, empty | yes |
| s5 | on | set | new `sudo` user carol2 (`useradd -m`, passwordless) | **Failed** | absent | no |
| s6 | on | set | bobsmith (already personalized at install) | not run (`wexsetup` names already replaced) | bobsmith:bobsmith 775 | yes |
| s7 | on | set | carol2 with the folders created by hand before logon | not run | carol2 755 | yes |

So the helper is the only switch that matters (s1 vs s2, s3 vs s4); s7 shows that creating the missing directories
is enough to bring the icons back with the helper set. `Public Desktop`
(`/var/lib/wineexplorer/machine/drive_c/users/Public/Desktop`) was empty, `root:wexsys` `drwxrwsr-x+`, in every
scenario: no `.lnk` files exist there, so no link points at an unreadable file; the icons (My Computer, My Documents,
Recycle Bin, ...) come from the shell namespace and need only the user's `Desktop` directory. The only `err:` line of
every session is the known `winemenubuilder.exe -a -r (126)`; explorer logged no `err:` or `fixme:` of its own in her
session.

**Step 3, did S2, S3 and S4 change Alice's prefix?** Yes, S3 does: `reset_all_users()`
(`installer/tests/test_s3_acceptance.sh:82`) deletes `/home/alice/.wine` and `/home/bob/.wine` and copies the template,
and on the installed guest `alice` is the account the installer made, so her real prefix is replaced. S2 initialises the
same prefixes. The reset also keeps the template user name: the `sed` at line 88
(`s/users\\\\wexsetup/users\\\\$u/g` in the script, i.e. one backslash after `users` in sed) never matches the
registry text, which has two backslashes, so `user.reg` still says `wexsetup` after the reset; that is why the next
logon runs the first-logon block above. The machine root ownership change (`root:wexsys`) did not matter here: the Public
Desktop and Alice's profile are readable. Alice's prefix after the tests: `drive_c/users/alice` only holds `AppData`
(created at the failed first logon), her `user.reg` is the personalized template.

**Proposed fix** (not applied; product code was not touched):

1. `scripts/wine-explorer-session-common.sh:43`: do not depend on `regsvr32` for the per-user part. The machine-wide
   registration should come from the machine setup (`scripts/setup-machine-root.sh` runs `wineboot -i` as root; I did
   not check that it includes everything `DllRegisterServer` of shell32 writes), so after the rename of the template
   user only the profile folders are needed (s7 shows that suffices). Smallest fix, no Wine change: after the `sed`
   block, `mkdir -p` the folders (`Desktop Documents Downloads Favorites Links Music Pictures Videos Searches Contacts
   "Saved Games"` under `$WINEPREFIX/drive_c/users/$USER`; s2 and s4 list the full set, including
   `Documents/My Music`, `My Pictures`, `My Videos` links and `desktop.ini`, and `Pictures/Screenshots`) and keep
   `regsvr32` only as the fallback when the helper is unset. Also stop discarding the result: log
   `regsvr32 failed (exit N)` so the next case is visible. Alternative with a stock change (list it in
   `docs/STOCK_CHANGES.md`): make shell32 `DllInstall(TRUE, L"user")` (a stub today, `shell32_main.c:1113`) run only the
   per-user part (`_SHRegisterUserShellFolders(FALSE)` and `create_extra_folders()`), and call
   `regsvr32 /s /n /i:user shell32.dll`.
2. Test: `installer/tests/test_s3_acceptance.sh:88` and the same reset in `test_s2_acceptance.sh`: use the right
   number of backslashes (or do not rename at all), and run the tests with their own users (for example `wext-alice`),
   not with the installed accounts `alice` and `bob`, so an acceptance run does not replace a real account's prefix.
   Add one check that a freshly created user with the helper set gets a `Desktop` directory.

Cleanup: after this run the guest `elev-val2` and `/var/tmp/elev` are deleted (see the PR description); the guest was
left with launcher on, helper set and the carol2 account, which is gone with it.

#### Fix of the first-logon folders

- Wine: `DllInstall(TRUE, L"U")` of shell32 is now the per-user setup (HKCU shell folders, the user's folders, per-user
  extra folders; no HKLM write). `DllRegisterServer` is stock. Listed in `docs/STOCK_CHANGES.md`. The machine part
  is done by `wineboot -i` in `setup-machine-root.sh` (`wine.inf` `RegisterDllsSection` registers `shell32.dll`).
- `scripts/wine-explorer-session-common.sh`: the first logon runs `regsvr32.exe /s /n /i:U shell32.dll` and logs a
  warning to the session log when it fails.
- Check: `check-token-security.py` (also `--focused`) simulates a first logon in a fresh prefix per architecture, with
  the helper unset and set (profile folders deleted but `AppData`, `regsvr32 /s /n /i:U shell32.dll`), and prints
  `first_logon_folders=PASS|FAIL`: Desktop, Documents, Downloads, Music, Pictures, Videos exist and the shell32 class
  under `HKLM\Software\Classes` (deleted beforehand) was not written. Run here as a `sudo` member: PASS in all four.
- `installer/tests/test_s3_acceptance.sh`: the rename `sed` now has the four backslashes it needs (checked on a sample
  line). Not run (needs the installed guest; `bash -n` only).
- Follow-up: S3 (and S2) still use the installed accounts `alice` and `bob` (many `su - alice` calls); they should use
  their own test users. `wineboot -u` in a user prefix also runs shell32's `DllRegisterServer` (`[UserRegisterDlls]`
  in `wine.inf.in`) and can hit the same early return under enforcement; not changed here.

### Results, third validation run (2026-10-23, new accounts and first logon)

Sources: WineExplorer `claude/elev-validation-3` from `e3fc0769`, Wine `gemini/token-security-b` at `5d373ba6`
(WoW64, `configure --enable-archs=i386,x86_64`, `make -j14`), wexcomp `acc46fe6`. Built in Debian WSL (kernel
6.18.33.2-microsoft-standard-WSL2); fresh ISO (1.7 GB); fresh QEMU guest `elev-val3` (kernel 6.12.113+deb13-amd64).
`install-interactive.sh` (with `check-wow64.sh`): PASS.

1. **`test_s3_acceptance.sh`, launcher installed** (`WEX_SYSRUN_ENABLE=1 setup-machine-root.sh --update`, then the
   test with `WEX_SYSRUN_ENABLE=1` so its own `setup-machine-root.sh` runs keep the launcher): "ALL PHASE S3 ACCEPTANCE
   TESTS PASSED PERFECTLY"; WAITING lines as before (Case 1 COM/Uninstall by Alice, Case 3 and 3b HKLM persistence
   with two administrators, Case 4). The test deletes and rebuilds the machine root (`rm -rf` at its start), so a helper
   set before it would be lost; I set the helper (`HKLM\Software\Wine\Elevation\Helper`, machine hive, as root, no
   server running) **after** S3, so the S3 run itself had the launcher but not the helper. Machine root afterwards
   `root:wexsys` 2775, `system.reg` 664, launcher `root:wexsys` 2755.
2. **New `sudo` user `dana`** (`useradd -m -G sudo`, passwordless, copy of `/etc/skel` including `.wine`), launcher
   installed and helper set, first logon through the Welcome screen:

   | Step | Result |
   | :--- | :--- |
   | Desktop after the first logon | **icons present** (My Documents, My Computer, My Network Places, Recycle Bin, Internet Explorer); session log: `regsvr32: Successfully installed DLL 'shell32.dll'`; `Desktop`, `Documents`, `Downloads`, `Favorites`, `Links`, `Music`, `Pictures`, `Saved Games`, `Searches`, `Videos`, `Contacts` created, owned by `dana` |
   | Explorer: My Computer, `C:\`, `C:\Program Files` | pass; listings shown (`AliceApp` from S3, `Common Files`, `Internet Explorer`, `Windows Media Player`, `Windows NT`) |
   | "Make a new folder" in `C:\Program Files` | no folder created, no dialog, no crash (still 5 objects; none new on disk) |
   | `notepad` (Run), type text, Save As in My Documents | pass: `dananote.txt` in `/home/dana/.wine/drive_c/users/dana/Documents/` (26 bytes) |
   | Log off, log on again | pass; desktop with icons again |
   | During the sessions | `wineserver` uid 1004, gid 1004, egid 1004, sgid 987 (`wexsys`), both sessions; `system.reg` md5 `60d6a44e...` before and after |

3. **New standard user `erin`** (`useradd -m`, not in `sudo`, passwordless), same steps: icons present at the first logon
   (`regsvr32: Successfully installed DLL 'shell32.dll'`), all profile folders created, Explorer browsing ok, no folder
   created in `Program Files`, `notepad` saved `erinnote.txt` in her Documents, log off and on again ok; `wineserver`
   uid/gid/egid 1005, sgid 987; `system.reg` md5 unchanged (`60d6a44e...`).

At the end (guest stopped): launcher present, helper present in `system.reg`, `Program Files` unchanged, machine root
`root:wexsys`. This confirms the first-logon fix of `09798772` with the helper set for both a `sudo` member and a standard
user.

Not checked: the HKLM denial and service creation in the GUI for these users (covered by the earlier command-line runs);
the 32-bit program in the session (the install-time `check-wow64.sh` passed on this image). Observation, not
investigated: right after a log off the Welcome screen shows the account as "Logged on" for a moment (dana, screenshot
`dana-11-welcome-end.png`); the sessions did restart cleanly each time. The in-guest collector script was not installed
in this guest, so only `ps`, `md5sum` and a directory listing were taken per session (`build/v3/shots/*-ps*.txt`;
screenshots in `build/v3/shots/`, not committed).

Cleanup: guest `elev-val3` and the build tree under `/var/tmp/elev` are deleted after this commit.

Closing note (2026-10-23): the evidence above met the condition; the launcher is now installed by default (`WEX_SYSRUN_ENABLE=0` opts out).
