<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work order for Gemini: unified tokens, elevation and Chromium compatibility

Date: 2026-10-17. Status: ready for staged implementation; not implemented by this
document. Research and source/license inventory: `TOKEN_SECURITY_REFERENCES.md`.

## Outcome and scope

Implement one coherent Windows-facing permission framework, used both by normal
application elevation and by applications that create restricted child processes.
The initial objective is correct functionality and application compatibility,
not resistance to adversarial Windows code or a certified browser sandbox.
Do not turn the initial project into a Linux sandbox/kernel rewrite. Do not omit
ordinary permission checks merely because hardening is deferred.

Tokens contain identity, SID attributes, privileges, restrictions, integrity and
other policy attributes. They are not a restricted/user/admin integer ladder.
Use Wine's token objects and effective-thread-token rules as the authority;
avoid a second divergent permission model in wexhost or the shell.

This order supersedes `ELEVATION_WORKORDER.md`'s Codex assignment, sequencing,
direct-master publication instruction and any conflicting design claims. Retain
its user-facing elevation goals as inputs, not an already approved final backend.
In particular, do not repeat its claim that a password dialog alone supplies a
hard boundary. Cross-process/display isolation is explicitly not proven here.

## Starting point, ownership and integration

Read `CLAUDE.md`, this order, its research companion, `ELEVATION_WORKORDER.md`,
`PREFIX_SPLIT.md`, `PREFIX_SPLIT_WORKORDER.md`, `DBUS_PLAN.md`, `WEBVIEW2.md`,
`STOCK_CHANGES.md`, and the session-0 section of `ROADMAP.md` before changing code.
Inspect actual source and deployment scripts; historical documentation is not
proof of the installed state.

Start a separate `gemini/token-security` branch from current reviewed master,
including this work order and its research companion. Record the actual parent
and Wine submodule SHAs. If this document currently exists only on another work
branch, bring over its documentation changes without importing unrelated WIP.
Do not reset, switch, clean or stash an occupied dirty checkout. Use a separate
checkout/worktree when necessary. The shared checkout currently contains active
OOBE/components/wexhost changes; those are not disposable and are not this order's
baseline. Integrate their reviewed descendants when needed, not an unreviewed
blanket merge. Coordinate ownership of wexhost, packaging and machine-root setup.

No merge/push to master, force-push, VM reset/reinstall, new agent/chat dispatch or
unrelated OOBE implementation is authorized by this work order. Supply a reviewable
branch and evidence. Keep coherent commits and small upstreamable Wine patches;
document all stock modifications in `STOCK_CHANGES.md` and pin submodule changes.

## T0. Reproduction and architecture checkpoint

Before implementation, write `docs/TOKEN_SECURITY.md` with:

- A per-API inventory: implemented, partial, success-only stub, unsupported;
  distinguish Wine semantics from Unix enforcement. Recheck the locally observed
  gaps in the research companion against the chosen base.
- Identity mapping: Linux UID/groups to stable Windows user/group SIDs; ordinary
  user, administrator's filtered/full pair, service identity, anonymous and
  restricted tokens. Keep the requesting user separate from an administrator
  authenticating an over-the-shoulder elevation, and from backend `wexsys` UID.
- A token/object-access data model and lifetime/locking plan, including which
  checks already exist and where inherited/duplicated handles are handled.
- An elevation launch/return contract: caller, executable, arguments, working
  directory, environment/profile, authorization result, cancellation, child
  lifetime, process-handle/wait/exit-code semantics and supported limitations.
- A pinned freely available Windows Chromium build/source pair and unmodified
  baseline run in a disposable prefix. Record startup stage and exact failures,
  process roles and display/graphics backend; separate sandbox from rendering.
  List the actual APIs/policies required by this version, not every API at HEAD.

Resolve this known design issue before promising transparent elevation: a
process in another UID's wineserver cannot simply return its numeric HANDLE to
the caller. Prototype a genuine process-object/proxy bridge with supported
rights, or document a limited launch interface separately. A Unix PID, event or
helper process handle is not transparently the elevated target's process handle.
If transparent ShellExecute behavior requires a substantial cross-server change,
present the options for review; continue independent token work meanwhile.
Do not silently expand into a whole session-0 rewrite.

## T1. Token representation, filtering and effective identity

