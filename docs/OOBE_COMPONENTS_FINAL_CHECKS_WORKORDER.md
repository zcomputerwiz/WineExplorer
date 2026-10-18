<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work order for Gemini: installed-image OOBE acceptance and regressions

Date: 2026-10-18. Implementation baseline: `48b179ce` on
`codex/oobe-components-repair`, including `caed67f2`, `b6fe7f43` and `3d70da25`.
**Status: acceptance pending; do not merge to master.**

## Outcome and scope

Independently prove the repaired optional-components framework works in a fresh
installed image, not only in fixtures or an upgraded developer VM. Verify the
real wizard, privileged backend, later-launch path, split-user runtime loading,
failure/recovery behavior and Classic/Open Luna regressions on X11 and Wayland.
Publish reproducible evidence and an explicit merge recommendation. A successful
installer exit, unit suite, source compile or screenshot alone is insufficient.

Start a separate available branch, preferably `gemini/oobe-acceptance`, from the
published repair branch **including this work order**, not from the old
`gemini/oobe-components` tip. Record the actual full base/tip SHAs and preserve
unrelated changes. Use your own checkout/build directories. Do not reset another
checkout, change the Wine/compositor pins, force-push, merge/push master, dispatch
agents/chats, or touch the token-security/Chromium-graphics work.

This is primarily acceptance work. Add reproducible test harnesses where useful.
If you find a defect, preserve the failing evidence and identify the smallest
repair; keep any necessary implementation fix in a separate reviewable commit,
then rerun the affected gates on the resulting exact candidate. Never weaken a
guard, suppress a failure or rewrite a passing assertion merely to complete this
checklist. Publish the branch and report even if the honest result is not ready.

Read `CLAUDE.md`, `OOBE_COMPONENTS_WORKORDER.md`, both FIX work orders,
`OOBE_COMPONENTS_REPAIR_REVIEW.md`, and `OOBE_COMPONENTS_PROGRESS.md`. Older author
reports and early sections describe historical states: the progress document's
later sections supersede earlier claims about consent, downloads and test counts.
Keep the original requirements; do not inherit historical blanket PASS claims.

## Starting evidence and a critical catalog gate

Independent integration checks have already established:

- Exact VC++ 14.44.35211.0 x86/x64 packages and full applicable terms are pinned;
  raw terms, rendered text, recipe, artifacts and architecture participate in
  approval/verification. No vendor payload/document is committed or bundled.
- An obsolete Winetricks cabinet workaround returned zero while leaving Wine's
  MSVCP builtin installed. Corrected extraction now validates each minimum-runtime
  DLL's exact hash and PE architecture before publication and after restoration.
- Three private-prefix backend runs and real x86/x64 load/function probes were
  performed in the disposable integration clone. An administrator and limited
  user passed with per-user HKCU defaults and no runtime environment override;
  shared DLL hashes did not change during their tests.
- Last recorded suites: runtime repairs 57 discovered/56 passed/one explicit Wine
  skip; terms 11 passed; runtime artifacts/defaults 14 passed; bridge 5 passed.
  Earlier native host tests passed 54 assertions per architecture. Rerun current
  sources; these totals are references, not your acceptance results.

**All six shipping catalog entries are still disabled.** An all-disabled image
can prove skip/layout/denial behavior but cannot prove that the optional-download
feature is complete. Test-only metadata changes or mock acceptance are not a
shipping-path pass. If prerequisite identity/terms/backend checks succeed, prepare
a separate, explicitly identified release-candidate commit enabling only the
repaired VC runtime entry for end-to-end testing. Retain the other five disabled
with accurate reasons unless separately validated. Update tests that explicitly
assert the old all-disabled catalog to assert the reviewed per-entry policy;
retain backend rejection tests for genuinely disabled entries. Build and test the
exact proposed candidate, and do not merge it yourself. If enablement cannot be
justified, report this gate open instead of claiming feature completion.

## Environment ownership and build handoff

The user authorized creating/cloning disposable VMs using WSL or VirtualBox.
Debian WSL is allowed. Do not use/reset Gemini's other WSL distributions or any
existing non-disposable VM. Never reinstall the original XP/reference/build VMs.
The XP VM may be observed as a visual reference only: no copied/traced art,
resources, source or vendor files may enter the repository or image.

Existing lab paths below are **Debian WSL paths**, not Windows paths. Inspect
current state and ownership before reusing anything; process IDs may have changed.

