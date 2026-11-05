# wexdev

Agent harness for the D: source tree, Wine's development branch and the existing
WoW64 build. **Runtime acceptance is pending. Do not run while Qwen owns
wexbuild.** Existing wexadm/wineexplorer state is never migrated.

From Windows:

```powershell
wsl -d wexbuild --exec bash /mnt/d/DeepThought/Projects/WineExplorer/scripts/wexdev.sh build dlls/advapi32
```

Inside wexbuild, as root:

```bash
wexdev=/mnt/d/DeepThought/Projects/WineExplorer/scripts/wexdev.sh
bash "$wexdev" build                         # detect, sync, targeted make
bash "$wexdev" build dlls/advapi32 server     # paths relative to wine/
bash "$wexdev" prefix acl-01 --gate SomeGate=1
bash "$wexdev" run acl-01 /absolute/probe.exe arg
bash "$wexdev" run acl-01 --arch both '/tmp/probe{arch}.exe'
bash "$wexdev" test advapi32 registry         # both architectures
bash "$wexdev" sysrole up
bash "$wexdev" sysrole status
bash "$wexdev" svc token /var/lib/wexdev-sys/probes/rolesprobe64.exe token --arch 64
bash "$wexdev" svc token32 /var/lib/wexdev-sys/probes/rolesprobe32.exe token --arch 32 --account LocalService
bash "$wexdev" sysrole down
bash "$wexdev" sysrole reset                  # owned wexdev fixture only
bash "$wexdev" guest 2222 push
bash "$wexdev" guest 2236 sysrole up
bash "$wexdev" guest 2236 sysrole status
bash "$wexdev" guest 2236 broker install --lab
bash "$wexdev" guest 2236 elev /opt/wex-lab-runas_wait.exe --help
bash "$wexdev" guest 2236 install /tmp/Browser.msi --elevated --debug err+all,fixme+msi --label browser
bash "$wexdev" guest 2236 debug off
bash "$wexdev" guest 2236 broker remove --lab
bash "$wexdev" guest 2236 sysrole reset
```

The guest commands use the same `WEXDEV_SSH` executable/options and
`WEXDEV_GUEST` destination as `guest push`. `sysrole up` stages a root-owned
runtime, NLS/fonts, launcher, machine-root setup, elevation Helper, test users,
first-logon personalization, and machine-root profile directories. `status`
checks ownership, setgid launcher/configuration paths, Helper, profiles, disk,
and prints the existing saved-gid/services probe to run. `reset` only accepts
the marked lab machine root. `broker` is deliberately refused without the
literal `--lab` flag and installs the compile-time approval broker. `install`
clears stale MSI copies, requires at least 8 GiB free in the guest, and runs
MSI/EXE payloads through the lab elevation holder with logs under `/tmp/gsr-out`.
`debug` changes the machine-wide WINEDEBUG registry value; `off` removes it.

Build defaults: `/root/WE-prefetch/build/wow64` and clone
`/root/WE-prefetch/wine`. The build's wine loads fresh DLLs directly; there is no
system-install staging. Build invokes `/root/sync-wine.sh` without duplicating its
copy logic. Dirty/untracked paths and differences against the clone select
DLL/program `/all` targets, DLL `tests/all` where tests exist, or
`server/wineserver`. Shared headers/other build inputs print a full-make note and
run full make. Explicit paths narrow targets; the sync helper still syncs all
its changes. Diagnostics and final PASS/FAIL are printed; full output is in
`/root/wexdev/logs/build.log`. A successful build records deployable module files
in `last-build.files`; a failed or no-change build invalidates that manifest.

All commands lock `/root/wexdev/lock`; `lock.owner` identifies the holder. Fresh
WoW64 prefixes are named under `/root/wexdev/pfx`; existing names are refused.
Gates are DWORDs in `HKCU\Software\WineExplorer\Shell`. Prefix creation prints
environment exports. Runs disable mscoree/mshtml and default WINEDEBUG to
`err+all`. `--arch both` selects x86_64-windows/i386-windows siblings or substitutes
`{arch}` with 64/32; explicit arches check PE machine type. Without it, Wine
selects the executable's architecture. Test runs the built module test executable
for both arches, once per test name or the whole executable without names.

