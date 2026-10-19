<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Gemini work order: embedded-license download for review

Date: 2026-10-19. Handoff branch: codex/oobe-final-correctness.
Start from the published commit containing this file, not an older Gemini tip.
Pre-documentation baseline: 6ae0e7e55eb3fcd01436a1fdaf9ce39f3a136095.

## Approved policy and precedence

The human explicitly approved: "Yes—explicit download-for-review, then separate
install acceptance." This resolves the previously pending product decision.

For components whose applicable agreements are embedded in a package, OOBE and
Windows Components may offer an explicit Download package to review its license
action. Explain the package/vendor, total required download size, and that no
installer or package code will run and nothing will install at this stage.
Then acquire and statically extract the complete applicable terms. Require a
SEPARATE affirmative installation acceptance after those terms are displayed.

This supersedes ONLY older instructions prohibiting all payload acquisition
before terms acceptance. It does not permit automatic downloads on opening a
page, prechecked acceptance, execution to extract terms, production auto-accept,
ignoring license scope, integrity bypass, or bundling proprietary payloads/legal
documents/fonts in Git or the ISO. Standalone authoritative matched terms should
still be reviewed before payload download where that route already works.

Download permission is NOT an installation receipt or terms-acceptance record.
Finding and displaying a parent agreement does not establish that every proposed
extraction/use is covered. Keep SDK/IE applicability questions separate from the
new product flow; do not invent a standalone license requirement for every file.

## Scope, ownership and required reading

Implement this feature, not just a new dialog or a Winetricks launcher. Use a
separate available branch/worktree from this complete published handoff, such as
gemini/oobe-embedded-terms. Record actual base/tip SHAs. Do not reset occupied
checkouts, touch graphics/token work, change Wine/compositor pins, dispatch agents,
force-push, or merge/push master. Use your own build directories and disposable
fixtures/VM; no reset/reinstall of an existing VM. Commit coherent steps and PUSH
the review branch so the other workers can see it.

Read CLAUDE.md, OOBE_COMPONENTS_FINAL_CHECKS_REPORT.md,
OOBE_COMPONENTS_PACKAGE_STATUS.md, OOBE_PARENT_PACKAGE_TERMS_RESEARCH.md and their
evidence. Older OOBE work orders retain their unrelated security, lifecycle,
shared-prefix, fresh-image and desktop regression requirements. This order takes
precedence for the approved acquisition/consent sequence and the repaired base.

Current production candidate enables only vcrun2022. The other entries remain
disabled pending their own integration/acceptance. Do not re-enable entries as a
shortcut to demonstrating the framework. Public metadata and proprietary private
caches must remain separate. New code stays GPL-2.0-or-later compatible.

Represent review availability separately from installation eligibility. A verified
Core Fonts adapter may expose a clearly labeled review-only action while install
remains unavailable pending its gates. Do not let the existing single enabled flag
either block all real review tests or accidentally authorize installation. Entries
with unresolved package identity/terms provenance must not offer a misleading
approval-ready review action.

Inspect scripts/wex-components.py, src/programs/oobe/{oobe.c,oobe_worker.h,
oobe_catalog.h,oobe.rc,resource.h}, the existing bridge/coordinator and the later
Windows Components launch path. Extend their single shared backend, not a second
privileged installer. Preserve authenticated machine maintenance, protected
snapshots, UNKNOWN/recovery-marker gates and architecture-correct regedit repair.

## Milestone 1 — explicit two-stage review and install flow

Implement and test these distinguishable states:
- Available metadata: no network or execution just from opening/selecting rows.
- Awaiting download-for-review permission: name, vendor/source, expected total
  size including dependencies/carriers, disk/cache needs and Cancel/Skip.
- Downloading: bounded progress/cancel; an interrupted transfer is not accepted.
- Verifying/extracting terms: no package code execution or machine-prefix change.
- Review terms: full ordered agreements, package/version and language identified.
- Install acceptance: separate unchecked affirmative action bound to this exact
  verified terms/package/recipe snapshot.
- Installing/result/recovery: only now enter the existing authorized installer.

