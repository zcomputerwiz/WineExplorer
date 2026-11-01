<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Independent OOBE review and repairs — 2026-10-18

Branch: `codex/oobe-components-repair`, based on Gemini's `e954c78c`.
Separate managed checkout; Gemini's `gemini/oobe-components` checkout was not
modified. The independent graphics-plan branch is `codex/chromium-graphics-plan`
at `12ca0aa6`; neither branch was merged to master by this repair pass.

## Verdict

**Framework repairs implemented; optional-component feature not ready for merge
as complete.** All six components remain disabled. No real vendor payload was
downloaded, no human consent was supplied, and no vendor binaries, artwork or
terms were added to this repository. Earlier acceptance claims are author reports,
not independent proof of an installed-image pass.

## Reproduced findings and fixes

1. CANCEL marked a live operation FAILED, admitted a second installer, and was
   later overwritten with SUCCESS by the original worker. A mock probe observed
   two concurrent installer calls. Cancellation now explicitly returns DEFERRED,
   retains RUNNING/deduplication, and waits for the actual outcome. The wizard
   refuses ordinary cancellation while installing; it does not claim MSI rollback.
2. Coordinator teardown abandoned daemon workers. Workers are now non-daemon,
   the endpoint is revoked before joining, and shutdown waits for safe completion.
   The backend wraps the installer in GNU timeout's own process group (30 minutes,
   TERM then KILL after 10 seconds). Machine-prefix Wine is waited for/stopped on
   timeout before restoring protected builtins. The maintenance lock remains held.
   If an idle prefix cannot be established, restoration is withheld, snapshots
   remain, and a root-owned `component-recovery-required` marker blocks installs
   until an administrator repairs and validates the prefix.
   This is failure/partial-install handling, not a transaction or rollback engine.
3. Operation IDs and results disappeared on restart, and unknown IDs guessed
   outcomes from a split verb/receipt. UUID-backed records are now atomically
   published in root-private `/var/lib/wineexplorer/component-operations/operations.json`.
   Interrupted RUNNING records become UNKNOWN. Queries never infer success from
   an ID; unresolved UNKNOWN records reject new installs pending administrator
   reconciliation. Runtime-directory cleanup does not remove this journal.
4. Bridge reconnect accounting reset on connect rather than valid response;
   empty/partial queries became installer failures. Actual bridge code now retries
   these queries, requires complete acknowledgements and exact success frames,
   and reports transport exhaustion as an unknown outcome. Socket writes suppress
   SIGPIPE. A lost START acknowledgement can be retried using the existing
   verb/digest deduplication; a restarted coordinator blocks uncertain work.
5. Legacy hash-only receipts and JSON with stale architecture/recipe/version
   could skip necessary work. Only matching structured records count now;
   successful JSON publication is atomic. Idempotency and regedit links are checked
   inside the maintenance lock, and links are not changed before the idle check.
6. Native tests substituted a stack-only imitation for the real worker lifetime
   and manually accepted their own digest. Production lifetime/acceptance helpers
   are now shared with tests. Real heap contexts and a delayed Win32 thread exercise
   late completion and reserved-reference failure cleanup. Failed CreateEvent or
   CreateThread releases both reserved references. Generation tags reject stale
   terms/install notifications. Terms remain Unicode through the edit control;
   no lossy system-codepage conversion of the legal text occurs.
7. The runner waited forever if the wizard exited without its completion marker.
   It now fails and preserves setup for retry. Signal traps exit through cleanup
   rather than returning to normal setup completion. Empty install-later selection
   exits without a bogus success notice; unavailable choices show reasons.
8. Stage authority now requires a root-owned regular non-writable stage file and
   trusted parent directory. Endpoint/state directories are checked, commands
   require exact arity and complete bounded lines, and a 15-second absolute input
   deadline prevents a byte-at-a-time client from extending its read indefinitely.

## Independent verification

- Debian WSL, `python3 -B -W error::ResourceWarning scripts/test_runtime_repairs.py -q`:
  **57 discovered, 56 passed, 1 explicitly skipped** (Wine unavailable). All backend
  mutations, installer calls and accepted terms in this suite are temporary/mock
  fixtures. Includes restart UNKNOWN, active-worker shutdown, deferred cancellation,
  scoped timeout cleanup before restoration, receipt changes and stage trust.
