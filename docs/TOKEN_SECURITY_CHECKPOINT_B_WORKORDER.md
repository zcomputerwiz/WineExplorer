<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Gemini: checkpoint B — request-bound authorization and elevation integration

Date: 2026-10-18. Start from the published `codex/token-security-correctness`
parent commit containing this file and its pinned Wine revision. Use a separate
branch such as `gemini/token-security-b`; do not continue from the earlier
`e35732a8` submission alone. Read `TOKEN_SECURITY_REPAIR_REVIEW.md` and its
evidence first. This is an implementation order, not a passing acceptance report.

## Ownership and scope

Read CLAUDE.md, TOKEN_SECURITY_WORKORDER.md, TOKEN_SECURITY_FIX_WORKORDER.md,
TOKEN_SECURITY_REFERENCES.md, ELEVATION.md, PREFIX_SPLIT.md and its work order,
DBUS_PLAN.md, STOCK_CHANGES.md and WEBVIEW2.md. Earlier blanket COMPLETE/PASS
captions are author claims superseded by the independent repair report.

Keep useful token/access-check work. The immediate objective is correct NT
permissions, authorization, elevation and application compatibility, not hostile
native-syscall containment or a certified sandbox. A full token is not Linux root;
a password-shaped dialog or a desktop dimmer is not authorization or isolation.
Ordinary incorrect permission denials/grants remain functional defects.

Do not switch/reset another checkout, reuse another worker's build directory,
modify their WSL distro, import graphics/OOBE WIP, dispatch agents, force-push or
merge/push master. Work in owned builds/prefixes/displays/process groups. Do not
run permission migration against the user's real machine tree or reset/reinstall
an existing VM. Obtain a disposable installed-test designation before that gate.
Coordinate wexhost and machine-prefix ownership before overlapping changes.

New project code stays GPL-2.0-or-later compatible; Wine retains LGPL notices.
Samba/GPLv3 sources are design guidance only, not code to copy. No proprietary
payloads, reference source, copied/traced art, passwords or authentication cookies
in Git/logs/argv/environment. Test helpers must never be shipped as authorization
alternatives or linked into production consent.

## Entry gate A — retain the independent repairs and prove remaining semantics

The repair fixes deny-only matching, removes the shipping file-based approval
fixture and direct unix_chkpwd password path, rejects unsupported credential
authorization, preserves helper failures, removes duplicate Wine-fork consent
sources, and replaces the unsafe/false-green runner. These are not permission
to declare all R1–R8 complete.

1. Fresh clone, initialize the exact Wine pin, assemble normally, and build combined
   WoW64 (`--enable-archs=i386,x86_64`). Consent has ONE source of truth under
   `src/programs/consent`; do not reintroduce fork copies that break assembly.
2. Run `bash scripts/check-token-security.sh --wine-build ABSOLUTE_BUILD --output-dir
   NEW_DIRECTORY --focused --all`. This checks both actual PE architectures,
   deny-only AccessCheck AND real event opens, thread isolation/revert, retained
   handle rights, mock-helper errors/target handles and actual shipping consent
   against a user-controlled former fixture and legacy environment variables.
   These are not real authentication tests. Preserve every negative assertion.
3. Run without `--focused` after building all required advapi32/ntdll/shell32 test
   executables. Missing binaries, zero executed tests, nonzero exits and assertion
   failures are NOT PASS. Record inherited upstream failures against a real baseline;
   do not weaken assertions or report a selected probe as the whole upstream suite.
4. Run `bash scripts/test-runner-sentinel.sh --wine-build ABSOLUTE_BUILD --output-dir
   NEW_DIRECTORY`. Success, error, timeout and SIGTERM must leave the independently
   owned live Wine server/client heartbeat and X display unchanged and remove only
   the runner's own resources, including the sentinel after the final teardown.
   On WSLg bind an owned mode-1777 temporary directory over /tmp in a private
   mount AND network namespace (isolate abstract sockets and X lock files too),
   never chmod/remount/delete global sockets. Run tests as an unprivileged UID.
   Capture the public host net namespace ID with readlink /proc/1/ns/net before
   dropping privileges into the namespace; export WEX_TOKEN_HOST_NETNS if that UID
   cannot read /proc/1/ns/net. Example namespace flags:
   unshare --mount --net --propagation private. Bring its loopback up locally.
