<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Qwen continuation: close the embedded-terms review findings

Date: 2026-10-20. Read this entire document before acting.
This supplements OOBE_EMBEDDED_TERMS_COMPLETION_WORKORDER.md (6a298d28) and
OOBE_EMBEDDED_TERMS_WORKORDER.md. Their safety rules and case IDs still apply.
This is a continuation of your existing work, NOT a restart or a merge approval.

## 1. Scope and preserved state

Work ONLY in D:/Projects/WineExplorer on gemini/oobe-embedded-terms. At review:
HEAD be7dadb7629dd967c7f4f539145640844e7d7acf, 14 tracked files modified and
these three new files untracked:

- docs/OOBE_EMBEDDED_TERMS_ACCEPTANCE.md
- scripts/test_component_review.py
- scripts/test_oobe_review.py

The tracked changes cover the backend, bridge, native OOBE UI/catalog/worker,
resources and tests. They are useful progress: preserve them. The branch still
tracks origin/codex/oobe-final-correctness and reports behind by one commit.
Do NOT pull/rebase/checkout over this dirty tree to obtain these instructions.
Read them from the published reviewer branch, saving only the new document if
needed. Do not overwrite your existing implementation files.

First record git status --short --branch --ignore-submodules=all, git rev-parse
HEAD and git diff --stat. Compare with this description. If files changed since
review, inspect the new diff; do not undo another worker's changes. Save a private
diff backup including the new files before editing. A clearly labeled UNVERIFIED
checkpoint is allowed; it is not acceptance. Never git reset --hard, git clean,
force push, blanket git add ., or merge to master.

Do not edit D:/DeepThought/Projects/WineExplorer or its Wine/wexcomp submodules:
that is the reviewer's separate graphics checkout. Do not edit the reviewer's
C:/Users/Owner/.codex/worktrees/oobe-components-repair/WineExplorer checkout.
Do not update any Wine/compositor pin. CoreFonts stays REVIEW ONLY, installation
disabled through UI/CLI/protocol. Other vendor adapters and font installation
are OUT OF SCOPE. Wine's open Tahoma remains unchanged.

## 2. VM access: use the verified connection, do not repair accounts

The reviewer successfully connected on October 20 with this PowerShell command:

```powershell
ssh -o BatchMode=yes -o ConnectTimeout=8 -o StrictHostKeyChecking=yes -o IPQoS=none -i D:/VirtualBox_VMs/vm_key -p 2222 wineuser@127.0.0.1 'id; systemctl is-active ssh; hostname'
```

The registered WineExplorer-VM is running from
D:/VirtualBox_VMs/WineExplorer-VM/WineExplorer-VM.vbox. Its forwarding is
127.0.0.1:2222 -> 10.0.2.15:22, NAT cable connected; hostname wine-builder.
Port 2223 is NOT this VM's configured forwarding. Do not switch ports at random.
The current and previous VM configs agree on the 2222 rule. ssh.service is active;
existing key authentication succeeds, and wineuser has sudo access.

Two password-protected desktop accounts are expected: wineuser (Wine User) and
wextest (Test User), with recorded password-change dates October 6 and October 12.
LightDM requires login: its active configuration has no autologin-user. The old
GDM auto-login configuration is inactive. wexsetup is a separate password-locked
service account added October 20, not a third password-login account.

Do NOT reset passwords, unlock wexsetup, enable autologin, weaken SSH/host-key
checking, edit authorized_keys, change NAT, restart SSH/LightDM, or reboot/reset
the VM to make tests pass. Do not log in to another user's desktop. If SSH fails,
record the EXACT command (redact secrets), stderr, timestamp and failure class:
refused/timeout/host-key/authentication. Inspect power/forwarding read-only and
report. Earlier failures were not reproduced; their cause is not established.

