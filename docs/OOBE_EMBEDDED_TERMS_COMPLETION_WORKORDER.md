<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# OOBE embedded-terms completion: bounded implementation work order

Date: 2026-10-19. Instructions for a worker needing explicit steps.
Read this entire document before editing. This supplements
OOBE_EMBEDDED_TERMS_WORKORDER.md; its policy and unrelated safeguards remain in
force. Complete only the framework and CoreFonts REVIEW adapter. Do not enable
CoreFonts installation or implement other vendor adapters in this assignment.

## 1. Starting point: preserve the actual unfinished work

Gemini's implementation is NOT in the main graphics checkout or a published
implementation commit. It is in D:/Projects/WineExplorer, on
gemini/oobe-embedded-terms, based on be7dadb7629dd967c7f4f539145640844e7d7acf.
At inspection, only these two tracked files were modified:

- scripts/wex-components.py: approximately 650 added lines of review backend.
- src/programs/oobe/oobe_catalog.h: review metadata fields and parsing.

The branch still tracks origin/codex/oobe-final-correctness, NOT a published
origin/gemini/oobe-embedded-terms implementation. Set the correct upstream when
publishing. The separate C:/Users/Owner/.codex/worktrees/oobe-components-repair/
WineExplorer checkout is the reviewer's documentation worktree: do not edit it.
D:/DeepThought/Projects/WineExplorer contains unfinished GRAPHICS work: leave it
and its Wine submodule untouched.

First run in D:/Projects/WineExplorer:

```
git status --short --branch --ignore-submodules=all
git rev-parse HEAD
git diff --stat
git diff -- scripts/wex-components.py src/programs/oobe/oobe_catalog.h
```

Stop and report if the branch/base/changed files differ materially or another
worker is active. Preserve all existing changes; never reset, clean, overwrite,
or switch this dirty checkout. Save an external private diff backup before
editing. A local checkpoint commit may preserve Gemini's work if clearly labeled
UNVERIFIED; it is not a passing implementation. Keep authorship/provenance.
Read CLAUDE.md, OOBE_COMPONENTS_PACKAGE_STATUS.md,
OOBE_PARENT_PACKAGE_TERMS_RESEARCH.md, OOBE_COMPONENTS_FINAL_CHECKS_REPORT.md and
OOBE_EMBEDDED_TERMS_WORKORDER.md. Obtain this new document from the reviewer's
published branch without overwriting the dirty source files.

## 2. Exact result required

Both initial OOBE and later Windows Components must support this sequence:

1. Open page or select CoreFonts: metadata only, ZERO network/execution.
2. Choose Review terms: show vendor, package source, all eleven packages' total
   bytes, cache/disk needs, and "Nothing will execute or install."
3. Decline: no downloads, privilege request or installed/accepted receipt.
4. Explicitly authorize download: bounded cancellable progress; static extraction
   from verified packages only. Never run PE/Wine/Winetricks/vendor extractors.
5. Display every distinct complete applicable agreement, in deterministic order,
   identifying its packages. Reopening verified cached terms uses no network.
6. CoreFonts remains labeled REVIEW ONLY / installation unavailable. Reviewing,
   accepting download permission, and closing the terms viewer never select it
   for installation or create install acceptance. Do NOT show a misleading
   install-accept checkbox for this disabled component.
7. For an install-eligible component, acceptance remains a separate unchecked
   action after full terms display, bound to that exact fingerprint. Preserve the
   working standalone VC++ DOCX route and its existing install gates.

The real CoreFonts install handoff is deliberately unavailable. Test the shared
two-stage acceptance logic using a synthetic eligible catalog fixture, never a
shipping bypass or a temporary production CoreFonts enablement.

## 3. Finish in this order; test each step before continuing

### A. Audit and repair the inherited backend

Review every added function; a comment is not proof. Retain useful code, but fix
incorrect assumptions. In particular:

- GET_TERMS currently tests metadata key review_enabled, which is not defined.
  Use one authoritative review-availability predicate independently of disabled
  install eligibility. Cached CoreFonts terms must be retrievable while install
  still fails closed. Missing/stale cache must return a distinct review-required
  result without a download. Do not catch an integrity error and silently refresh.
