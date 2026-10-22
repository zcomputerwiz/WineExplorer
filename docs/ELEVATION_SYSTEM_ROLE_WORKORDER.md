<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work order: the system role and elevation as a Unix group (`wexsys`)

Date: 2026-10-22. Status: phase 0 done, design calls decided; phases A and B done, C1, C2 and C3 done (see Results); the launcher is still off by default (constraint g: the user decides), F (documentation) is mostly in the commits. Part of `ELEVATION_BOUNDARY_PLAN.md`; implements step 2b
item 5 and decision D2, and gives the broker (step 4) and polkit consent (step 6) their Unix anchor.

## Why

D2 gives the system role only to a `services.exe` the user's processes cannot replace. The machine root is
`root:sudo`, mode `2775` with a default ACL `g:sudo:rwX` (`setup-machine-root.sh`), and a desktop user is in
`sudo`. Wine's file access is decided by the Unix UID and groups, not by tokens, so that user's limited processes
can rewrite `services.exe`, `system.reg` and everything else in the machine root. Elevated processes would run
with the very same Unix rights, so elevation could not add any.

## Model (decided by the user, 2026-10-22)

- The machine root is `root:wexsys`. `setup-machine-root.sh` creates the `wexsys` group and does not add desktop
  users to it, sudoers included. Existing installs migrate from `sudo` to `wexsys`.
- Elevated and system-role processes run as the same user with `wexsys` as their effective group, granted by a
  small setgid-`wexsys` launcher. The launcher clears the environment, accepts only fixed programs from the
  machine root, and refuses to run if any of them is writable without `wexsys`.
- Limited processes therefore cannot write the machine root, even with raw Linux syscalls; and a setgid-started
  process is not dumpable, so the user cannot ptrace it.
- The server gives the system role (SYSTEM token, S-1-5-18) to a process only when that process runs with
  `wexsys` and its image is the machine root's `services.exe` (later also the broker). Service tokens
  (LocalSystem, LocalService, NetworkService by `ObjectName`) need an enabled `SeTcbPrivilege`, so only the
  system-role `services.exe` can make them.

Still out of scope (T5): a limited process can kill the user's `wineserver` and start its own. It then controls
only that user's session state; it still cannot write the machine root.

## Phase 0: facts (Sonnet agent, read-only, report before any design call)

Answer with `file:line`, from `/home/user/wine-dev` and `/home/user/WineExplorer`:

1. How `services.exe`, `wineboot --init` and `wineboot -u` are started today (ntdll / kernelbase / loader / the
   server), from which process, with which environment.
2. How `wineserver` is started (who forks/execs it, which environment and paths it trusts: `WINESERVER`,
   `WINEPREFIX`, the socket directory under `/tmp/.wine-<uid>`), and whether it can read a client's effective
   GID: `SO_PEERCRED` on the request socket (`server/request.c`), or `/proc/<pid>/status`.
3. Which environment variables and files the Wine loader trusts at startup (`WINEDLLPATH`, `WINEDLLOVERRIDES`,
   `WINEPREFIX`, `WINELOADER`, `LD_*`, `WINEDEBUG`, the prefix's `dosdevices`, `user.reg`): the list the
   launcher must clear or pin.
4. Linux rules for a setgid exec followed by exec of a plain binary: is the process still not dumpable, is
   `AT_SECURE` set for the second exec, and what `setresgid` choices keep both (cite the kernel source:
   `begin_new_exec`, `commoncap.c`).
5. The S3 registry design (`docs/PREFIX_SPLIT.md`, phase S3, `server/registry.c` save path): who saves
   `system.reg` today, how sudo membership decides it, and what changes when no desktop user is in the owning
   group. In particular: once a desktop admin is not in `wexsys`, can their sessions still persist `HKLM` at
   all, and which process would have to do it.
6. How Wine gives every user a linked full token today (`token_create_admin`, `create_linked_token`), whatever
   their Unix groups. On Windows a standard user has no linked token; elevation needs an administrator's
   credentials. What would decide "this Unix user is an administrator" (sudo or a `wexadmin` group).
7. What `scripts/test_b3_machine_root.sh`, `installer/tests/test_s2_acceptance.sh`,
   `installer/tests/test_s3_acceptance.sh` and `installer/tests/check-wow64.sh` assume about `sudo` ownership,
   so the migration knows what to update.

## Design calls after phase 0 (main session, then the user where marked)

- S1. Who runs `wineserver`: unchanged (the user, no `wexsys`; `system.reg` then saved by a system-role helper
  or not at all), or started through the launcher with `wexsys` (it saves `HKLM`, and enforcement decides which
  writes reach it). **User decision.**
- S2. Which programs the launcher accepts: `services.exe` only now, the broker in step 4, `wineboot` for the
  prefix's boot-time `HKLM` work (`ActiveComputerName`, `Session Manager\Environment`), so that the two temporary
  Users-writable registry grants can be removed.
- S3. Administrator identity: which Unix users get a linked full token at all (sudo, or a new `wexadmin`
  group), and what standard users get (no linked token, as on Windows). **User decision.**
- S4. Migration of existing installs from `sudo` to `wexsys`, and what desktop admins lose until the broker
  exists (installing into the shared `Program Files`).

### Decisions (2026-10-22)

- **S1 (user):** for now, `wineserver` is started through the launcher with effective group `wexsys`, and the
  two `access()` checks of the registry save path (`server/registry.c`) use `faccessat(..., AT_EACCESS)`. The
  registry checks decide which writes are made; the user cannot ptrace the server. Direction for later (user):
  an always-running system service, so that user sessions are fully separate; this phase must not make that
  harder (keep the system role tied to the peer credentials and image path, not to "same server").
- **S2:** the launcher accepts `wineserver`, `services.exe` (via `wineboot --init`, which writes the boot-time
  `HKLM` state) and nothing else now; the broker is added in step 4. With those in place the temporary
  `ServiceCurrent` and `ActiveComputerName` grants are removed.
- **S3 (user):** members of the Unix `sudo` group are administrators (override like `WEX_MACHINE_GROUP`). The
  decision is made when the user's session starts (the root token is created), from the group database
  (`getgrouplist`), so a change of `sudo` membership takes effect at the next logon, as on Windows. An
  administrator gets today's limited token with a linked full token; anyone else gets a standard-user token: no
  Administrators group, Medium integrity, `TokenElevationTypeDefault`, no linked token. Wine's view of the local
  Administrators group (`NetLocalGroupGetMembers`, `net localgroup Administrators`, the local users and groups
  UI) lists exactly the `sudo` members, refreshed the same way at logon.
- **Administrators group view (user, 2026-10-22):** `NetLocalGroupGetMembers`, `NetLocalGroupEnum` and
  `net localgroup` ask the server, the one authoritative source that holds the membership read at logon and refreshes
  it at logon and logoff; implemented after the system role.
- **S4:** `setup-machine-root.sh --update` migrates `root:sudo` to `root:wexsys`. Until the broker exists,
  administrators cannot install into the shared `Program Files`; the S2/S3/S4 acceptance tests change from
  direct writes by alice to writes through the launcher's test hook or are marked as waiting for step 4.

### Constraints for phase C (review of phases A and B; not implemented yet)

The launcher alone is not safe to install setgid: a process with egid `wexsys` that consumes the user's prefix
(`dosdevices`, `user.reg` DLL overrides, the `system.reg` symlink) can be made to run user code or overwrite machine
files. Until these hold, `setup-machine-root.sh` installs the launcher only with `WEX_SYSRUN_ENABLE=1`.

a. A process with egid `wexsys` never reads the user's prefix. System-role clients run with `WINEPREFIX` = the
   machine root (root-owned `dosdevices` and drive mapping) and connect to the user's server through a socket that
   the launcher connected and passed as `WINESERVERSOCKET`.
b. Before passing it, the launcher checks with `SO_PEERCRED` that the server process has egid `wexsys` (an
   unprivileged replacement server is refused); ntdll does the same check for any client whose own egid is `wexsys`.
   A user can kill the server and start an unprivileged one first, so a system-role client must not trust a server
   without this check.
c. The `wexsys` wineserver loads and saves the machine hives (`system.reg`, `userdef.reg`) from the fixed machine
   root path in the root-owned configuration, never through the prefix's symlinks (`O_NOFOLLOW` on the final
   components, `fstat` checks), and uses `faccessat(AT_EACCESS)` only for those.
d. `HKU\S-1-5-18` and the other well-known service profiles are not part of the user's Users-writable user tree:
   `key_in_user_tree` must match only the token user's own SID subtree.
e. Known gap: no UIPI or Session 0 isolation, so window messages from limited processes reach system-role windows;
   services must not create visible windows.