Your draft names ~/oobe-lab on this existing VM. It may be used for its OWN
isolated source/build/tests after verifying ownership and hashes. It is NOT a
fresh-installed or disposable-image acceptance result merely because named lab.
No machine-global install, display takeover, account/session change or live
system-prefix mutation on this VM. Use a newly created/cloned disposable VM for
destructive/fresh-OOBE tests; the user already authorized such a clone. If cloning
would require interrupting this running VM, stop and request coordination.
WSL Debian is also available for isolated tests. Never reuse the graphics lab.

## 3. Finish these fixes in order

### Q1 / P2: reject malformed PE headers (independently reproduced)

In scripts/wex-components.py, pe_resource around lines 839-850 reads optional
header fields outside the DECLARED optional header. _pe_unpack checks whole-file
bounds, which does not establish optional-header bounds.

Exact negative control using your existing synthetic build_pe fixture:

1. Make a valid fixture and require pe_resource to return its original bytes.
2. Read pe = DWORD at file offset 0x3c; optional = pe + 24.
3. Read original SizeOfOptionalHeader = WORD at pe + 20.
4. Save the 16 bytes at optional + original_size + 8 through +24 (the section
   virtual-size/RVA/raw-size/raw-offset fields).
5. Set SizeOfOptionalHeader at pe + 20 to ZERO.
6. Put those saved 16 bytes at optional + 8 through +24. Leave the PE32 magic,
   old directory fields and original resource bytes intact.
7. Current production parser returns the license bytes: reviewer reproduced
   INVALID_ZERO_OPTIONAL_HEADER_ACCEPTED True with only AST-selected pure parser
   functions, synthetic bytes and an unprivileged process. No payload ran.
8. The repaired parser MUST reject this fixture with a structural ValueError.

Before reading magic/directory count/resource directory, prove those fields fit
inside the declared optional header, and that the declared header/whole section
table fit the input. Validate each supported PE32/PE32+ layout independently;
unsupported layouts must fail explicitly. Add tests for zero/short/truncated
optional headers, insufficient directory count/extent, truncated section tables,
ambiguous mappings and resources crossing declared spans. Keep valid pinned
CoreFonts extraction and existing synthetic controls passing. Do not relax hashes,
reinterpret arbitrary later bytes as header fields or execute a vendor extractor.

### Q2 / C1: truthful cache availability without network

review_state (around 1023) calls embedded_license_text(..., strict=False), so a
valid terms JSON can advertise cached even when a package is missing/tampered.
The root GET_TERMS path subsequently revalidates packages and can fail. Align
GET_COMPONENTS, REVIEW_INFO, GET_TERMS, native UI and later frontend semantics.

Required controls: create a complete valid synthetic review cache; establish the
valid state; then independently delete one package, change its bytes, change a
resource/hash/policy, replace a file/directory with a symlink, and alter ownership
or modes. For EACH independent variant, call production availability AND terms
seams. Never advertise fully verified cached state for a variant that strict
retrieval rejects. Return a distinct missing/stale/integrity outcome or an honest
not-verified state; do not silently refresh/download or swallow integrity errors.

Opening/catalog/selecting/querying remains ZERO network and ZERO elevation.
Do not make private package caches world-readable to solve later-user access.
If an unprivileged view cannot establish package validity, distinguish a cached
document from a fully verified package cache, or use an existing authenticated
metadata path; do not claim verification you did not perform. Test root and
normal-user behavior explicitly. Keep all receipt/machine/default state unchanged.

### Q3 / A1-A2-U1: unchecked acceptance and actual controller behavior

The NEW review-dialog completion path in oobe.c around 719-724 prechecks
IDC_REV_AGREE when accepted_sha256 matches. This contradicts this milestone's
explicitly unchecked separate acceptance action. Opening/reopening/retrying the
review dialog must not precheck or record fresh install acceptance. CoreFonts
shows no install-accept control and remains unselectable for installation.

Test an INSTALL-ELIGIBLE SYNTHETIC component, not disabled CoreFonts temporarily
enabled in production. Cover initial display, reopen same digest, changed digest,
download retry, Cancel/Close/Back and stale/closed-window completions. Until the
user explicitly checks acceptance and confirms it, no START_INSTALL/install call
or accepted/install receipt may occur. Backend must recheck the exact fingerprint.

