<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work order: consent for the elevation broker (plan step 6)

Date: 2026-10-24. Status: Part A built; Part B revised (see "Decisions"). Part of
`ELEVATION_BOUNDARY_PLAN.md` (design D3.3, step 6). Builds on the broker (`ELEVATION_BROKER_WORKORDER.md`): the
`wexelevate` service exists, freezes requests and fails closed (21) in shipping builds because nothing approves.

## Why

Until something approves requests, no one can elevate when a helper is configured: administrators cannot install
into the shared `Program Files`, and every `WAITING` line in the S2, S3 and S4 acceptance tests waits. This step adds
real approval, after closing the one hole the broker review left open: the target image can be changed between the
broker's last check and the launch, and after launch while it runs. With a real approver that would let a limited
process swap the approved program for its own.

## Part A: the image-swap window (first, before any approver ships)

The broker opens the image at freeze time and checks file index, size, time and SHA-256 again just before launch.
Two gaps remain: the bytes can change between that check and the mapping, and Wine maps PE images with private file
mappings, so pages not yet read still follow the file after the process starts.

**Proposed (C1): hold a Linux read lease on the image for the elevated process's lifetime.**
- The broker takes `fcntl(F_SETLEASE, F_RDLCK)` on its frozen file descriptor before the hash check. The kernel
  grants a read lease only if no one has the file open for writing; afterwards, any open for writing or truncation
  by anyone blocks and the broker gets a signal (lease break). Mapping the file writable and shared also needs an
  open for writing, so it is covered.
- The broker then hashes, launches, and keeps the lease until the elevated process exits.
- On a lease break the broker terminates the elevated process (and anything it started? decide: only the process
  itself, documented), then releases the lease. Documented behavior: changing the program file of a running
  elevated program ends that program.
- Leases need the broker's file system uid to own the file (it is the user's own file, and the broker runs as the
  user's uid) or `CAP_LEASE`. Files the user does not own (root- or `wexsys`-owned, for example in the machine root)
  cannot be written by the user anyway, so no lease is needed there; the broker checks that with the effective rights
  of the user's real gid.
- If a lease cannot be taken on a user-owned file (a file system without lease support, such as some network or
  FUSE mounts), the request fails with a distinct error; no fallback copy.
- Out of scope, as on Windows: DLLs the elevated program loads later from a directory the user can write (DLL
  planting). Documented.

Accept (probes): a writer that opens the image after freeze blocks and the request fails with 1392; a writer that
opens it while the elevated program runs ends that program and the write then succeeds; truncation and a writable
shared mapping behave the same; root-owned images need no lease; a file system without leases gives the distinct
error.

## Part B: approval (revised 2026-10-24, see "Decisions")