f. Wine's server opens files for its clients (`create_file` and `open_fd` in `server/fd.c`, and the requests that
   rename, link, unlink or delete on close). A `wineserver` that runs with effective group `wexsys` would open
   machine-root files for write on behalf of any limited client: a confused deputy. So the `wexsys` server runs with
   effective gid = the user's real gid and keeps `wexsys` only as its saved gid (`setegid` back and forth is allowed
   because the saved gid is `wexsys`). It raises the effective gid only (1) around the machine hive load and save,
   and (2) for Unix paths requested by a client that itself runs with effective gid `wexsys` (`SO_PEERCRED` of that
   client's connection, recorded at connect time, never the client-sent `unix_pid`), and drops it again straight
   after. The kernel rule to check and prove in a test: a process stays non-dumpable (and not ptraceable) across
   these `setegid` calls, because `commit_creds` sets the dumpable flag to `fs.suid_dumpable` (normally 0) when the
   effective gid changes, and ptrace needs real, effective and saved ids to match the caller's. Because the server's
   effective gid is then the user's, "the peer has `wexsys`" is checked on the saved gid from
   `/proc/<pid>/status`, not on `SO_PEERCRED`.

g. Review of C2 (main session; met by C3, probes in `scripts/test_sysrun_server.sh` section 8): until C3, the processes the launcher starts (`wineboot --init`, `services.exe` and
   the services it starts) run on the user's own token, so a limited process can open them through Win32
   (`OpenProcess` with full access) and run code in them without ptrace: `CreateRemoteThread`,
   `QueueUserAPC`, `WriteProcessMemory` and handle duplication go through the server, not the kernel. That code
   then has effective group `wexsys`. So the launcher must stay disabled (`WEX_SYSRUN_ENABLE` test-only) until C3
   gives every process with effective group `wexsys` the SYSTEM token (or its service token) with a default DACL
   and process descriptor that deny the user, and a probe proves that a limited and a standard-user process both
   get `ERROR_ACCESS_DENIED` for `OpenProcess(PROCESS_VM_WRITE | PROCESS_CREATE_THREAD | PROCESS_DUP_HANDLE)`
   on them, and cannot inject an APC or duplicate a handle into them. The same holds for the `wexsys` server's
   in-process objects the user's token can name. An elevated administrator with `SeDebugPrivilege` may open them,
   as on Windows (administrator to SYSTEM is not a boundary there either).

## Phases after the design calls

A. Machine root on `wexsys`: `setup-machine-root.sh` (create the group, ownership, mode, default ACLs, `--update`
   migrates), and the tests from phase 0 item 7.
B. The launcher (`wex-sysrun`, C, GPL header, a few hundred lines at most): fixed paths, cleared environment,
   writability check of every file it trusts, fixed argument lists; installed setgid `wexsys` by the setup.
   Unit tests for each refusal.
C. Server: system-role recognition (effective GID `wexsys` plus image path), a SYSTEM token (S-1-5-18, all
   privileges, Administrators enabled, System integrity), and a `SeTcbPrivilege`-gated request that makes
   LocalSystem, LocalService (S-1-5-19) and NetworkService (S-1-5-20) primary tokens.
D. `services.exe`: start through the launcher when enforced and a machine root exists; start each service with
   `CreateProcessAsUserW` and the token its `ObjectName` names; `winedevice` as LocalSystem. Read and write
   `Services\<name>\Security` so per-service descriptors persist. Remove the temporary `ServiceCurrent` and
   `ActiveComputerName` grants once their writers run with the system role.
E. Probes: step 2b item 5 (SYSTEM, LocalService, NetworkService tokens; a user copy of `services.exe` gets no
   role; limited process cannot create or reconfigure a service; the two grants are gone); plus a raw-syscall
   probe showing a limited process cannot write the machine root, and that the system-role process is not
   ptraceable by the user.
F. Docs: `STOCK_CHANGES.md`, `PREFIX_SPLIT.md` (ownership change), the plan's status.

## Acceptance

The registry enforcement work order's acceptance, unchanged, plus phase E's probes on both architectures, plus the
S2/S3 acceptance tests updated for `wexsys`.

## How the work is split

Phase 0 and each phase: a Sonnet agent. Design calls and review: the main session; S1 and S3 go to the user.

## Results

### Results, phase 0 (facts)

Date: 2026-10-22. Wine paths are relative to `/home/user/wine-dev` (working tree, which has uncommitted changes
outside the files cited), WineExplorer paths to the repository root. Kernel sources are Linux v6.18 (the running
kernel is 6.18.44), saved under `/tmp/wexout/kernel` (not in the repository) and cited as `fs/exec.c:N` etc.

#### 1. How `services.exe`, `wineboot --init` and `wineboot -u` are started

- A program started from the shell has no startup info (`startup_info_size == 0`, `dlls/ntdll/unix/env.c:2010`), so
  its ntdll calls `build_initial_params()` (`env.c:1877`). That builds the process environment from the Unix
  `environ` (`get_initial_environment`, `env.c:871-910`), adds the dynamic and registry environment
  (`env.c:1895`) and calls `run_wineboot()` (`env.c:1900`). Every first client of a prefix does this, whatever
  the program is, including `wine wineboot -u`.
- `run_wineboot()` (`env.c:1541-1610`) creates the event `\KernelObjects\__wineboot_event` (`env.c:1563`; if it
  exists it only waits), then starts `C:\windows\system32\wineboot.exe --init` with `NtCreateUserProcess`
  (`env.c:1592`), with the environment just built (`env.c:1574`) and the caller's token (no parent: the server gives
  the first process `token_create_admin(...Limited)`, `server/process.c:672`; a child gets a duplicate of the
  parent's token, `server/process.c:692`). It waits up to 5 minutes (`env.c:1608`).
- `wineboot.exe` (`programs/wineboot/wineboot.c`, main body `1855-1960`) parses `-i/--init`, `-u/--update`, `-k`, `-r`,
  `-s` (`wineboot.c:1901-1922`). A WoW64 `wineboot` first restarts the native-machine one (`wineboot.c:1868-1888`).
  `services.exe` is started only when `init || (kill && !restart)` (`wineboot.c:1955-1958`), by
  `start_services_process()` (`wineboot.c:1465`): `CreateProcessW` of `C:\windows\system32\services.exe` with a NULL
  environment (inherited from wineboot), `DETACHED_PROCESS`, native machine type (`create_native_process`,
  `wineboot.c:1446-1463`); it then waits for the `SVCCTL_STARTED_EVENT`. The prefix update (`update_wineprefix`) runs
  for `init || update` (`wineboot.c:1960`); `-u` alone does not start `services.exe`.
- So `wineboot -u` started by the user is: user process (the first client) -> `wineboot --init` child (started
  from ntdll, runs `services.exe`) -> the `-u` work in the user's own `wineboot.exe`.
- Unix side of every `NtCreateUserProcess`: `spawn_process()` (`dlls/ntdll/unix/process.c:402-446`) forks twice and
  `exec_wineloader()` (`dlls/ntdll/unix/loader.c:478-501`) execs the loader (`loader_exec`, `loader.c:458-472`) with
  the parent's whole Unix environment plus `WINEPRELOADRESERVE` and `WINESERVERSOCKET`. The child has the parent's
  UID/GIDs.
- Implication: a launcher cannot just exec `services.exe`; the first client starts `wineboot --init` itself, so
  the launcher must be what runs `wineboot --init` (or `services.exe`) with the machine-root path, not the
  `run_wineboot` path in `env.c`.

#### 2. How `wineserver` is started; what it trusts; can it read a client's GID

- Started by the first client, not by a daemon: `server_connect()` (`dlls/ntdll/unix/server.c:1402`) calls
  `start_server()` (`loader.c:534-553`) when the server dir or socket is missing (`server.c:1411-1414`, `1426-1435`).
  `exec_wineserver()` (`loader.c:507-526`) tries, in order: the 64-bit build dir server (`loader.c:511-515`),
  `<bin_dir>/wineserver` (`517`), the `$WINESERVER` path (`518`), every directory in `$PATH` (`520-524`), `BINDIR`
  (`525`). `build_path_and_exec()` (`loader.c:248-256`) is `posix_spawn` with the full `environ`. The server then forks
  and calls `setsid` (`server/request.c:840-860`) and runs as the same UID and GIDs as that client.
- Trusted by the server: `$WINEPREFIX` or `$HOME/.wine` (`server/request.c:579-603`; must be absolute, owned by
  `getuid()`, `request.c:617-619`), socket dir `/tmp/.wine-<uid>/server-<dev>-<ino>` (`request.c:627-639`; created
  `0700`, owned by `getuid()`, `request.c:560-574`; the client repeats the checks, `server.c:1409-1411`, `1436-1440`),
  socket `chmod 0600` (`request.c:811`), `$WINE_MACHINE_ROOT` (`server/registry.c:2360`), `$WINEARCH`
  (`registry.c:2532`). The dir depends only on UID and the prefix inode, never on GID.
- The server does not read a client's credentials. `master_socket_poll_event()` (`request.c:522-545`) is a plain
  `accept()`. No `SO_PEERCRED`, `getpeereid` or `ucred` in `server/` (grep). `SO_PASSCRED`/`SCM_CREDENTIALS` is used
  only the other way, so the client learns the server pid (`dlls/ntdll/unix/server.c:1015-1019`, `1453-1457`, `1651-1656`).
  The server's `unix_pid` for a process or thread is sent by the client (`server/protocol.def:1195`, `1367`), so it
  cannot be trusted for `/proc/<pid>/status`. The only `/proc/<pid>/status` read is `get_process_vm_counters`
  (`server/process.c:1570`), for memory counters.
- Consequence for the model: every process of one UID with one prefix connects to the same `wineserver`, whatever its
  GID. A `wexsys` launcher child therefore talks to the user's existing server; if it is the first client, the
  server it starts would inherit egid `wexsys`.
- Implication: to decide the role the server must obtain the peer's egid itself (`getsockopt(SO_PEERCRED)` on the
  accepted fd gives pid, uid, gid at `connect()`; `/proc/<pid>` by that pid is racy only after exit) and must not
  use the client-sent `unix_pid`.

#### 3. What the loader trusts at startup (the list the launcher must clear or pin)

Read by the Unix side of ntdll or the server (every one comes from the process environment unless noted):