- Disposable integration clone:
  `/var/tmp/wex-oobe-acceptance-20261018/vm/disk.qcow2`, with its own serial/QMP
  sockets and vmtest state under the same parent. Last seen running, 2 CPUs/3 GiB.
  This is NOT the required fresh installation of the repaired ISO.
- Its root partition was safely expanded into the clone's existing 20 GiB disk;
  root now has more space. Repeated snapshots/installs previously exhausted it.
  Check free space before every run; use adequate space for the new fresh VM.
- It contains new private runtime prefixes, protected recovery snapshots and a
  shared fixture `/var/tmp/wex-oobe-shared-runtime-20261018`. Alice's existing
  prefix was untouched; separate `~/.wine-oobe-runtime-test` prefixes and locked
  limited account `oobetest` were used. Do not treat these as fresh OOBE users.
- A loopback-only transfer server was left on port18768 serving only the lab
  transfer directory. Do not widen its bind address or serve disks/credentials.
- Independent build source:
  `/var/tmp/wex-oobe-build-20261018/source-local`; logs `build.log`,
  `configure.log`, `apt-extra.log` in its parent. Initial make has now ended with
  `Wine build complete.` This is NOT final acceptance: update to your exact
  candidate, reconfigure after newly installed dependencies, and rebuild.
  The initial source was older than the final user-defaults repair.
- Initial configure lacked both C++17 PE compilers/gettext and some media headers.
  Dependencies have since been installed. Confirm the final configure uses them;
  no silent module omissions or missing translations may be counted as parity.

The clone includes Debian Wine10 as a winetricks dependency. Always explicitly
use the product's `/usr/local/bin/wine` and `/usr/local/bin/wineserver`. Record
binary paths/versions; a PATH-selected Debian Wine pass is not a product pass.
The serial helper can reuse an already logged-in shell: its username argument
does not establish identity. Check `id`; use appropriate authenticated elevation.
Do not print passwords, put them in argv/Git, or assume cached sudo is still valid.
The clone's `sudo: unable to resolve host WEX-PC` warning needs checking in the
fresh image rather than suppressing it.

## Gate A — exact build, packaging and source/art provenance

1. Record candidate SHA, clean/dirty state, Wine pin
   `ed5fe967ed471ca524666631ad70dabbed6af076`, compositor pin, tool versions,
   configure flags and selected features. Assemble only open project sources.
   **Do not use `vm.sh sync`**: the legacy developer overlay may include private
   reference assets. Do not rebase onto another agent's unfinished implementation.
2. Build combined WoW64 with `--enable-archs=i386,x86_64 --prefix=/usr/local`, both
   MinGW C++17 compilers and the actual Unix bridge. Fully link both OOBE PE
   variants and the required product runtime. Resolve missing mandatory features;
   explain any optional configure omission rather than silently accepting it.
3. Use `installer/build-iso.sh` and its packaging path with BUILD_DIR belonging
   to THIS source tree. Use new explicit native-ext4 WORK_DIR/OUT_ISO paths and
   an independent compositor build. Do not package the host's installed Wine,
   reuse a stale chroot, run concurrent make/configure in one tree, or use a broad
   cleanup target. An unattended install that bypasses OOBE cannot prove the UI.
4. Audit packaged files, manifests/licensing and ISO contents. Prove both PE
   architectures and the current backend/session helper are present. Identify
   absence of vendor installers, DLLs, fonts, art and legal documents in the ISO;
   inspect contents, not filenames alone. Private test caches must not be staged.
   Record ISO SHA256, bytes and size delta against an identified comparable image.

## Gate B — repeatable automated and native checks

From the exact candidate run, retaining commands/exit codes and separate logs:

```sh
python3 -B -W error::ResourceWarning scripts/test_runtime_repairs.py -q
python3 -B scripts/test_component_terms.py -q
python3 -B scripts/test_component_runtime_artifacts.py -q
python3 -B scripts/test_oobe_bridge.py -q
bash -n installer/scripts/wex-oobe.sh
bash -n scripts/wine-explorer-session-common.sh
git diff --check
```

Run trusted-cache fixtures in an isolated root-owned sandbox so they actually
execute; a non-root skip is not a pass. Run the real native production-helper
test `scripts/test_oobe_native.c` as both x86 and x64 PEs under the product Wine,
not just on the Windows host. Compile with warnings as errors and record any
existing narrowly justified suppression. Verify late completions, generation
checks, thread-creation failure and reference ownership; a copied mock controller
cannot establish worker safety. Preserve all skips as explicit open evidence.