The Windows rule: an administrator who chooses "run as administrator" confirms; only a request that comes from another
account (a standard user typing an administrator's password) asks for credentials. So:

**Administrators: click consent, no password, no polkit.** The broker starts the consent window itself as a
system-role process (SYSTEM token, its own child, not the caller's), on the user's default desktop for now (step 5
moves it to the secure desktop; synthetic input is the accepted gap until then). The window cannot be requested or
steered through the request pipe: the broker passes it the frozen image path, the publisher (the signer of the image,
if any) and the command line, all taken from the frozen request. It launches only on Approve. `src/programs/consent`
is reused if it fits. Policy: `ConsentPromptBehaviorAdmin` (HKLM `Software\Microsoft\Windows\CurrentVersion\Policies\System`, writable by
administrators only while elevation is enforced). The values 2 and 5 select click consent; absent or any other value
keeps failing closed (`ERROR_NOT_READY`, 21), so nothing changes for a build whose policy is not set (the default policy
is decided after step 5). 1 and 3 (credentials) also stay at 21 until the polkit work order.

**Errors:** Approve launches exactly one target; Cancel or close: 1223 (`ERROR_CANCELLED`); timeout (120 s, the request
dies with it): 1460 (`ERROR_TIMEOUT`); the consent UI cannot start: 1062 (`ERROR_SERVICE_NOT_ACTIVE`). Never success
without Approve.

**Test hook.** The test build (`-DWEXELEVATE_TEST_APPROVAL`) drives Approve and Cancel through a test-only hook (files
in the harness directory read by the consent program), never by injecting input. The shipping build has no such hook
(a grep check, as with the stub).

**Standard users (C4):** `ERROR_NOT_SUPPORTED` (50), unchanged. Credential elevation with selection of the administrator
account (polkit `CheckAuthorization`, the wexhost agent) is its own work order, after this one.

**Known limits (C5):** until step 5, a limited process of the same user can send synthetic input to the consent
window on the default desktop. Recorded, not fixed here.

## Part C: tests

Part A's probes (the broker fixture of `scripts/test_sysrun_server.sh`), and for Part B: Approve launches exactly one
target through the broker with the full token; Cancel (1223), timeout (1460), consent UI missing (1062) launch nothing;
two concurrent requests: approving one does not approve the other; the consent window shows the frozen image, publisher
and command line of its own request; the hook is absent from the shipping build; regression: the usual full harness with
the helper unset at baseline, focused with the helper set, B4 both modes. No dbus-daemon or polkitd is needed.

## Decisions (user, 2026-10-24)

Nothing is deployed until the whole plan is complete, so no interim safety measures are needed. Applied as follows:
- C1 (read lease) is kept: the image swap is a hole in the finished design under any approval method. Built first.
- C4: (a), standard users get `ERROR_NOT_SUPPORTED` (50) for now.
- Scope change (user, later the same day): "only ask for a password when elevating from a different account, like
  Windows; an administrator who chooses run as administrator just confirms; a standard user selects the administrator
  account and types the password if one is set". Therefore Part B is click consent for administrators (above), the
  polkit credential path (C2 as first drafted) is dropped from this work order and becomes its own work order
  (over-the-shoulder elevation).

## How the work is split

Planning and review: the main session. Implementation: a Sonnet
agent, Part A first (it can be merged on its own), then Part B and C.

## Results

### Part A: the read lease (2026-10-24)

**What was built.**
- `src/dlls/wexhost/lease.c` (Unix side, new) and `WexLeaseTake`, `WexLeaseArm`, `WexLeaseRelease` in `wexhost.dll`
  (`src/include/wexhost.h`, `wexhost.c`, `wexhost.spec`, `wexhost_unix.h`, `unixlib.c`, `Makefile.in`).
- `src/programs/wexelevate/main.c`: the broker takes the lease on its frozen file handle before it hashes, keeps it through the
  approval wait, `verify_image` and the launch, and hands it to a guard thread that releases it when the elevated process
  exits.

**Unix-side route.** Reuse of wexhost's unixlib (the DLL already exists with a `.so`, and a test broker compiled with mingw can
load it by `LoadLibrary`; a unixlib of `wexelevate.exe` itself would need a new module kind and the test broker could not
use it). The broker loads `%SystemRoot%\system32\wexhost.dll` by full path. Without it every request fails with 665.

**Mechanism.** `wine_server_handle_to_fd` gives the Unix descriptor of the frozen handle; `fcntl(F_SETSIG, SIGURG)` (the
kernel's default signal on a lease break is SIGIO, which would end the broker; SIGURG is ignored and unused by Wine) and
`fcntl(F_SETLEASE, F_RDLCK)`. A Wine thread of the caller sits in a Unix call (`lease_monitor`) that polls `F_GETLEASE` every
10 ms (the kernel reports `F_UNLCK` while a lease is breaking). It never calls the wineserver, because a Wine program that
writes the file makes the wineserver itself block in `open()` until the lease is gone. It cannot be a pthread: a foreign thread
cannot call `NtSetEvent` (the first try crashed the broker, error 109). On a break it ends the elevated process by its Unix
pid (taken from the wineserver's `list_processes`, i.e. the credentials of its connection, `pidfd_open` + `pidfd_send_signal(SIGKILL)`),
waits for it to be gone, gives the lease up with `F_UNLCK`, and the Wine thread sets the event; the broker then fails a pending
request with `ERROR_FILE_CORRUPT`.

**Lease behavior per case.**

| Case | Result |
|---|---|
| A writer opens the image (append, `O_TRUNC`, `O_RDWR` + shared writable mapping) while the request waits for approval | the writer blocks for about one poll (10 ms), then goes on and changes the file; the request fails with 1392; the lease is gone |
| The same while the elevated program runs | the elevated process is killed first (the caller's handle is signalled, exit code is not 42), then the writer goes on; the lease is gone |
| A Wine program opens the running image for writing | the wineserver opens the file first (a kernel `open()` for write), which breaks the lease and ends the elevated program; Wine then refuses by share mode (Win32 error 32). So any write open counts, even one Wine refuses |
| Normal exit of the elevated program | the lease is released; a writer afterwards does not block |
| Image the user cannot write (root-owned, `/proc/locks` shows no lease) | no lease is taken, the request runs |
| User-owned image | `/proc/locks` shows a READ lease while the program runs |
| Someone has the file open for writing at freeze time | `ERROR_SHARING_VIOLATION` (32), nothing runs |
| File the user can write but does not own (root-owned, mode 666) | `ERROR_FILE_SYSTEM_LIMITATION` (665), nothing runs |
| Kernel without lease support (`fs.leases-enable=0` gives the same `EINVAL` as a file system without leases) | 665, nothing runs, no fallback copy; control run with leases on works |

**Design calls.**
1. Error codes: 32 (writer present) and 665 (no lease possible) are new in this path; 1392 as before for a changed image.
2. Only the elevated process itself is ended on a break, not its children (as proposed).
3. Windows that cannot be closed: between `CreateProcessAsUserW` and the `Arm` call (some milliseconds) a break finds no pid. The
   monitor then keeps the lease until the pid is known and ends the process; if the writer is a Wine program the wineserver is
   blocked meanwhile and the process cannot be created, so the kernel breaks the lease after `lease-break-time` (45 s).
   Recorded, not closed.
4. The polling interval is 10 ms; the kernel allows the writer to wait 45 s. Stopping the service drops the leases of running
   elevated programs (they keep running unprotected).
5. DLL planting stays out of scope, as on Windows.

**Tests** (`scripts/test_sysrun_server.sh` section 9, fixture kept, sections 1 to 8 skipped for speed): 48 checks, 0 failures,
among them the 15 new `c4_` lease checks above, and every earlier broker check unchanged. New probe modes `leasekill` and
`winwrite` in `scripts/wexelevate_probe.c`. The complete run is part of the final acceptance run (see below, Part B).
