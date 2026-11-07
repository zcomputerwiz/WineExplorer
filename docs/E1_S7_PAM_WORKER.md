<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# E1-S7: disabled native PAM identity worker

Implemented for static review, 2026-11-06. **No compile, PAM call, Linux process,
WSL, Wine or VM test was run. Qwen builds and tests next. Do not enable in
production.** Read [S1](E1_S1_CONTRACTS.md), [S4](E1_S4_ACCOUNTS_MAP.md),
[S5](E1_S5_BROKER_RECORDS.md), [S6](E1_S6_TRANSPORT.md),
[S2](E1_S2_LAB_BASELINE.md), [E1 decisions](E1_DECISIONS.md) and
[tooling](TOOLING.md). This implements work-order row 7, not S8 evidence,
S17 presentation or S18 launch integration.

## Scope and authority

Without `WEX_E1S7_LAB`, the executable is an exit-1 stub, with no PAM/provider
implementation. No installer, ordinary build, systemd/D-Bus activation or PAM
provisioning hook enables or installs it. The native service must be started as
root; the worker is root-owned and **never setuid/setgid**. Only its parent
service starts it. Neither broker nor UI runs as host root. There is no target
execution interface, host UID/group switch, `pam_setcred`, `pam_open_session`,
password change or account recovery. Only `pam_authenticate` with
`PAM_DISALLOW_NULL_AUTHTOK`, `pam_acct_mgmt`, final-user checking and `pam_end`
can lead to an identity result. Every other PAM status, including expired
password/change-required and PAM_MAXTRIES, denies; no internal retry restarts
PAM or resets a PAM delay/lockout counter.

The fixed provisioned service is `wex-e1s-test`. A different service requires
rebuilding this isolated worker with `WEX_E1S7_PAM_SERVICE` set to a trusted
literal. Neither request nor environment contains a service/module/config path.
`WEX_E1S7_TEST` alone adds root-startup `--test-service`, `--test-confdir` and
`--test-account-dir` arguments; these must never appear in the fixed lab binary
or shipping artifacts. Test code uses `pam_start_confdir` so it creates **no
files in `/etc/pam.d`**. S2's isolated `/etc/pam.d/wex-e1s-test` and
`/etc/wex-e1s-fixtures` remain exactly as S2 provisioned them. No login/sudo
stack is modified here.

Before starting PAM, the worker requires the exact provisioned service file
to exist, be nonempty and LF-terminated, with root-owned protected nonsymlink
ancestors, no group/other write, no access ACL and one hard link. This prevents
libpam's missing-service fallback to `other`; included stacks/modules remain
trusted provisioning inputs requiring distro review.

S4's existing `wine/server/accounts.c` is compiled unchanged into the native
worker with `WEX_ACCOUNTS_STANDALONE`. A request supplies canonical name, UID,
incarnation key and map generation from the trusted selection/lookup. The worker
independently opens the protected S4 authority and checks all four before PAM,
then repeats the lookup for final `PAM_USER` after full authentication/account
success. A changed name (even a same-account rename), UID, incarnation or map
generation denies and requires a fresh selection/confirmation. No display name,
UID-only match or invented SID can substitute for the map. The S4 provider's
existing local-account/NSS/protected-file/ambiguity checks apply.

**Identity only:** a valid Eve password may return Eve identity. Administrator
eligibility, fresh policy/group generations, entire S5 binding, service-wide
attempt/concurrency quotas and single-use S1 kind-3 evidence belong to S8 and
the broker. A worker result or random seed never approves a Wine launch. There
is no readiness/capability advertisement or new S1 record kind.

## Private channels and protocol