- Frontend install validation was restored to reject disabled entries after an
  old test hung at a new console prompt. Keep install rejection, but provide an
  actual explicit review-only entry in the later-launch UI. review_only() is
  presently not reachable through that UI; merely defining it is insufficient.
- Reconcile review_state(), cache validation, error taxonomy, CLI, coordinator,
  and UI. Invalid cache must not be advertised as valid or installed.
- Audit embedded_fingerprint/component_identity: bind every package/dependency,
  architecture policy, resource locator, decoding/rendering recipe version,
  ordered raw/rendered terms and policy. Add missing inputs, not just comments.
- Validate PE optional-header/directory bounds, mapped resource ranges, duplicate
  entries, section boundaries, lengths and decoding. Reject malformed ambiguity.
- Enforce root-owned private cache directories and private package files,
  symlink/ownership/mode checks, bounded transfer/parser work, atomic publication,
  disk-space checks and safe partial cleanup. Do not follow attacker paths.
- Review threads must serialize shared-cache writes across initial/later clients
  and processes, handle cancel before cached work/after final chunk/before publish,
  and shut down within a measured bound. No duplicate download race or stale
  callback may publish acceptance. Keep review failures out of install journals.
- Recheck network redirect policy with actual approved CoreFonts source behavior.
  Never broaden to arbitrary hosts or update pins just to make a download pass.

CoreFonts metadata must describe all eleven pinned packages, including Arial
Black, and every distinct LICENSE resource. Keep proprietary bytes/text private.
Wine already supplies its own open Tahoma; do not substitute Microsoft's Tahoma,
add IE downloads, or claim exact XP glyph/metric parity in this task.

### B. Finish the native bridge and catalog contract

Inspect src/dlls/wexhost/{unixlib.c,wexhost.c}, its export/spec/header definitions,
and src/programs/oobe before adding functions. Extend the existing bridge; do not
create another privileged installer or spawn arbitrary shell commands.

Wire REVIEW_INFO, START_REVIEW, QUERY_REVIEW, CANCEL_REVIEW (or equally explicit
documented equivalents) through the real transport to the native UI. Bound verbs,
operation IDs, numeric fields, message lengths and response framing. Enforce the
existing authenticated setup session, peer checks and stage expiry. A retry of a
query must not resubmit a start/install action. No user URL/path/command inputs.

The appended catalog column is R:<bytes>:<cached 0|1>. Specify and validate exact
grammar; reject overflow, negative numbers, trailing junk and unsupported flags.
The inherited sscanf >= 1 check is too permissive. Use an explicitly bounded
integer representation; preserve old seven/nine-column rows where supported and
empty fields. Bad review metadata must not grant review/install capability.

### C. Finish initial OOBE and later-launch UI

Update oobe.c, oobe_worker.h, oobe.rc/resource.h as needed, and the Python later
Components frontend. Separate selection for review from install checkboxes.
Existing unconditional disabled-row checks must not block review-only access.
Show source/size and Download/Cancel BEFORE START_REVIEW, then progress/cancel,
complete cached terms, and an honest review-only result. Do not label download
success "installed". Preserve offline Skip and truthful finish counts.

Reuse the existing reference-counted/generation-checked worker pattern. Do not
block the UI thread on network/socket/thread joins. Close, Back, Cancel, restart
and worker completion races must not touch destroyed dialogs or approve a newer
terms snapshot. Initial setup must work without a pre-existing admin account.
Do not add production auto-accept flags, environment switches or prechecked terms.

## 4. Required automated deliverables (tests must call production seams)

Add scripts/test_component_review.py for parser/cache/download/fingerprint tests
and scripts/test_oobe_review.py for real coordinator protocol tests. Extend
test_oobe_bridge.py and test_oobe_native.c for transport/native controller cases.
Equivalent file organization is acceptable only if the report maps every case.
Use original synthetic PE/resources and fake HTTP responses for ordinary CI; no
vendor text, packages or copied XP captures in fixtures. Every fixture must be
isolated from /var/lib/wineexplorer, real accounts and installed prefixes.

Required case IDs, all individually reported:

- N1: opening/catalog/selecting/GET_TERMS missing-cache => zero network, elevation
  and installation calls. N2: decline download => same. N3: only explicit start
  requests eleven pinned packages; repeat cached review makes zero requests.
