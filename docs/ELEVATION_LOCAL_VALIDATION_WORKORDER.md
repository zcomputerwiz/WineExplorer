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

### Status

Part 1 done. Optional integrity-raise test (SetTokenInformation to High, expected 1314): not done, needs user consent.
Parts 2 and 3 (WSL/VM build, install, acceptance tests, desktop session): pending.