The parent creates an `AF_UNIX/SOCK_SEQPACKET` pair for START/RESULT and passes
the child endpoint through `--control-fd`. Kernel `SO_PEERCRED` must identify
the actual root parent. This is not a public socket or a setuid command line.
The inherited `--dialog-fd` is the specific S6-pinned child's existing private
channel. Prompt/answer bodies travel **directly between C and that child**;
Python, D-Bus, broker metadata and audit never carry answers. The parent supplies
the retained kernel pidfd through `--peer-pin-fd` and its associated numeric
`--peer-pid`/`--peer-uid`, derived from S6 pins, not a public client claim. The
worker checks pidfd fdinfo/liveness and every answer's kernel SCM_CREDENTIALS
and SCM_PIDFD. An inherited/delegated FD from another writer cannot answer as
the pinned child. Unsupported kernel pinning fails closed.

Each packet is exactly one seqpacket. Integers are unsigned LE, explicit bytes,
no native C layout or padding. Header, 72 bytes:

| Offset | Field |
| --- | --- |
| 0 | `E1S7PAM` plus NUL, 8 bytes |
| 8 / 10 / 12 | version u16 (=1), type u16, total length u32 |
| 16 / 24 | frozen request ID u64, attempt u32 (both nonzero) |
| 28 / 32 / 36 | round u32, message index u32, PAM style u32 |
| 40 | fresh nonzero attempt nonce, 32 bytes |

| Type | Payload and rules |
| --- | --- |
| START=1, parent only | key[16], UID u64, map generation u64, absolute `CLOCK_MONOTONIC` deadline ns u64, attempt limit u32, name byte length u32, canonical ASCII name (1..63, no NUL). Total 121..183 bytes. Round/index/style zero. |
| PROMPT=2, to pinned child | 0..4096 bytes strict UTF-8, no NUL. Round starts at 1 per callback, message index starts at 0 within that callback; style is PAM_PROMPT_ECHO_OFF=1, PAM_PROMPT_ECHO_ON=2, PAM_ERROR_MSG=3 or PAM_TEXT_INFO=4. UI must mask style 1 and escape/render all text as plain text under its own headings. No UI is implemented here. |
| ANSWER=3, from pinned child | Exact echoed header binding, round/index/style, 0..4096 strict UTF-8 bytes without NUL. Information/error styles require empty acknowledgement. Other styles supply a distinct answer for that prompt, never reuse one saved password for every field. |
| CANCEL=4, from pinned child | Empty, bound ID/attempt/nonce, round/index/style zero. EOF, unexpected packet/style/encoding/order, extra/early answers, truncation, ancillary data or binding mismatch also abort. |
| RESULT=5, parent only | status u32, key[16], UID u64, map generation u64, seed[32]; exactly 140 bytes. Round/index/style zero. Status 0 means both PAM calls and final S4 lookup succeeded. Status 1 is the **only public failure class**; all remaining payload bytes are zero. No existence/locked/expired/backend distinction is exposed. |

Limits: eight callback rounds, eight messages per callback, 64 messages total,
4096 bytes per prompt or answer, 4168 bytes per packet, one PAM authentication
submission per worker. START's attempt and limit are 1..3, with attempt <= limit.
It carries an existing absolute deadline, never an extension; at most 120 seconds
remain. A common-host monotonic timer covers NSS, modules/delays, account checks
and `pam_end`, including a module hung outside its callback. No unbounded packet
allocation is driven by peer bytes. The initial START wait also has a 120-second
bound. S8 must enforce total request time/queueing and cross-worker attempt counts;
starting a new process is not throttling.

RESULT is produced once after `pam_end`, a final cancellation/deadline gate and
fresh 32-byte `getrandom` seed. The supervisor checks exact result size/binding/
selected identity, deadline and natural process exit. Its private `take_seed()`
is single-use and clears retained result bytes. The seed is material for S8's
connection-owned record, not a portable capability, signature, PAM cache or S1
evidence record. No success result/seed is sent over the current S6 broker link.
The repaired default lab path uses the fixed conversation and release bound
below; real PAM prompt/timing equivalence remains unverified.

## S6 hook and supervision