`WEXDEV_TIMEOUT` defaults to 120 seconds per run/test/sync/bootstrap/SSH action.
Timestamped run logs retain the last `WEXDEV_LOG_BYTES` bytes (default 2 MiB);
exit status and last lines are printed (124 means timeout). Unset DISPLAY starts
an allocated Xvfb; `WEXDEV_HEADLESS=1` forces it. Cleanup stops that Xvfb and only
the selected prefix's server. Exported prefix environments need a DISPLAY for
manual use; use `run` for automatic headless handling.

System-role setup shares machine/registry/service helpers with
`test_sysrun_server.sh` through `scripts/lib/sysrun-fixture.sh`. It provisions
`/var/lib/wexdev-sys`, group wexdevsys, limited administrator wexdev (sudo member),
and standard user wexdevstd. It installs the setgid launcher with BUILD wine,
server and ntdll, enables elevation Helper, verifies system-image resolution into
BUILD, and compiles the existing probes afresh. It grants only the newly created
users search ACLs on otherwise inaccessible build/source ancestors, records them
for reset, and refuses existing ACL-mask conflicts. Build binaries must already
be readable/executable. The launcher paths are compiled
into Wine: `/etc/wineexplorer` and `/usr/local/libexec/wineexplorer`. Conflicting
directories/users cause refusal. **Do not stop wexadm or remove Qwen's setup to
pass this check.** Arrange a maintenance window with its owner.

`down` stops only owned users' named prefixes and retains configuration; `reset`
removes recorded wexdev resources. Partial setup/changed launchers can require
manual inspection. Status reports provisioning, not a persistent server.
Services are auto-start entries written offline as root. A freshly compiled
MinGW GUI service adapter runs an ordinary program, redirects stdout/stderr to
`C:\wexdev-out\<name>.txt` and records completion/exit code. Accounts:
LocalSystem (default), LocalService, NetworkService; arch: 64 (default) or 32.
Windows CRT quoting and a UTF-16 command file preserve arguments. Use `--` before
child arguments matching harness options. The child's binary must already be
fresh. Cleanup stops the standard user's prefix and restores the pre-service
hive, removing the temporary service. Failed cleanup retains
`/var/lib/wexdev-sys/service-hive.backup` for offline recovery. The scratch output
directory is writable by all fixture service accounts.

Guest push uses `last-build.files` (PE/Unix module files including tests), matching
the existing guest layout at
`/home/debian/development-validation-20261101/memory-runtime2/build/wow64`.
Each original gets one `.bak-wexdev`; upload verification precedes atomic replacement,
then downloaded bytes get a local MD5 check. Missing guest files fail. Earlier
replacements remain if a later upload fails; restore backups offline if needed.
No guest desktop restart is performed.

Default SSH: `ssh -i /var/tmp/wex-g4-lab-credentials/vm_key -o BatchMode=yes -o ConnectTimeout=10`,
destination `debian@127.0.0.1`. `WEXDEV_SSH` supplies an executable/options separated
by whitespace, without shell evaluation (use an executable wrapper for complex
quoting); `WEXDEV_GUEST` overrides destination. To push from Debian WSL, copy the
artifacts and manifest there and set `WEXDEV_BUILD` and `WEXDEV_HOME` accordingly.
`WEXDEV_CLONE` overrides the build comparison clone; `WEXDEV_JOBS` selects jobs.
Agents must share a state directory/distro for lock serialization; cross-distro
guest operators must coordinate exclusivity.

Dependencies: Bash 4.4+, GNU make/coreutils/findutils, git, flock, Perl, file,
iconv, Xvfb (headless), SSH/base64/MD5 (guest); gcc, both MinGW compilers,
runuser/account tools, sudo group and POSIX ACL tools for system-role setup.

## TESTPLAN

### Guest system-role workflow

Run these later on an exclusive disposable VM; no guest commands were run while
authoring this interface.

1. Set `WEXDEV_SSH` and `WEXDEV_GUEST`, then run `guest port sysrole up` twice.
   Confirm the second run is idempotent, `/opt/wex-rt2` and its NLS/fonts are
   root-owned, the launcher is `2755 root:wexsys`, and `sysrun.conf` resolves
   only through root-owned ancestors.
2. Run `guest port sysrole status`; verify the Helper value, both personalized
   user registries, machine-root Temp/Roaming directories, free-space guard,
   saved `wineserver` gid and `services.exe` egid probe. Check that neither test
   user is in `wexsys`.
3. Verify `guest port broker install` fails, then `broker --lab install` emits
   the warning and is idempotent. Exercise `elev`, then remove the broker and
   confirm its files, policy and registry backup are restored.