- `WINEPREFIX` (`dlls/ntdll/unix/loader.c:366`, `server/request.c:579`) and, if unset, `HOME` and `USER`
  (`loader.c:342-343`): decide the config dir, hence `dosdevices`, `drive_c`, `system.reg`, `userdef.reg`, `user.reg`
  (loaded relative to it, `server/registry.c:2530`, `2554`, `2565`; `dosdevices/z:` points to `/` and `c:` to
  `../drive_c`, created at `dlls/ntdll/unix/server.c:1350-1354`; the config dir is only checked for owner
  `getuid()`, `server.c:1346`). Pin to the machine-owned prefix.
- `WINESERVER` (`loader.c:518`) and `PATH` (`loader.c:520`): which `wineserver` binary is run if none is running.
- `WINEDLLPATH` (`loader.c:297`): extra directories searched for Unix and PE modules (`set_dll_path`).
- `WINEDLLOVERRIDES` (`dlls/ntdll/unix/loadorder.c:286`): native or builtin choice per DLL.
- `WINEDEBUG` (`dlls/ntdll/unix/debug.c:196`), `WINEARCH` (`server.c:1609`, `1626`, `registry.c:2532`),
  `WINELOADERNOEXEC` (`loader.c:461`, `2108-2109`), `WINEPRELOADRESERVE` (`dlls/ntdll/unix/virtual.c:3631`),
  `WINESERVERSOCKET` (`server.c:1610`: an fd number taken from the environment and used as the server socket if set;
  a launcher must clear it), `WINE_MACHINE_ROOT` (`server/registry.c:2360`).
- `TZ`, `TZDIR`, `LC_ALL` (`dlls/ntdll/unix/system.c:3022`, `3069`; `env.c:647`), `PWD` (`env.c:1414`): behaviour only.
- Not read in this tree: `WINELOADER` (the loader path comes from `dladdr` on `ntdll.so`, `loader.c` `init_paths`,
  and is exported to the child as `WINELOADER`, `env.c:1043`). `WINEBUILDDIR`, `WINESYSTEMDLLPATH`, `WINEDATADIR`,
  `WINEHOMEDIR`, `WINECONFIGDIR` are Wine-generated Windows-side variables (`env.c:1020-1052`), but `WINEBUILDDIR`
  and `WINESYSTEMDLLPATH` are read back from the Windows environment (`dlls/ntdll/loader.c:3167`, `4559`), so they
  must not come from the caller either.
- `WINE*` variables from the Unix environment become Windows variables (`env.c:888-896`; `WINE_HOST_` prefix for
  `PATH`, `HOME`, `TEMP`, `XDG_*`, `env.c:332-341`, `899-904`). The Windows environment also gets `HKLM\...\Session Manager\Environment`
  and `HKCU\Environment` (`add_registry_environment`, `env.c:1204`, called at `1895`), which is a trust path from `system.reg`/`user.reg`.
- `LD_*`: nothing in Wine reads them, but the dynamic linker honours them. For a setgid exec the kernel sets
  `AT_SECURE` (item 4) and glibc's `ld.so` then ignores the unsecure ones (`LD_PRELOAD`, `LD_LIBRARY_PATH`, ...) for
  that image; after a later exec of a plain binary `AT_SECURE` stays 1 only while rgid != egid (item 4). glibc's
  `rtld` source was not fetched here.
- Implication: the launcher must `execve` with a built environment (`WINEPREFIX`, `HOME`, `USER`, `PATH`, and
  nothing else starting `WINE` or `LD_`), pin `WINESERVER`/loader via absolute paths in the machine root, and not
  trust `WINEPREFIX` from the caller unless it is checked.

#### 4. Linux: setgid exec, then exec of a plain binary

Verified on this kernel (6.18.44, `suid_dumpable = 0`) with a setgid test binary (`first`, mode `2755`, group
`wexsgidtest`) run as an ordinary user who is not in that group, which prints `getresgid`, `/proc/self/status`
`Groups`, `prctl(PR_GET_DUMPABLE)` and `AT_SECURE`, optionally calls `setresgid`, then execs a plain binary
(`second`) that prints the same. The test group and binaries were removed afterwards.

| step | rgid | egid | sgid | dumpable | AT_SECURE | supplementary Groups |
|---|---|---|---|---|---|---|
| `first` (setgid exec) | user | wexsys | wexsys | 0 | 1 | unchanged (wexsys not added) |
| `second` after no change | user | wexsys | wexsys | 0 | 1 | unchanged |
| `second` after `setresgid(r,e,e)` (no change) | user | wexsys | wexsys | 0 | 1 | unchanged |
| `second` after `setresgid(e,e,e)` | wexsys | wexsys | wexsys | **1** | **0** | unchanged |

Kernel source (v6.18):

- The setgid bit is applied in `bprm_fill_uid()` (`fs/exec.c:1534-1586`): `bprm->cred->egid = file gid` when
  `S_ISGID` and `S_IXGRP` are set (`exec.c:1580-1583`). It is skipped on a `nosuid` mount (`exec.c:1541-1542`) and
  under `NO_NEW_PRIVS` (`exec.c:1544-1545`). `begin_new_exec()` (`exec.c:1099`) calls it through
  `bprm_creds_from_file()` (`exec.c:1105`, `1589-1596`).
- `cap_bprm_creds_from_file()` (`security/commoncap.c:909`): `id_changed = !uid_eq(euid) || !in_group_p(new->egid)`
  (`commoncap.c:938`); under `ptrace` or without `CAP_SETUID` under no_new_privs it resets `egid = gid`
  (`commoncap.c:940-948`); `sgid = fsgid = egid` (`commoncap.c:953-954`). `bprm->secureexec = 1` when
  `id_changed || !gid_eq(new->egid, old->gid) || ...` (`commoncap.c:990-996`). So every exec while egid differs from the
  real gid is a secure exec. The ELF loader turns that into `AT_SECURE` (`fs/binfmt_elf.c:264`).
- Dumpability: `begin_new_exec()` sets it from the creds of the process doing the exec: not
  `uid_eq(euid, uid) && gid_eq(egid, gid)` -> `suid_dumpable` (0), else `SUID_DUMP_USER` (`exec.c:1214-1223`; the
  comment there says it should test `secureexec`). Then `commit_creds()` sets `suid_dumpable` again whenever euid,
  egid, fsuid, fsgid or capabilities changed (`kernel/cred.c:405-412`), which is what makes the first (setgid) exec
  non-dumpable. The dumpable flag lives in the `mm`, so it is recomputed on every exec.
- Consequence: a plain exec after a setgid exec stays non-dumpable and secure only while rgid != egid. `setresgid(e,e,e)`
  (or `setgid(e)`) before the second exec makes the new image dumpable with `AT_SECURE` 0 (last row). Keep rgid (and
  the supplementary groups) as they are and egid = sgid = `wexsys`: do not call `setresgid` to merge them.
- ptrace: `__ptrace_may_access()` (`kernel/ptrace.c:276-348`) allows an ordinary caller only if its uid and gid
  equal all of the target's euid/suid/uid and egid/sgid/gid (`ptrace.c:317-322`); with egid `wexsys` != gid the
  caller needs `CAP_SYS_PTRACE` (`ptrace.c:324`). Independently a target whose `mm` is not `SUID_DUMP_USER` needs
  `CAP_SYS_PTRACE` over `mm->user_ns` (`ptrace.c:340-343`). `/proc/<pid>/mem` uses the same check. Two independent
  barriers, both present only while rgid != egid.
- Gotchas found: (a) `access(2)` checks the real IDs (see item 5); the effective-ID check needs `faccessat(AT_EACCESS)`
  or just the open; (b) `execve` of the launcher under `NO_NEW_PRIVS` (systemd `NoNewPrivileges=`, some sandboxes)
  or from a `nosuid` mount silently ignores the setgid bit; (c) `wexsys` is the effective group only: it is not in
  `Groups:`, so `initgroups`/`setgroups` by the program would also drop nothing and add nothing.
- Implication: the launcher should `execve` with egid `wexsys` and leave rgid alone; the later plain exec of
  `wineserver`/`services.exe`/the loader stays non-dumpable and not ptraceable by the user.

#### 5. S3 registry save path

- Who saves `system.reg` today: the user's own `wineserver`, on its 30 s timer (`server/registry.c:132`, `periodic_save`
  `2995-3005`) and at exit (`flush_registry`, `3015`), for the three branches `system.reg`, `userdef.reg`, `user.reg`
  (`load_init_registry_from_file`, `2386-2420`, `is_shared_machine` when the file is a symlink into
  `$WINE_MACHINE_ROOT`, `2354-2384`). No separate process saves it.
- How group membership decides it: only by Unix permission, not by a token or a group lookup. `save_branch()`
  (`registry.c:2753`) resolves the symlink (`2775-2788`), then `access(target_path, W_OK)` (`2813`) and
  `access(target_dir, W_OK)` (`2825`); on `EACCES`/`EROFS` it drops the dirty state and returns success
  (`2814-2822`, `2826-2834`) without writing. Otherwise it takes `flock(LOCK_EX)` on `<dir>/.reg.lock` (`2838-2857`),
  merges with the on-disk file, writes `reg<pid><n>.tmp` in the machine dir (`2909`) and `rename()`s it over the target
  (`2956`). Limited sessions keep `HKLM` writes in memory only (`docs/PREFIX_SPLIT.md`, "Phase S3 - Implementation",
  items 1-4; `is_per_session_key` is skipped for `geteuid() == 0`, `registry.c:316`).
  Today a desktop admin in `sudo` passes both `access()` checks (machine root `root:sudo` `2775`,
  `scripts/setup-machine-root.sh:263-288`).