Only `tools/e1s6/e1s6-lab.py` has hook points; the existing uncommitted wexhost,
Wine, broker and S4 files were not edited. Default S6 behavior stays the empty
metadata echo. Root provisioning may explicitly append:

```text
--pam-worker /PROTECTED/PATH/pam-worker --pam-audit /PROTECTED/PATH/audit.log
```

The service pins the root-owned executable and existing root-only audit file
through protected nonsymlink ancestors, rejects extended access ACLs, and execs
the held inode via `/proc/self/fd`. No request nominates these paths. Install the
root-protected `tools/e1s7/supervisor.py` as a sibling directory of `e1s6` when
assembling this manual lab fixture; its protected path is checked before import.
The existing S6 fixture installer does not copy/enable S7 automatically.

After S6's ordinary endorsement and child registration, provisioned PAM mode
expects role-3 registration (112 bytes) + START rather than the empty answer.
It requires the sequence-2/operation-3 server endorsement and compares the START
ID/attempt/nonce with the saved S6 template. Actual broker credentials/pin and
registration/generations remain S6 checks. The complete S5 digests/channel stay
in that transaction; START cannot replace them. The worker inherits only its
pinned executable, control/dialog/audit descriptors, peer pidfd and `/dev/null`
standard streams, in a new process group while keeping its parent's Unix
session. This group is for supervision; no POSIX/PAM session or UID switch.
The supervisor passes `--executable-fd`, which the worker closes after exec and
before NSS/PAM. The worker restores CLOEXEC on transaction/audit/pin descriptors before PAM,
so a module's helper exec cannot accidentally inherit its private channels.

The S6 setup still has its existing **five-second** lab deadline; the hook
shortens the worker's supervision deadline to that value. It does not lengthen
S6's stub policy. Every 50ms S6 checks peer/owner/generation/process liveness and
peeks only the public 72-byte dialog header. Cancel/EOF can therefore stop a
module even outside its callback without Python reading a secret body. Broker
disconnect/cancel, worker failure and service cleanup close the transaction.
The supervisor TERM/KILLs only its own process group and waits its exact child.
It keeps the leader unreaped through final signaling to prevent PID/group reuse.
If SIGKILL cannot immediately terminate a kernel-uninterruptible child, a
retained non-daemon parent waiter reaps it eventually; `reap-pending` is audited
and confirmed cleanup is not claimed. No result can be accepted on that path.
The worker installs parent-death SIGKILL and rechecks parent PID. Its own
absolute monotonic signal timer exits a hung worker. A callback cancellation
wipes owned responses and exits naturally; the test checks natural exit before
cleanup's kill fallback. Deadline/forced kill cannot run general PAM cleanup.

The present PE/wexhost transport and metadata child cannot emit START or render
these prompts. They are deliberately untouched. The C worker/supervisor fixture
is independently driveable; the native S6 branch is ready for later trusted
dialog plumbing, **not an end-to-end Wine credential path or a tested S6 PAM
transaction**. S8/S17 must integrate real bound account selection and answers;
S19 owns crash-proof service-stop supervision. Trusted provisioned modules must
not daemonize/escape the owned process group; malicious root PAM code is outside
this boundary. Parent SIGKILL, detached module descendants, init/subreaper
behavior and service-wide cgroup containment remain runtime acceptance work.

## Secrets and audit

`PR_SET_DUMPABLE=0` and both RLIMIT_CORE limits zero precede input. Environment
is cleared. Packet storage is bounded, locked with best-effort `mlock` (success
recorded as 0/1), and cleared with `explicit_bzero` on each reuse, callback exit,
failure and ordinary process exit. Owned response arrays/strings are wiped
before error-path freeing. Response allocation pages also get best-effort
`mlock`; the worker never touches transferred/freed response pointers to unlock
them later, and remaining page locks end with this bounded process.
The deadline handler also wipes response strings still owned by the callback;
short signal-masked publication/free/handoff sections prevent it following
freed pointers. Once handed to PAM, those pointers are no longer tracked.
No answers in argv/environment, temporary files,
stdout/stderr, audit, raw diagnostics or D-Bus. The test client necessarily
loads S2's disposable password file; this is not a worker password store.

