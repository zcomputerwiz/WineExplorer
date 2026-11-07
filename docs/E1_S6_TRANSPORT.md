<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# E1-S6: disabled endpoint and metadata transport

Implemented for static review, 2026-11-06. **No build, Wine execution, WSL, VM,
system-bus or kernel test was run. Qwen must build and run the fixture.** Read
[S1](E1_S1_CONTRACTS.md), [S5](E1_S5_BROKER_RECORDS.md),
[S4](E1_S4_ACCOUNTS_MAP.md), [decisions](E1_DECISIONS.md) and [tooling](TOOLING.md).
This is a foundation for S7/S8/S17/S18, not a credential dialog or elevation.

The isolated lab server and wexhost Unix build require the explicit define
`WEX_E1S6_TRANSPORT_TEST`. Without it the server refuses endorsement and
`WexE1s6Transport` returns 50, zeroing bounded result bytes/channel. No build,
installer, provisioning or production service enables the define or installs
the native endpoint. The broker probe also needs the existing
`WEXELEVATE_TEST_APPROVAL`. Standard callers still receive error 50 in
`identify_caller`; the filtered-admin path remains separate. No S1 readiness
capability is advertised. There is no PAM, credential handling, profile or
token creation, target execution or portable approval capability.

## Endorsement and ownership

The existing launcher/system-role path remains the authority. On the first
mapped executable, wineserver compares the held file's device/inode with the
machine root's `windows/system32/wexelevate.exe`. The lab define also permits
the existing harness's exact `wexelevate_test.exe`. Later executable mappings
cannot acquire this role. Other SYSTEM services are not brokers.

Each endorsement requires protected system role, privileged sysrun connection,
SYSTEM service-account primary, enabled SeTcbPrivilege and no thread token.
Re-endorsement compares the retained broker primary object, TokenId and
ModifiedId captured at registration. A fresh registration changes broker
generation and replaces its primary snapshot; old registrations cannot renew.

The existing `get_pipe_caller_binding` request accepts an optional 96-byte
lab sideband. Before endorsement, the pipe must have a connected, complete S3
consumed message: actual writer object == retained opener, equal original
primary objects, no open/write impersonation, unchanged current primary and
captured IDs. No second read refreshes provenance. The normal no-sideband
query, reply layout and four non-inheritable result handles are unchanged.
There is no request-number, protocol-version, generated layout or ntdll change.
An old server ignoring sideband cannot send the required native endorsement;
its ordinary successful binding reply is insufficient. Shipping code denies
nonempty sidebands.

Wexhost imports three non-inheritable Wine handles for this call: the private
endorsement socket, a broker-created socket and the data socket. Each import requests only
`FILE_READ_ATTRIBUTES`, with attributes zero. Wine's normal `alloc_handle`
rejects a zero mapped access mask with STATUS_ACCESS_DENIED; attributes zero
are valid and keep the handles non-inheritable. `create_file_for_fd` accepts
an fstat-capable socket without a file-type filter, so no new server object,
request or bypass is needed. All fixture sockets are Unix SOCK_SEQPACKET;
the endorsement type checks still reject SOCK_STREAM. The server reads kernel
SO_PEERCRED on the broker-created socket, compares UID/native PID with this actual server
process object, and closes local FD duplicates. The native verifier separately
pins that socket creator with SO_PEERPIDFD and compares it with the actual
D-Bus sender's unique connection and daemon UID/PID. A PID lookup alone,
inherited `process.peer_pid`, SYSTEM SID or prefix secret is insufficient.
Wexhost closes imported handles and all ordinary S3 result handles on exit.
Both wexhost and wineserver require the control and data peers to be the same
live native root service: matching kernel SO_PEERCRED PID/UID plus two live
SO_PEERPIDFD pins. Wexhost also binds both to the unique bus owner's identity.
Wineserver checks both before raising its effective group; unsupported peer
pinning fails closed. No pidfd inode comparison or PID-only fallback is used.

