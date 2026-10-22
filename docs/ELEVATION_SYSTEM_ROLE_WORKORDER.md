<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work order: the system role and elevation as a Unix group (`wexsys`)

Date: 2026-10-22. Status: scoped; phase 0 (facts) first. Part of `ELEVATION_BOUNDARY_PLAN.md`; implements step 2b
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
