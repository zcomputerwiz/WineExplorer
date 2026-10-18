<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Fix-it work order for Gemini: OOBE optional components

Date: 2026-10-17. Status: required repairs; not merge-ready.
Reviewed implementation: `gemini/oobe-components` through `12c0b546`.
Original requirements: `docs/OOBE_COMPONENTS_WORKORDER.md` remain in force.

## Outcome and branch

Complete the existing implementation, retaining its native wizard, fixed component
allowlist, shared installer and unprivileged setup account. Fix the concrete
failures below and supply evidence that the actual UI and installed system work.
Do not substitute passing mocks or successful socket communication for acceptance.

Continue on `gemini/oobe-components` from the reviewed tip plus this document,
or a dedicated repair branch from that descendant. Record actual parent/tip SHAs.
Read `CLAUDE.md`, the original order, `OOBE_COMPONENTS_ACCEPTANCE.md`,
`FONTS_RUNTIMES.md` and this order before editing. Preserve unrelated edits and
the separate token-security work order. No merge/push to master, force-push,
new chat/agent dispatch, proprietary payload installation without human consent,
or unrelated VM reconfiguration is authorized here.

## Review baseline: what was independently checked

- The existing 30 tests pass in Debian WSL with Python 3.13; `bash -n` passes for
  `installer/scripts/wex-oobe.sh`. One test run emitted an unclosed-socket
  ResourceWarning; fix test cleanup rather than suppressing it.
- Reproducing the exact nested C `strtok` pattern with libc and a synthetic
  two-row catalog returned only the first row.
- With network calls mocked and UID set to an ordinary user, fetching XACT
  terms returned embedded text, made zero network calls and created no trusted
  cache. Subsequent backend-style license lookup rejected the missing terms.
- In an isolated coordinator with installation mocked, a client connected while
  the stage was `oobe`, the stage changed to `finished`, then the client sent an
  install request. It received `OK` and invoked the installer mock once.
- No live proprietary installer, fresh ISO, visual layout, WoW64 build or
  cross-user runtime acceptance was performed by this review. No VM state was
  changed. These omissions are not passing results.

## F1. Parse the entire catalog and test the real controller

In `src/programs/oobe/oobe.c:PopulateComponentsList`, row parsing and field parsing
share `strtok`'s single cursor, so field parsing destroys the remaining-row cursor.
Replace this with independent explicit cursors or a compatible parser with
separate contexts. Handle empty fields, malformed rows and overlong input without
shifting columns, silently truncating valid rows or indexing outside arrays.
Keep strings terminated and convert protocol UTF-8 to the correct Win32 encoding;
passing UTF-8 directly to ANSI UI APIs is not reliable for vendor terms/names.

Add tests exercising the parser/controller used by the PE wizard, not just the
Python socket catalog. Require six distinct rows in order with the right fields,
all initially unchecked, and malformed/empty/long-row cases. Fix checkbox events
to review the component whose checkbox changed, not whichever row happens to be
selected when a posted command runs. Verify keyboard and rapid-selection behavior.

## F2. Authentic terms and enforced package identity, not decorative metadata

In `scripts/wex-components.py`, `_VENDOR_TERMS_TEXT` embeds abbreviated purported
vendor agreements, and `fetch_vendor_terms` returns them as authentic terms.
Remove these production substitutes. Do not commit retrieved proprietary vendor
documents or payloads. Retrieve full applicable terms on explicit review using
curated official sources and real document extraction; fail closed if terms
cannot be verified or read completely. A third-party license copy is not proof
that it covers the separate Tahoma/IE package. An EXE URL in a terms metadata
field is not a license-document retrieval implementation.

Current `recipe_url`, `recipe_sha256` and `mutable_url` fields are not consumed by
the installer, which invokes the ambient winetricks verb. Implement binding to
the actual packaged recipe and every payload/dependency it can download, including
both architectures where used. Check the installed recipe/version before enabling
a component. No wildcard URL or literal `pinned` placeholder is integrity data.