If size cannot be established from reviewed metadata, do not show a fake size:
resolve it before offering this route, or leave the component unavailable with
the precise reason. Separate download authorization and install authorization in
the protocol and backend; they cannot share an ambiguous boolean/operation type.

Closing/declining after download leaves zero installation/defaults/receipt
changes. A verified private cache may remain for future review, but must be
identified as cached, not installed or accepted. Define bounded retention and
safe partial-download cleanup. Reuse a full verified cached payload at install
time where valid; do not download it twice unnecessarily or trust partial bytes.

The same semantics must work during initial wexsetup OOBE and later authenticated
Components use. Initial setup must not depend on a pre-existing administrator
account or manually created terms files. Preserve offline skip/decline behavior
and a working desktop even when every optional item is declined/unavailable.

## Milestone 2 — trusted exact-package terms pipeline

Extend catalog policy with explicit acquisition mode, reviewed URLs/redirect
policy, expected package identities/sizes, extraction recipe/member/resource,
encoding, ordered agreement set and raw/rendered hashes. Pin exact packages;
do not silently replace hashes when Microsoft republishes or a mirror changes.

Use bounded static archive/resource parsing, no PE/Wine/Winetricks execution,
MSI custom actions, DLL loading/registration, shell association or vendor extractor
execution to obtain terms. Treat parser input as untrusted: limit download,
expanded/member sizes, recursion, CPU/time, member count and paths; reject path
traversal, links, malformed/ambiguous members, unexpected content and empty terms.
Run extraction without machine-install authority wherever possible, then publish
only verified data through the narrowly scoped trusted backend.

Keep root-owned/private cache publication, symlink/ownership/mode checks, atomic
writes, sanitized privileged environment and catalog-only backend selectors.
Frontend-supplied arbitrary paths/URLs/member names/commands must not become
privileged parser/downloader inputs. Opening Review must never implicitly install.

Bind the consent fingerprint to the component/version, every package/dependency,
architecture, exact extraction recipe, raw terms identity, complete rendered
ordered terms and policy version. Changes to any binding invalidate previous
acceptance. Old cache schema/consent records need an explicit invalidation or
migration rule; download authorization must never be migrated into acceptance.

Preserve complete clauses, numbered lists, bullets, Unicode and hyperlink targets.
CP1252 resources are not UTF-8; fail closed on unsupported decoding. Use a reviewed
full RTF rendering/extraction approach, NOT regex stripping or unrtf's lossy plain
export as the production consent text. Preserve the SDK's introductory agreement
selection instructions and full applicable section; do not accidentally show only
the first ADK agreement in the four-kit document. No proprietary text is committed.

## Component adapters and exact remaining questions

Core Fonts is the first concrete integration target:
- Use all eleven reviewed originals, including Arial Black, and the corrected
  dependency closure. The inventory proves LICENSE resources and 30 font files.
- Bind every package's applicable complete resource. Do not assume the eleven
  resource byte strings are identical or approve all packages using one unchecked
  sample agreement.
- First prove download -> review -> decline/accept with the real private packages.
  Font publication/registration and normal-user actual face selection are separate
  enablement gates; preserve existing user substitutions and unrelated settings.

XACT:
- Use the exact acquired package and nested dsetup32.dll agreement chain recorded
  in the parent-terms research/evidence. The English runtime resource is DATA /
  SETUP_AGREEMENT_ENG / 1033, CP1252, 4,849 bytes. Validate full recorded hashes.
- Include the applicable outer/inner ordered terms; don't discard the SDK wrapper
  merely because the runtime agreement is more convenient. Resolve selected
  extraction applicability and the changed upstream package identity.
- Keep x86/x64 extraction and functional/audio proof separate. No DXSETUP execution
  is allowed in the review stage.

D3DCompiler 47:
- SDK bootstrap manifest binds license.rtf/u1 to both recorded compiler cabinets.
  Use that provenance rather than unrelated modern SDK terms.
- The moved redist-list URL now targets a newer document; it does not by itself
  verify the historical 8.1 list. Development-use and platform/distribution scope
  questions remain. An adapter can be tested while production enabling stays off.