## Gate C — fresh installed-image setup and authority

Install the new ISO onto a NEW disposable disk, with no inherited terms, payload,
receipt, journal or user-prefix fixtures. Record the install, reboot into OOBE,
and create an administrator plus a limited user through the actual setup flow.
Prove the wizard runs as `wexsetup`, not root/admin, while the narrowly scoped
coordinator has root authority only during the OOBE stage.

On a separate clean run/snapshot disconnect networking before OOBE. All choices
must start unchecked, unavailable choices must show accurate reasons, and Skip/
install later must finish setup into a working desktop without payload requests,
invented receipts, a false success tally or manually provisioned legal text.
The finished/aborted stage must not admit new requests through an old connection.
Check bounded framing, unauthorized UID rejection and strict verb/path/URL limits.

After normal completion inspect processes, sockets, stage markers, journal,
service status, user provisioning and shared-prefix permissions. No OOBE worker,
coordinator endpoint or setup authority may remain usable. No orphan installer,
wineserver or display service may be mislabeled as cleaned up. Distinguish the
normal desktop's processes from OOBE-owned ones. Reboot/login must remain usable.

## Gate D — real terms, online installation and install later

The human explicitly accepted the full VC++ 2015–2022 terms for the described
14.44.35211.0 x86/x64 disposable-VM test in the originating chat. Preserve that
scope/evidence; this is not blanket consent, redistribution permission or a
production auto-accept setting. If package/terms/scope changes, obtain appropriate
human confirmation. Do not fabricate acceptance for any other component.

Exercise the real native wizard/bridge/backend chain on the candidate:

1. Start without terms/payload caches. Review retrieves the exact full terms,
   including numbered clauses, bullet markers, hyperlink targets and readable
   Unicode. Prove no binary payload request occurred before explicit acceptance;
   identify URLs/timestamps with appropriately scoped network/download evidence.
2. Closing/declining leaves acceptance unset and launches no installer. Back,
   changed selection, reopening, cache/policy changes and changed response between
   size-query/read cannot reuse stale acceptance. A fresh Review can refresh a
   trusted stale cache; insecure/malformed/truncated sources still fail closed.
3. Accept the approved runtime in the real UI using the recorded human authority;
   install to `/var/lib/wineexplorer/machine`, never `/etc/skel/.wine` or a setup
   user's private Windows tree. Verify exact x86/x64 package/member identities,
   actual resulting DLLs, current receipt and truthful finish-page tallies.
4. From an offline-skipped installation with an EMPTY terms cache, log in as the
   ordinary administrator and launch Windows Components. Exercise authenticated
   preparation/review/install using the same backend, not a root shell shortcut.
   A limited user cannot prepare/install machine components or forge consent,
   while already installed components remain usable. Test canceling elevation.

Private cache reuse may be a separately labeled repeat/idempotency test, never
the evidence for empty-cache acquisition. Other five components must remain
honestly unavailable unless their complete independent approval path is proven.

## Gate E — responsiveness, truthful failure and safe recovery

Use controlled synthetic/local fixtures for damaging faults and forced crashes;
do not damage a real accepted install just to make a test. Exercise the actual
native UI and production bridge where applicable, labeling substitutions:

- Slow terms, an installer longer than 15 seconds, partial/empty frames, lost START
  acknowledgement, disconnect/reconnect and failure after acknowledgement. UI
  repaints and remains usable; one operation/installer is launched, not two.
- Decline/close/reopen within 500 ms, delayed completion, repeated Review, wizard
  closure and runner signal during installation. No stale control update,
  access-after-free, unexplained hang or premature setup completion is allowed.
- Cancellation during an unsafe-to-interrupt MSI must be explained/deferred, not
  reported as rollback. Finish/cleanup waits for actual safe completion and outcome.
- Restart/resume with an interrupted RUNNING journal, missing operation ID,
  failed install, tampered/outdated receipt and corrupt cache. UNKNOWN is not
  SUCCESS/FAILED by guess; retry cannot launch overlapping maintenance.
- Recovery-required marker and retained protected snapshots block new installs.
  Document actual administrator reconciliation: establish idleness, inspect logs,
  package/artifact and protected-state evidence, reconcile the matching record,
  then prove a safe retry. Never blindly delete journals/markers to unblock tests.