Extend Wine's existing token/server protocol, not registry-only flags:

- Accurate token queries, buffer sizing, access masks, duplication, primary vs
  impersonation type and impersonation-level validation, ownership/default DACL,
  identity/groups, privilege state, restricting SIDs, integrity and mandatory
  policy. Add modern fields incrementally as required by the chosen consumer.
- Filtering flags and deny-only SID behavior; removal versus disabling of
  privileges; restricted-on-restricted operations. Apply both ordinary and
  restricting-SID access evaluation where the API contract requires it.
- Stable administrator limited/full pairing and appropriate elevation queries.
  A linked-token query is not a host elevation authorization. Opening/querying
  tokens or changing reported elevation must not silently grant machine writes.
- Correct effective token selection per operation: process primary versus thread
  impersonation, OpenAsSelf where applicable, revert and inheritance. A renderer
  startup thread's temporary impersonation must not become every thread's token.

Keep normal Win32 semantics: dropping a thread impersonation token can restore
the process primary token. Chromium's lockdown works by starting with a restricted
primary token and discarding its more permissive startup impersonation context;
do not invent a universal rule that every token change is irreversible.

## T2. Functional access checks

Use the effective token consistently for the selected Wine-managed object paths:
registry, files, processes/threads, synchronization objects and named pipes.
Support default DACLs, SID attributes and mandatory labels for exercised cases.
Preserve granted handle rights: opening an object and using an existing handle
are not equivalent, and lowering a token must not magically revoke every handle.
Add inheritance/duplication tests and avoid losing access-mask information.

Inventory filesystem ACL storage/mapping and shared-HKLM persistence before
changing them. For paths not yet covered, report the limitation explicitly. Do
not imply a Wine check prevents bypass via native Linux calls. There is no need
to invent every Windows object class before delivering a tested vertical slice.

## T3. Elevation using the same identity/token framework

- Ordinary applications of an administrator run with a filtered token. Inspect
  manifest handling end to end: asInvoker, requireAdministrator, highestAvailable,
  direct CreateProcess failure semantics and shell-mediated elevation. An
  ordinary standard user does not automatically get a prompt for every
  highestAvailable executable. Preserve legacy Run as behavior deliberately.
- Connect runas/manifest launch to a configured generic helper seam and a native
  consent/credentials dialog. Reuse wexhost/polkit authentication; no password
  files, hand-written credential verification or arbitrary shell command strings.
- Keep the existing Linux-service authentication use cases working. Administrator
  consent-only versus credentials mode must have a concrete authorization path,
  not a broad persistent polkit allowance for arbitrary unconfirmed launches.
  Match the displayed request to what actually launches; handle cancellation,
  timeout, authentication/launch failure and concurrent requests explicitly.
- Use a dedicated non-root backend identity for shared machine writes where the
  current prefix-split architecture requires it. A Windows full/SYSTEM token must
  never automatically mean Linux root. Preserve user/profile semantics or state
  and test the exact separate-prefix limitation; do not pretend the backend
  service account is the user's Windows identity.
- Remove normal-session write grants to the shared machine tree as needed, with
  a repeatable migration and rollback. Audit wineboot, HKLM save/merge, updates,
  themes/fonts, components/OOBE and All Users shortcuts. Do not break maintenance
  paths or silently require an interactive prompt during unattended setup.
- Native UI and independent/open art only. Use verified publisher information
  only when actual signature verification exists; otherwise report Unknown
  publisher. Do not treat a signature's mere presence as verified trust.

Prototype both X11 and Wayland display access. Neither a shared X11 cookie nor a
Wayland socket ACL is proof of isolated privileged UI; do not broaden socket
permissions globally or change the compositor architecture without review.

## T4. Chromium functional compatibility

Use Chromium's own ordinary-user broker/target architecture. Do not route
renderer resource requests through the privileged elevation service, elevate the
browser, or require administrator authentication to create restricted children.

Implement the chosen version's missing token and startup operations, followed by
its required job, alternate desktop/window-station, inherited-handle, LowBox/
AppContainer capability and mitigation behavior in evidence-driven slices.
Each field must round-trip correctly and participate in the supported access
checks; returning a dummy successful handle or an invented enabled policy is not
implementation. Cover initial-thread impersonation and token lowering in a small
probe before debugging the entire browser.

