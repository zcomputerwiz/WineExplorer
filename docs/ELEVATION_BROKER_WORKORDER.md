<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work order: the elevation broker (plan step 4)

Date: 2026-10-23. Status: scoped. Part of `ELEVATION_BOUNDARY_PLAN.md` (design D3, step 4). Builds on the system
role (`ELEVATION_SYSTEM_ROLE_WORKORDER.md`): the `wexsys` launcher is on by default, `services.exe` runs as SYSTEM,
and service tokens exist.

## Why

With a helper configured, nothing can elevate: `runas`, `ShellExecute(... "runas")` and manifests that require
administrator fail closed, and an administrator cannot install into the shared `Program Files`. Every `WAITING`
line in the S2, S3 and S4 acceptance tests is waiting for this. Windows starts elevated processes from the AppInfo
service, which holds `SeTcbPrivilege`, not from the caller (plan D3; native result N6: a TCB holder gets the linked
token as a primary token).

## Decisions taken here (main session; review before implementation)

- **B1. The broker is a service.** `wexelevate.exe` (WineExplorer `src/programs/wexelevate`), installed in the
  machine root's `system32`, registered as a demand-start LocalSystem service `wexelevate` (display name
  "Application Information", like AppInfo). `services.exe` starts it with the SYSTEM token, so it holds TCB
  through the existing system role. The server's system-role image list (`sysrun_main_image`, dev/ino of the
  machine root's images) gains `wexelevate.exe`; nothing else grants it TCB. Without the launcher (user-writable
  prefix) the service runs on the user's token and refuses every request with `ERROR_NOT_SUPPORTED`.
- **B2. Transport.** A named pipe `\\.\pipe\wexelevate`, created by the broker with a descriptor that lets
  Authenticated Users connect. The broker identifies the caller from the server (`GetNamedPipeClientProcessId`
  backed by `SO_PEERCRED`-recorded data, and the client's token through pipe impersonation at Identification level,
  which the C2 rule gives a SYSTEM pipe server), never from fields in the request.
- **B3. Request.** Image path, argv vector, cwd, environment policy (inherit the caller's block or the target
  user's default), show state, and whether the caller wants a process handle back. The broker freezes it: opens
  the image by path, records its device, inode and a hash, and refuses later if any differs at launch. Each request
  gets a single-use id; it is destroyed on completion, error, or caller exit. Requests are independent.
- **B4. Authorization in this step.** Only an approval stub: a separate test-only executable the broker calls
  through a path that exists only in the test harness build, never in shipping code (as the plan requires). The
  real consent (step 6, polkit; step 5, secure desktop) replaces it.
- **B5. Who may elevate.** An administrator (a token with a linked full token, C1) gets the linked token.
  A standard user gets `ERROR_NOT_SUPPORTED` in this step; credential elevation for standard users is step 6.
- **B6. Launch.** The broker gets the caller's linked token as a primary token (TCB), creates the process with
  `PROC_THREAD_ATTRIBUTE_PARENT_PROCESS` = the caller, and duplicates the new process handle into the caller.
  The caller sees a real `hProcess` (wait and exit code work as before).
- **B7. Unix rights of the elevated process (awaiting the user's confirmation).** It keeps the effective group `wexsys` (it is an administrator's
  elevated process, so it may write the machine root, as an elevated installer must). C3's ntdll rule that drops
  the group for non-SYSTEM tokens must make an exception for a token with `TokenElevationTypeFull` created by the
  broker. The process stays non-dumpable (egid differs from rgid), so the user cannot ptrace it.
- **B8. Protection of the elevated process.** A limited process of the same user must not be able to open the
  elevated process (or its threads) for write, inject an APC, create a remote thread or duplicate a handle into it
  (Windows: the mandatory integrity policy, No-Write-Up on process objects, High over Medium). Check whether
  Wine's server applies mandatory labels to process and thread objects; if it does not, give processes created
  with a full token the same kind of descriptor C3 gives system processes (owner the user, full access for SYSTEM
  and the full token's Administrators, limited query for the user's limited token), and probe it.

## Scope

1. The service: `wexelevate.exe` with the pipe server, request parsing, freezing, single use, launch (B1-B3,
   B5, B6), registered in the machine root setup (`setup-machine-root.sh` / the WineExplorer `.inf` that installs
   services, whichever the project uses).
2. Server and ntdll changes: `wexelevate.exe` in the system-role image list; B7; B8.
3. A client in Wine for tests: a small library function or test program that sends a request and gets the handle
   back. Caller migration (shell32 `runas`, msi, the manifest path) is step 7 and is not done here, except that
   `runas` may call the broker behind the test stub if that is the smallest way to drive the S2/S3/S4 `WAITING`
   assertions; decide after item 1 and report.
4. Probes (`scripts/test_sysrun_server.sh` or a new fixture), with the launcher and helper set:
   - the elevated process has the full token (High integrity, Administrators enabled) and egid `wexsys`, and can
     create a folder in the machine root's `Program Files`;
   - its parent is the caller, and the caller's `hProcess` waits and returns the exit code;
   - a request whose image changes after freezing is rejected; A/B concurrency: approving A does not approve B;
     long, Unicode and quoted arguments survive unchanged;
   - a standard user gets `ERROR_NOT_SUPPORTED`; without the launcher every request gets `ERROR_NOT_SUPPORTED`;
   - a limited process of the same user cannot open the elevated process or its threads with `PROCESS_VM_WRITE`,
     `PROCESS_CREATE_THREAD`, `PROCESS_DUP_HANDLE` or `THREAD_SET_CONTEXT`;
   - a request forged with another process's id in its fields is attributed to the real caller;
   - the stub is absent from a shipping build (grep the installed tree).
5. Regression: the usual full harness with the helper unset at baseline; focused with the helper set as
   administrator and standard user; B4 smoke test both modes.

## How the work is split

Planning and review: the main session. Implementation: a Sonnet agent, items 1 and 2 first, then 3 to 5. Any
security choice not decided above: stop and report.

## Results

(none yet)