Report missing recovery UI as a real limitation. If an ordinary recoverable
network failure traps Setup or requires undocumented manual edits, report a
merge blocker rather than calling persistence a complete recovery workflow.

## Gate F — installed runtime, permissions and regressions

Compile `scripts/tests/component-runtime-probe.c` for both architectures with
MinGW (`-Wall -Wextra -Werror`, link `-lversion`). In the FRESH installed image,
run both PEs as the administrator and limited user after normal login on each
supported display backend. Require approved DLL versions and successful native
function invocation, not file existence or a Winetricks success receipt.

Do NOT set runtime WINEDLLOVERRIDES to force a pass. Prove the packaged shared
login helper supplies only missing HKCU defaults, repeats idempotently and leaves
existing global/wildcard/AppDefaults/environment choices intact. Test an explicit
user builtin choice as a negative control. Refuse live-hive mutation; after a
retry/login, user settings must still be intact. Fresh users created after the
installation must also work. Plain Wine execution outside a desktop login should
be tested separately and any lifecycle limitation reported, not hidden by fixtures.

Capture before/after hashes, links, owners, modes and ACLs for protected DLLs,
global `/usr/local/lib/wine` files, machine hives, user hives and shared-directory
policies. Core dwrite/gdiplus/usp10/riched20/mf*/msxml* protection and unrelated
overrides must survive success/failure. Native DLL publication must not write
through links into installed Wine, drop sudo-group/default ACL access, or grant
limited users write access. Run a 32-bit baseline application without components
and regress Mono/Gecko availability plus the open-font/fallback smoke checks.

## Gate G — visual, keyboard and desktop-theme matrix

Inspect Classic AND Open Luna, each on X11 AND Wayland, at 800x600 and a normal
setup/desktop resolution. Record actual resolution, theme, backend and screenshots
for each matrix cell. Do not mark untested cells PASS or substitute an Xvfb-only
probe for a real Wayland session. Setup's runner may use X11; distinguish that
from Wayland post-install wizard/launcher and desktop regressions.

Check native OOBE catalog/terms/progress/summary and later Windows Components:
no clipped controls, unreadable disabled reasons, corrupted Unicode, unreachable
accept/decline/skip controls or hidden essential terms. Verify Tab/Shift-Tab order,
visible focus, keyboard checkbox toggles, Enter/Escape, scrolling to the end,
resize behavior where supported, cancel and return navigation.

For EACH theme/backend inspect the desktop before and after install/login:

- Taskbar locked and unlocked, grippers, boundaries, button metrics and resizing.
- Toolbars on/off, handles, separators and overflow chevrons where supported.
- Notification-area hidden-icons chevron in show/hide and expanded/collapsed
  states; toggle auto-hide inactive icons and compare enabled/disabled behavior.
- Theme switching, Explorer windows, Start menu and common dialogs. Classic must
  not inherit Luna-only art/metrics; Luna must not lose its themed states.

Compare themed states visually against the XP VM where available, using it only
as reference. Separate inherited baseline differences from new regressions using
an identified baseline screenshot/build. Never count an existing mismatch as a
newly fixed result or conceal it. Record actionable visual defects for review;
do not import proprietary art or broaden this into a graphics-architecture rewrite.

## Deliverables and review decision

Commit `docs/OOBE_COMPONENTS_FINAL_CHECKS_REPORT.md` with a gate/case matrix:
PASS / FAIL / BLOCKED / NOT RUN, exact candidate/full SHAs, reproducible commands,
versions, ISO digest/bytes, VM identities, architecture/theme/backend/resolution,
actual expected/observed outcomes and evidence locations. Separate source/unit,
native fixture, integration clone and fresh installed-image results. Reference
the original requirements and unresolved F/R items; don't overwrite history.

Include per-component availability/terms/payload/functional status; all failing
commands with exact errors/workarounds; pending defects ranked by impact; cleanup
and remaining VM/process state; and a clear ready/not-ready recommendation.
Keep proprietary payloads/legal text, XP screenshots/art and credentials outside
Git. Store permitted project screenshots/logs in a documented artifact location,
with hashes if external; scrub private data without hiding functional failures.

Commit coherent test/fix steps and **push the branch**. Hand back branch name,
tip SHA, report filename and blockers. No merge to master. A gate without real
evidence remains open; an all-disabled or test-bypassed image is not full feature
acceptance.