4. Run MSI and EXE installs as the test user with `--elevated`, labels and debug
   channels. Confirm stale `C:\windows\Installer` copies are removed, logs and
   result summaries are retained, and an 8-GiB-or-less guest refuses before
   starting Wine. Test paths with spaces and a failed installer.
5. Run `debug off`, `sysrole reset`, and repeat reset. Confirm only the marked
   lab runtime/users/machine-root are removed; foreign `/etc/wineexplorer`,
   users, groups and launcher paths must be refused and left unchanged.

Execute later after the distro owner releases it. No builds, Wine, wineserver,
Xvfb or fixtures were run during authoring. First read `/root/sync-wine.sh` and
`/root/p6-test.sh`: read-only WSL/UNC access was denied during authoring, so their
contents could not be reviewed. Confirm sync source/destination, untracked/deleted
file handling and branch assumptions; compare p6's visual-test environment.

1. **Static:** run `bash -n` separately on wexdev.sh, lib/sysrun-fixture.sh and
   test_sysrun_server.sh; from the repo root run
   `shellcheck -x scripts/wexdev.sh scripts/lib/sysrun-fixture.sh`. Compile both
   service adapter arches with the exact svc flags; inspect all warnings.
2. **Lock/errors:** hold the lock with flock, populate lock.owner, invoke prefix
   and expect nonzero/one final FAIL identifying the owner. Release it; verify
   unknown commands, invalid names/options/arches fail. During a long run,
   another command must refuse. Check final lines agree with exit codes.
3. **Build:** record source/build hashes, change DLL/program/server sources and
   add an untracked path. Inspect build.log for the expected module/test/server
   targets and both PE/Unix output hashes. Repeat with explicit paths, staged
   edits, deletions, committed unsynced changes and a DLL without tests. Change
   an include header: confirm full-make note. Introduce a compile error: expect
   FAIL/diagnostics and no deployable manifest; fix/rebuild. Run without changes
   and confirm no-change behavior. Confirm no system install files changed.
4. **Prefix:** create a unique prefix with two gates; query both via BUILD's
   reg.exe. Verify WoW64 links/load paths/hashes resolve to BUILD. Test existing
   DISPLAY, unset DISPLAY, forced headless, duplicate name, invalid gates and
   failed wineboot. Old prefixes must survive refusal. No owned Xvfb/server
   should remain after success or failure.
5. **Run/test:** compile 32/64 ordinary probes printing argv and returning 0/7.
   Exercise native/explicit/both arches, sibling and {arch} paths, missing/wrong
   machine files, spaces, quotes, Unicode and trailing backslashes. Use a sleep
   probe with short timeout and a flood larger than the log cap, including one
   huge line; inspect size/status/tail. Interrupt with INT/TERM. Run
   `test advapi32 registry` and a small whole module suite; verify both built
   test paths and failures from either arch. Check cleanup after every case.
   Compare p6 visual behavior separately; headless success does not establish
   presented pixels or screenshot correctness.
6. **Foreign setup:** record hashes of Qwen's config/machine paths and wexadm
   process IDs without altering them. All sysrole commands must refuse foreign
   state and leave these unchanged. Do not remove/stop that setup. Only after
   its owner retires it independently, test conflicting users/directories.
7. **Owned fixture:** ensure BUILD accessibility; up/status. Check launcher
   `2755 root:wexdevsys`, config paths, Helper, skeletons, system-image readlink
   targets, neither user in wexdevsys and only wexdev in sudo. Use the existing
   roles/server probes to check limited HKLM/system-file denial. Temporarily
   point one owned system image at an older install: status/svc must refuse;
   restore it. Verify repeated up is idempotent.
8. **Service:** run rolesprobe token for 32/64 and all three accounts. Expect
   S-1-5-18/19/20 and corresponding token/group facts. Confirm auto-start works
   without limited-user sc or HKLM writes. Exercise reused names/stale output,
   child exit 7, timeout, missing exe, interruption and complex argv. Verify
   output, bounded log, service removal/hive restoration, backup retention on
   cleanup failure and no remaining named-prefix processes.
9. **Down/reset/regression:** start an owned prefix; down twice, confirm config
   persists and another independent prefix survives. Reset; only recorded
   wexdev resources must disappear. Check partial setup and changed-launcher
   refusal. With fixed paths free, run full test_sysrun_server.sh against BUILD;
   compare its helper/service ImagePath output with the pre-extraction version.
   It must still refuse an existing fixture. Never run it beside Qwen.
