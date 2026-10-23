<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work order: consent for the elevation broker (plan step 6)

Date: 2026-10-24. Status: scoped; decisions C1 to C6 below are proposals for the user, marked. Part of
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

## Part B: approval

**Proposed (C2): credentials for administrators until the secure desktop exists.** Click consent can be faked by
any same-user process sending synthetic input (plan Q4), so it waits for step 5. Until then the broker asks polkit
for administrator credentials for every elevation:
- polkit action `org.wineexplorer.elevation.runas`, default `auth_admin` for active, inactive and any sessions,
  never `auth_admin_keep` (no reuse window). Policy file installed by `setup-machine-root.sh`.
- The broker calls `CheckAuthorization` itself over the system bus, subject = the caller process (unix-process with
  pid, start time and uid, taken from the server's record, not from the request), flag `AllowUserInteraction`,
  details: the frozen image path, its SHA-256 and the command line, so the prompt shows what will run. polkit's own
  admin identities (by default `sudo` members on Debian) decide whose password counts.
- The password is typed into the existing wexhost polkit agent (`src/dlls/wexshell/uac_agent.c`,
  `src/dlls/wexhost/unixlib.c`) and checked by PAM through `polkit-agent-helper-1`. The broker never sees it.
- After step 5, policy 2 (click consent on the secure desktop) becomes the default for administrators and this
  becomes policies 1 and 3, as the plan says.

**Errors (C3):** approve: launches exactly one target; cancel or dismiss: 1223 (`ERROR_CANCELLED`); polkitd not
reachable: 1062 (`ERROR_SERVICE_NOT_ACTIVE`); no agent registered (polkit returns not authorized without a
challenge): 5 (`ERROR_ACCESS_DENIED`); timeout (proposed 120 s, the request dies with
it): 1460 (`ERROR_TIMEOUT`); wrong password: polkit's not authorized, 5. Never success without polkit's
`is_authorized`.

**Standard users (C4, user decision).** On Windows a standard user can elevate by typing an administrator's
password ("over the shoulder"); the program then runs as that administrator. Here the elevated process would run
under the standard user's Unix account with an administrator token and `wexsys`. Options:
- (a) keep `ERROR_NOT_SUPPORTED` (50) for standard users in this step; decide later;
- (b) allow it: after polkit accepts an administrator's credentials, the broker builds a full administrator token
  for the standard user's session (new: the standard user has no linked token today) and launches with it.
Recommendation: (a) now, (b) as its own small work order once (a) is validated, because it creates the first full
token for a user who is not an administrator and needs its own review.

**Known limits to document (C5):** on X11 any client of the same user can read keystrokes, so an administrator's
password typed into the agent can be captured by a same-user process; the same is true of `sudo` in a terminal. A
secure desktop (step 5) under X11 does not change that; under Wayland the compositor isolates input. The agent runs
in the user's session; a limited process that injects into the agent's process can read the password the same way.
Recorded, not fixed here.

**Where the approver lives (C6):** the shipping broker gets its approver behind the same function the test stub uses
today; the test stub stays test-only, and the test build can also use a test polkit agent instead of the stub.

## Part C: tests

In this container, as the plan says: start a system `dbus-daemon` and `polkitd` in the test fixture, two accounts
with known test passwords (one in `sudo`, one not), the polkit policy file installed, and a test authentication agent
registered for the test process (polkit's `pkttyagent` driven by the test, or a small agent using
`polkit-agent-helper-1`; no logind session is needed for a unix-process subject). Probes:
- approve with the administrator's password launches exactly one target, through the broker, with the full token;
- wrong, empty and arbitrary passwords, and a valid password of the non-administrator, launch nothing (5);
- cancel (1223), no polkitd (1062), no agent (5), timeout (1460) launch nothing;
- two concurrent requests: approving one does not approve the other;
- the password never appears in any log, environment or argv (grep the logs and `/proc/*/cmdline` of the fixture
  during the run);
- Part A's probes;
- regression: the usual full harness with the helper unset at baseline, focused with the helper set, B4 both modes,
  the broker fixture section.

## How the work is split

Planning and review: the main session; C1, C2 and C4 need the user's agreement first. Implementation: a Sonnet
agent, Part A first (it can be merged on its own), then Part B and C.

## Results

(none yet)