- When nobody is in the owning group: the user's `wineserver` fails both `access()` checks, so no session can persist
  `HKLM` or `HKU\.DEFAULT` at all; changes (installers' COM and Uninstall entries, `ActiveComputerName`, ...) vanish at
  logoff. Wine's own boot-time writes would be lost the same way, except for what the two temporary Users-writable
  grants in `registry.c:920-935` allow in memory.
- Whether a `wexsys`-egid process could save: `rename`/`open` would succeed (the new egid is the fsgid), but
  `save_branch()` would still skip it, because `access()` uses the real gid (rgid = user): verified with the item 4
  setup, a setgid process whose real group lacks write got `access(dir, W_OK) = -1 EACCES`,
  `faccessat(..., AT_EACCESS) = 0`, and `open(dir/f, O_CREAT|O_WRONLY) = 3`. So a `wexsys` `wineserver` needs
  `access` changed to `faccessat(..., AT_EACCESS)` (two lines, `registry.c:2813`, `2825`), or the decision removed.
- Which process would have to do it: either (S1 a) a `wineserver` started through the launcher with egid `wexsys`
  (it also holds `HKLM` in memory for the user's session, so enforcement must decide which writes reach the dirty
  tree), or (S1 b) a system-role helper outside the user's server that takes `HKLM` deltas by an RPC; the present
  `save_branch` merge-on-save code (`registry.c:2860-2990`) is reusable in both.
- Implication: with the user's server left as is, `HKLM` is never saved; either the server is started with
  `wexsys` (and `access` fixed) or a helper does it.

#### 6. Linked full token today, and who is an administrator

- Every user's first process gets `token_create_admin(TRUE, -1, TokenElevationTypeLimited, ...)`
  (`server/process.c:672`), filtered when an elevation helper is configured (`process.c:674`,
  `token_make_filtered`, `server/token.c:1671`). `token_create_admin()` (`token.c:817-883`) takes only `getuid()`
  (`token.c:824`) and always puts `BUILTIN\Administrators` and `BUILTIN\Users` in the groups (`token.c:859-860`)
  and the full administrator privilege list (`SeTcbPrivilege`, `SeDebugPrivilege`, ... disabled except a few,
  `token.c:826-847`); the comment at `token.c:850` says non-builtin groups are the job of a client program. The
  server reads no Unix group anywhere (no `getgroups`/`getgrnam`/`getgid` in `server/`).
- `create_linked_token` (`token.c:2086-2118`) flips Limited <-> Full for any token it is given, with no check of who
  the user is, and returns an identification-level token (`token.c:2105-2108`); `grant_process_admin_token`
  (`server/process.c:1816-1840`) gives any caller a primary admin token with Default elevation unless elevation is
  enforced, then only a caller with `SeTcbPrivilege` (`process.c:1822`).
- So Wine has no notion of a standard user: Unix-wise limited users (not in `sudo`) get the same filtered Administrators
  token as admins. The write protection is Unix (item 5), not the token.
- Client side: the consent program asks consent-only (`src/programs/consent/consent.c:282-306`; credentials mode
  returns `ERROR_NOT_SUPPORTED`, `consent.c:383-391`); account type "administrator" is the `sudo` group only in
  `src/dlls/wexhost/unixlib.c:811` (AccountsService `CreateUser` type 1). Existing docs name `sudo`, `wheel` and
  `wexsys` as authorised administrative groups (`docs/ELEVATION.md:187`) but nothing in code reads them.
- What would decide "administrator": nothing exists yet. The server could get the Unix groups of its own user once
  (`getgrouplist(getuid())` at start, or `getgroups` of the first client; not per process), check for `sudo` or a
  new `wexadmin`, and then (a) build the first token as Administrators-in-deny-only/filtered for admins and
  `Users` only, no linked token, for standard users (`create_linked_token` returns nothing for them), and
  (b) refuse `grant_process_admin_token` to them. The `wexsys` model does not need a login in `sudo` for this; a
  separate group keeps the machine-root group out of the decision.
- Implication: a new server-side lookup of the UID's groups against one named group is required, plus a standard-user
  token without a linked partner; neither exists.

#### 7. What the four scripts assume about `sudo`

`setup-machine-root.sh` already picks `wexsys` when `getent group wexsys` succeeds, else `sudo`
(`scripts/setup-machine-root.sh:18-25`); it never creates `wexsys`, adds nobody, and uses `MACHINE_GROUP` for every
chown, mode and ACL (`:171`, `:263-265`, `:271`, `:276`, `:280-288`). The migration must add the `groupadd` and the
`--update` regrouping; the tests below pick the group by that rule unless they set `WEX_MACHINE_GROUP`.

- `scripts/test_b3_machine_root.sh`: no `sudo` literal. It sets `WEX_MACHINE_GROUP` to a throw-away group
  (`:18`, `:33`, `:39`) and creates `IN_USER` as a member (`:34`); `:113-115` asserts that a group member can write the
  machine root, `:116-120` that an outsider cannot. Under the new model `IN_USER` is a "member" only in the Unix
  sense: update that assertion to run through a setgid helper or to check it in the new meaning (no desktop user is
  in the group). The check that the backup dir is not group-writable (`:123-130`) still holds.
- `installer/tests/test_s2_acceptance.sh`: Alice and Carol are added to `sudo` (`:24-28`, header `:5-6`) and write the
  machine root through the default ACL `g:sudo:rwX` (`:103` "Carol, second administrator in sudo"); it calls
  `setup-machine-root.sh` with no group override (`:21`, `:198`), so after `wexsys` exists the group is `wexsys` and the
  Alice/Carol writes fail. Bob (no `sudo`) is the limited user (`:8-9`, `:129-178`).
- `installer/tests/test_s3_acceptance.sh`: same user setup (`:84-88`) and hard-coded `sys_grp = "sudo"`, mode 664
  (`:52-53`, message `:63`); Alice and Carol write `system.reg` through their sessions (Cases 1, 3, 3b, 4), which only
  works if they can pass `access(W_OK)` (item 5). The `test_s4_acceptance.sh` (`:102-103`) has the same `sudo` setup.
- `installer/tests/check-wow64.sh`: no group name. Step 4 has `alice` write `.../Program Files/CommonApp`
  (`:75`) and step 6 has the desktop user (`/tmp/desktop_user.txt`) denied write to the machine root (`:87-96`);
  `alice` must exist and be an administrator in the VM image, created outside this file. Under the new model Alice's
  direct write (step 4) no longer works until the broker exists; step 6 stays valid.
- Implication: tests that must change: s2 (user setup `:24-28`, header), s3 (`:52-53`, `:63`, `:84-88`), s4
  (`:102-103`), b3 (`:113-115`), and `check-wow64.sh` step 4 (`:75`); none other reads `sudo`.

### Results, phases A and B

Date: 2026-10-22. Branch `ccr-08223ee4-p830bf` of WineExplorer. No Wine file changed, so `STOCK_CHANGES.md` has no
entry. **Not run:** the changes to `test_s2_acceptance.sh`, `test_s3_acceptance.sh`, `test_s4_acceptance.sh` and
`check-wow64.sh` were checked with `bash -n` only (the container has no installed Wine and no `/root/WineExplorer`)
and have not been run, with or without `WEX_BROKER_AVAILABLE=1`. `test_b3_machine_root.sh` and
`test_wex_sysrun.sh` were run here as root (below).

Commits: `878a2483` (phase A: machine root on `wexsys`, migration, test updates, `PREFIX_SPLIT.md` note) and the
phase B commit (launcher, probe, test, setup-script install step, the group-creation move, this section).

#### Phase A: what changed

- `scripts/setup-machine-root.sh`: the group is `wexsys` by default (`WEX_MACHINE_GROUP` overrides); it is created
  with `groupadd --system` on apply/`--update`, after the path checks, when missing; no user is ever added.
  `sudo`, `wheel`, `admin`, `adm`, `staff`, `users` and `root` are refused as the machine group. Apply and
  `--update` now also migrate: extended ACLs are removed (`setfacl -b`) and set again for the group, everything
  below the root (`chown -R -h`, so files owned by a desktop administrator too) becomes `root:<group>`, modes and
  default ACLs as before (`2775`, `664`, `775` for `.exe`/`.dll`, hives `664`), and a final scan fails if anything is
  not `root:<group>`. A second run changes nothing (tested). Rollback restores the first captured metadata, so it
  returns to `root:sudo`.
- `PREFIX_SPLIT.md`: note at the top that `root:sudo` and `g:sudo` are superseded.

#### Phase A: test assertions changed

| File | Assertion | Change |
|---|---|---|
| `scripts/test_b3_machine_root.sh` | `b3_unix_group_write_allowed` | Kept (a member of the throw-away group writes). Meaning changed: the member stands for a process with effective group `wexsys`. Message reworded. |
| same | outsider, backup-dir checks | Unchanged. |
| same | new `b3_desktop_admin_write_denied`, backup dir not writable by a `sudo` user | A `sudo` user outside the machine group is denied. |
| same | new `b3_group_created_without_members`, `b3_migration_from_sudo`, `b3_migration_idempotent`, `b3_sudo_group_refused` | Old-style `root:sudo` fixture with `g:sudo:rwX` and files owned by a desktop user migrates; owner cannot write afterwards; second run identical; `sudo` refused. |
| same | `run_setup` | Passes `WEX_SKIP_SYSRUN=1` so the fixture does not install the launcher. |
| `installer/tests/test_s2_acceptance.sh` | header, user setup | New check: alice, carol, bob are not in `wexsys`. New Test 0: all three are denied `touch` in `windows`, `Program Files`, the root, and `echo >> system.reg`. |
| same | Test 1 (Alice installs `AliceApp`, COW-breaks `msvcp140.dll`) | Without `WEX_BROKER_AVAILABLE=1` the install runs as root (`install`, `root:wexsys`); the assertion checks `app64.exe`, `app32.exe`, `settings.ini`; the COW-break part prints WAITING. With `=1` the original block and assertion run. |
| same | Test 1b (Carol modifies Alice's files and the COW-broken DLL) | Without the broker: Carol's overwrite must be denied and `settings.ini` stays `Installed_By_Alice=1`; the original `Modified_By_Carol=2` assertion runs only with `=1`; prints WAITING. |
| `installer/tests/test_s3_acceptance.sh` | `verify_symlinks` | `sudo` group replaced by `$SYS_GRP` (default `wexsys`); message likewise. |
| same | setup | New: not in `wexsys`, and `echo >> system.reg` denied, for alice, carol, bob. |
| same | Case 1 (Alice registers COM and Uninstall keys) | Without the broker, root does the same install and `reg add` on the machine root and flushes; the grep for the keys in `system.reg` and Bob's three checks are unchanged. Alice's own session runs with `=1`. |
| same | Case 3, Case 3b | Whole cases wait for the broker (WAITING line); `verify_symlinks` still runs. |
| same | Case 4 | Root upgrade part and the `MachineUpgrade` assertions still run. Alice's session, the `AlicePostUpgrade` assertion and Bob's `AlicePostUpgrade` query wait. |
| same | Case 2 | Unchanged (Bob's in-memory write, S0.2 keys, unlinked prefix); the "not saved" checks now hold trivially for Alice. |
| `installer/tests/test_s4_acceptance.sh` | `verify_symlinks`, Case 2 ownership | `sudo` / `root:sudo` replaced by `$SYS_GRP` / `root:$SYS_GRP`. New not-in-`wexsys` check. |
| same | Case 5 (Alice installs `SharedTool`) | Without the broker the install runs as root; WAITING for Alice's own. New: alice and carol are denied `touch` in `Program Files` and `>> system.reg`. Bob's run and Bob's denial unchanged. |
| `installer/tests/check-wow64.sh` | step 4 (Alice installs `CommonApp`) | Without the broker: Alice is denied (`mkdir` in `Program Files`, `>> system.reg`), then root installs `CommonApp`; WAITING for Alice's own. With `WEX_BROKER_AVAILABLE=1` the original command runs. Steps 5 and 6 unchanged. |

#### Phase B: the launcher

- `tools/wex-sysrun/wex-sysrun.c` (about 320 lines, GPL-2.0-or-later header); `tools/wex-sysrun/sysrun-probe.c` is
  the test stand-in for loader and server. `setup-machine-root.sh` builds the launcher (or installs the binary in
  `WEX_SYSRUN_BIN`) to `/usr/local/libexec/wineexplorer/wex-sysrun`, `root:<group>` mode `2755`, and writes
  `/etc/wineexplorer/sysrun.conf` (`group`, `loader`, `ntdll`, `wineserver`, `machine_root`; `root:root 0644`).
  The configuration path is compiled in, so neither environment nor arguments can change it.
- Modes: `wineserver` runs the configured server with no arguments; `wineboot-init` runs the loader with the
  machine root's `drive_c/windows/system32/wineboot.exe --init` as a Unix path (so the image is the verified file
  whatever `dosdevices` says). Anything else, and any extra argument, exits 77.
- Checks before exec: no `no_new_privs`; effective and saved gid are the configured group; the real gid and the
  supplementary groups do not contain it; the passwd entry gives an absolute home. Then the loader, `ntdll.so`,
  the server, the machine root's `wineboot.exe` and `services.exe`, the configuration file, and every directory
  up to `/` (as written and as resolved): root-owned, no other-write bit, no group-write bit unless the group is root's or the system group, and
  `access(W_OK)` with the real ids fails (that covers the user's groups and ACLs). Root itself is refused by the same test.
- Environment built fresh: `HOME`, `USER`, `LOGNAME` and `WINEPREFIX=<passwd home>/.wine` from the passwd entry,
  `PATH=/usr/bin:/bin`, `LANG` and `LC_*` with a safe charset, and for `wineboot-init` only `DISPLAY`,
  `WAYLAND_DISPLAY`, `XDG_RUNTIME_DIR` (safe charset). Nothing starting `WINE` or `LD_` comes through.
- `setresgid(rgid, wexsys, wexsys)`, `PR_SET_DUMPABLE 0`, `umask 022`, `chdir /`, all fds above 2 closed
  (`close_range`). Decision from phase 0 item 2: no descriptor is passed and `WINESERVERSOCKET` is never set,
  because `wineserver` starts a fresh server and `wineboot --init` connects to the user's server like any first
  client.

#### Phase B: test results (`scripts/test_wex_sysrun.sh`, run as root here, 55 checks, 0 failures)

Last line of the run: `=== wex-sysrun: 55 checks passed ===`. The review added three other-group-writable refusals and
two checks that the setup script installs the launcher only with `WEX_SYSRUN_ENABLE=1`.

| Check | Result |
|---|---|
| positive control (good fixture runs) | pass |
| unknown mode (`services`, `/bin/sh`), no mode, extra argument (both modes) | refused, exit 77 |
| each trusted file (server, loader, `ntdll.so`, `wineboot.exe`, `services.exe`): other-writable, user-owned, group-writable by the user's group | refused (15 checks) |
| directories (fixture root, `bin`, machine root, `system32`): other-writable, user-group-writable, user-owned | refused (12 checks) |
| symlink whose target lies in a user-owned directory | refused |
| config user-owned, group-writable, world-writable, unknown key, symlink | refused |
| `setpriv --no-new-privs`; copy without the setgid bit; user in the system group; root | refused |
| environment injection (`WINEDLLPATH`, `WINEDLLOVERRIDES`, `WINEDEBUG`, `WINELOADER`, `WINESERVER`, `WINESERVERSOCKET`, `WINEPREFIX`, `WINEARCH`, `WINE_MACHINE_ROOT`, `LD_PRELOAD`, `LD_LIBRARY_PATH`, `LD_AUDIT`, `HOME`, `USER`, `PATH`, `SHELL`, `BASH_ENV`, `IFS`, `TZ`, unsafe `LC_*`) | none reaches the program; `WINEPREFIX` and `HOME` come from passwd; positive control shows the variables did reach the user's process |
| success, both modes | uid unchanged; gids `(user, wexsys, wexsys)`; `DUMPABLE 0`; `AT_SECURE 1`; cwd `/`; fds exactly 0, 1, 2 (descriptor 7 closed); fixed argument lists |
| ptrace attach and `/proc/<pid>/mem` by the user | `EPERM` and `EACCES` (control on the user's own child succeeds) |

#### Open points and design notes for phases C and D

- **services.exe identity.** `wineboot --init` starts `services.exe` as `C:\windows\system32\services.exe`,
  resolved through the user's own prefix (`drive_c/windows` and `dosdevices/c:` symlinks, which the user owns and
  can change after the launcher's check). The launcher verifies the machine root's file, but cannot stop the user
  from pointing the prefix elsewhere. The server in phase C should therefore decide the system role from the
  image's file identity (device and inode of the opened image against the machine root's `services.exe`), not
  from the NT path.
- `WINEPREFIX` is fixed to `<passwd home>/.wine`, as the work order says. A user with a different prefix gets no
  system role for it.
- The writability test refuses group write by any group other than root's and the system group, so Debian's
  `/usr/local` (`root:staff 2775`) is refused: point the configuration at another prefix. It does not look at ACL
  entries for other users.
- Migration drops every extended ACL in the machine root, user entries included, before setting the group's.
- Packaging: `installer/package-wine.sh` was not changed. The packaged `wex-setup-machine.sh` has no source next
  to it, so it needs `WEX_SYSRUN_BIN` pointing at a prebuilt launcher or the source shipped beside it; without
  either, the setup prints a warning and installs no launcher.
- The launcher is not wired into Wine's startup (phases C and D).


### Results, C1 (administrator identity)

Date: 2026-10-22. Decision S3. Wine `f180417289b` (branch `ccr-work`, pushed as `ccr-08223ee4-p830bf`); WineExplorer
commits on `ccr-08223ee4-p830bf`. Entries in `STOCK_CHANGES.md`: "Elevation boundary: administrator identity (C1)".

What was built (only while the elevation helper is configured; with it unset nothing changes):

- The server decides once per lifetime whether its user is in the administrator group (`user_is_administrator()` in
  `server/registry.c`: `getpwuid(getuid())`, `getgrouplist`, group `sudo`). The override is `admin_group=` in
  `/etc/wineexplorer/sysrun.conf`, honoured only if the file is a regular, root-owned, not group/other-writable file
  (checked on the open descriptor). The environment is deliberately not a source: a user could name a group they
  belong to (their own primary group) and become an administrator. A missing user or group means standard user.
- Administrator: unchanged (filtered token with the linked full token). Standard user: the filtered token without the
  Administrators group, Medium integrity, `TokenElevationTypeDefault`, the filtered privilege set. `create_linked_token`
  fails with `STATUS_NO_SUCH_LOGON_SESSION` (Win32 1312) and `grant_process_admin_token` with `STATUS_ACCESS_DENIED`
  once the session was made standard. The refusal is keyed to the session (`standard_user_session`), not to the group
  lookup at call time, and not to the token's elevation type: a standard user can `NtFilterToken(LUA_TOKEN)` its own
  token, which makes it Limited again, so a type-based check would hand it a linked full token.
- Provisional: the error code 1312 for `TokenLinkedToken` on a non-split token. Microsoft Learn documents only that
  `TokenElevationTypeDefault` means "the token does not have a linked token"; no native measurement was made. Check
  with `native_token_contract_probe.cpp` on a standard native account.
- A user who is root (uid 0) but not in `sudo` is a standard user. Run Wine as an unprivileged user in `sudo` to get
  the administrator shape.
- `tools/wex-sysrun/wex-sysrun.c` ignores the `admin_group` key (it refused unknown keys).
- Shell: for a standard user `runas` finds no linked token and starts the program with the caller's own (Default)
  token. Native Windows would ask for administrator credentials; the consent program has no credentials mode
  (`ERROR_NOT_SUPPORTED`), so this stays a gap. The probe checks that no full token results.

Not done (needs a design call): item 3, Wine's view of the local Administrators group. Findings: `NetLocalGroupGetMembers`
(`dlls/netapi32/netapi32.c`) is a stub that returns the current user for level 3 only (and `NetLocalGroupEnum` returns
no groups); `programs/net` has no `localgroup` command at all (net.c lacks it), and there is no local users and groups
UI. A real list needs a new netapi32 unix call (`getgrnam` and a walk of the passwd database, because `gr_mem` leaves
out users whose primary group is `sudo`), levels 0-3 of `LOCALGROUP_MEMBERS_INFO`, `NetLocalGroupEnum` and
`NetLocalGroupGetInfo` for "Administrators", a copy of the `admin_group` config rule on the client side (about 250
lines, plus a `localgroup` command in `programs/net`, plus changes to `dlls/netapi32/tests/access.c`, which assumes
the stub). Decision needed: the client side re-reads the group database when queried, so it can disagree with the
session's token until the next logon; making them agree needs a server request that returns the cached answer and
member list. Stopped here as instructed.

Test users (created as root in the container, removed afterwards): `wexadm` (member of `sudo`) and `wexstd` (not).
Run the harness from a directory the user can write, and make the user the owner of the `advapi32`, `ntdll` and
`shell32` test executables for the run: `security.c:4750` sets a DACL on the test executable itself and fails with 5
(access denied) when it is root-owned (also on the commit before C1; it is not caused by this change).

Probe: `scripts/test_token_security_regressions.cpp` has `--standard-shape` (marker `admin_identity=PASS|FAIL`; the
administrator case prints the same marker after `--limited-shape`). `scripts/check-token-security.py` works out the
identity of the user it runs as (same rule as the server) and runs the matching shape check; registry and pipe
checks run only for an administrator. Standard-user checks: no Administrators SID in `TokenGroups`, Users enabled,
Medium, exact filtered privileges, `TokenLinkedToken` fails with 1312, `ProcessWineGrantAdminToken` refused and the
child stays Default, `CreateProcessW` of a `requireAdministrator` program gives 740, `ShellExecuteExW` (default verb
and `runas`) gives no full token.

| Run | Result |
|---|---|
| `check-token-security.py --all`, as `wexadm` (administrator), x86_64 and i386 | exit 0; limited, registry, pipe PASS; advapi32 security 3728/0 (both); ntdll info 2811/0 and 2744/0; shell32 shlexec 1805/2 (both); consent PASS |
| same, as `wexstd` (standard) | exit 0; standard-shape PASS (`admin_identity=PASS`); advapi32 3728/0; ntdll 2811/0 and 2744/0; shlexec 1805/2; consent PASS |
| `--focused`, helper set in the throw-away prefix, both archs, `wexadm` / `wexstd` | PASS / PASS |
| override: `admin_group=wexstd` in a root-owned 0644 `sysrun.conf`, run as `wexstd` | administrator shape (PASS) |
| same file made 0666 | ignored, standard shape (PASS) |
| B4 smoke, helper unset / `WEX_B4_HELPER=1`, as `wexadm` | PASS 34 s / PASS 54 s |
| B4 smoke, helper unset / `WEX_B4_HELPER=1`, as `wexstd` | PASS 37 s / PASS 54 s |
| `--all` as root | not accepted as a baseline: root is a standard user here and, probably because uid 0 passes the file permission checks, so advapi32 security reports 3732/6 (`security.c:3995-4047`) |

### Results, C2 (server and launcher wiring, constraints a to d and f)

Date: 2026-10-22. Wine commits on `ccr-work` (pushed as `ccr-08223ee4-p830bf`): `0e98e935c36`, `71fe07ea021`.
Entries in `STOCK_CHANGES.md`: "Elevation boundary: system role server and launcher wiring (C2)". Not done here: the
SYSTEM token and service tokens (C3).

What was built (active only when the root-owned `/etc/wineexplorer/sysrun.conf` names the machine root and the
server was started by the setgid launcher; otherwise stock behavior):

- Constraint f. The server keeps `wexsys` only as its saved gid and runs with the user's gid (`sysrun_init`). It
  raises the effective gid for the machine hive load and save, and for a Unix path a client asks it to touch, only if
  that client's connection credentials (`SO_PEERCRED`, recorded in `create_process`) are the server's uid and the
  system gid. The `unix_pid` the client sends is never used. The server calls `prctl(PR_SET_DUMPABLE, 0)` after every
  `setegid`.
- Server-side opens for a client, and how each is handled (all of `server/*.c` was searched for `open`, `creat`,
  `mkdir`, `rename`, `link`, `unlink`, `rmdir`, `symlink`, `truncate`, `chmod`, `bind` and the like):
  `open_fd` `mkdir` and `open` (also the read-only retry for a directory), `set_reparse_point` and
  `delete_reparse_point` `rename`, `set_fd_name` `unlink` (replace) and `link` and `rename`, and `unlink_closed_fd`
  `unlink` and `rmdir` (delete on close, disposition): all raise only for a system-group client
  (`sysrun_open`, `sysrun_mkdir`, `sysrun_rename`, `sysrun_link`, `sysrun_unlink`), and the delete runs raised only
  for a file such a client opened. Run with the user's group, by design: `ftruncate` and `fchmod` on a descriptor that
  was already opened (`fd.c`, `file.c`, `change.c`; they need ownership, not the group), the mapping temporary file in
  the server directory (`mapping.c`), the prefix directories created at start (`registry.c`, the user's prefix), the
  server lock and socket (`request.c`), Unix socket `bind` for a Win32 `AF_UNIX` socket (`sock.c`; a system-group
  client cannot bind in the machine root, which no stock feature needs), and `/proc` reads (`procfs.c`, `ptrace.c`).
  The machine hive lock, temporary and in-place files are opened only inside the raised save. There were no more
  client-path opens than the two dozen call sites above, so the design held.
- Constraint c. Machine hives come from the fixed machine root (`O_NOFOLLOW`, `fstat`: regular file, root, system
  group, not other-writable) and are written in place (`save_hive_in_place`; `faccessat(AT_EACCESS)` and the raise
  only there). Shortcut: the write is a `pwrite` and a `ftruncate`, not an atomic rename, because a new file would
  belong to the user's uid; upgrade when the system role has its own uid.
- Constraint d. `key_in_user_tree` matches the token user's own SID subtree and `_Classes` only; `S-1-5-18/19/20` are
  created at start and, like `.DEFAULT`, get the machine default descriptor.
- Constraints a and b. `wex-sysrun wineserver` and `wineboot-init` connect to the server socket of the user's own
  prefix themselves (dev and inode of `~/.wine` from `stat`, not from the environment), check the peer, pass the
  descriptor as `WINESERVERSOCKET`, and set `WINEPREFIX` to the machine root for `wineboot-init`. ntdll in a client
  whose effective gid is the system group refuses a server that fails the same check.
  Design call: the check is on the peer's ids and saved gid in `/proc/<pid>/status`, not on `/proc/<pid>/exe`,
  because the server is non-dumpable and its `exe` link is not readable for the user or for a group member without
  `CAP_SYS_PTRACE`. The saved gid is sufficient: only a program run through the setgid launcher has it, and the
  launcher runs only the configured server and the loader.
- Wiring. `start_server` starts `wex-sysrun wineserver` and `run_wineboot` starts `wex-sysrun wineboot-init` (the
  normal wineboot still follows for the user's part); a launcher refusal (exit 77) prints a warning and falls back
  to stock. The system-group `wineboot --init` starts `services.exe`; `wineboot` does not start a second one. The
  launcher serves only the user's own `~/.wine`; another prefix gets a warning and stock behavior.
- Found while testing and fixed: a machine hive file owned by the user would stay writable for the user, so the
  server refuses hives not owned by root; saving by temporary file and rename would create such a file, hence the
  in-place save.

Tests (run as root here; the fixture is a throw-away group, machine root and two users, all removed):

| Run | Result |
|---|---|
| `scripts/test_sysrun_server.sh` (launcher-started server; ids user/user/saved `wexsys`; `services.exe` with the system group; limited client create, write, truncate, rename, link, delete, delete-on-close and mkdir in the machine root all denied, 64 and 32 bit; system-group client does them; replacement server refused by `wex-sysrun` (exit 77) and by a system-group client; limited client of a replacement server still denied; `system.reg` and `userdef.reg` symlinks into a machine-root file: target not overwritten, HKLM from the machine root, hives stay `root:wexsys`; `wineboot --init` writes the machine root; server not ptraceable and `/proc/<pid>/environ` root-owned before a save, after saves, after limited and system-group opens, and with `fs.suid_dumpable=1`; with the helper set `HKU\S-1-5-18/19/20` and `.DEFAULT` not writable, `wexadm` and `wexstd`, both archs) | 28 checks, 0 failures |
| `scripts/test_wex_sysrun.sh` (launcher unit test) | 60 checks, 0 failures |
| `check-token-security.py --all`, helper unset, as `wexadm` (unprivileged, in `sudo`; the three test executables owned by `wexadm` for the run) | exit 0; advapi32 security 3728/0 (both archs); ntdll info 2811/0 (x86_64) and 2733/0 (i386); shell32 shlexec 1805/2 (both, the known upstream failures); limited, registry, pipe, consent PASS |
| `--focused`, helper set in the throw-away prefix, both archs, as `wexadm` and as `wexstd` | exit 0, PASS / PASS |
| B4 smoke, helper unset / `WEX_B4_HELPER=1`, as `wexadm` | PASS 35 s / PASS 53 s |

Note: the i386 `ntdll info` count is 2733 here; the C1 table says 2744. Both have 0 failures. Not investigated (the
count is the executed-test total and changes with the environment); the gated code is not reached without the
configuration.

Test hygiene found: a probe started inside `$(...)` hangs the script when it is the first client of a prefix,
because `wineboot` starts `services.exe`, which keeps the captured output open. The test starts the first client
with its output discarded.

Known gaps: Unix-socket `bind` and other paths opened by the client itself (not by the server) are the client's
own rights; a system-group client that is itself malicious is out of scope (it is the system role). The saved-gid
check does not tell the server from `wineboot-init`, which also has the saved gid; both are launcher-run programs.


### Results, C3 (system role recognition, SYSTEM and service tokens, services.exe, probes)

Date: 2026-10-22. Phases C and D, the probes of phase E and step 2b item 5, honoring constraint g. Wine commits (branch
`ccr-work`, pushed as `ccr-08223ee4-p830bf`): `edf575a2260` (server), `9ab7dee645e` (ntdll, services), `bd22d5ee0d9` (deferred stop of an unverified process, sechost). Entries in
`STOCK_CHANGES.md`: "Elevation boundary: the system role, SYSTEM and service tokens (C3)". Everything is active only
with the launcher-started server and elevation enforced; without either, stock behavior (one exception: sechost's
service security calls, below).

**The default stays off.** `setup-machine-root.sh` still installs the launcher only with `WEX_SYSRUN_ENABLE=1`. The
probes below pass; the decision is the user's (item 7).

#### What was built

- **Recognition (item 1).** A root process (a new connection) whose `SO_PEERCRED` uid and gid are the server's user
  and the system group is *protected from its first moment*: process and thread objects get an owner, group and DACL
  from the SYSTEM token (SYSTEM and Administrators only), so there is no window in which a limited token could open
  it. It gets the SYSTEM token when its **main image is verified**: `add_process_view` hands the server's descriptor of
  the mapped file to `sysrun_main_image`, which compares device and inode with `drive_c/windows/system32/{services,
  wineboot,winedevice}.exe` and `syswow64/{wineboot,winedevice}.exe` of the machine root (`stat` of the configured
  path; the NT path is never used, it goes through the user's prefix links). With C2 the launcher-started processes
  use the machine root as prefix; verified in the probe. Any other image: `STATUS_ACCESS_DENIED` for the mapping and
  `kill_process`. A child gets its parent's token (duplicate) or the one it is given and, if that token's user is
  SYSTEM, LocalService or NetworkService, the same kind of descriptor from that token. Services that services.exe
  starts are children, so they are not checked by image: the check is for the processes the user can get started with
  the system group.
- **Connection credentials of a child.** `SO_PEERCRED` of a new process is that of the process that made the socket
  pair (the parent), not of the child. So a service started by services.exe would look like it has the system group
  even after the child dropped it. The server therefore clears `peer_gid` for a child whose token is LocalService or
  NetworkService: such a process never gets the raised-group file access of constraint f.
- **Tokens (item 2).** `token_create_service` and the request `create_service_token` (reached through
  `NtSetInformationProcess(ProcessWineCreateServiceToken = 1003)`: input account 0 LocalSystem, 1 LocalService, 2
  NetworkService; output token handle). `STATUS_PRIVILEGE_NOT_HELD` without an enabled SeTcbPrivilege,
  `STATUS_ACCESS_DENIED` for a caller that is not the system role's SYSTEM process (Wine's administrator token has
  SeTcbPrivilege too: this is stricter than the work order), `STATUS_NOT_SUPPORTED` when the system role is off,
  `STATUS_INVALID_PARAMETER` for another account number.
- **services.exe (item 3).** With the SYSTEM token every service starts through `CreateProcessAsUserW` with the token
  of its `ObjectName`. winedevice is a service entry with `LocalSystem`. `ObjectName` other than the three: the
  service does not start, `ERROR_SERVICE_LOGON_FAILED` (1069), documented. The control pipe of a service has a
  descriptor for SYSTEM and the account. services.exe sets its own token's default DACL to SYSTEM, Administrators
  and Authenticated Users (read, write, execute), because objects it creates without a descriptor (the `\pipe\svcctl`
  endpoint made by rpcrt4, the started event) would otherwise be reachable by SYSTEM and Administrators only.
- **Dropping the system group (item 3, no design call needed).** The Wine path is `NtCreateUserProcess` ->
  `spawn_process` (`dlls/ntdll/unix/process.c`), which forks twice and execs the loader. A client with the system group
  checks the token it was given (`sysrun_must_drop_group`: `NtQueryInformationToken(TokenUser)`; not SYSTEM, or no
  answer, means drop) and in the forked child calls `setresgid(gid, gid, gid)` before the exec: effective and saved
  group become the real group, the child cannot get the system group back. Because that makes it an ordinary dumpable
  process of the user, it also marks itself not dumpable first thing (`WINESERVICENODUMP`, read in `__wine_main`),
  and so do its children. Shortcut: the flag is set after the exec; in the short window before it, with
  `kernel.yama.ptrace_scope` 0, the user could attach. With scope 1 (the Debian default) a non-descendant cannot.
  Upgrade: a service-child mode in the launcher.
- **Registry grants (item 4).** `ServiceCurrent` and `ActiveComputerName` are Users-writable only while the system role
  is not active (`!sysrun_active`). Without a launcher services.exe and `wineboot --init` run limited and still need
  them.
- **Service security (item 5).** `Services\<name>\Security` (binary, self-relative) is read at load; the check in
  `OpenService` uses it, else the native default. `SetServiceObjectSecurity` needs `WRITE_DAC` on the handle, takes the
  DACL only (shortcut), writes the value (saved with the machine hive). `QueryServiceObjectSecurity` returns owner,
  group and DACL. sechost's two calls were a semi-stub and `return TRUE`; they now call the RPC functions. This is
  the only change that is not gated.
- **Found and fixed on the way.** (1) `NtCreateToken` needed no privilege: a limited process could make itself an
  administrator token, impersonate it and pass every access check, so no descriptor of the system role would have held.
  While enforced it now needs SeCreateTokenPrivilege, which no Wine token has. (2) `token_duplicate` lost an owner
  that is the user (a service account's owner is the account). (3) SeDebugPrivilege now opens any process or thread
  (Windows does; without it an administrator could not open a LocalService process).

#### Privilege lists (all provisional: not measured on a native system)

Wine defines privileges 2 to 30 only, so SeIncreaseWorkingSet and SeTimeZone are missing. The lists are from memory
of `whoami /priv` in a service and the documented defaults. A native measurement should replace them (the probe
`native_token_contract_probe.cpp` can print them).

| Account | Enabled | Disabled |
| :--- | :--- | :--- |
| LocalSystem | Tcb, Debug, Backup, Restore, Shutdown, SystemEnvironment, Audit, Impersonate, CreateGlobal, ChangeNotify, SystemProfile, ProfileSingleProcess, IncreaseBasePriority, CreatePagefile, ManageVolume, LockMemory, CreatePermanent | AssignPrimaryToken, IncreaseQuota, Security, TakeOwnership, LoadDriver, Systemtime, RemoteShutdown, Undock |
| LocalService | ChangeNotify, CreateGlobal, Impersonate | AssignPrimaryToken, Audit, Shutdown, Undock |
| NetworkService | ChangeNotify, CreateGlobal, Impersonate | AssignPrimaryToken, Audit, IncreaseQuota |

Groups: SYSTEM: Administrators (enabled, owner, primary), Everyone, Authenticated Users. The service accounts:
Everyone, Users, Authenticated Users, SERVICE (S-1-5-6), LOCAL (S-1-2-0); no Administrators; owner is the account.
All three: System integrity, `TokenElevationTypeDefault`, no linked token, primary. Default DACL: SYSTEM and
Administrators for SYSTEM, SYSTEM and the account for the others. Unknown (provisional): the native primary group of the
service accounts (Users here), CONSOLE LOGON and the logon SID, the exact native SYSTEM group list (native also has
BUILTIN\Users).

#### Cross-process server paths and their access checks (item 1)

All of these check against the descriptor of the target, because a handle to a protected process or thread can only be
made through a path that checks it (`alloc_handle` -> `check_object_access`) or by the process that created it.

| Path | Requests | Right | How |
| :--- | :--- | :--- | :--- |
| OpenProcess, OpenThread | `open_process`, `open_thread` | the requested rights | `alloc_handle` checks the descriptor |
| NtGetNextProcess, NtGetNextThread | `get_next_process`, `get_next_thread` | the requested rights | `alloc_handle` |
| DuplicateHandle | `duplicate_handle` | `PROCESS_DUP_HANDLE` on both process handles; more rights than the source handle has: checked against the descriptor with both tokens | handle rights, `check_object_access` |
| QueueUserAPC | `queue_apc` | `THREAD_SET_CONTEXT` | handle rights |
| CreateRemoteThread, VirtualAllocEx and friends | `queue_apc` with the create-thread and virtual-memory calls | `PROCESS_CREATE_THREAD`, `PROCESS_VM_OPERATION` | handle rights |
| New thread in another process | `new_thread` | only the initial thread of a process that has no thread yet | the creator's own child |
| Read and WriteProcessMemory | `read_process_memory`, `write_process_memory` | `PROCESS_VM_READ`, `PROCESS_VM_WRITE` | handle rights |
| Get and SetThreadContext | `get_thread_context`, `set_thread_context` | `THREAD_GET_CONTEXT`, `THREAD_SET_CONTEXT` | handle rights |
| Debug attach | `debug_process` | `PROCESS_VM_READ`, `PROCESS_VM_WRITE`, `PROCESS_SUSPEND_RESUME` | handle rights |
| NtSetInformationProcess | `set_process_info` (priority, affinity, token), `grant_process_admin_token`, `make_process_system`, `ProcessWineCreateServiceToken` | `PROCESS_SET_INFORMATION` (plus SeTcb) | handle rights |
| Suspend, resume, terminate, job assignment, new process with a parent | `suspend_*`, `resume_*`, `terminate_*`, `assign_job`, `new_process` | `PROCESS_SUSPEND_RESUME`, `PROCESS_TERMINATE`, `PROCESS_SET_QUOTA`, `PROCESS_CREATE_PROCESS` | handle rights |
| **By id, no handle:** targeted `SetWindowsHookEx` (`set_hook` with a thread or process) | `set_hook` | `PROCESS_VM_WRITE` and `PROCESS_CREATE_THREAD` | **new**: `sysrun_check_process_access` |
| **By id:** AttachThreadInput | `attach_thread_input` | `PROCESS_SET_INFORMATION` | **new** |
| **By id:** AttachConsole | console ioctl | `PROCESS_QUERY_INFORMATION` | **new** |

Not checked, and why: window messages sent or posted by thread id or window handle (`send_message`, `post_message`,
`get_thread_input`) and global hooks: known gap e (no UIPI or Session 0 isolation). Services started by services.exe
use the `__wineservice_winstation` window station, which the user's desktop does not share, so a global hook of the
user's does not reach them. `get_process_image_name` by pid and `list_processes` only report names. The wexsys server
has no Windows objects of its own that a token can name.

#### Test results

Run as root in this container (fixture: throw-away group, machine root, two users, all removed afterwards), Wine
`/home/user/build/dev`, WoW64, both architectures.

| Run | Result |
| :--- | :--- |
| `scripts/test_sysrun_server.sh`, complete, fresh fixture (C2 sections 1 to 7 and the new section 8, both users, both architectures, the user's own prefix) | **121 checks, 0 failures**, exit 0 (28 C2 checks, 93 C3 checks). Two checks (system programs not dumpable) were added after that run and passed in a later fresh-fixture run of section 8 for the standard user, 64-bit only (65 checks, 0 failures) |
| Section 8, tokens the services report: LocalSystem service S-1-5-18, System integrity, Administrators enabled and owner, Default elevation, no linked token, primary, SeTcb and SeDebug enabled, writes the machine root | PASS |
| LocalService and NetworkService services: S-1-5-19 and S-1-5-20, owner the account, System integrity, Default, no linked token, in Everyone, Authenticated Users, SERVICE, LOCAL, **no Administrators**, exact privilege lists (7 and 6 entries), cannot write the machine root | PASS |
| Unix side: the LocalSystem service has gids (user, wexsys, wexsys); the LocalService and NetworkService services have (user, user, user) and a root-owned `/proc/<pid>/environ` (not dumpable); services.exe and winedevice.exe not dumpable | PASS |
| services.exe (64-bit probe and 32-bit probe), winedevice.exe: SYSTEM, System integrity, Administrators enabled; a verified machine-root image gets the SYSTEM token | PASS |
| A service with a user account in `ObjectName` | `ERROR_SERVICE_LOGON_FAILED` (1069) |
| Service-token request by the system role (64-bit; the three tokens; account 3 refused) | PASS |
| Copy of a system program in the user's directory, started by the user: no SYSTEM token, no Administrators. Same copy started with the system group: stopped by the server within 12 s, the server survives | PASS |
| Limited (administrator user) and standard user, 64-bit and 32-bit: `OpenProcess` of services.exe, winedevice.exe and the LocalSystem, LocalService and NetworkService service processes with VM_WRITE, CREATE_THREAD, DUP_HANDLE, SET_INFORMATION, VM_OPERATION, VM_READ, TERMINATE, SUSPEND_RESUME, CREATE_PROCESS, QUERY_INFORMATION and ALL_ACCESS: `ERROR_ACCESS_DENIED`; `OpenThread` with SET_CONTEXT, GET_CONTEXT, SUSPEND_RESUME, SET_INFORMATION, TERMINATE, ALL_ACCESS: denied; `NtGetNextProcess` hands out no such handle; `AttachConsole` fails | PASS |
| Same users: `CreateService` and `OpenSCManager(CREATE_SERVICE)` denied; `OpenService` with CHANGE_CONFIG, START, STOP, WRITE_DAC, WRITE_OWNER, DELETE denied; connect, enumerate and query work; `NtCreateToken` and the service-token request refused with `STATUS_PRIVILEGE_NOT_HELD` (64-bit: the 32-bit probe cannot reach the native-only request) | PASS |
| Administrator user, a system role process that turns its token into an administrator token and enables SeDebugPrivilege: opens services.exe, winedevice.exe and the LocalService/NetworkService/LocalSystem service processes with `PROCESS_ALL_ACCESS`, allocates memory in them, opens a thread with `THREAD_SET_CONTEXT` | PASS |
| `Control\ServiceCurrent` and `ActiveComputerName` written by the limited and the standard user with the system role active: `ERROR_ACCESS_DENIED` (5), both architectures. In a prefix of the user's own (no launcher): written (0), services run with the user's token | PASS |
| Service descriptor: default does not let Authenticated Users start; `SetServiceObjectSecurity` by the system role; applies at once; other services keep the default; saved in the machine hive; read again after a restart | PASS |
| `scripts/test_wex_sysrun.sh` (launcher unit test, root) | 60 checks, 0 failures |
| `scripts/test_b3_machine_root.sh` (root) | exit 0, every assertion PASS |
| `check-token-security.py --all`, helper unset, as `wexadm` (unprivileged, in `sudo`; the six test executables owned by `wexadm` for the run) | exit 0; limited, registry, pipe, consent PASS; advapi32 security 3728/0 (both); ntdll info 2811/0 (x86_64) and 2744/0 (i386); shell32 shlexec 1805/2 (both, the known upstream failures) |
| `--focused`, helper set in the throw-away prefix, both architectures, as `wexadm` (administrator shape) and as `wexstd` (standard shape) | exit 0, PASS and PASS |
| B4 smoke (`test_b4_explorer_fresh.sh`), helper unset / `WEX_B4_HELPER=1`, as `wexadm` | PASS (29 s) / PASS (53 s) |

Failures along the way and what they were (all fixed): the first stop of an unverified process crashed the server (the
killed thread was still in use by the request; the probe passed anyway because the stopped process printed nothing, so
the probe now checks that the server survives and that the descriptor reaches the hive); the 32-bit probe called
the native-only service-token request; the first administrator check ran as a standard user, who has no
administrator token; the CRLF of Wine's console output; WexSvcLs got a descriptor in the same run that expected the
default.


#### Design calls and decisions to review

1. **create_service_token needs the system role as well as SeTcbPrivilege** (stricter than the work order): Wine's
   administrator token has SeTcbPrivilege too, and an elevated administrator could otherwise make SYSTEM tokens.
2. **NtCreateToken needs SeCreateTokenPrivilege while enforced.** New, outside the letter of the work order, but without
   it the descriptors of the system role (and every other access check) could be bypassed by impersonating a made-up
   administrator token. It changes stock behavior under enforcement only; `advapi32` tests that create tokens run with
   the helper unset.
3. **Administrators get the same access to the system role's processes as SYSTEM** (the default DACL of the work order),
   where Windows gives them less and lets SeDebugPrivilege through. SeDebugPrivilege is honored on top (an administrator
   token with it enabled opens LocalService processes, which have no Administrators entry). Native Windows gives
   standard users `PROCESS_QUERY_LIMITED_INFORMATION` on most system processes; here they get nothing. Task manager style
   tools see less. Say if the limited-information right should be granted to Users.
4. **LocalService and NetworkService processes are ordinary Unix processes of the user** (real, effective and saved
   group the user's, no setgid). They cannot write the machine root, but the user can signal them, and can
   ptrace them if `kernel.yama.ptrace_scope` is 0 in the short window before the process marks itself not dumpable.
   Closing the window needs a launcher mode for service children. Decision for the user: accept, or add that mode.
5. **Standard users have no way to elevate** (as the work order says), so the administrator check runs for the
   administrator user only.
6. **Known gaps (constraint e), not closed here:** window messages and global hooks reach system role windows; services
   started by services.exe are on their own window station, which limits this. `TokenIntegrityLevel` can be raised by the
   token's holder without SeTcbPrivilege in `set_token_integrity_level` (read from the code, not tested): it does not
   open a protected process (the descriptors have no label), but Windows refuses it. A limited process can still
   kill the user's wineserver (T5).
7. **Not measured natively:** the privilege and group lists above (provisional), the error codes of the refusals
   (`STATUS_PRIVILEGE_NOT_HELD` for NtCreateToken follows the documented requirement).
8. **item 7:** `setup-machine-root.sh` still installs the launcher only with `WEX_SYSRUN_ENABLE=1`. All probes of
   constraint g pass; the default is yours to flip.