The current native test_review_only_controller_seam only changes fields, compares
an incremented generation and calls small helpers. That does NOT dispatch a
stale completion through OobeReviewProc or prove an actual checkbox stays clear.
Exercise the production controller invoked by the dialog (factor a small shared
controller seam if necessary), or run the real dialog with synthetic transport.
Send a stale-generation completion and a completion after close and require zero
acceptance/installation and no destroyed-window/borrowed-context access. Show the
same-digest reopen case failing on the old prechecking implementation.

Do not casually redesign the existing standalone VC++ consent route. Preserve
its behavior/tests; this assignment fixes the new embedded-review flow.

### Q4 / A2: repair nondiscriminating fingerprint tests

test_A2_each_fingerprint_input_change_invalidates_the_cached_binding (around 503)
first writes TAMPERED text, then runs the metadata subtests WITHOUT restoring a
valid record. The record is restored only AFTER those subtests. They can all pass
because of the pre-existing text corruption, even if metadata binding is broken.

Start EVERY mutation from an independently verified valid record/cache. Assert
baseline success immediately before applying that one mutation. Restore or use
fresh fixtures between subtests. Cover package identity/order/dependencies,
architecture policy, resource locator/language/encoding, schema/render recipe,
policy and rendered/raw identities as applicable to production. Test that each
change invalidates prior acceptance and cannot install using the old digest.
As an isolated TEST negative control, make identity omit one tested input and
require that corresponding test to fail. Never ship that weakened identity.

### Q5 / N2-U1-U2: honest source, size and cancellation messages

The native dialog currently says "official vendor distribution archive" but the
shipping CoreFonts review route is the pinned SourceForge archive. Show the actual
approved source/host and distinguish publisher Microsoft from download provider
SourceForge. Do not imply a Microsoft-hosted download. Show all eleven packages'
combined bytes, cache/disk needs and that PACKAGES are downloaded only for static
license extraction; nothing executes/installs until a separate eligible install.
Do not label a package download merely a tiny terms download.

Cancellation can leave verified cache files; "nothing was installed or changed"
is inaccurate. Say nothing was installed and explain any retained verified cache
and removed owned partials. Do not promise rollback of already cached downloads.
Prove initial and later UI actually reach the review-only action, with no install
checkbox/auto-accept. Confirm readable long terms at 800x600 and normal resolution.

### Q6 / B1-L1: close numeric and lock-wait edge coverage

Audit oobe_review_info in wexhost/unixlib.c: sscanf %llu/%u plus cached<=1 does not
prove nonnegative, nonoverflowing, catalog-bounded numbers. Require exact decimal
grammar, bounded total/package count and complete framing. Apply equivalent
checks to progress (done<=total), IDs/status/digest fields. Test negative signs,
plus signs if unsupported, overflow, zero/over-cap totals/counts, suffix junk,
partial/truncated/oversized replies through the ACTUAL bridge parsing seam.
Keep valid maximum-boundary cases; your 16 GiB catalog digit-count correction is
reasonable and should stay. Do not infer a native bridge pass from Python-only
protocol mocks or from matching a console summary string.

_review_download_lock currently waits up to 60 s without a cancellation argument;
review_download checks cancel only after entering it, while shutdown joins have
a 30 s budget. Hold the cache lock in another process, start review, then cancel
and separately expire/shut down the stage. Measure latency and worker/partial
cleanup. Make lock acquisition cancellation/deadline aware; no abandoned daemon
worker writing later. Also retain existing transfer/cache/publication cancel
cases. Record actual bounds rather than reducing production timeouts only in
tests and calling that a production-bound proof.

## 4. Verification and report: no invented completion