On a successful callback, the response allocations become Linux-PAM/module
owned and are freed there, as specified by
[Linux-PAM's response ownership contract](https://github.com/linux-pam/linux-pam/blob/master/libpam/include/security/_pam_types.h).
The worker cannot wipe or access those pointers after returning without a
use-after-free. It calls `pam_end` before identity success. Erasure/locking of
all PAM/module/libc/kernel/swap copies is **unverified**, and forced termination
does not prove erasure. Review the exact provisioned modules and their logging;
do not claim a clean dummy-canary scan proves complete erasure. The test PAM
module explicitly wipes its own replies. UI masking/clipboard/history/clearing
are S17 work.

The audit descriptor is provisioned by root, opened append-only, regular,
single-link, `root:root` 0600 in the test, with no access ACL. The worker also
checks root ownership/mode/ACL/type/link/append flags. One bounded <=255-byte
completion line per worker (plus possible deadline/cleanup line):

```text
E1S7 request=0000000000000001 attempt=1 stage=7 reason=11 pam=0 map=0 rounds=1 locked=1 success=1
E1S7 request=0000000000000001 attempt=1 event=hard-deadline success=0
E1S7 request=0000000000000001 attempt=1 event=supervisor-cleanup success=0
```

Numeric reason values: 1 setup, 2 protocol, 3 cancel, 4 deadline, 5 map,
6 PAM, 7 limit, 8 final user, 9 RNG, 10 audit, 11 identity success.
Stage values: 0 setup/protocol, 1 initial map, 2 PAM start, 3 authenticate,
4 account management, 5 final user/map, 6 PAM end, 7 RNG/result.
`pam` is the numeric PAM status, `map` is the existing S4 status. No PAM error
strings, candidate names, prompt text, answer lengths/content, nonce, evidence
seed or command/environment are logged, including on failure. No success UID
is logged by this package. Ordinary outcomes require a complete write and
`fsync` before success; audit failure always denies. Completion and supervisor
cleanup refuse writes at 16MiB; in-flight/deadline lines may exceed that soft
limit slightly. No rotation/truncation is performed. A hard-deadline line is
best effort async `write`; supervisor cleanup logging is best effort after
reaping. Persistent rate limits/retention and production audit failure policy
remain S8/release work. An audit `success=1` describes identity checks, not
delivery, consent, evidence redemption or a committed launch.

## Qwen build and test handoff

Run **as root in wexbuild or an exclusively owned throwaway VM**, with S2's
disposable identities/password fixture provisioned there. NEVER run this test
or the S6/S10 system-role provisioning harness in shared lab guest VM B, and
never remove its `/etc/wineexplorer` or `/usr/local/libexec/wineexplorer` to
bypass a refusal. Read TOOLING.md. No Windows-host build/run or change to another
agent's setup. `libpam-dev` is installed in wexbuild for the next Qwen run;
verify the actual compiler/dependencies there. Check `cc`, `<security/pam_appl.h>`, `libpam.so`,
`pam_start_confdir` (test binary), `explicit_bzero`, POSIX monotonic timers,
Linux SO_PASSPIDFD/SCM_PIDFD (kernel 6.12), Python >=3.11 (`process_group`, pidfd/waitid),
`flock`, shadow-utils, `journalctl`, `sudo` and `wexsys`. Missing fixtures or
kernel support must fail/inconclusive, never silently skip or weaken pinning.

From the synced repository in the root shell of that disposable Linux guest:

```sh
bash scripts/test_pam_worker.sh --disposable-guest wex-e1s-test
```

The script builds, with `-O2 -Wall -Wextra -Werror`, the default disabled stub,
fixed-service lab worker, test worker, test PAM shared module and unchanged S4
provisioner. Native worker linking is `-lpam -lrt`; no Wine build is needed for
these component tests. For a fixed-service isolated binary specifically:

```sh
cc -O2 -Wall -Wextra -Werror -DWEX_E1S7_LAB \
    -o /root/OWNED/pam-worker tools/e1s7/pam-worker.c -lpam -lrt
```

No command here installs the executable or a PAM file. Do not add the defines
to a shipping build. Hash and retain the exact compiled objects/source/config
when collecting evidence. Native S6 integration additionally needs the S6
WoW64 build/provenance controls, a real trusted child and the manual protected
service/module layout above; standalone component tests cannot establish it.

The script locks `/run`'s root-owned directory inode (no lock-file symlink),
creates one random `/run/wex-pam-test-XXXXXXXX` tree, and refuses existing
`wexs7_locked`/`wexs7_expired` identities. It creates those two disposable users
without homes, supplies new random passwords privately through `chpasswd`
stdin, locks one and expires the other, and removes only its own matching UIDs
on exit. It never alters Bob/Alice/Carol/Eve, their passwords or S2 configuration.
The private PAM stack uses S2's pam_listfile/onerr=fail + pam_unix authentication
and account checks, plus pam_deny/pam_permit. All paths and alternate test service
names are provisioned under the owned tree using `pam_start_confdir`. A fresh
S4 map is built against real guest NSS in that tree. No existing map is edited.
Cleanup removes that exact tree and releases the directory lock. A failure in
UID-verified user cleanup changes the final status to FAIL.

| Test | Required observation |
| --- | --- |
| Success twice | Canonical selected key/UID/generation, distinct nonzero seeds, parent-only result, single-use local seed accessor, natural exit then reap. |
| Eve | Identity success without implying administrator eligibility. |
| Wrong password / unknown / locked / expired | Same fixed masked prompt, generic failure and zero identity/seed; wrong/locked fail authentication and expired fails account management. Unknown key takes the decoy result path after the same prompt. |
| Timing classes (three interleaved samples each) | Wrong password, unknown and known-ineligible decoy have exactly `(round=1,index=0,style=1,"Password: ")`, failure status 1, zero identity/seed and 4-second post-answer release within 250 ms; spread <=250 ms. |
| Missing dedicated service | Denies even with a permissive private `other` stack, never authenticates against a fallback. |
| Multi-round/message | Two test-module rounds with info, echo-on and echo-off, followed by real pam_unix; distinct per-message replies, >=3 rounds. |
| Too many rounds | Test module asks for nine; worker denies at its eight-round limit. |
| Oversized prompt / answer | 4097-byte payload rejected without success or surviving worker. |
| UTF-8 / sequence / style | Overlong UTF-8, wrong index and unsupported PAM style deny. |
| Final user redirect | PAM changes user to root; full success cannot silently change selected identity. |
| Deadline / module hang | Missing answer and a module sleeping outside callback terminate under a two-second absolute deadline. |
| Cancel / EOF | Cancel during conversation or closing channel aborts; natural worker exit is observed before cleanup. |
| Cancel outside callback | Parent header-only watchdog stops/reaps blocked module's group. |
| Audit and journal | Scan worker audit and guest journal since test start for all disposable passwords and secret/wrong-answer canaries; no secret printed by failure reporting. |
| Disabled builds | Stub refuses work; fixed-service binary rejects test-only service argument. |

Each case confirms the exact worker is reaped and its owned process group no
longer exists. The client does not print PAM strings/exception details. The
script ends **E1S7_PAM_WORKER OK** or **E1S7_PAM_WORKER FAIL**. An interrupted
shell or SIGKILL can bypass fixture-user cleanup: inspect the two exact fixture
UIDs before manual cleanup, never delete a foreign user. The journal scan is
bounded to this test interval and known canaries; tracing, crash artifacts,
other logging sinks and complete memory/swap inspection are not covered.

## Files and verification status

New: `tools/e1s7/protocol.h`, `pam-worker.c`, `supervisor.py`, `pam-lab-module.c`,
`test-client.py`, `scripts/test_pam_worker.sh`, this document. Small native S6
hook: `tools/e1s6/e1s6-lab.py`. Register: `docs/STOCK_CHANGES.md` records **zero
additional stock Wine changes**. No changes to the protected broker/wexhost/
Wine/S4 files, no commit.

Static checks: Git Bash `bash -n scripts/test_pam_worker.sh` and tracked
`git diff --check` passed, and all new/untracked files passed no-index whitespace
checks against NUL (exit 1 indicates different file content, no check errors).
Python was absent from PATH; no Python parsing or C
compilation has been claimed. Protocol offsets, success/failure zeroing, PAM
ownership, both channel directions and parent cleanup were reviewed by hand.
All native linking, PAM/NSS behavior, signal/timer cancellation, kernel ancillary
semantics, process/pidfd/FD lifecycle, fixture cleanup and all table tests are
**unverified**. Also unverified: actual S6 worker branch, concurrency/restart/
bus-loss races, process-group escaped descendants, RNG/allocation/disk errors,
core/swap forensics and production PAM/MFA/lockout policy. Service-wide quotas,
S1 evidence binding/redemption, full dialog integration and launch remain later
packages; a successful PAM component test completes none of them.

## Combined-batch repair: fixed conversation and failure bound

Reason enumerators are now `RES_*`, avoiding libc `R_OK` and related macro
names; audit numeric values are unchanged. Disabled-binary tests require exit
1, so a missing binary/exit 127 cannot count as a successful refusal. The test
PAM module checks CLOEXEC on all remaining nonstandard descriptors.

Before account lookup, the default worker asks **one** fixed echo-off
`Password: ` prompt for every admitted selection. It retains the answer only
in bounded C storage, wipes it on handoff/failure/deadline, and answers at most
one backend echo-off prompt privately. Backend text is never forwarded. Other
conversation shapes fail closed with the same public transcript. This is a
narrow fixed single-password lab policy; general MFA requires a trusted
equivalent decoy transcript policy before enablement, with no raw fallback.

Every ordinary result, including success, releases four seconds after the
answer. PAM's own failure delay/lockout accounting is not disabled or restarted.
A monotonic timer caps NSS/PAM/pam_end at the same release time; backend overrun
wipes owned buffers, sends only a prebuilt generic failure and exits 1 for exact
supervisor reaping. Forced termination cannot run general PAM cleanup or prove
erasure of library copies; process-group supervision remains mandatory.
The original absolute deadline is never extended. Fewer than four seconds
remaining denies; cancellation, EOF and pinned-peer death abort the wait.

Private `DECOY=6` has START's layout, with zero key/UID and literal name `decoy`;
map generation, request, attempt, nonce and deadline stay bound. Only the root
service factory constructs it. It uses the same prompt/release path without
authenticating an account and can only return status 1 with zero identity/seed.
S7 uses that behavior for an ordinary START whose S4 snapshot no longer resolves.
Supervisor and S8 independently reject successful decoy results.

`WEX_E1S7_TEST` alone adds root-startup `--test-raw-conversation 1` for the
existing multi-round/fault tests. It is never account-selected, never accepted
for DECOY and absent from the fixed worker. Raw tests retain eight-round bounds
and do not claim normalized timing. The default-path test compares full public
prompts, worker-start-to-first-prompt latency and post-answer latency across
three interleaved samples per class. First-prompt latency spread must also be
<=250 ms.
Required local tolerance: **250 ms**, each result 3.9..4.25 s, full spread <=250 ms,
excluding user think time. This is an unmeasured acceptance threshold, not a
constant-time NSS/PAM claim. Scheduling overload, broken audit/storage/transport,
quota denial, cancellation and original deadline exhaustion remain fail-closed
operational failures. Qwen must validate the actual kernel/stack without weakening
the transcript or bound to make the test pass.