- S1: download/extract cannot invoke install(), Wine/Winetricks/vendor binaries,
  mutate machine/HKCU/defaults, write install journal or accepted/install receipts.
  S2: disabled CoreFonts cached review works, installation still rejects.
- A1: download completion is not acceptance; reopen/restart/replay cannot install.
  A2: synthetic eligible fixture requires separate unchecked acceptance; a change
  to each fingerprint input invalidates it and backend rechecks exact identity.
- P1: all packages/raw resources/rendered clauses verified; duplicate agreements
  preserve package mapping. P2: CP1252, Unicode and complete ordered text work;
  bad encoding, empty terms, controls and truncated/ambiguous PE inputs reject.
- C1: hash/size/redirect/TLS errors; stale schema/recipe/policy/rendered text;
  missing/tampered package, symlink, bad owner/mode, oversize input and disk-full
  all fail closed without receipts. Test both root and normal-user boundaries.
- L1: cancel before transfer, mid-transfer, after last chunk and with full cache;
  timeout, interrupted publication, simultaneous starts and shutdown. Bound time,
  remove owned partials, preserve valid cache, no stale acceptance/worker leaks.
- B1: actual bridge partial/oversize/truncated/invalid replies, reconnect, expired
  stage, wrong peer, unknown operation and duplicate start behavior. B2: catalog
  valid old/new rows, empty fields, malformed/overflow review fields.
- U1: native production state/controller seam: review-only cannot be checked for
  install; acceptance false initially; changed fingerprint resets it; stale
  generation/closed window completion cannot approve. U2: later frontend review
  action reachable, disabled install rejected before prompting/elevation.

Mock input/GUI in tests: tests must never hang waiting for "Type yes", fetch the
public Internet, or display real prompts. Bound subprocess/test timeouts and
propagate failures. Do not weaken old assertions merely to make a new flow pass.

## 5. Commands and environment: avoid the previous dead ends

Use a NEW owned native Linux source/test directory, private prefixes/displays and
output paths. Record how source bytes were copied and compare source hashes.
Do not use or mutate another worker's build/prefix. WSL Debian is authorized;
Windows shell syntax is PowerShell, not Bash. Use script files for complex Bash
or Python. Execute tests from the actual repository root, not D:/AntiGravity.

Run these exact entry points under Debian with bounded execution, recording
exit codes, test totals and skips (new review cases require ZERO skips):

```
python3 -m py_compile scripts/wex-components.py
python3 -m unittest discover -s scripts -p test_component_review.py -v
python3 -m unittest discover -s scripts -p test_oobe_review.py -v
python3 scripts/test_runtime_repairs.py
python3 scripts/test_component_terms.py
python3 scripts/test_component_runtime_artifacts.py
python3 scripts/test_component_recovery.py
python3 scripts/test_oobe_bridge.py
python3 scripts/test_oobe_capture_audit.py
python3 scripts/test_gate_e_synthetic.py
```

Root-sensitive fixtures must run in an isolated root-owned lab as well as their
applicable normal-user controls. Gate E uses a mock installer: label it synthetic.
The historical repaired baseline was 56/57 runtime-repair passes (one explicit
live-Wine skip), terms 11, artifacts 14, recovery 14, bridge 5, capture audit 4,
Gate E 3. New test counts may increase; investigate any loss or additional skip.

Compile the native helper for BOTH architectures (ordinary main, no -municode):

```
i686-w64-mingw32-gcc -Wall -Wextra -Werror scripts/test_oobe_native.c -o LAB/oobe-native32.exe
x86_64-w64-mingw32-gcc -Wall -Wextra -Werror scripts/test_oobe_native.c -o LAB/oobe-native64.exe
```

Replace LAB with a new absolute owned path. Run BOTH executables using the exact
project combined-WoW64 Wine loader and owned fresh prefixes. A single modern
WoW64 wine loader can run both PEs; do not assume a wine64 command exists.
Compile-only is NOT a native-test pass. Record loader/binary hashes and commands.
Inherited helper baseline is 54 assertions per architecture; add review cases.
Build changed OOBE/wexhost targets for both architectures with project build rules;
all Wine configurations MUST use --enable-archs=i386,x86_64. No Win64-only build.