10. **Guest:** first use a disposable guest/runtime copy. Record original hashes,
    push a DLL/program/Unix module and compare local/remote/readback MD5. Repeat
    and confirm the first backups remain unchanged. Check missing targets,
    interrupted SSH, invalid manifest paths and changed payload failure without
    truncating destinations. Restore backups offline and compare original hashes.
    Repeat from Debian with copied artifacts/state and WEXDEV_SSH wrapper.
    Coordinate exclusive guest use. Schedule desktop/fresh-prefix visible and
    presented-pixel checks separately.

## Acceptance results (2026-11-05)

Executed in the `wexbuild` WSL distro (owner released it exclusively for this
acceptance; no other agent in use). BUILD `/root/WE-prefetch/build/wow64`, clone
`/root/WE-prefetch/wine` (513bd8e80dd), source `wine/` (14e252dc, development).
Invoked as `wsl -d wexbuild --exec bash /mnt/d/.../scripts/wexdev.sh <cmd>`.
Guest push (step 10) NOT tested: no VM available.

### Fixes made during acceptance

1. `scripts/wexdev-service.c`: `#define UNICODE` / `#define _UNICODE` wrapped in
   `#ifndef` guards. Both MinGW arches previously warned `"UNICODE" redefined`;
   now 0 warnings.
2. `scripts/wexdev.sh` `stop_prefix()`: both `wineserver -k` lines changed
   `|| rc=1` to `|| :`. `-k` exits nonzero when no server is running, which is a
   clean state, not a failure; only a `-w` timeout (stuck server) is an error.
   This made `prefix`/`run`/`test` report "prefix server cleanup failed" on a
   fresh prefix.
3. `scripts/wexdev.sh` `prefix()`: added a gate-validation loop (valid name +
   DWORD value) before `display`/wineboot, so an invalid gate no longer leaves an
   orphaned prefix.
4. `scripts/wexdev.sh` `fixture_paths()`: WoW64 has no 32-bit wineboot (BUILD
   ships only `x86_64` wineboot) and `services.exe` lives only in system32, so the
   check now tests each directory against the images it actually holds
   (system32: wineboot/winedevice/ntdll/services; syswow64: winedevice/ntdll).
   It also tests existence before `readlink -f`, which succeeds even when the
   final component is absent (the old `|| fail "missing"` never fired). This made
   `sysrole up` fail with "syswow64/wineboot.exe resolves outside BUILD".

### 1. Build (targeted rebuild)

Changed a harmless comment in `wine/dlls/advapi32/advapi.c` and
`wine/dlls/advapi32/tests/registry.c`, then:

- `build` (no paths): printed NOTE lines for `configure`, `configure.ac`,
  `include/Makefile.in` and fell back to full make (by design). Root cause of the
  drift: source configure is Autoconf 2.73, clone is 2.72, so auto-detect always
  full-makes in this environment.
- `build dlls/advapi32` (explicit): `OK: PASS build: dlls/advapi32`. build.log
  shows targeted compile+link for both arches; all four advapi32 artifact hashes
  changed; `last-build.files` = exactly the six advapi32 files. Source reverted
  (git checkout, clean).

### 2. Prefix + run + test

- `prefix acc-02 --gate Composition=1 --gate DwmService=1` → `OK: prefix acc-02
  created`; both gates present as `dword:00000001` in `user.reg`.
- `run acc-02 /tmp/probe64.exe` native OK; `run acc-02 /tmp/probe64.exe fail7`
  → `FAIL` rc=1 (correct); `run acc-02 --arch both '/tmp/probe{arch}.exe'` → both
  arches OK.
- `test advapi32 service` → x86_64: 759 tests, 0 failures (25 todo); i386: 777
  tests, 0 failures (27 todo); `OK: test advapi32: both architectures passed`.
- Error cases all `FAIL` rc=1: unknown command, `--arch 16`, name with a space,
  invalid gate, duplicate prefix name. No leftover wineservers.

### 3. Lock behaviour

Background `run acc-02 /tmp/sleepprobe64.exe 10`; while it held the lock,
`prefix test-lock` → rc=1 with
`FAIL: harness locked by pid=118660 user=root host=DEEPTHOUGHT command=run acc-02 /tmp/sleepprobe64.exe 10 started=...`.
First invocation rc=0. The second names the holder.

### 4. Sysrole (foreign state removal + fresh up)