Run the COMPLETE command matrix in section 5 of the previous completion work
order in a new owned lab/output directory. Use bounded subprocesses; no real
Internet/prompts for synthetic suites. Root-only suites must also retain their
applicable normal-user denial/transport controls. Build changed OOBE/wexhost for
BOTH PEs in a combined i386,x86_64 Wine configuration, and EXECUTE both native
test binaries with the explicitly verified project loader and fresh prefixes.
Do not install a random Wine, guess wine64, add -municode to main, or count only
compilation as success. Record configure args, loader/source/binary hashes.

Your DRAFT claims: review backend 30 tests, coordinator 13, runtime 57 with one
explicit Wine skip, terms 11, artifacts 14, recovery 14, bridge 14, capture audit 4,
synthetic Gate E passing, native 8 groups/114 assertions per PE. These are worker
claims, NOT independent acceptance. Preserve old assertions and investigate any
lost test/additional skip. New tests must be discriminating, not inflated counts.

Update docs/OOBE_EMBEDDED_TERMS_ACCEPTANCE.md with Q1-Q6 AND original N/S/A/P/C/L/B/U
case IDs, exact commands/exits/counts/skips, before/after negative controls and
private evidence locations. Remove EVERY [FILL]. Replace unavailable evidence
with BLOCKED/NOT RUN and its exact prerequisite, never PASS or vague "pending".
Separate synthetic, source/controller, native executed, actual live UI, desktop
and fresh-installed-image evidence. Carry the failures/workarounds and cleanup.

On a disposable clone/owned isolated environment, finish the previous section 6
live checks: open/select/decline zero requests; explicit download/progress/cancel/
retry; exact eleven packages and complete mapped agreements; no PE execution;
cached offline reopen; no accepted/install receipts; CoreFonts UI/CLI/protocol
install rejection; eligible synthetic separate acceptance; standalone VC++ route;
32-bit ordinary app; limited-user denial and Setup Skip. No GUI bypasses or
production auto-accept flags. Keep vendor packages/text/captures outside public Git.

Record actual Classic/Open Luna at 800x600 and normal resolution, then the
required X11/Wayland taskbar locked/unlocked, toolbar and expanded/collapsed/absent
tray-chevron cells. If unavailable, mark NOT RUN with the missing environment;
do not substitute login-screen pictures, old screenshots or software claims for
actual evidence. Fresh corrected-ISO acceptance is a separate gate unless run.

## 5. Publish and stop

Commit only explicit reviewed source/test/report filenames on
gemini/oobe-embedded-terms. Check git diff --check, staged diff and the absence
of proprietary payload/text/art, keys/secrets and unrelated submodule changes.
Keep original authorship/provenance. Publish to the CORRECT branch:

```powershell
git push -u origin gemini/oobe-embedded-terms
```

No force, master push, merge, extra adapters or CoreFonts install enablement.
Stop and request independent review after Q1-Q6 and available acceptance checks.
Missing hardware/live cells may remain honestly NOT RUN; unresolved functional
failures must stay explicitly FAIL/BLOCKED, not "ready to merge". If interrupted,
save/push a labeled checkpoint and write the exact next command and owned session
state, rather than leaving another large local-only diff.

Return: branch, commit, acceptance-report filename, Q1-Q6 status, actual executed
test totals and exact remaining blockers. Stop only processes you created. Leave
the existing VM running at its login screen unless separately authorized to
change its state. Do not message other agents or change their checkout.

## Authoring / inspection notes

This is a documentation-only continuation, not an implementation fix. Reviewer
did not edit Qwen's dirty checkout or change VM/account/network settings. SSH
diagnostics were read-only using existing key/sudo access. An initial PowerShell
double-quoted remote awk command expanded its $ fields and failed; a literal
here-string fixed argument transport. Initial unelevated Git/CIM inspection was
denied; scoped read-only elevation succeeded. Direct serialization of a TCP CIM
object produced a depth-truncation warning; its useful listener was 127.0.0.1:2222.
No failed diagnostic was treated as proof of a guest configuration problem.
Current documentation authoring likewise required scoped Git access after an
unelevated status hit submodule/worktree permission denials.
