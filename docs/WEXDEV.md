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
```

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