Previous Gemini failures to preserve in the report: wrong cwd => source not
found; -municode on a main program => undefined wWinMain@16; wine on PATH =>
command not found; old frontend test blocked on a new console review prompt;
mixed PowerShell/Bash quoting caused parser errors. Do not repeat global find /,
kill arbitrary PIDs, use global killall/pkill, or install random system Wine.

## 6. Live acceptance, not just mocks

Use a newly created/cloned disposable VM or owned isolated lab, not an existing
user VM reset/reinstall. Record initial versus later session identity and actual
backend/prefix/display. A donor image containing old binaries is not proof of
new installed code: record deployed hashes. User authorized private downloads;
production UI must still ask. Keep terms/packages/captures outside public Git.

Prove initial OOBE AND authenticated later Components with private real packages:

1. Record before-state hashes/receipts/defaults and network trace. Open/select,
   then decline download: zero requests and no state change.
2. Explicit download: truthful source/size, progress, cancel and retry. Verify
   eleven exact package/resource hashes and full rendered terms. No PE execution.
3. Decline/close after terms, reopen cached terms offline: no install acceptance,
   no machine/default/receipt mutation, no additional payload download.
4. CoreFonts stays review-only; disabled install fails via UI, CLI and protocol.
   Exercise separate acceptance with the labeled synthetic eligible fixture.
5. VC++ standalone terms path and 32-bit ordinary app remain functional; limited
   later user cannot gain admin review/install powers. Setup Skip still finishes.

For changed UI, show real 800x600 and normal-resolution OOBE/Components/long-terms
dialogs with Classic and Open Luna: readable text, scrolling, Download/Cancel/
review-only labels, no clipped acceptance controls. Run actual X11/Wayland desktop
regressions for locked/unlocked taskbar, toolbars, expanded/collapsed/absent tray
chevron. Record unavailable cells NOT RUN, not PASS. Screenshot names alone are
not evidence; existing capture audit rejects wrong-size/login-screen captures.
Fresh corrected-ISO acceptance remains a separate release gate unless actually run.

## 7. Required handoff and precise stopping rule

Deliver ALL of the following:

- Finished backend, bridge, native/later UI and test code; no extra components
  enabled, no Wine/compositor pin or master change.
- docs/OOBE_EMBEDDED_TERMS_ACCEPTANCE.md: case IDs above with PASS/FAIL/BLOCKED/
  NOT RUN, exact commands/exits/counts/skips, base/tip/submodule and deployed
  hashes, private evidence paths, request/state comparisons, parser/retention/
  cancellation limits, known limitations, failures/workarounds and cleanup.
- Updated package/progress documentation accurately stating CoreFonts review
  availability versus install unavailability; no claim of full OOBE merge readiness
  or font installation from this milestone.
- Coherent commits on gemini/oobe-embedded-terms, pushed to its own origin branch.
  Inspect git diff --check and staged diff; stage explicit filenames, not git add .
  Check for vendor payloads/legal text/secrets before each commit. Push normally
  with git push -u origin gemini/oobe-embedded-terms; no force or master push.

Stop after this milestone and request independent review; do not start font
installation/adapters for Tahoma/XACT/SDK/PhysX or merge anything. If live proof or
hardware cells cannot run, publish an honest reviewable implementation with those
items BLOCKED/NOT RUN and list the exact missing prerequisite. Do not claim the
entire task complete or ready to merge. Do not replace required behavioral tests
with source-string checks, successful compilation or inherited screenshots.

Return a short blurb: branch, commit, report filename, passed gate IDs and exact
remaining blockers. Record and stop only processes you created; state VM status.

## Work-order authoring notes

This document is instructions, NOT implementation acceptance. Source inspection
found the two dirty Gemini files intact; no implementation was edited/committed.
The reviewer wrote this document in the separate documentation worktree. No VM
was started for it. Prior Windows rg glob arguments ending in /*.c or bridge*
returned OS error 123: search the directory with -g instead. A guessed progress
filename OOBE_FINAL_CORRECTNESS_PROGRESS.md returned OS error 2: use the verified
final-checks report and discover actual filenames. No source tests were rerun
for this documentation-only handoff.