Tahoma:
- Historical parent is IE 5.5 SP2 according to the pinned recipe; exact matching
  agreement/supplements/use applicability still need verification. Do not use
  Core Fonts, a different IE release, or modern Windows terms as a substitute.
- Keep disabled/open fallback until the chain and font-only installation scope
  are established. The new flow is not an automatic licensing clearance.

PhysX:
- Complete the reviewed modern-package terms/recipe work separately. Preserve the
  corrected understanding of the stale Wine regression cutoff. Do not waive a
  hash, force through an outdated recipe, or promise GPU PhysX on Intel/AMD.

VC++:
- Preserve the existing standalone-DOCX path and exact x86/x64 artifact validation.
  The new route must not regress its no-payload-before-install-acceptance behavior.

## Required proof and next review checkpoint

Stop for review after Milestones 1/2 and the Core Fonts terms adapter are working,
BEFORE enabling additional production components or claiming all acceptance done.
Deliver code, tests, exact commits and a report even if one vendor remains blocked.

Automated tests must include:
1. Opening OOBE/Components and selecting a row sends no network request. Declining
   download permission fetches nothing. Only the explicit review-download action
   permits the approved package/carrier requests.
2. Review download and static extraction cannot call any install entry point,
   Wine/Winetricks/vendor executable, mutate the machine prefix/HKCU defaults, or
   create installed/accepted receipts. Instrument the actual production seams.
3. Separate acceptance is mandatory; cached payloads, downloaded state, reopen,
   restart, cancellation and replay cannot authorize installation. Bind/recheck
   exact terms at install time. Reject changed terms/package/recipe/architecture.
4. TLS/redirect/hash/size failures, malformed archives/resources, expansion/path
   attacks, empty/truncated/changed terms, bad decoding, tampered/stale cache,
   symlinks, unexpected owner/mode, disk-full, cancellation, timeout and crashes.
5. Full ordered terms rendering and long-document layout/scrolling; CP1252,
   Unicode, lists/hyperlinks and multi-agreement identity. No omitted clauses.
6. Download/terms/install workers closed mid-flight: no use-after-free, leaked
   handles, stale callback acceptance, duplicate install or premature Setup finish.
7. Existing terms/artifact/runtime/bridge/recovery/Gate E suites, both native PE
   architectures, VC++ standalone path and disabled-entry rejection remain valid.
   Synthetic acceptance fixtures are labeled synthetic; no shipping bypass flag.

Then provide live initial-OOBE AND later-launch proof with private real packages:
explicit permission/size, progress/cancel, complete review, decline without
installation, cached re-review and separate acceptance through the real bridge.
Record exact package/request traces and machine/default/receipt before/after
checks, not merely successful unit tests or exit codes. User-provided lab consent
does not justify a production auto-accept switch.

After independent review, each newly enabled component still needs exact shared
artifact hashes/architectures, normal administrator/limited-user loading or actual
font selection, preserved overrides, failure/recovery, fresh corrected-ISO content
audit/new disposable installation, and actual X11/Wayland Classic/Open Luna tests:
800x600/normal resolution, locked/unlocked taskbar, toolbars and expanded/collapsed/
absent tray chevrons. These are pending gates, not inferred from unchanged UI files.

Deliver docs/OOBE_EMBEDDED_TERMS_ACCEPTANCE.md with PASS/FAIL/BLOCKED/NOT RUN cases,
base/tip/Wine/compositor SHAs, source/binary/package hashes, commands/exits,
expected/actual state, private evidence locations, failures/workarounds and cleanup.
Keep vendor text/binaries, credentials and XP captures out of public artifacts.
Push your branch and return its name, commit and report path. Do not merge master.

## Authoring checks

This document records approval, not implementation or completed live acceptance.
No VM was started and no production code/catalog was changed to write it.
A Windows rg invocation using docs/OOBE_COMPONENTS*WORKORDER.md returned OS error
123; used rg with -g 'OOBE*WORKORDER.md' over docs instead. A guessed
src/programs/oobe/oobe_backend.h lookup returned OS error 2; rg --files confirmed
the actual frontend helpers are oobe_worker.h and oobe_catalog.h.