The server sends directly to the verifier, explicitly supplying actual PID,
UID and **effective** wexsys GID as kernel-checked SCM_CREDENTIALS (automatic
credentials can contain real IDs). The verifier requires that GID and the
kernel-generated SCM_PIDFD, then compares the sender's executable inode with
its pinned expected wineserver. Root startup opens that executable and every
ancestor without symlinks, requiring root ownership and no group/other write.
Wexsys alone does not authenticate some other native executable. Process pins
must be live; unsupported pinning denies without a PID-only fallback.

Linux 6.12 source cross-checks for the UAPI and credential checks:
[socket options](https://github.com/torvalds/linux/blob/v6.12/include/uapi/asm-generic/socket.h),
[SCM_PIDFD](https://github.com/torvalds/linux/blob/v6.12/include/linux/socket.h),
[automatic credentials](https://github.com/torvalds/linux/blob/v6.12/include/net/scm.h),
[explicit credential validation](https://github.com/torvalds/linux/blob/v6.12/net/core/scm.c).
These source checks do not establish the VM's kernel/version/behavior.

`server_gen` is CSPRNG allocated per server instance; `broker_gen` is fresh for
each registration and stored on that server's process object. The verifier
refuses repeated broker generations and a server generation reused by another
or dead pinned server. Its bounded history lasts for the service instance.
Service restart closes channels and changes root/verifier generations, unique
owner and kernel process identity. Values are correlation, never bearer grants.

The native verifier is the root-started `tools/e1s6/e1s6-lab.py`, owning system
bus name/interface `org.wineexplorer.E1S6Lab`, object
`/org/wineexplorer/E1S6Lab`. Wexhost resolves the unique owner, queries UID/PID,
calls that **unique** destination, checks reply sender and ties both returned
sockets' SO_PEERCRED/SO_PEERPIDFD to it. It checks name ownership again after
pinning and at operations. The verifier pins the broker's bus connection and
native process. NameOwnerChanged, process death, ownership loss and deadline
cancel all subordinate state. Only protected root startup registers the native
verifier; ordinary Wine clients cannot nominate an executable or bus name.

After direct server endorsement, the service issues S1 kind-7 registrations
for broker (role 1), verifier (2), then native child (3), with nonzero opaque
IDs retained with actual connections/process pins. The role-2 host identity is
tied to wexhost's kernel verifier pin. Serialized registration bytes alone
never register an endpoint.

## Private child and records

Wexhost starts an owned native **metadata echo stub**, not UI, for one request.
It drops effective/saved system GID, installs parent-death SIGKILL after that
drop, checks its parent, sets non-dumpable, and closes inherited FDs with
close_range. Only the private socket and `/dev/null` standard streams survive.
It does not exec an interpreter (exec could reset dump protection), enter Wine
or receive caller handles. Post-fork code uses kernel/async-signal-safe calls.

The broker transfers its child socket privately. Refreshed server endorsement
binds the operation, sequence, expected child, session and connection challenge.
The native verifier checks the child's first `E1S6CHLD` packet's actual
SCM_CREDENTIALS/SCM_PIDFD against that endorsement and retains its process pin.
Every later child packet is checked again. A claimed parent/PID or creator UID
does not substitute the actual sender. There is no presentation/desktop success
claim or DIALOG_V1 bit: S17 must supply the protected UI and Wine desktop route.

Application records use S1's exact LE encoding, never a C layout. This subset
negotiates min=max=1 and caps=0. Unsupported versions/caps deny. Sizes are fixed
before reads/allocations; excess/truncated data and unexpected ancillary data
deny, closing received FDs. Public metadata contains no credentials or launch
operation.

| Operation | Encoding |
| --- | --- |
| Public D-Bus `Open(huut) -> ayhh` | Broker pin FD, minimum/maximum version, capabilities; returns 96-byte challenge plus endorsement/data FDs. The only application method; standard D-Bus introspection contains interface metadata. No application properties or signals. |
| Server sideband (96) | Input offsets: imported control/broker-pin socket handles 0/4 (u32); expected broker generation 8 (u64, zero initially); imported data socket handle 16 (u32), reserved input 20..23; root/verifier generations 24/32 (u64); nonce 40 (32 bytes); sequence 72 (u64); operation 80 (u32); child PID 84 (u32); zero reserved 88 (u64). |
| Direct endorsement (96) | Server overwrites 0/8 with server/broker generations (u64), 16/20 with pinned broker native PID/Wine session (u32). Verifier compares challenge, generation, sequence, operation, child and reserved fields. This is transport framing, not an additional S1 record kind. |
| Private open | `E1S6OPEN` (8), after sequence-0 endorsement. Reply: broker and verifier kind-7 records, each exactly 112 bytes. |
| Child bind | Broker registration (112), kind-2 empty answer template (156), child PID (u32), exactly one SCM_RIGHTS socket. After sequence-1 endorsement and actual child hello, reply: role-3 registration (112). |
| Exchange | Role-3 registration (112) plus kind-2 answer (156), after sequence-2 endorsement. Reply: the 156-byte answer from the pinned child. Single use. |

Only empty explicit confirmation (answer_kind=2) and cancellation (3) exist.
Credential text/kind 1, prompts, evidence, account selection, token requests,
executable paths, commands and environments are unsupported. The template binds
request ID, attempt, round=1/index=0, nonce, binding/request digests and channel.
Confirmation must equal it exactly. Cancellation matches everything except
kind and closes without echoing success. S5 fixture digests still contain
synthetic prefix/route authority inputs; S6 does not make those full caller
bindings authoritative.

Every packet checks kernel sender credentials/pidfd and retained state. The
registration wrapper must equal the issued bytes, including generations.
Replay/late messages, endpoint replacement, wrong identity/nonce/digest/channel/
sequence, extra FD, cancellation and disconnect are terminal. One absolute
five-second monotonic lab deadline includes setup; no retry extends it. Limits
are eight pending connections and 4096 generation histories. Packet content,
method arguments and authentication answers are not logged. The native lab
logs bounded, single-line escaped exception text/class, D-Bus error name and
method metadata to diagnose OS/bus failures. Metadata echo is never authentication.

Private export:
`WexE1s6Transport(pipe, operation, buffer, size, &channel)`. OPEN (1) uses 224
output bytes; CHILD_START (2)/EXCHANGE (3) use 268, answer at offset 112; CLOSE
(4) needs no buffer. Local channel IDs increase under one mutex; one active
conversation per broker, no pointers or public grants. Native/WoW64 Unixlib
tables append the same 40-byte fixed-width parameter slot. Failed valid calls
close FDs/bus and kill/reap the owned child; bounded results/channel are zeroed.
Test-only operations inject child death, wrong expected owner, disconnect and
child identity mismatch. No production build can execute them.

## Qwen: clean wexbuild / throwaway VM procedure

Run **as root in the wexbuild WSL distro**, where `test_sysrun_server.sh` also
runs, or in an exclusively owned throwaway VM **without an installed system
role**. NEVER run `test_e1s6_transport.sh` or its sysrun harness in the shared
lab guest VM B. Read TOOLING.md. These commands were not run here.
The inherited harness correctly refuses existing `/etc/wineexplorer`,
`/usr/local/libexec/wineexplorer`, users/group (including `wexadm`) and output
root. Do not delete, rename or move foreign configuration/accounts to bypass
that refusal. Use a clean machine; never reset VM B's installed role.
It provisions the machine role, compiles probes, and broadly cleans its own
test users. The wrapper does not weaken these ownership rules.

1. Sync/assemble changed overlay/Wine files into an isolated root-owned tree.
   Configure WoW64 (`--enable-archs=i386,x86_64`). Keep a separate normal build
   for shipping controls. The service refuses wineserver under user-writable
   ancestors: use root-controlled `/root/...` or protected `/opt/...` real paths.
2. Build the changed server, both PE wexhost DLLs, wexhost Unix library and normal
   broker. In the isolated lab build (also `server/named_pipe.o`: its pipe-impostor audit line is compiled only with the define), force recompilation of `server/sysrun.o`
   and `dlls/wexhost/e1s6.o` with
   `CFLAGS='-g -O2 -DWEX_E1S6_TRANSPORT_TEST'` (Wine's makefiles use CFLAGS; EXTRA_CFLAGS is ignored), then relink `server/wineserver` and
   `dlls/wexhost/wexhost.so`. Inspect compile commands: changing flags alone
   does not invalidate old objects. The harness compiles its own test broker/
   probes with WEXELEVATE_TEST_APPROVAL; do not add that define to production.
3. Guest prerequisites: running system D-Bus, python3-dbus, python3-gi/GLib,
   dbus-send, flock, normal sysrun compilers/tools/Xvfb, and Linux supporting
   SO_PEERPIDFD=77, SO_PASSPIDFD=76/SCM_PIDFD=4 and close_range. Missing kernel
   support must fail, not skip or fall back to PID authentication.
4. From root in that clean wexbuild/throwaway fixture, with repository and runtime already there:

   ```sh
   WINE_BUILD_DIR=/root/owned-e1s6/build/wow64 \
       bash /root/owned-e1s6/scripts/test_e1s6_transport.sh
   ```

The wrapper sets WEX_E1S6_LAB=1 and existing harness subset options; it does not
build Wine. The sourced `scripts/lib/e1s6-fixture.sh` locks S6, refuses foreign
files, temporarily installs the root-owned Python endpoint and root-only bus
policy, reloads policy and starts one exact root service PID. Cleanup stops/
waits that PID and removes only its inode-checked files, even with WEX_C2_KEEP=1.
No systemd unit, D-Bus activation or PAM configuration is installed. Logs:
`e1s6-service.log`, `e1s6-impostor.log` in the inherited fixture output root;
preserve that root with its keep option when collecting results.
The empty `/run/lock/wex-e1s6.lock` file may remain after its lock is released.
The wrapper ends with `E1S6_TRANSPORT OK` or `E1S6_TRANSPORT FAIL`.

Both 32- and 64-bit callers must complete these `c4_e1s6_*` checks:

| Case | Required observation |
| --- | --- |
| `accept` | Real endorsements, all three role records, pinned child echo; no target, consent UI or remote process handle. |
| `stale` | After valid setup change server generation in role-3 bytes; native verifier rejects. |
| `replay` | Complete one exchange, actually send it again over native channel; verifier cancels. |
| `late` | Kill/reap actual endpoint, submit late message; dead pin and revoked local ID deny. |
| `owner` | After valid setup inject client's unique name as saved expected owner; actual name-owner check rejects. Controlled mismatch, not a measured bus-daemon takeover. |
| `disconnect` | Close broker data FD; observe native child's natural exit after verifier EOF cancellation **before** cleanup's kill fallback. Old channel cannot reopen. |
| `cancel` | Bound kind-3 cancellation closes without a success reply. |
| `child-impostor` | Real same-UID child socket, endorsement naming another process; actual hello credentials/pidfd reject. |
| `transport-impostor` | Ordinary same-UID Wine process consumes a real message on its own pipe; cannot register broker role. |
| Native `--impostor` | Same-UID bus peer submits forged direct endorsement; kernel credentials/executable check rejects. Positive transport controls run first. |

The broker returns 0 only for the acceptance fixture, 5 plus completion marker
for completed negative fixtures, no marker on setup failure, and never a target
handle. Existing probes require no target/consent-stub marker. Also run existing
filtered-admin approve/cancel, standard-user 50, delegated/impersonating writer,
caller death and image-race controls; kernel32 pipe and advapi32 security in
both ABIs. Separately verify normal server/wexhost binaries contain no S6 bus
strings or enabled endorsement/fork code and exercise default error 50. Lab
success is not proof of shipping exclusion.

## Files and unverified work

Stock Wine changes are exhaustively listed in [STOCK_CHANGES.md](STOCK_CHANGES.md):
`server/sysrun.c`, `sysrun.h`, `process.c`, `process.h`, `named_pipe.c` only.

Project transport: `src/include/e1s6_protocol.h`, `wexhost.h`;
`src/dlls/wexhost/e1s6.c`, `Makefile.in`, `wexhost.c`, `wexhost.spec`,
`wexhost_unix.h`, `unixlib.c`. Broker/probes: `src/programs/wexelevate/e1s.h`,
`main.c`, `scripts/wexelevate_probe.c`. Native fixture:
`tools/e1s6/e1s6-lab.py`, `org.wineexplorer.E1S6Lab.conf`;
`scripts/lib/e1s6-fixture.sh`, `scripts/test_e1s6_transport.sh`,
`scripts/test_sysrun_server.sh`. Docs: this file, E1_DECISIONS.md, STOCK_CHANGES.md.
Existing unrelated/S5 edits were preserved. No commit.

All compilation/linking, deployed dbus-python/libdbus reply behavior, kernel ancillary
semantics, FD import, exact native PID/server-object agreement, close_range,
GID dropping/non-dumpability, WoW64 ABI, bus policy and every test above are
**unverified**. Also unrun: real broker/server/verifier restarts during traffic,
actual owner loss/replacement, PID churn, allocation/FD/RNG failures, concurrent
token mutation/cancel/stop and descriptor-leak/stress tests. Static review and
whitespace checks do not establish runtime security.

Host static checks completed: Git Bash `bash -n` on all three changed/new shell
scripts, matching native/WoW64 entry tables, matching all 27 enum/probe case
names, and `git diff --check` in both repositories. Python AST/XML parsing was
not completed: `python` was absent from PATH and the available python3.12
trampoline failed with `permission denied (os error 5)`. No fallback WSL or
guest execution was attempted. Temporary static-check helpers were removed.

The five-second stub and per-fixture root generation are not production policy/
configuration freshness or a PAM conversation. S7/S8 must provide the fixed
protected authentication worker and connection-owned evidence. S17 must replace
the echo endpoint with protected UI and validate Wine session/desktop routing.
S18 must install authoritative prefix/root/policy/account/clock bindings and all
readiness gates; metadata echoes/registrations never grant consent, identity
evidence or launch authority. S19 crash/stop supervision and the existing
lost-reply termination gap remain outside S6.

## Combined-batch review corrections (static only)

`Open` now retains its remote socket objects until a GLib idle callback after
synchronous reply serialization, with cancellation cleanup too. It releases
them promptly so they cannot mask broker EOF. The review's specific EBADF
explanation is not established: upstream
[UnixFd documentation](https://dbus.freedesktop.org/doc/dbus-python/dbus.types.html#dbus.types.UnixFd)
says its constructor owns a duplicate of the supplied FD. Retention makes the
requested lifetime explicit; Qwen must still run real Open/EOF controls.

Executable checking now opens `/proc/PID` once while the kernel-issued SCM pidfd
is live and its fdinfo PID agrees with credentials, then retains that directory
beside the server pin. Each endorsement opens `exe` relative to that directory,
compares fstat device/inode with the root-pinned expected executable, and checks
pidfd identity/liveness before and after. `/proc/self/fd/PIDFD` is an anon_inode,
not a path to the process executable. Dead-pin/PID-reuse paths deny; there is no
PID-only fallback. The earlier post-stat liveness check already rejected the
review's proposed PID-reuse substitution, so finding 8 is partly real.
Residual limit: neither pidfd nor proc-directory/executable FDs prevent the live
process from execing after the check, changing memory, or modifying an executable
inode in place. This is inode/process authentication under trusted root-owned
code, not executable-content attestation or an atomic no-exec guarantee.

Server lab code casts retained token references explicitly and duplicates
transport FDs with `F_DUPFD_CLOEXEC`. Both test-defined server objects must be
rebuilt without `-fpermissive`; no build acceptance is claimed here.

## Second-batch acceptance investigation (2026-11-07)

See [E1_S6_ACCEPT_REVIEW.md](E1_S6_ACCEPT_REVIEW.md) for the complete static
route/denial inventory, unavailable report evidence and limits. Win32 13 is
ERROR_INVALID_DATA, the broker fixture's incomplete-test sentinel; access denied
is 5. The original failing transport boundary remains unproven from that number.
Test builds now report distinct denial sites, the harness preserves broker/server
stderr, and negative cases require their targeted denial. Disconnect requires a
normal EOF cancellation exit rather than any child death. No security check or
accept expectation was relaxed. Also rebuild server/named_pipe.o with the S6
define for its new gated audit assertions; no build/runtime acceptance is claimed.

The follow-up review read the exact output in E1_S6_ACCEPT_FAILURE. It does not
identify which of the broker's error-13 routes ran. The native stub's previously
silent `_exit(1)` failures now have private exit codes and fixed parent-side
`child-*` audit sites, without retaining stderr or another child FD. Native
verifier checks have explicit site names, including the proc/executable pin and
inode checks. These are diagnostic changes; the underlying original failure
still requires the matching broker/server/native denial logs from Qwen's run.

## Round-3 descriptor import repair (2026-11-07; static only)

The newly supplied `s6-logs-round3/e1s6-wine.log:404-405` identifies
`endorse-import-control`, then `open-endorsement`, before any native private
Open packet. Source review establishes a deterministic zero-access import bug:
`wine_server_fd_to_handle(fd, 0, 0, ...)` reaches the normal `alloc_handle`,
which rejects zero access. Both imports now request `FILE_READ_ATTRIBUTES`.
STATUS_ACCESS_DENIED (c0000022) is the source-derived prediction; the old log
did not record the returned NTSTATUS. Its errno 11 is unrelated ambient state.
No stock Wine change or authorization relaxation is part of this repair.

Every wexhost denial now reports `source`, `status` and `errno`. `source=ntstatus`
contains the actual returned Wine status with errno zero; `source=errno`
contains the immediately captured syscall errno with a local unsuccessful
status. Logical checks, timeout/poll states, short sends and allocation failures
have explicit sources/local statuses and errno zero. D-Bus failures additionally
report the actual error name. Wrapper sites preserve their callee's cause.
Native child sites retain their private exit codes and publish status/errno in
one anonymous shared diagnostic mapping; the parent reads it only after waitpid
and unmaps it during cleanup. No extra child FD, packet or authority is added.
Late-child injection now checks kill/wait and the actual SIGKILL wait status;
disconnect still requires natural exit zero before cleanup, with wait errors
distinguished from timeout.

The native `call_blocking:696` location belongs to dbus-python, not this fixture.
After the import denial, broker cleanup closes its private bus connection;
`Transaction.live` can then query its vanished unique name before processing
EOF. This explains a reachable consequent bus exception. The old log omitted
the exception class/message and calling method, so it cannot establish the
exact bus error or exclude an additional bus failure. New diagnostics include
the originating fixture frame and synchronous bus method metadata, without
call arguments, for that distinction. Errors still cancel the transaction.

The complete cause/candidate analysis, subsequent-case re-review and Qwen
handoff are in the round-3 section of [E1_S6_ACCEPT_REVIEW.md](E1_S6_ACCEPT_REVIEW.md).
No compilation, service execution, runtime acceptance or ABI result is claimed.

## Independent post-import review fixes (2026-11-07; static only)

The full `s6-review2-antigravity.md` was checked against the committed sources
before edits. Findings are numbered as in that report:

1. **Real: control peer unchecked.** Previously `open_channel` pinned only
   data, while `sysrun_e1s6_endorse` checked only the control socket type. The
   S1/S6 endpoint is the native service that creates both socket pairs, so both
   must identify the same live root process. Both sides now check credentials
   and acquire live pidfds for both peers. Equal kernel PIDs with both acquired
   pins still live exclude different PID lifetimes. Wexhost checks the bus
   owner too. Server comparison needs the third metadata import at sideband
   input offset 16; the size remains 96 and the server overwrites 16..23 with
   broker PID/session before delivery. Rebuild server and wexhost together.
   Every new handle, duplicate and temporary pidfd is closed on exit. The
   server cannot independently resolve the bus owner; that remains wexhost's
   check and protected native service startup remains the provisioning authority.
2. **Real: registration published before delivery.** Server/broker generation,
   grabbed primary reference and its TokenId/ModifiedId are staged locally.
   Only a complete successful `sendmsg` publishes them, after group drop.
   Raise/send failure preserves the prior registration and releases the staged
   reference. Renewal retains the existing primary/version comparisons.
   Successful kernel enqueue is the boundary here, not a verifier acknowledgement
   or atomic success of the later ordinary binding-reply allocations.
3. **Not real in this tree: partial handle leak.** The existing
   `get_pipe_caller_binding` `done:` block (`named_pipe.c:1861-1868`) closes
   writer token/process and opener token/process on error. All locals start at
   zero and reply handles are assigned only after all four allocations succeed.
   No named_pipe.c edit was needed.
4. **Real: unbounded retained server pidfds.** The native lab now caps server
   history at 4096, checks capacity at Open and at endorsement insertion, and
   closes dead pins during ticks and before insertion. Bounded tombstones retain
   used generations so pruning cannot authorize generation reuse. Final service
   cleanup skips already-closed pins. Broker history capacity is also checked
   at insertion so the eight pending Opens cannot overrun its existing bound.
5. **Real unpolled read; claimed happy-path race unproven.** The nonblocking
   control receive now polls for at most one second, limited by the remaining
   five-second absolute deadline, then rechecks liveness/deadline. Timeout,
   HUP/ERR, unexpected readiness or receive failure cancels. Normal wexhost sends
   each data packet only after its synchronous server endorsement call succeeds,
   and that call sends the control packet first. Scheduling jitter alone does
   not establish the report's proposed reordering or acceptance flakiness. The
   bounded poll defensively handles data arriving without an already-queued
   endorsement; no timeout retry extends the deadline.

All C changes stay under WEX_E1S6_TRANSPORT_TEST/Linux and the native changes
stay in the uninstalled lab fixture. Existing GPL-compatible headers remain.
No production define, capability, installer, broker/probe, acceptance assertion,
Wine protocol layout or generated artifact was changed. The only stock Wine
file edited in this follow-up is `server/sysrun.c`; pre-existing unrelated
working-tree changes are preserved. No commit, build, WSL/VM, Python/service or
fixture execution was performed. Static source/diff review and whitespace
checks cannot prove kernel behavior, descriptor counts or either ABI's results.

Qwen must force rebuild/relink of lab sysrun and wexhost, install the updated
Python fixture, and rerun `scripts/test_e1s6_transport.sh` as root in clean
wexbuild: **302 PASS expected, not measured here**. Preserve the complete
accept/stale/replay/late/owner/disconnect/cancel/child-impostor/pipe-impostor/
same-UID-bus-impostor matrix and shipping exclusion checks. Static path review
finds no intended change to their target sites: real service pairs share the
same root creator; the forged bus endorsement still reaches endorsement-gid;
the ordinary Wine pipe impostor still fails the earlier authority gate.
Also unverified: swapped control peer (including a different root PID), missing
peer-pidfd support, raise/send failure with prior registration retained and no
reference leak, partial handle allocation pressure, generation capacity/dead-pin
pruning/tombstone rejection, missing/delayed endorsement deadlines and FD stress.