- `python3 -B scripts/test_oobe_bridge.py`: **4 passed**. This extracts and compiles
  the actual production Unix helper/install functions, not a Python reimplementation,
  and runs them against a temporary socket peer. Covers partial/empty query recovery,
  lost acknowledgement, installer failure, and RUNNING polling over 16 seconds
  (longer than the 15-second per-socket timeout). One START only during recovery.
- Both `x86_64-w64-mingw32-gcc` and `i686-w64-mingw32-gcc`, with
  `-Wall -Wextra -Werror`, compile `scripts/test_oobe_native.c`; both resulting PE
  executables run on the Windows host: **6 tests / 54 assertions each**.
- Actual `src/programs/oobe/oobe.c` compiles as both 32/64-bit objects using
  `-Wall -Wextra -Werror -Wno-unused-parameter -I src/include`.
  This is not a combined WoW64 link/build or Wine execution claim.
- `bash -n installer/scripts/wex-oobe.sh` and `git diff --check` pass.

## Still required before feature acceptance

- Resolve each package's actual supported download, complete dependency/architecture
  manifest and authentic applicable terms. Existing `validate_recipe_files` searches
  names/hashes anywhere in the pinned script: it does **not** prove the complete verb
  dependency closure or downloaded package identity. In particular, the vcrun list
  only describes x86, and corefonts' list needs auditing for omitted packages.
  Do not flip `disabled` merely because these tests pass.
- Current metadata records mutable VC runtime URLs, retired font distribution,
  terms embedded in Tahoma/DirectX packages and the upstream PhysX failure. Those
  are inherited research claims, not newly verified vendor availability findings.
  Revisit them with current primary-source evidence; missing standalone terms alone
  does not establish that an authentic consent workflow is impossible.
- A human must review/accept the real applicable terms. No agent acceptance or
  fabricated text is an acceptable substitute. Keep payloads/terms outside Git.
- UNKNOWN requires administrator inspection: establish that the machine prefix is
  idle, inspect logs, protected recovery snapshots and actual installed package
  state, then reconcile the matching journal record. Do not blindly delete the
  journal or retry a possibly running installation. A user-facing reconciliation
  workflow is not implemented yet. A `component-recovery-required` marker requires explicit
  administrator recovery from its recorded snapshot before removing the marker.
  Forced host power loss remains partial-install
  recovery work; atomic JSON publication is not a guarantee of power-loss durability.
- Do a fresh combined WoW64 build/link, real Wine worker/dialog tests, installed ISO
  acceptance, offline skip/resume, actual component installs and post-install app
  probes on a designated disposable VM. Verify Classic and Open Luna at 800x600,
  keyboard/scrolling/Unicode terms, and both X11 and Wayland. No visual/theme change
  is claimed independently verified in this pass.

## Failed attempts and environment state

- Initial review suite: `test_coordinator_reconnect_query` raised
  `ConnectionRefusedError: [Errno 111] Connection refused`. Waiting for pathname
  creation raced `listen`; fixture readiness now attempts a real connection.
- Multiline `apply_patch.bat` failed with `Invalid patch: The last line of the patch
  must be '*** End Patch'`. Direct `codex.exe --codex-run-as-apply-patch` with a
  PowerShell literal here-string worked. Two earlier out-of-order patch contexts
  (`class PackagingTests(unittest.TestCase)` and a `patch.object(... 'run')` line)
  failed to match; smaller correctly ordered hunks worked, with no partial edits.
- Initial native link commands targeted a missing `build/` and failed with
  `cannot open output file .../build/oobe-native64.exe: No such file or directory`
  (likewise `oobe-native32.exe`). Creating the ignored build directory fixed both.
- Wizard object compilation with `-Wextra -Werror` initially failed on six existing
  `unused parameter 'wParam'` callbacks. Re-run with `-Wno-unused-parameter` passed;
  no other warning suppression was needed for the wizard. The bridge extraction
  uses `-Wno-sign-compare` for existing ssize_t/sizeof comparisons.
- An early shutdown test attempted `GET_RECEIPTS` after intentional endpoint
  revocation and raised `FileNotFoundError: [Errno 2] No such file or directory`.
  Best-effort wakeup plus explicit endpoint/worker/journal assertions removed
  that test race. The final suite result above is after this fix.
- No VM was started, stopped, synced or redeployed; no graphical session/prefix
  was touched. Only Debian WSL and pure Windows test executables were used, not
  Gemini's WSL distributions. Temporary fixtures self-cleaned. Ignored PE/object
  build artifacts remain in this repair worktree's `build/` directory.
