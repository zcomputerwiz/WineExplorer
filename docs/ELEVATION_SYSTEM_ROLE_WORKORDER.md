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

(none yet)