5. Complete unresolved R2–R5 semantics from the original repair order: restricting
   SID deny/allow ordering, repeated restriction intersection including disjoint
   sets, allocation/protocol errors, MIC/default labels/owner rights, child LowBox
   capabilities, actual handle-table entries/closure, advisory per-thread priority,
   and accepted mitigation/job attributes. Query actual child state and operations.
   In particular establish the native disjoint-restriction contract; do not assume
   inserting the real Null SID is an ungrantable internal sentinel. Allocation
   failures must not publish partially restricted tokens or leak handles.

Required evidence per case: exact parent/Wine SHAs, source/binary hashes, command,
architecture, expected and actual rights/state, return/error codes, positive AND
negative assertions, failures/todos/skips, cleanup and remaining resources. Native
Windows controls are useful; the XP VM cannot validate Vista+ token/UAC behavior.
Keep source-built, extracted-function, native-control and installed-image results
separate. Submit any newly found A defect as a regression plus isolated repair.

## B1 — agree the authorization/identity contract before privileged implementation

Write a short concrete design checkpoint in ELEVATION.md. Identify the requesting
Windows token/SID and Unix peer, chosen administrator principal, approved target
and machine backend identity separately. Define how the mechanism obtains each
identity authoritatively, rather than trusting client-supplied names or SID strings.

Use the existing wexhost polkit agent / native D-Bus infrastructure. Add a narrow
system mechanism/action as needed; authentication belongs to polkit/PAM's existing
trusted path, not a direct unix_chkpwd command or custom password checker. The
agent reports authentication to polkit; the mechanism must check authorization
for the exact requester/action and honor cancellation/failure. An authentication
success for a normal Unix account does not authorize administrator elevation.

Bind each server-owned request ID to the requester (D-Bus peer and PID/start-time
where appropriate), executable identity, argv vector, cwd, profile/environment
policy and chosen principal. Freeze the displayed request; reject malformed,
truncated or changed targets rather than displaying A and launching B. Define
single-use lifetime, cancellation, concurrent requests and helper restarts.

Important design trap: CheckAuthorization's result is an authorization decision,
not a general API returning the identity of the administrator who authenticated.
Explain how standard-user over-the-shoulder identity is actually established and
mapped into a correct Windows primary token. Do not turn an arbitrary requester
into an administrator under their original SID, infer the identity from an
untrusted dialog field, or equate a successful helper exit with a host credential.
If that identity channel is unresolved, submit the design question and keep the
standard-user route explicitly unsupported; do not restore the insecure fallback.

Preserve the initial functional boundary: same Unix UID for filtered/full Windows
tokens can be an explicitly documented compatibility route. Do not label it native
Unix confinement. If dedicated wexsys/cross-UID writes are required, show how the
actual target remains represented by a genuine usable Win32 process handle in
the calling server. Do not substitute a launcher/proxy handle silently. State any
unsupported cross-UID route as such before expanding the architecture.

## B2 — implement real authorization without restoring bypasses

Deliver the administrator filtered-token approval route first, then genuine
standard-user administrator authentication. Unsupported routes must continue to
fail closed with accurate errors until implemented. Keep helper-unset compatibility
explicit; it is not an authenticated production path. Production consent must
never read test-elevation.conf or any AUTO_APPROVE/AUTO_DENY equivalent, even if
someone claims its Windows path refers to a root-owned Unix file. Prefix drive
mapping is not proof of trusted host ownership.

Test valid administrator authentication separately from mock fixtures. Empty,
wrong, arbitrary nonempty password, valid password for a non-admin/wrong account,
cancel/close, no agent/service, timeout and helper crash must launch no target.
No credentials/cookies in artifacts. Correct approval launches exactly one target
with the documented SID/elevation/integrity/Unix identity and exact argv/cwd.
Concurrent A/B: approve A, refuse B; approval must not transfer between requests.
Exercise long/Unicode/quoted paths and arguments resembling helper switches.

Keep cancellation distinct from unsupported policy, failed startup, authentication
failure and timeout. Preserve the repair's actual target hProcess/wait/exit contract:
the child waits on a test event, is initially unsignaled, then exits with 37 and
reports identity/argv/cwd. Refusals return no hProcess and leave no orphan target.
Test asInvoker/requireAdministrator/highestAvailable and explicit runas under
standard, filtered and full tokens. highestAvailable must not force a standard
user into administrator credential elevation.