The distro held a previous agent's hand-made system role:
`/etc/wineexplorer/sysrun.conf` (md5 10666016f8012e305a9aaec9aac33d49),
`/usr/local/libexec/wineexplorer/wex-sysrun` (3f52b47346fb82b5224046f240bcf768),
`/usr/local/bin/sysrun_asgroup`, `/var/lib/wineexplorer/{machine,backup}`, user
`wexadm` (sudo), group `wexsys`, four orphaned `winedbg --auto` procs, and
`/root/probe-pfx`. All four sysrole commands correctly refused it
(`status`/`down`/`reset`: "foreign or incomplete fixture: /var/lib/wexdev-sys";
`up`: "/etc/wineexplorer already exists; leave Qwen/wexadm state intact").

`/etc/wineexplorer` also held two pre-existing WineExplorer project files that
predate the foreign role (2026-10-17): `component-licenses/corefonts.txt` and
`setup/stage` (`oobe`). These were backed up to
`/root/wexdev-acceptance-backup/etc-wineexplorer-foreign/` and are NOT part of the
foreign role.

As owner (user-authorized maintenance window): killed the four winedbg procs,
backed up `/etc/wineexplorer`, removed the foreign files/dirs, `userdel -r
wexadm`, `groupdel wexsys`. Verified all paths gone and user/group deleted.

- `sysrole up` → first attempt FAILED at `fixture_paths` (see fix 4). After the
  fix and a `sysrole reset` of the partial fixture: `OK: sysrole ready; Helper
  enabled; system images resolve into BUILD`.
- `sysrole status` → accurate: conf `group=wexdevsys`, loader/ntdll/wineserver all
  under BUILD, `machine_root=/var/lib/wexdev-sys/machine`; `OK: sysrole
  provisioned; servers are started per invocation, not left running`.
- Fixture verified: launcher `2755 root:wexdevsys`; `wexdev` in sudo, `wexdevstd`
  not; neither in `wexdevsys`; probes `probe{32,64}.exe` + `rolesprobe{32,64}.exe`
  compiled; `asgroup.sg` `2755 root:wexdevsys`; `"Helper"="C:\windows\system32\consent.exe"`
  in system.reg; `owner` = BUILD.

### 5. Svc (service token as each account)

`svc <name> /var/lib/wexdev-sys/probes/rolesprobe{64,32}.exe token [--account A]
[--arch N]`. Reported `user=` SID:

- LocalSystem, arch 64: `user=S-1-5-18`, owner `S-1-5-32-544`, elevation=1,
  integrity=16384, `token=PASS`, service exit=0.
- LocalService, arch 64: `user=S-1-5-19`.
- NetworkService, arch 64: `user=S-1-5-20`.
- LocalSystem, arch 32: `user=S-1-5-18`.

All `OK: service <name> completed as <account> (arch=N); service removed on exit`.

### 6. test_sysrun_server.sh regression

Run after `sysrole reset` (it needs `/etc/wineexplorer` and
`/usr/local/libexec/wineexplorer` free; it uses its own root
`/var/lib/wexsys-c2` and users wexadm/wexstd/wexcar):

```
WINE_BUILD_DIR=/root/WE-prefetch/build/wow64 timeout --kill-after=30 600 \
    bash scripts/test_sysrun_server.sh
```

Result: **214 PASS, 0 FAIL** before the 10-minute limit (exit 124). The suite is
long; it reached the secure-dialog policy checks (`sd_policy_*`) with no failures,
so the refactor into `scripts/lib/sysrun-fixture.sh` did not break it. Note: the
`timeout` SIGTERM did not run the script's `trap cleanup EXIT`, so its fixture
(`/var/lib/wexsys-c2`, `/etc/wineexplorer`, `LIBEXEC`, users, group) had to be
removed manually afterwards (replicating the cleanup function).

### 7. Guest push

NOT tested: no VM available in this environment.

### Environment notes

- Source `wine/` configure is Autoconf 2.73, the clone's is 2.72, so `build` with
  no paths always prints the shared-header NOTE and falls back to full make in
  this environment; explicit paths (e.g. `build dlls/advapi32`) still target
  correctly.
- The acceptance was driven over a Windows→WSL transport that strips backslashes
  and truncates multi-line output; that is a property of the driving harness, not
  of wexdev.sh. All wexdev.sh invocations used the on-disk script (backslashes
  intact) and passed.
- Final distro state: foreign role removed; wexdev fixture up (`sysrole up`
  re-run after the regression test, `sysrole status` accurate); test fixture
  cleaned up. Project files preserved at
  `/root/wexdev-acceptance-backup/etc-wineexplorer-foreign/`.