Do not globally flip every old Wine stub to an error without regression tests.
For changed paths, use correct unsupported results or clearly isolated, explicitly
documented compatibility behavior when enforcement is deferred. Record requested,
reported and actually enforced policy separately. If an API contract needs wider
support, implement it or leave the milestone incomplete rather than masking it.

No shipping `--no-sandbox`, `--single-process` or equivalent sandbox-disabling
default. Such switches may only be labeled diagnostic controls in disposable
tests, never the acceptance run. A version/graphics workaround from `WEBVIEW2.md`
is not evidence of sandbox correctness or a solution for every browser. Do not
make an obsolete, unmaintained browser the permanent compatibility target.

## T5. Deferred hardening design, not an initial delivery blocker

Document enforcement seams and limitations for future namespaces/seccomp/Landlock,
Linux credential changes, direct syscalls, inherited descriptors, process-memory
access, wineserver trust and desktop/D-Bus exposure. No custom kernel, complete
hostile-renderer campaign or full cross-user services tier is required now.
Keep hardening outside completed-functional claims: describe the initial result
as token/elevation/browser compatibility, not a secure Chromium sandbox.

## Acceptance and evidence

Build combined WoW64 (`--enable-archs=i386,x86_64`) and run 32- and 64-bit probes.
Use Wine's existing tests plus focused new tests; Chromium token tests supply
consumer expectations, not authority to discard Windows contracts. Record exact
test revisions, patches, command lines, failures/skips and before/after results.
Use modern Windows behavioral controls if available; do not claim XP validates
Vista+ features or acquire/reconfigure another VM without authorization.

Required functional checks:

1. Tokens: query sizes/errors, duplication independence, deny-only SIDs,
   restricting-SID intersection, privilege flags/removal, integrity round trips,
   process/thread identity and revert. Test normal success and expected denials.
2. Objects: explicit allow/deny, relevant label rules, default/inherited ACLs,
   handle rights and duplication, plus file/registry persistence across restart.
3. Elevation: admin consent accept/decline, standard-user admin credentials,
   wrong password/cancel, direct versus shell launch and manifest variants;
   no shared-machine change after refusal. Test hProcess/wait/exit status for the
   supported contract, paths/arguments with spaces and non-ASCII characters.
4. Machine integration: a benign 32/64 installer into the shared tree, visibility
   to another user, ordinary non-elevated write denial, and working updates,
   fonts/themes/component setup after migration. No proprietary payloads in Git.
5. Browser: normal unelevated launch without disabling its sandbox; renderer and
   utility startup, local-page painting/input/scroll/resize, a network page if
   allowed, and browser restart/profile persistence. Capture actual child Windows
   command lines, token properties, job/AppContainer status, logs and
   chrome://sandbox diagnostics. These establish functionality, not containment.
6. Desktop regressions on X11 and Wayland: existing polkit applets, normal 32-bit
   programs, classic/Open Luna themes, locked/unlocked taskbar, toolbars and tray
   show/hide chevron appearance. No new theme redesign is part of this order.

Run first in isolated prefixes/build sessions. Installed-ISO testing follows only
on a designated disposable VM; preserve the user's existing desktop, VM disks
and unrelated workloads. Record every changed VM/session state and restore test
configuration. Do not claim a source-build smoke test is installed-ISO acceptance.

## Deliverables and review gates

- `docs/TOKEN_SECURITY.md`: architecture decisions, actual implementation coverage,
  source/license provenance, pinned browser/source versions, limitations and
  reproducible tests/log locations. Maintain a coverage table with semantic state,
  Wine enforcement, Unix enforcement and tests for each implemented feature.
- `docs/ELEVATION.md`: actual user/admin behavior, deployment and rollback,
  process-handle contract and prefix/profile/display limitations.
- Focused automated probes/tests and small coherent implementation commits, with
  updated stock-change documentation and explicit submodule revision handling.
- Deliver T0-T2 for review first, then T3, then T4; report partial completion
  honestly and keep working independent slices when one integration is blocked.
  Hardening (T5) remains a documented follow-on, not an excuse to postpone useful
  functionality. Do not mark the entire order complete until the functional
  acceptance matrix passes or the user explicitly narrows the target.