The privileged side must independently resolve/validate the approved package
identity, recipe/dependency hashes and terms digest. Persist that identity with
the terms and consent/receipt, not only a verb and text hash. Recipe/package/terms
changes invalidate prior approval. Do not use force/unchecked-hash behavior to
work around a mutable upstream URL; inspect actual winetricks flag semantics.

For vcrun2022's reported mutable-URL mismatch, provide a real reviewed immutable
recipe/payload solution or disable it with a clear reason. Telling an administrator
to repair hashes manually is not a completed default-image experience. If any of
the six verbs cannot meet these requirements, show it disabled with the exact
reason and report it as incomplete, rather than presenting a working checkbox.
Do not invent download sizes or universal reboot guarantees.

Tests: synthetic format extraction, malformed/partial documents, changed recipe,
dependency/architecture mismatch, redirect/version drift, and payload/hash failure.
Assert no redistributable payload fetch before valid acceptance. If terms are
only obtainable inside a redistributable, do not download it before acceptance
and claim compliance; report the unresolved source problem or disable the entry.

## F3. Bind acceptance to the displayed immutable snapshot

`OobeTermsProc` writes newly retrieved digests directly into `s_comp->sha256`
before approval and preserves `accepted=true` from an earlier review. Cancelling
can therefore leave a new digest treated as accepted.

Keep candidate terms and accepted terms separate. Only an explicit acceptance
of the successfully displayed candidate snapshot may commit its digest/package
identity to the component. Changed identity/text must reset acceptance and require
fresh review. Cancel must not create or silently transfer acceptance. Initialize
the OK/Accept button disabled and update it consistently with the checkbox.
Avoid independently refetching terms during the size-query/data-read sequence;
use a stable snapshot/token or verify identity across both responses.

Tests: accept A, fetch B, cancel; accept A, fetch fails; changed selection;
Back/Next; opening then pressing Enter without checking; document changes between
size query and retrieval; repeated review and separate decisions for two packages.
No path may install B using A's acceptance. Do not mistake a public terms digest
for a secret authorization token or proof that review happened.

## F4. Make install-later use the same trusted acquisition path

After skipping OOBE, an ordinary administrator's `consent()` fetches missing
terms without writing the root-owned cache, but `install()` subsequently requires
that cache and fails. Do not solve this by making the trusted directory writable
or accepting caller-supplied text as authoritative.

Provide a narrowly authorized privileged preparation/verification path shared by
OOBE and the post-install launcher. It may retrieve approved terms/metadata, not
payloads before consent; the frontend displays the resulting exact snapshot.
Preserve normal administrator authentication, deny limited users and bind the
eventual install to the independently verified identity. Clearly distinguish
preparing terms from authorization to install the selected package.

Test on a fresh empty terms cache as an ordinary sudo-group administrator, not
root: decline, offline, accept, backend verification, install failure/success.
Also test limited-user denial. Fixtures remain synthetic and installers mocked
unless actual human review/acceptance is available.

## F5. Responsive UI and bounded protocol/worker lifetime

Terms retrieval currently blocks `WM_INITDIALOG`; installation blocks
`PSN_WIZNEXT`. `UpdateWindow` is not a message loop or asynchronous worker.
Move network and installation waits off the UI thread and marshal results safely
back to the live dialog. Disable conflicting actions while preserving repaint,
move, close/cancel explanation and actual status. Do not use fake percentages.

The Unix bridge has unbounded blocking reads, while the coordinator processes
one connection synchronously using unbounded `readline()`. Add bounded request
framing, exact argument validation, bounded memory/document sizes, I/O deadlines,
partial-read/write and EINTR handling, and complete-response validation. A
truncated or incomplete terms body must not be displayed as complete and accepted.
Do not let one idle client prevent stage expiry, cancellation or all other work.

Coordinate cancellation with the actual subprocess/installer lifetime. If an MSI
cannot safely stop mid-operation, explain that and wait to a documented safe
point; never kill unrelated Wine servers. Prevent UI/worker use-after-destroy and
double installs after retries or navigation. Bound or explicitly manage stuck
installer recovery; do not leave a root child running after reporting cancellation.

