<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work order: the elevation broker (plan step 4)

Date: 2026-10-23. Status: items 1 to 5 done (see Results), decisions B1-B8 settled. Part of `ELEVATION_BOUNDARY_PLAN.md` (design D3, step 4). Builds on the system
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
- **B7. Unix rights of the elevated process (decided by the user, 2026-10-23: match Windows; any process holding an
  administrator's full token may write the shared machine root).** It keeps the effective group `wexsys` (it is an administrator's
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

Date: 2026-10-23. Wine commit (branch `ccr-work`, pushed as `ccr-08223ee4-p830bf`): `9be2135ad66`. WineExplorer commits:
the broker (items 1 and 2), the probes and the harness section (items 3 and 4), this documentation. Entries in
`STOCK_CHANGES.md`: "Elevation boundary: the elevation broker (plan step 4)".

#### What was built

- **`src/programs/wexelevate/main.c`** (Wine `programs/wexelevate`, `wexelevate.exe`): the LocalSystem service (B1).
  `loader/wine.inf.in` registers it (demand start, "Application Information", own process) with a `Security` value that
  lets Authenticated Users query and start it. It creates `\\.\pipe\wexelevate` with `FILE_FLAG_FIRST_PIPE_INSTANCE`
  (if another program holds the name the service stops with an error), message mode, remote clients rejected,
  descriptor SYSTEM and Administrators full, Authenticated Users read and write. One connection is one request, served
  by its own thread (at most 64 threads, 32 requests in the table, 4 per caller; 10 s to send the request).
- **`src/include/wexelevate.h`** (item 3): the protocol and the client, `wexelevate_run()`, as header code so that
  tests (and shell32 and msi, step 7) can use it. The client starts the service when it is not running, and trusts the pipe only
  if the process at its other end is the process the service control manager reports for the service (a program of the
  user could have created the pipe first).
- **Request protocol.** One pipe message: `struct wexelevate_request` (magic `WXEL`, version 1, flags, show state,
  `advisory_pid`, the lengths in WCHARs) followed by the image path (absolute, with a drive letter), the command line
  (exactly the string the program gets from `GetCommandLineW`, **not** an argv vector), the current directory and the
  environment block. Flags: `WEXELEVATE_WANT_HANDLE`, `WEXELEVATE_ENV_INHERIT` (the request carries the block; else
  the broker builds the target user's default). The size must match the header to the byte. Reply: `struct
  wexelevate_reply` (magic, Win32 error, pid, tid, handle value in the caller's process). `advisory_pid` is ignored.
- **Caller.** `GetNamedPipeClientProcessId` (recorded by the server when the client opened the pipe), then the
  user of the client thread's token (pipe impersonation, reverted at once) must equal the user of that process's token.
  Only a caller whose token is the limited half of a split token is served (`TokenElevationTypeLimited`); a standard user
  (type Default) and an already elevated caller (type Full) get `ERROR_NOT_SUPPORTED`.
- **Freezing and the single-use id** (B3). After parsing, the request gets a random 64-bit id and a slot in the table.
  The broker opens the image with `FILE_SHARE_READ` only, keeps the handle until the launch, and records volume serial,
  file index (the inode), size, last write time and SHA-256. Approval is waited for together with the caller's process
  handle (the caller exiting destroys the request) and a 120 s timeout. Then the id is consumed (found once, then gone),
  and the image is opened again by path: the file index, size, time and hash must equal the frozen values, else
  `ERROR_FILE_CORRUPT` (1392).
- **Approval** (B4). `approve()` in the shipping build returns `ERROR_NOT_READY` (21): no consent exists yet (steps 5
  and 6), so every request of an administrator fails closed. With `-DWEXELEVATE_TEST_APPROVAL` it runs
  `C:\windows\system32\wexelevate_stub.exe <id> <image>` and waits for it (exit 0 approves, anything else is
  `ERROR_CANCELLED` 1223).
- **Launch** (B6). The broker opens the caller's process and token, gets `TokenLinkedToken` (a primary token for the
  system role, see the server change), checks that it is primary and `TokenElevationTypeFull`, creates the process with
  `CreateProcessAsUserW` (`PROC_THREAD_ATTRIBUTE_PARENT_PROCESS` = the caller, no inherited handles,
  `CREATE_NEW_CONSOLE`) and duplicates the process handle into the caller with `SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION`
  only (wait and exit code work; terminate, read and write do not).
- **Server and ntdll** (item 2): see STOCK_CHANGES. `create_linked_token` hands the system role's SYSTEM process a
  primary token (N6); `sysrun_process_created` protects an elevated process (B8); `open_token` needs the query right; the
  `system_images` list has `wexelevate.exe` (B1); `sysrun_must_drop_group` keeps the group for a full token (B7).

#### B8: does the server label process and thread objects? No. Route taken: a descriptor

`server/token.c` applies mandatory labels to **token** objects (`token_assign_label`, a High or System
`SYSTEM_MANDATORY_LABEL_NO_WRITE_UP` ACE) and `check_object_access` and `token_access_check` honor a label on any object,
but no code gives a **process or thread** object a label or any descriptor (`process->obj.sd` is NULL, and an object
without a descriptor grants everything). So a label-based No-Write-Up would protect nothing; the second route of B8
was taken: a process whose token is `TokenElevationTypeFull` (and its threads) gets the descriptor of a system role
process: SYSTEM and the full token's Administrators `GENERIC_ALL`, Users and Authenticated Users
`PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE` (threads `THREAD_QUERY_LIMITED_INFORMATION | SYNCHRONIZE`). The
user's limited token has Administrators deny-only, so it matches only the Users ACE.

Two findings of the probes, both fixed here and both outside the letter of B8:

1. **The owner must not be the user** (B8 says "owner the user"). `token_access_check` gives the owner
   `STANDARD_RIGHTS_REQUIRED` (WRITE_DAC, WRITE_OWNER, DELETE, READ_CONTROL) before it looks at the DACL, and the token
   of an administrator has the user as its owner, so the limited token of the same user got WRITE_DAC and could have
   rewritten the descriptor. The first probe run showed `OpenProcess(WRITE_DAC | WRITE_OWNER | DELETE)` succeeding. The
   owner is now Administrators (deny-only in the limited token). This also holds for the existing system role
   descriptors (their tokens' owner is Administrators or the service account).
2. **Token theft.** `open_token` (OpenProcessToken, OpenThreadToken) asks for no right on the process or thread handle in
   stock Wine, so a limited process could open the full token of the elevated process with the limited query right and
   got TOKEN_DUPLICATE, TOKEN_IMPERSONATE and TOKEN_ASSIGN_PRIMARY (the token object has no DACL, only the label that
   denies the write rights). It would have been able to impersonate a full administrator token. Fixed twice: while the
   system role is active and elevation is enforced, `open_token` needs `PROCESS_QUERY_INFORMATION` (`THREAD_QUERY_INFORMATION`),
   as on Windows; and the token of an elevated process gets a descriptor of SYSTEM and Administrators only.

#### Test results

Run as root in this container (Debian, Wine `/home/user/build/dev`, WoW64), fresh throw-away fixture (users `wexadm` in
`sudo`, `wexstd`, group, machine root, launcher, `/etc/wineexplorer`, `/usr/local/libexec/wineexplorer`), all removed
afterwards; `/root` stays 700.

| Run | Result |
|---|---|
| `scripts/test_sysrun_server.sh`, complete, fresh fixture (all earlier sections and the new section 9, both users, both architectures, the user's own prefix) | **174 checks, 0 failures**, exit 0 (33 of them `c4_`, the broker) |
| Section 9: the shipping broker, administrator, 64 and 32 bit | `ERROR_NOT_READY` (21), nothing started |
| Standard user, shipping and test broker, 64 and 32 bit | `ERROR_NOT_SUPPORTED` (50), nothing started |
| Administrator, test broker, 64 and 32 bit | process has: elevation type Full, primary token, High integrity, Administrators enabled, can create a folder in the machine root's `Program Files`; its parent is the caller; the caller's handle waits and returns exit code 42; Unix ids (user, `wexsys`, `wexsys`), `/proc/<pid>/environ` root-owned (not dumpable) |
| Environment block of the request / default environment of the user | arrives (Unicode) / absent |
| An elevated caller; the child of an elevated process | `ERROR_NOT_SUPPORTED`; the child is Full, High and writes `Program Files` (it kept the group) |
| Image appended to in place after freezing; image replaced (same bytes, new inode) | both `ERROR_FILE_CORRUPT`, no process; control without a change runs |
| A and B pending at once, only A approved | A runs, B gets `ERROR_CANCELLED`, B's process never starts, ids differ |
| 20000-character argument, accented, CJK and non-BMP characters, quotes, backslashes, tab, empty argument (64 and 32 bit) | the child's `GetCommandLineW` equals the request byte for byte |
| A request with `advisory_pid` = a bystander process | the parent is the real caller, not the bystander |
| Relative path / missing image / approver says no | 87 / 2 / 1223 (`ERROR_CANCELLED`), nothing started |
| Limited process of the same user against the elevated process | `OpenProcess` with VM_WRITE, CREATE_THREAD, DUP_HANDLE, VM_OPERATION, VM_READ, SET_INFORMATION, TERMINATE, SUSPEND_RESUME, CREATE_PROCESS, QUERY_INFORMATION, WRITE_DAC, WRITE_OWNER, DELETE and ALL_ACCESS: `ERROR_ACCESS_DENIED`; `OpenThread` with SET_CONTEXT, GET_CONTEXT, SUSPEND_RESUME, SET_INFORMATION, TERMINATE, SET_THREAD_TOKEN, IMPERSONATE, WRITE_DAC and ALL_ACCESS on every thread: denied; `OpenProcessToken` with DUPLICATE, IMPERSONATE, ASSIGN_PRIMARY, ADJUST_*: denied; the broker's handle cannot terminate, read or write, nothing can be duplicated into the process; the limited queries are allowed |
| The stub is absent from the shipping build | the macro is defined by no build file; the built program, the machine root and the build tree have no `wexelevate_stub*`; a broker compiled without the macro does not contain the name, one compiled with it does (control) |
| Administrator in a prefix of the user's own (no launcher) | `ERROR_NOT_SUPPORTED`, nothing started |
| `scripts/test_wex_sysrun.sh` | 60 checks, 0 failures |
| `check-token-security.py --all`, helper unset, as `wexadm` (unprivileged, in `sudo`; the six test executables owned by `wexadm` for the run) | exit 0; limited, registry, pipe, consent PASS; advapi32 security 3734 tests, 0 failures (both architectures); ntdll info 2812/0 (x86_64) and 2734/0 (i386); shell32 shlexec 1805 tests, 2 failures (the known baseline, both) |
| `--focused`, helper set by the runner, as `wexadm` and as `wexstd` | exit 0 / exit 0 |
| B4 smoke (`test_b4_explorer_fresh.sh`), helper unset / `WEX_B4_HELPER=1`, as `wexadm` | PASS / PASS |

Failures along the way (all fixed): the broker built with Wine's headers used `SE_TCB_NAME`, a narrow string, for
`LookupPrivilegeValueW`, so it believed it was not SYSTEM and refused every request with 50 (shipping build only; the
test build compiled with mingw did not show it); the test broker could not replace `system32\wexelevate.exe` because
Wine loads a program of `system32` that has a builtin twin from the builtin directory whatever the file is (the test
broker has another name and the service's `ImagePath` is changed in the throw-away hive); the probe's facts file has
CRLF; `test_sysrun_server.sh` counted one `Security` value in the machine hive (now two: the broker's own, from `wine.inf`).

#### Design calls and decisions to review

1. **Owner of the elevated process is Administrators, not the user** (B8 said "owner the user"): see finding 1. Strictly
   a deviation, with a probe.
2. **`open_token` needs the query right while the system role is active** (stock Wine: none), and the elevated process's
   token has a descriptor. Outside the letter of B8; without it the elevated token could be taken.
3. **The primary linked token is for the system role's SYSTEM process only** (N6 says any TCB holder): Wine's
   administrator token holds SeTcbPrivilege too.
4. **The command line is carried as the string, not as an argv vector** (B3 says argv vector). Re-quoting an argv would
   change what `GetCommandLineW` returns; a consent window can split the string for display (step 6).
5. **An already elevated caller gets `ERROR_NOT_SUPPORTED`.** Windows starts the program without a prompt. Not decided by
   B1-B8; it refuses, which is safe (the caller can start the program itself with its own token). Step 7 decides what the
   callers do.
6. **No approver in the shipping build fails with `ERROR_NOT_READY`** (21), distinct from 50 and 1223.
7. **`runas` is not migrated** (item 3's open question). The S2, S3 and S4 `WAITING` assertions need `shell32` to call the
   broker, and the shipping broker cannot approve anything yet, so a migrated `runas` would fail closed in every
   shipping build; the assertions can be driven only in a harness with the test broker. Left to step 7, with
   `wexelevate_run()` ready to use.
8. **Environment of the request is trusted as the caller's own choice**, as on Windows. The Windows environment does not
   reach the Unix loader (only `WINEDEBUG` is copied to the Unix environment, which is a debug option); the broker's Unix
   environment is the services.exe one.
9. **The `wexelevate.exe` entry of the server's system image list is dormant**: the launcher has the modes `wineserver`
   and `wineboot-init` only, and the broker is started by services.exe as a LocalSystem service (it is not a root
   process). It was added as B1 says.

Known limits: (a) the image is checked by path, once more right before `CreateProcessAsUserW`, which opens it again by
path; a swap in that last window (a directory the user can write, a symlink) is possible. Closing it needs a launch
from the frozen handle. (b) A limited process can fill the broker's pipe instances and request slots (a denial of service
only). (c) The pid of the pipe client could be reused between the pipe query and the process open; the broker checks
that the process token's user equals the pipe client's and that the pid is unchanged, not more. (d) No UIPI or secure
desktop (step 5): a limited process can send window messages to an elevated process's windows (constraint e). (e) The
elevated process has the machine root's write access (B7, decided by the user).
