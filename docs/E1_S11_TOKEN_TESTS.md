<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# E1-S11 token test prerequisites and runnable matrix

2026-11-07. Implementation and **static review only** in this change. No Linux
build, protocol generator, Wine, WSL, VM or matrix execution was performed.
S6's accepted runtime evidence is recorded in the last S6 status bullet of
[E1_DECISIONS.md](E1_DECISIONS.md); it is a prerequisite, not an S10 result.
The earlier S10 probe built for both ABIs. The extended probe in this change
still needs both builds and the complete run below. S11 acceptance remains open.

Current-tree protocol: S13 advances the version to **978**, appending request
325 `load_e1s_profile`. S10 request 324, its byte records and the 88-byte raw
carrier are unchanged. The harness pins the current two-request suffix even
when S13 coverage is disabled.

Read with [S10's wire contract and mode table](E1_S10_TOKEN.md),
[S6's owned fixture](E1_S6_TRANSPORT.md), [S5](E1_S5_BROKER_RECORDS.md),
[standard-user packages 10 and 11](E1_S_STANDARD_USERS.md#7-ordered-delegated-work-packages) and
[TOOLING.md](TOOLING.md). Synthetic installation is not S8 authentication or
S18's trusted consumed-record producer/validator. Nothing here enables shipping
credentials, token issuance, profiles or an elevation launch route.

## Review fixes verified against source

The independent review is
`D:/DeepThought/ChromiumLab/guest-sysrole/s10-s5-review-antigravity.md`.
Both requested findings are real, but their owning files differ:

* `src/programs/wexelevate/main.c:service_main` registered `handler` before
  creating `stop_event`. The handler already used initialized `table_cs`, but
  could call `SetEvent(NULL)` and lose an early STOP/SHUTDOWN. Initialization
  now creates and checks the manual-reset event before registration. Failed
  registration closes it. The event remains latched for service/launch checks;
  the existing stop/lease gates and serialization before resume are preserved.
* `wine/server/token.c:e1s_install_consumed` grabbed caller/primary references
  and returned directly on failed `e1s_caller_current`. The consumed context
  retained both objects until close. That sole failure after the grabs now
  releases and clears both; earlier failures take neither reference. Validator
  cookie ownership still transfers only on successful installation. The context
  remains permanently consumed on failure. The function does not live in the
  broker's `e1s.h`; that header was inspected and needs no change.

Lost-reply termination is the review's separate S19 finding and is unchanged.
This patch does not claim its resolution or runtime race/leak evidence.

## Qwen run procedure

Use **root in clean wexbuild or an exclusively owned throwaway VM**. Never run
this provisioning script in shared VM B. It refuses `/etc/wineexplorer`,
`/usr/local/libexec/wineexplorer`, `/var/lib/wexsys-s10`, fixture users/homes/
private groups `wexadm`, `wexstd`, `wexcar`, `wexret`, and groups `wexsysc2` and
`wexs10admin`. Do not remove/move installed state to bypass a refusal. No keep,
reuse or skip option is accepted. Run this alone, with exclusive ownership of
the two runtime builds. The lab tree is intentionally left as a lab build.

Before invoking it:

1. Sync and assemble the exact Wine and project overlays in a root-controlled
   Linux source tree. Regenerate `tools/make_requests` and compare the three
   checked-in artifacts as S10 step 1 requires. Verify protocol 978, request 324
   and appended request 325,
   fixed request/reply 32/24 and max 88. Resolve any generator differences.
2. Configure/build **two separate root-protected WoW64 runtimes**, both with
   `--enable-archs=i386,x86_64`, matching S10's exports and S6's corrected handle
   access. Both must already contain wineserver, Unix ntdll/wexhost and both PE
   ntdll/wexhost DLLs. The normal runtime must have neither test define. Do not
   substitute the lab runtime for the normal control. No system install is
   performed by this harness.
3. Supply both MinGW GCC compilers, native GCC/make, runuser/shadow tools,
   ACL tools, Python 3 with dbus and GI/GLib, system D-Bus/dbus-send, flock,
   Xvfb, iconv and GNU timeout/coreutils. S6's required Linux pidfd credential
   support and close_range must work. Missing kernel/bus support fails setup.

From the assembled repository root, for example:

```sh
WINE_BUILD_DIR=/root/owned-e1s11/build/lab-wow64 \
WINE_NORMAL_BUILD_DIR=/root/owned-e1s11/build/normal-wow64 \
    bash scripts/test_e1s10_token.sh
```

The harness first creates `/var/tmp/e1s10-results.XXXXXXXX` and prints its path.
It builds the probe with the documented commands (the only added include search
path is `src/include`, never `wine/include`):

```sh
x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror -Isrc/include \
    scripts/e1s10_token_probe.c -ladvapi32 -o "$OWNED_OUT/e1s10-64.exe"
i686-w64-mingw32-gcc -O2 -Wall -Wextra -Werror -Isrc/include \
    scripts/e1s10_token_probe.c -ladvapi32 -o "$OWNED_OUT/e1s10-32.exe"
```

It also builds the existing `wexdev-service.c` adapter, roles bootstrap probe
and S4 `wex-accounts` tool. It explicitly removes the lab's `server/token.o`,
`server/sysrun.o` and `dlls/wexhost/e1s6.o`, recompiles token.o with
`CFLAGS='-g -O2 -DWEX_E1S10_TOKEN_TEST -DWEX_E1S6_TRANSPORT_TEST'` (not EXTRA_CFLAGS: Wine's makefiles ignore it), recompiles the
two S6 objects with the S6 define, then relinks wineserver and wexhost.so.
Compiler commands must contain the expected flags; review `build-token.log`,
`build-s6.log` and `build-link.log`. This follows the S6 documentation's build
recipe: `test_e1s6_transport.sh` itself uses an already built runtime and does
not build Wine. No production Makefile or test export was added.

The script sources `scripts/lib/sysrun-fixture.sh` for machine provisioning/
registry/service helpers and **sources `scripts/lib/e1s6-fixture.sh`** for the
real native endpoint, bus policy, S6 lock and inode-checked cleanup. It does not
copy their implementation. Each lab broker must complete OPEN, CHILD_START and
EXCHANGE with the real S6 endpoint/child and byte-identical echo before S10
negotiation. `E1S10 TRANSPORT OK` is mandatory, including in negative cases.

Each case starts a LocalSystem service adapter in Bob's standard-user prefix.
The adapter starts the selected ABI staged as the exact root-controlled
`windows/system32/wexelevate_test.exe`. The shipping broker is not replaced.
The peer uses the other ABI, Bob's same prefix/server/session and a fresh pipe.
Both broker ABIs are exercised sequentially. An independent outer 120-second
broker watchdog stops only that owned prefix on timeout; the peer and standalone
denial clients have their own `timeout -k 2 90`. Bootstrap is bounded at 130
seconds. S6's `el_wait_file` permits 90 seconds and its service-report wait
permits 120 seconds; the old shared 20-second budget also included cold
Wine/SCM and the other ABI's startup. Only outer orchestration limits changed:
S6 and synthetic token-install authority still expire after five seconds, the
membership gate after three seconds. A timeout/setup/transport failure always fails the case.
The adapter's flushed `.done` must report broker exit 0, broker output must
include exact `E1S10 OK` and `E1S10 CASE MODE OK`, peer exit must be 0 with
`E1S10 PEER OK`, and neither log may contain FAIL. Positive S10 cases also
require exactly two `E1S10 IDENTITY OK` lines. Broker greps run after `.done`,
since a peer can exit before broker cleanup and final output complete.
The wait also requires nonempty `.done`: the adapter creates that file before
writing its exit code, so existence alone can race an empty completion file.
Standalone denial/normal checks have no peer and still require exit 0 and OK.

The lab fixture is completely cleaned before provisioning the normal fixture.
Normal SYSTEM operations use the same staged name and service adapter, without
trying to establish a lab S6 channel. They must return STATUS_NOT_SUPPORTED for
NEGOTIATE, ISSUE and TEST_INSTALL. Ordinary and Limited normal clients must also
receive STATUS_NOT_SUPPORTED; lab unauthorized clients require ACCESS_DENIED.
Normal server/wexhost string exclusions are checked against positive lab controls.

## Covered cases

All matrix cases below run for both broker/client ABIs. Alice = `wexadm`,
Bob = `wexstd`, Carol = `wexcar`. The private configured administrator group
`wexs10admin` isolates membership tests from the machine's real sudo group.

| Case | Required observation |
| --- | --- |
| `accept` Alice and Carol | Different S4 user SIDs; Primary/Full/High, user/owner SID, enabled Administrators and primary/logon groups, 21 privileges/Tcb and notify defaults, SYSTEM/account DACL, non-inheritable handle, session and query/response agreement. Two contexts issue distinct nonzero logons, different from Bob; DuplicateTokenEx preserves logon. Bob's TokenId, ModifiedId and AuthenticationId remain unchanged. |
| `unknown` | Absent canonical account, arbitrary valid nonzero key; successful install followed by ISSUE denial. |
| `tombstone` | Provision and retire `wexret` with S4, save its original key/SID, remove its NSS account to prevent later sync from re-creating it. Use the old record and fresh pipe; ISSUE denies. |
| `stale` | Increment installed map generation; real ISSUE lookup denies. |
| `no-cap` | Real S6 control then negotiation missing TOKEN_V1; no context/token. |
| `digest`, `evidence` | Alter bytes only after successful install; ISSUE denies. Restored original bytes still deny. |
| `expired` | Sleep 5100 ms after the server's five-second synthetic install deadline; ISSUE denies. |
| `replay` and replay after every issuance path | Successful issuance followed by repeat denial; negative ISSUE followed by repeat denial. Canonical failure and no token required. |
| `absent-install` | Negotiated context without TEST_INSTALL cannot ISSUE; retry also denies. |
| `raw-short`, `raw-trailing`, `raw-oversized`, `raw-malformed` | Bypass wrapper validation through existing exported `wine_server_call`: 203, 205, 269 bytes or nonzero reserved field. Installed record burns before rejection; retry with restored canonical 204 bytes denies. Check the raw server failure and absent reply handles. |
| `thread`, `process` | Actually duplicate the installed context, use it on another thread or inherit the copy into a new process with the original input shared through an owned mapping; ISSUE denies. Original owner thread subsequently issues successfully, proving setup and no accidental consumption there. |
| `caller-death` | After install, broker tells the peer to exit normally, waits for the retained process to signal, then requires ISSUE and retry denial. Peer exit 0 remains mandatory. |
| `group-removed` | Pause after install, root removes Alice from the private administrator group and runs S4 sync. Key/generation must stay unchanged, so ISSUE denial measures membership revocation. Root releases the gate, then restores membership via S4 sync. |
| `missing-group` | Pause after install; delete only the owned configured admin group, release the gate, require denial. Restore group/memberships and sync before continuing. |
| `denied` Bob and Limited Alice | Query the actual expected elevation type first; NEGOTIATE, ISSUE and TEST_INSTALL all deny without a handle, with canonical ISSUE failure. |
| Normal runtime SYSTEM, Bob, Limited Alice | All three operations return STATUS_NOT_SUPPORTED, including from the correctly staged LocalSystem image. |
| Benign child and grandchild | After successful positive issuance and replay check, close the context and use CreateProcessAsUser with the issued primary. Child queries its own SID, primary type, Full elevation, AuthenticationId, derived logon SID and session; ordinary CreateProcess descendant must preserve them. Both exit 0. No credentials/profile handoff or production launch code. |

The raw driver mirrors S10's unchanged 88-byte request union and the
native/32-bit request-info pointer/iovec carrier; it does not include Wine SDK
headers in a MinGW compilation. Compile-time offsets and a harness protocol
number/version/count guard reject drift. These four raw cases cover bounded
representative malformed requests, not all possible byte mutations or enormous
IPC traffic. Missing export or ABI mismatch fails, never skips.

The process-copy child inherits SYSTEM but has no independent S6 registration;
its denial verifies that a copied context does not carry authority. It does
not isolate `c->owner` from the broker-role gate using a second fully registered
broker. Thread copying exercises connection/thread affinity in a real broker.

## Output, cleanup and retained evidence

Expected output includes (additional cases follow the same convention):

```text
results=/var/tmp/e1s10-results.<unique>
probes_both_abis=PASS
lab_build_and_normal_exclusion=PASS
lab_accept_wexadm_64=PASS
lab_accept_wexcar_64=PASS
lab_raw-oversized_wexadm_32=PASS
normal_system_all_operations_32=PASS
NOT_COVERED=...
E1S10_TOKEN OK
```

There are 50 matrix PASS lines (44 lab, 6 normal), plus the two build/control
PASS lines, if all current cases pass. On any failure the exit is nonzero and
the final line is `E1S10_TOKEN FAIL`. PASS lines describe only completed cases;
a final FAIL invalidates whole-matrix acceptance. Preserve the entire output,
all build logs and both phase directories, not just the last line.

EXIT/INT/TERM traps stop the watchdog/peer/bootstrap and only the owned prefix
servers, stop/reap S6's exact native-service PID, remove its inode-checked files
and reload bus policy. The staged broker is removed only at its recorded inode;
the fixture hive is restored after shutdown, then the newly created users/groups
and inode-checked role/output directories are removed. User-scoped process
cleanup covers only accounts created by this invocation. No global wineserver
kill, shared guest reset, password, PAM policy or production installation occurs.
If a fixture directory/image was replaced, cleanup refuses the foreign object
and reports FAIL; manual inspection is then required. Result logs and empty lock
files remain. Cleanup copies setup and per-case broker/completion/timeout logs,
peer logs, bootstrap output, `case-N-wineserver.stderr.log`, command text and
exact UTF-16LE command bytes into the phase's `fixture/` directory, including on
failed peers or provisioning. The stderr file captures the prefix wineserver
and service children inheriting bootstrap stderr, as in S6's `e1s6-wine.log`.
Failure collection runs before prefix teardown and again after shutdown for
final output. The watchdog also snapshots before killing a timed-out prefix.
`failure-pipes.log` or `timeout-pipes.log` enumerates `\\.\pipe\*` in Bob's same
prefix, capped at 256 entries and `timeout -k 2 5`; unsupported enumeration or
a stalled server is recorded as unavailable/incomplete. Wine pipe objects are
not Linux FIFOs. The harness then cleans the owned provisioning directories.

## 2026-11-07 failure repair and static case audit

The original evidence remains in [E1_S10_HARNESS_FAILURE.md](E1_S10_HARNESS_FAILURE.md).
There is a concrete pre-transport rights mismatch: the original `broker()` opened
Bob's process with only `PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE`, then
called `OpenProcessToken(TOKEN_QUERY)`. In `wine/server/token.c:open_token`,
`sysrun_active && elevation_enforced()` requires `PROCESS_QUERY_INFORMATION` on
that process handle. `wine/server/handle.c:get_handle_obj` denies missing rights
with STATUS_ACCESS_DENIED; `kernelbase/security.c:OpenProcessToken` maps that to
Win32 5. SYSTEM does not bypass the granted-access mask of an existing handle.
The probe now requests `PROCESS_QUERY_INFORMATION | SYNCHRONIZE`.
`wine/server/process.c:process_map_access` adds LIMITED_INFORMATION to that grant,
preserving TEST_INSTALL's limited-query requirement. No server check changed.

This deterministically predicts an `OpenProcessToken` failure before S6 OPEN
on the old enforced fixture. It fits READY without TRANSPORT OK, but the old
silent branch log does not prove the precise executed branch. Nor does it alone
prove the peer's 20-second timeout: the old cleanup attempted to write failure
marker 1, which a running peer should read and return. The original peer printed
no lines on either success or early failure. Its empty log cannot distinguish
pre-main loader startup, WaitNamedPipe, the result read, teardown or buffering.
New flushed startup/stage lines and captured stderr resolve that distinction on
the next run. An empty new peer log indicates it did not reach the startup print.

Other candidate checks against source:

* The pipe SD is `D:(A;;GA;;;SY)(A;;GRGW;;;WD)`: SYSTEM all, World read/write.
  Bob is `wexstd`, the ordinary standard-token peer, not the selected S4
  administrator. In this tree its initial user SID is `local_user_sid`,
  S-1-5-21-0-0-0-1000 (`security_unix_uid_to_sid`); it retains enabled World
  membership when filtered/made standard. No administrator SID is needed for
  the pipe DACL. The explicit SD has no mandatory label/SACL; object SD defaults
  do not invent one here. The DACL and remote-client rejection are unchanged.
* Bash `pipe="\\\\.\\pipe\\e1s10-..."` produces `\\.\pipe\e1s10-...`.
  `quote_arg` adds Windows argument quotes and rejects embedded quotes/trailing
  backslashes. Backslashes before ordinary characters remain literal under the
  Windows argv parser. `printf '%s'` and iconv encode the command verbatim as
  UTF-16LE; the service adapter reads even byte counts, adds a zero terminator
  using calloc and passes the writable command directly to CreateProcessW.
  Registry escaping applies to the adapter ImagePath, not to command-file bytes.
  No extra command-file unescaping occurs. Both endpoints now print the name;
  retained command files allow byte-for-byte inspection of the actual run.
* The broker creates the pipe, flushes READY, then calls ConnectNamedPipe;
  the harness launches the peer after READY. Blocking ConnectNamedPipe does
  not prevent that separate Wine process from starting. ERROR_PIPE_CONNECTED
  remains a valid raced connection. The adapter reports SERVICE_RUNNING before
  CreateProcessW, so SCM startup does not wait for the broker to accept a peer.
  No static circular wait was found.
* Peer traffic is one byte in each direction; it shares no native structure
  with the broker. S6 records use fixed-size byte buffers. The raw carrier is
  broker-local, with compile-time offsets matching `wine/server.h` and the
  `__server_request_info32`/`wow64_wine_server_call` translation in
  `ntdll/unix/server.c`. Mixed ABI is intentional; runtime ABI proof is still open.
* Before first ISSUE, the broker must consume marker 0x5a, query Bob's retained
  token/session, complete S6 OPEN/CHILD_START/byte-identical EXCHANGE, negotiate
  exactly TOKEN_V1 + ACCOUNT_MAP_V1, then install the synthetic record. The
  record copies root/server/broker generations from S6 and Bob's queried
  session. None of these checks was bypassed to reach issuance.

All failure jumps now use a named `require` check; each assertion reports its
step, function, line, expression, GetLastError and the last explicitly captured
NT operation/status. LastError on logical assertions is ambient context, not
an API failure code; transport failures report their actual Win32 return too.
There is no terminal `check(FALSE)` hiding an earlier cause. Export resolution
now stores the function pointer in `token_export`, completing the interrupted
diagnostic wrapper edit. Copied-process stdout/stderr inheritance is retained.

The following table rechecks **every row** of the covered-case table above,
including invocation shape and pass-output agreement. All paired cases use
`broker PIPE NAME KEY GENERATION SID MODE GATE` (argc 9); peer arguments are
`peer PIPE` (argc 3), except the caller-death row. Common paired success means
broker `E1S10 TRANSPORT OK`, `E1S10 CASE MODE OK`, `E1S10 OK`, exit 0 and
flushed adapter `.done=0`; peer `E1S10 PEER RESULT marker=0`, `E1S10 PEER OK`,
exit 0. Harness greps the completion lines, rejects FAIL, and keeps the real
transport positive control mandatory even for negative token cases. Additional
stage names below have the same `E1S10 ` prefix; they aid diagnosis, while the
case-specific completion certifies all assertions for that mode succeeded.

| Cases checked | Broker path / additional output on pass | Peer / harness agreement |
| --- | --- | --- |
| `accept` Alice/Carol; `replay` | Two installed contexts issue successfully with different logons; `ISSUED FIRST`, `ISSUED SECOND`, `REPLAY CHECKED`; all token-shape/response/caller checks remain. | Common paired success; exactly two `IDENTITY OK` lines are required for child/grandchild. Alice/Carol SID difference is checked before the cases. |
| `unknown`, `tombstone`, `stale` | Install succeeds, then real lookup denies ISSUE and retry: `INSTALLED`, `ISSUE DENIED`, `REPLAY CHECKED`. Unknown uses absent name/arbitrary key; tombstone uses saved retired key/SID with NSS identity removed; stale increments generation only in the record. | Common paired success; all three modes exist and args match. |
| `no-cap` | After real transport, NEGOTIATE without TOKEN_V1 must return STATUS_NOT_SUPPORTED with zero caps/context/token/size; `NO CAP DENIED`. No installation or ISSUE. | Common paired success; tightened from any nonzero status to the handler's precise status. |
| `digest`, `evidence` | Mutate only after install; ISSUE denies, restore bytes, retry denies; `ISSUE DENIED`, `REPLAY CHECKED`. | Common paired success; original input restored before retry. |
| `expired` | Sleep 5100 ms after install; ISSUE and retry deny; `ISSUE DENIED`, `REPLAY CHECKED`. | Common paired success; five-second token expiry unchanged despite larger outer bounds. |
| `absent-install` | Skip TEST_INSTALL; ISSUE and retry deny; no INSTALLED marker. | Common paired success; the mode is recognized and uses negotiated context. |
| `raw-short`, `raw-trailing`, `raw-oversized`, `raw-malformed` | Raw calls submit 203/205/269 bytes or reserved-field corruption; status ACCESS_DENIED, no raw reply handles, canonical server failure; restored 204-byte retry denies. | Common paired success; four names match dispatch; carrier offsets and current protocol guard checked. |
| `thread`, `process` | Copied context ISSUE denies; original owner then issues twice; `COPY DENIED`, `ISSUED FIRST`, `ISSUED SECOND`, `REPLAY CHECKED`. Process uses inherited mapping/context; `copied HANDLE` argc 3 exists. | Common paired success plus two IDENTITY OK lines. Copied child inherits adapter stdout/stderr; failure is checked through its exit code and retained output. |
| `caller-death` | Install, send marker 0x7e, wait for retained process signal, ISSUE/retry deny; `CALLER EXITED`, `ISSUE DENIED`, `REPLAY CHECKED`. No second result write to the exited peer. | Uses existing `peer-death PIPE` mode; peer requires marker **126**, prints PEER OK and exits 0. Forced timeout cannot count as caller death. |
| `group-removed`, `missing-group` | INSTALLED is flushed before root mutates membership/group; gate release within 3s, ISSUE/retry denial; `MEMBERSHIP GATE RELEASED`, `ISSUE DENIED`, `REPLAY CHECKED`. | Common paired success. Harness waits exact INSTALLED, verifies key/generation unchanged for membership removal, releases gate, restores membership/group and syncs before next case. Deadline failures stay failures. |
| Lab `denied` Bob/Limited Alice | `denied default` / `denied limited` argc 3 queries actual elevation then requires ACCESS_DENIED for NEGOTIATE/ISSUE/TEST_INSTALL and canonical ISSUE failure; `E1S10 OK`. | No peer. Harness requires exit 0, exact OK, no FAIL, both ABIs. |
| Normal SYSTEM/Bob/Limited Alice | SYSTEM `normal` argc 2 also checks SYSTEM SID; ordinary `normal-denied default/limited` argc 3 checks elevation. All three operations require STATUS_NOT_SUPPORTED, canonical ISSUE failure and no token; `E1S10 OK`. | No peer/S6. Service case requires `.done=0` plus OK; ordinary cases require exit 0 plus OK and no FAIL. All modes exist. |
| Benign child/grandchild | `identity SID AUTH_HIGH AUTH_LOW SESSION DEPTH` argc 7 checks its primary SID/type/Full/auth/logon SID/session; depth 1 creates depth 0; each flushes `E1S10 IDENTITY OK`. | Both exit codes are checked; positive S10 cases now also grep exactly two completion lines after `.done`. No profile/production launch interface was added. |

S13 still selects its own source, compiler defines, profile/storage checks and
`hku-peer PIPE SID KEY NAME LOADED` argc 7 route. Its peer now uses the same
90-second outer bound and PEER OK check; before/after HKCU checks and the two
required `E1S13 HKU CHECK OK` lines remain. S13's conditional child behavior is
unchanged; the unconditional two-IDENTITY-line requirement applies only to S10
positive `run_case` modes. The diagnostic `pipes` mode runs before private export
resolution and does not issue tokens or count as a matrix case.

Static verification: Git Bash `bash -n` for the two changed shell files and
`git diff --check`; no build, harness, Wine, WSL, guest, service or generator
execution. No table row was omitted from source review. All runtime results,
actual mixed-ABI compilation, precise old executed branch/peer stall, namespace
enumeration support, cold-prefix timing, cleanup behavior and S13 regressions
remain unverified. The existing NOT_COVERED items below remain outside this repair.

## Explicitly not covered / acceptance still required

* Broker primary replacement and broker TokenId/ModifiedId mutation; caller
  primary replacement/version mutation. A controlled TCB driver must replace,
  independently verify and restore actual objects, and keep unrelated authority
  gates valid. Existing S5 replacement tests are not S10 evidence.
* S6 re-registration with an old context, including replacement generation and
  old-channel revocation ordering. Needs a dedicated re-registration driver;
  this harness creates fresh processes/contexts instead.
* Allocation/handle/FD failure and LUID exhaustion. No deterministic injection
  facility exists here; any future server injection must stay inside the existing
  S10 **and** S6 lab guards, never production. No such hook was added.
* A copied context used by a second independently registered broker, stressed
  concurrent copies/cancel/stop/close, reference-count instrumentation, the
  service's early-stop race under SCM scheduling, NSS races and large-input
  fuzzing. Those require controlled orchestration or instrumentation beyond the
  bounded probe modes here.
* Child/grandchild host UID measurement, desktop inheritance and broader
  process-tree/crash supervision. The benign driver checks Windows identity and
  session; it does not measure raw Unix credentials or implement S19 launch.
* S18 real S8 evidence redemption/current-authority validator, S9 authentication
  acceptance, profiles/HKCU/environment and actual broker credential launch.
  Always-true synthetic callbacks cannot establish these properties.
* Protocol regeneration, compile/link/export/WoW64 correctness and **every run
  above** are unverified in this sandbox. The native Git Bash syntax parse and
  root/Wine `git diff --check` are static checks only. Qwen must also run S10's
  `wex_token`/`security` and kernel32 pipe checks in both ABIs, preserve the
  filtered-admin approve/cancel, standard linked-token/grant denial, S3
  provenance and S6 impostor regressions, and check installed artifact exclusion
  separately. A passing matrix must not be reported as complete E1-S11 until
  the remaining required evidence is accounted for.