Tests: delayed/no response, incomplete frame/body, client disconnect, idle client,
slow/failing worker, close/cancel during retrieval/install, navigation and retry.
Verify the actual wizard remains responsive, not just a Python controller fixture.

## F6. Fail-closed setup authorization and reliable teardown

The coordinator checks the stage before `accept`, not immediately before each
privileged operation, and skips UID rejection entirely when the wexsetup account
lookup fails. Fix both. A missing setup account is a startup error, never an
authorization wildcard. Root allowance, if production requires it, must be explicit
and documented rather than described as test-only while active in production.

Keep stage/authority state root-controlled or validate a separate root-owned
setup-session lifecycle. The existing stage lives in a directory recursively
owned by wexsetup; its text alone cannot establish privileged authorization.
Revalidate the active session after receiving a complete request and just before
dispatch. A connection opened earlier must not retain authority after expiry.
Reject duplicate/replayed installs and changed identities appropriately; use
durable verified results to avoid reinstalling completed packages. Receipt-file
existence alone is not verification of the currently approved installation.

Add reliable runner cleanup on normal finish, error, signal and aborted wizard.
The existing `set -e` runner reaches cleanup only on the success path. Track and
stop/wait for the coordinator and its owned workers, revoke the endpoint and
authority on abort, and preserve useful failure/receipt state. Do not claim
revocation just because the socket pathname was removed while an accepted
connection or child operation remains active. Keep recovery/resume possible.

Tests: missing wexsetup, unauthorized UID, stage expiry after connection and
before dispatch, forged/extra arguments, replay/duplicate install, symlinked
authority state, runner error/signal, and cleanup while a bounded worker exists.
Check no surviving privileged endpoint/worker or broad sudo/polkit exception.

## F7. Honest receipts, summaries and acceptance report

Persist per-component identity/outcome needed for resume. Distinguish selected,
accepted, declined, skipped, failed and already verified installed states. Do
not count failed attempts as multiple failed components or silently re-run an
already completed component. Summaries must survive appropriate restart/resume.

Correct `OOBE_COMPONENTS_ACCEPTANCE.md`, `FONTS_RUNTIMES.md` and handoff claims.
The test named `test_install_success_creates_receipt` currently mocks `install()`
and checks dispatch, not receipt creation; rename it or test actual receipt logic
with only external installer execution mocked. Clearly separate socket tests,
native UI tests, build tests and installed-ISO evidence. Remove claims of strict
compliance unsupported by those tests. Keep still-valid historical evidence.

## Delivery and acceptance gates

First add regressions reproducing F1-F6, then fix in coherent commits. Keep all
30 existing tests; do not weaken assertions to produce green output. Commit test
fixtures as unmistakably synthetic data, with no production consent-bypass flag.
List exact source/build/package versions, commands and before/after results.

Before declaring completion, satisfy the original work order's remaining gates:

- Combined WoW64 build and 32-bit regression; PE-to-Unix bridge in both applicable
  calling architectures. No Wine source-pin or graphics/theme redesign.
- Actual wizard with all supported/disabled entries, separate terms decisions,
  skip/offline, retry/failure, navigation and summary; visual/keyboard checks at
  800x600 and standard setup resolution in Classic and Open Luna.
- Fresh installed ISO on a designated disposable VM: offline skip reaches the
  desktop; install-later works from an empty cache; online accepted-component
  testing only after genuine human terms review/acceptance. Verify use from
  administrator and limited split prefixes, X11 and Wayland, default open
  runtimes/fonts, graphics switching and core builtin protections.
- Root-owned terms/metadata and durable receipts survive OOBE cleanup; setup-only
  authority does not. Record ISO size delta, test artifacts and restored/retained
  VM/session state. No unrelated VM shutdown, disk replacement or global killall.

Update the acceptance report with fixed/open findings, actual tip SHA, raw-log
locations and explicit blocked tests. If infrastructure or human consent is not
available, report that gate pending; do not mark it passed or claim merge-ready.
Deliver the repair branch for review. Do not merge to master.