Audit Helper configuration in BOTH shell32 and kernelbase: distinguish absent or
explicitly empty compatibility configuration from denied reads, wrong registry
types, odd/truncated/nonterminated/oversized data and invalid configured paths.
These errors must not silently select helper-unset elevation. Add negative
regressions before changing the contract, including manifest and explicit runas.
Preserve both credential policy values (1 and 3), malformed-policy rejection,
and Cancel as both the resource default and initial keyboard focus.

## B3 — machine writes, migration and rollback

Audit the existing setup-machine-root.sh before use: stripping all ACLs and
resetting owners/modes to guessed defaults is NOT restoration of original state.
Implement metadata capture, validated exact targets, apply/rollback and failure
recovery in a disposable fixture. Do not suppress chmod/chown/setfacl failures
and then print successful rollback. Preserve existing data, owner/group/mode/ACLs.

Prove ordinary non-elevated Win32 writes to the selected shared files/HKLM fail;
an approved benign 32/64 installer succeeds; a second user sees persistent state
after restart. Refused/authentication-failed requests leave hashes, registry
values and ownership/ACLs unchanged. Record Windows-side versus native Unix
enforcement separately; Linux root is not the default installation identity.
Recheck wineboot/save/merge, fonts/themes, components/OOBE and All Users shortcuts
with their owners. Do not import unreviewed work to make a scenario pass.

## B4 — UI/deployment regression and subsequent C gate

Use genuine X11 and Wayland sessions for real approve/refuse and valid/wrong-admin
authentication. Check cancellation defaults, keyboard/focus, long text and
multi-monitor placement. The overlay is cosmetic, not a secure desktop. Keep
Classic/Open Luna, 800x600, locked/unlocked taskbar, toolbar grippers/overflow,
tray show/hide chevron expanded/collapsed/absent, ordinary 32/64 applications
and existing polkit applets intact. Use independently created/open artwork only.

After isolated A/B proof, obtain a designated fresh installed-image acceptance
environment; record ISO and installed binary hashes and restored VM state. Do
not label a root WSL disposable-prefix test as normal-user installed acceptance.

The independent repair's fresh uninstalled prefixes also logged a background
division-by-zero at 0x14000ca88 while token probes passed (before AND after).
Symbol lookup in that build's explorer.exe points into the balloon/tray path;
this is a lead, not a confirmed root cause. See the retained stderr evidence.
Investigate and resolve or isolate with a proper baseline before calling the
installed desktop/tray gate PASS; do not disable explorer or hide the diagnostic.

Only then proceed to original checkpoint C browser proof: pinned executable and
matching Chromium sources, stock applicable policies, actual child token/job/
capability/handle state, typing/scroll/resize, restart/profile persistence and
network page if allowed. Graphics workarounds must be labeled and must not disable
security policies. Multiple process names or a screenshot is not enough. Do not
use --no-sandbox, --single-process, disabled GPU sandbox, RendererAppContainer or
Win32k policy bypass as acceptance. Hostile native-code containment remains T5.

## Deliverables and review

Submit checkpoints in order: unresolved A repairs; B1 design; B2 real authorization;
B3 migration/rollback; B4 installed regression; C separately. Maintain a per-case
PASS/FAIL/PARTIAL/BLOCKED/NOT RUN matrix in TOKEN_SECURITY_B_ACCEPTANCE.md, with
exact reproducible source probes, redacted evidence and changed machine state.
Update ELEVATION.md, TOKEN_SECURITY.md and STOCK_CHANGES.md honestly. Push Wine
first on your own branch, then the parent gitlink/branch; provide both SHAs and
report path. Do not merge master or call all T0–T5 complete.

Behavioral references checked 2026-10-18 (contracts/design guidance, not code imports):
[SID attributes](https://learn.microsoft.com/en-us/windows/win32/secauthz/sid-attributes-in-an-access-token),
[polkit Authority](https://www.freedesktop.org/software/polkit/docs/latest/eggdbus-interface-org.freedesktop.PolicyKit1.Authority.html).
Pin any additional borrowed implementation by source revision/path/license.
