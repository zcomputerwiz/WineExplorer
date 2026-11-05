<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->

# Shell DWM backend unification

## Chosen end state

The shell computes one neutral effect description from broker state and submits it
through one backend-dispatched `WexWm*` API. Keep X11 drawing in
`src/dlls/wexhost/compose.c` and Wayland drawing in the existing `wexcomp` scene.
No new compositor, broker transport, or material policy is needed.

Use the new, explicitly named
`WexWmApplyEffects(HWND, const WEX_WINDOW_EFFECT *, WEX_WM_EFFECT_RESULT *)`,
rather than changing the ABI of the handle-based
`WexWmSetEffects`. The public description carries kind none/blur/acrylic/mica/gradient,
ABGR tint plus validity, client-space region plus validity, dark, corner preference
(including default versus explicit square), glass default radius, and shadow intent.
Coordinates remain Wine pixels until the backend adapter converts them. Carry a
separate material-application bit: gate-off currently means skip X11 backdrop writes,
but send no Wayland backdrop; a kind of none alone cannot express both operations.

One PE-side helper in wexhost resolves HWND identity: effects use the historical
XID-first rule; thumbnail/Peek adapters can request a specific backend to preserve
their session policy. It returns an X window ID or 32-digit wex handle, plus the
scale used by Wayland. Client-to-window offsets and USER/window-property calls
stay on the PE side; Unix backends receive
fixed-size records, never HWNDs to dereference. Identity is the only platform-specific
window input; conversion metadata is derived by that helper. Use a tagged internal
identity, not an XID encoded as a fake Wayland string. Preserve effects on arbitrary
Wine windows, not just entries in the foreign-window inventory.

## What exists and must survive

- `dwm_bridge.c`: `apply_window` prefers an available XID, falls back to a wex handle,
  and retries missing identities for 40 polls. X11 errors remain retryable; Wayland
  retries only `ERROR_NOT_READY`, recording the scale for every other result.
  Wayland scale changes rearm completed pending entries without a broker revision.
- `desktop.c`: both session paths currently require `Shell/Composition=1` to start
  the bridge/clock. X11 additionally acquires optional composition through
  `WexCompositionSetEnabled`; keep extension/selection failures and default-off
  behavior. Wayland already composes and must never acquire X redirection. Bridge
  polling also requires `DwmService`; the independent clock gate is `DwmFrameClock`.
- `read_materials_gate`: `DwmMaterials`, high contrast and `EnableTransparency` are
  sampled at thread start. Do not introduce live gate changes in this refactor.
- `wexhost.c`, `wexhost.spec`, `wexhost_unix.h`, `unixlib.c`: current composition
  exports use separate Unix calls; both native and WoW64 call tables must stay in
  step. Preserve old exports for existing callers. New records need fixed-width
  members and size/offset assertions, including pointer/page transport.
- `wm.c` already dispatches optional effects/thumbnails/peek operations.
  `waylandwm.c` implements them (protocol v13 effects, v14 thumbnails/peek).
  `x11wm.c` now supplies neutral effects, thumbnails and peek ops; its composition calls serialize
  `compose_corner/backdrop/shadow/thumbnails/peek` with `begin_call/end_call`.
  Do not hold the dispatch mutex while taking backend locks or waiting for frames.
- Clock backend ops and `WexWmSetClock/WexWmWaitFrame` now exist.
  `WexCompositionGetEnabled` retains its Wayland fallback in `x11wm.c`.
  `composition_clock_wait` in `compose.c` is a shared generation/condition wait;
  Wayland frame events call `compose_clock_wake`. The shell's Wine thread pulses
  the Win32 event; the Unix producer must not touch Win32 events.
- Thumbnail paths fetch one broker list, resolve identities on every poll, and omit
  unresolved pairs. X11 sends each poll (`compose_thumbnails` suppresses duplicates);
  Wayland caches successfully sent records **and both scales**. Dest coordinates
  round nearest; source bounds round outward. Keep 80-record capacity.
- Peek's iconic-picture popup remains in `taskband_preview.c`, outside the bridge;
  start/end now call the neutral host adapter. Preserve iconic-picture fallback,
  target selection and the different keep limits (X11 8, Wayland 64).
- Bridge start/stop now explicitly clear either backend's thumbnails and reset
  broker/pending state; the host invalidates its sent cache. Repeated lifecycle
  and failed-clear tests remain acceptance gates for slice 4.

## Ordered slices

Each slice leaves existing exports/callers working and can be built and tested
independently before proceeding. Slices 1-5 are committed and runtime verified
(user-provided baseline; slices 2-3: a1e27298, slices 4-5: a620e492).
The implementation notes below retain their original static-checkpoint wording;
their outstanding acceptance status is superseded by that baseline. The follow-up
review below is static only and needs its own build/runtime acceptance.

1. **Share effect policy locally (implemented).** Add `effect_kind`, `window_effect`
   and `pending_effect` in `dwm_bridge.c`. Compute once, preserve raw tint/region,
   corner sentinel and glass defaults, and translate through the existing adapters.
   Check the argument contracts below against the previous code, then build both
   WoW64 architectures and compare effect traces/pixels when a build is available.
   Risk: precedence, inactive fields, dark-with-none, or premature scale conversion.
2. **Add the neutral host contract and X11 ops (implemented statically).** Add the API, fixed-size
   transport, identity/conversion helper, and X11 effect op above `compose.c`;
   adapt Wayland to the same neutral input. Retain legacy entry points and callers.
   X11 must preserve corner-first writes, material gating and first-error precedence;
   do not add a new shadow write until existing X11 shadow ownership is reconciled
   with `desktop.c`'s hook and compose's glass-frame policy. Verify old/new tuples,
   missing identities, disabled composition, extension failures, and protocol <13.
   Risk: ABI layout, nested locks, HWND/XID/handle confusion, changed retry errors.
3. **Switch bridge effects to the neutral API (implemented statically).** Move conversion/identity knowledge
   out of `apply_window/apply_wayland`; retain retry timing and scale refresh through
   a neutral host revision/scale query. Retire the local adapters only after parity.
   Compare all contracts below, gate-off behavior, delayed X windows/handles, destroyed
   windows, fractional scale changes and broker snapshots/restarts. Risk: lost refresh
   or accidentally changing the historical XID-first dispatch in mixed sessions;
   explicitly validate mixed-session policy before selecting solely by backend.
4. **Unify thumbnails and peek (implemented statically).** Add HWND-based neutral list/peek APIs using the
   same helper, reuse the X11 backend ops from slice 2, and migrate bridge plus preview callers.
   Preserve per-backend keep limits, list order, omissions, return codes, geometry
   rounding, and success-only cache updates. Clear lists and caches on stop/restart
   in a separately verified lifecycle change within this slice. Test empty lists,
   late identities, source/destination scale changes, minimization/iconic Peek,
   broker disappearance and repeated start/stop. Risk: stale scenes and handle reuse.
5. **Unify clock and startup last (implemented statically).** Backend clock ops and
   `WexWmSetClock/WexWmWaitFrame` are available from slice 2; keep the existing shared wait primitive and
   producer ownership. Migrate clock callers and centralize composition eligibility,
   so `bridge_wayland` and the separate Wayland start wrapper can be removed.
   Keep explicit backend adoption, Composition/DwmService/DwmFrameClock gates,
   bounded waits, page detach before unmap and wake-on-disable. Verify DwmFlush,
   timing info, disabled/old-compositor cases and shutdown under concurrent waits.
   Risk: deadlocks, stale mapped pages, false composed-frame reports or X selection
   acquisition on Wayland. Keep legacy composition exports as compatibility wrappers.

For every later slice: WoW64 build (i386 and x86_64), a 32-bit probe, fresh-prefix
X11 and Wayland runs, and visible captures of material/corner/shadow/thumbnail/Peek
results are acceptance gates. Build/API success alone does not establish parity.

## Follow-up static review (2026-11-04)

Reviewed a1e27298, a620e492 and the complete src/ delta since 948b4e04,
plus compose.c and the backend lifecycle code. No compiler, WSL, build, VM or
runtime test was used. The five committed slices remain the user-provided
runtime baseline; the changes in this section are uncommitted and unverified at
runtime. No stock Wine file was edited, so docs/STOCK_CHANGES.md needs no new entry.
The pre-existing dirty wine/ worktree and unrelated untracked files are preserved.

### Edits and fixed defects

- No bridge_wayland branch or separate Wayland startup wrapper remains in src/;
  slice 5 already removed them. Consolidated the bridge's identical start/stop
  state resets without changing their ordering relative to thread creation/join.
- Completed effects previously monitored only nonzero applied scales. A window
  moving from Wayland to X11 retained that scale; monitoring then compared it
  against a raw Wayland property/default and could submit every poll forever.
  Completed X11 entries could also miss a later Wayland identity. The public
  WexWmGetEffectScale query now resolves the same AUTO, XID-first identity as
  application: zero for X11/missing identity, otherwise Wayland thousandths.
  Completed entries compare both zero/nonzero values, and every non-retry result
  records its scale, including zero. A stable transition stops resubmitting;
  errors still consume the bounded 40-poll budget. Fractional-scale events still
  rearm without a broker revision. Thumbnail conversion uses the separate raw
  scale helper and still honors its explicitly selected backend.
- Wayland frame callbacks read clock_page after releasing clock_page_mutex,
  racing detach. Publication and wake now both happen while the page lock is
  held. Clock attach previously released wl_mutex between its availability test
  and page assignment, allowing stop/disconnect to win that gap. It now holds
  wl_mutex through the page update; nested order is wl_mutex -> clock_page_mutex
  -> the shared generation mutex. Reader exit clears DWM_CLOCK_COMPOSED and wakes
  waiters before retiring its connection. Detach remains successful even when
  disconnected or on an old protocol, and the shell still detaches and joins
  before unmapping. The shell stop flag now uses Interlocked reads/writes.
- X11 and Wayland now share compose_clock_publish outside COMPOSE_X11, including
  builds without X libraries. Each backend retains its own page ownership lock.
  The odd sequence transition uses acquire/release to order subsequent field
  stores after it; the even transition remains release. This replaces duplicate
  publishers whose first transition was release-only. No event handle enters
  either Unix producer; only the Wine wait thread pulses the Win32 event.
- Shared the Wayland connection/protocol eligibility checks used by composition,
  effects, thumbnails, Peek and clock attachment. Clock attachment now returns
  ERROR_NOT_READY for a stopped connection and ERROR_NOT_SUPPORTED for missing
  protocol v14, instead of collapsing both into NOT_SUPPORTED. Effect retry
  semantics remain: missing identity and every X11 error retry; only NOT_READY
  retries after resolving Wayland. Registry gates remain on the PE side; no
  duplicate registry gate readers exist in x11wm.c/waylandwm.c to move.
- Any failed thumbnail submission now invalidates the sent cache. Previously:
  successful A, failed flush of B (possibly already queued), desired A again
  could incorrectly skip A using its old cache. This includes a failed empty
  list on broker loss. Successful tuple/scale duplicate suppression is unchanged.
- Reject broker replies with count above DWM_POLL_MAX_CHANGES before multiplying
  the count or indexing the fixed reply buffer.
- Caller audit: git grep over the complete tracked tree, rg across src/, wine/,
  scripts/ and tests/, and hidden/no-ignore repository source/script searches
  (including ignored build scripts and nested worktrees) found callers only for
  WexCompositionSetEnabled, WexCompositionGetEnabled and WexCompositionSetShadow.
  Retained those three. Removed the six dead PE exports, definitions and public
  prototypes: SetCorner, SetBackdrop, SetClock, WaitFrame, SetThumbnails, SetPeek.
  Both Unixlib tables and enum retain all 87 indices. The dead composition_clock
  slot is an ERROR_NOT_SUPPORTED stub; corner/backdrop/thumbnails/Peek retain
  their internal implementations used directly by the X11 backend, and the
  shared generation wait remains live. No slot is deleted or reused.

### Review findings left open

- Same-backend identity replacement is still not an effect refresh trigger:
  replacing an XID, or replacing a Wayland handle at the same scale, while
  retaining the HWND and broker revision can leave effects unapplied on the new
  surface. HWND reuse can similarly retain old pending attribute fields if it
  happens between polls. A scale/backend discriminator cannot solve this;
  identity/lifetime revision tracking needs an explicit contract and runtime
  evidence. Initial missing identities still get the existing bounded retries;
  identities arriving after exhaustion require another broker change/snapshot.
- X11's running flag is read under queue_mutex and outside locks in wm_loop,
  while stop writes it under x_mutex. end_call also wakes the pipe after
  releasing x_mutex, while stop can close/reset its descriptors. These existing
  lifecycle races are not solved by the new effect dispatch. A consistent
  lifetime/queue/pipe synchronization change needs dedicated stop stress tests.
- WexWmStop still closes the PE event-thread handle after its five-second wait
  even on timeout. A blocked callback can outlive stop and overlap a restart.
  This pre-existing lifecycle contract needs a separate coordinated fix;
  bridge/clock teardown itself retains resources on a failed join.
- backend_mutex protects selecting the static ops pointer, not the lifetime of
  an in-flight operation. Arbitrary concurrent Start/Stop/Start is not serialized
  for the full backend teardown. Existing shell ordering stops bridge/clock
  first, but direct callers must also serialize WM lifecycle. Do not add backend
  lock holds around waits/reader joins to address this: callbacks must drain.

### Open policy questions

- Should same-backend native-surface replacement be guaranteed to reapply effects
  without broker changes? If so, define a neutral identity revision query before
  extending the fixed pending table. Preserve the historical XID-first effects
  rule unless mixed-session runtime results justify changing it.
- Is handle-first Peek keep-list selection, including its X11 early-stop limit
  in mixed sessions, still intended? It is preserved, not silently normalized.
- A failed final thumbnail/Peek clear cannot prove remote scene cleanup. Confirm
  disconnect cleanup and whether shutdown should retain/retry clear failures.
  The current shell still proceeds to WM teardown after thumbnail clear failure.
- All gates off starts neither bridge nor clock and acquires no X selection.
  Wayland itself still composes, and GetEnabled still describes that backend
  availability; it does not read the shell Composition gate. Keep that existing
  distinction when asserting disabled-session behavior.

### Required acceptance for this follow-up

1. Build the WoW64 pair (i386 and x86_64), plus the configuration without X11
   libraries if supported. Run 32-bit and 64-bit callers of the neutral effects,
   thumbnails, Peek and clock APIs. Check err/scale/retry fields and all retained
   exports. Rebuild import libraries after removing the six dead exports.
2. In fresh X11 and explicitly adopted Wayland sessions, apply effects before
   native identity exists, then publish it within and after the 40-poll budget.
   With no broker changes, exercise X11 -> Wayland and Wayland -> X11, both
   properties present, XID disappearance, and repeated fractional scale changes.
   Expect bounded retries and no repeated writes once identity/scale stabilizes.
   Separately reproduce same-backend replacement to characterize the open defect.
3. Run scripts/compose-probe.c, the material/corner/shadow visual probes,
   scripts/check-wayland-dwm.sh and the thumbnail/iconic visual probes against
   the verified baseline. Capture GDI and final-screen pixels; compare tint,
   inactive regions, signed/extreme bounds, glass defaults, square corners and
   source/destination rounding at fractional scales. Exercise Peek keep caps
   (8 X11/64 Wayland), mixed identities and minimized iconic pictures.
4. Send thumbnail A successfully; inject a flush error submitting B, then
   immediately restore A. Verify A is sent again. Repeat with B empty during
   broker loss, both scales changing without changing rounded rectangles, and
   failed clears during repeated shell stop/start.
5. Run dwm_clock/DwmFlush/timing probes while repeatedly attaching/detaching and
   stopping the shell under concurrent waits and frame events. Disconnect the
   Wayland compositor between eligibility and attach and while frames arrive.
   Expect NOT_READY for disconnection, NOT_SUPPORTED for protocol <14, successful
   NULL detach, COMPOSED cleared on reader exit, and no page writes after detach.
   Inject thread-creation/join/detach failures; verify retained resources can be
   stopped on retry. Check that restart frame counters remain usable.
6. Explicitly set Composition, DwmService, DwmFrameClock, DwmMaterials,
   TaskbarThumbnails, TaskbarPeek and AltTabThumbnails to zero, then enable one
   relevant combination at a time. Composition=0 starts no bridge/clock or X
   redirection; Composition=1/DwmService=0 may run the independent clock;
   Composition=1/DwmFrameClock=0 may run polling. Materials=0/high contrast/
   transparency=0 suppress material policy as before while corners/shadows stay.
   Exercise missing X extensions/occupied selection and Wayland protocol <13/<14.
7. Fault-inject an oversized broker reply count and verify it is rejected without
   indexing changes; then resume a normal snapshot. Stress WM stop during pending
   native events and blocked callbacks to characterize the unresolved races.

Static validation: re-read each edited function, declarations/prototypes and
conditional builds. Both tables are identical to HEAD, with 87 entries in the
same order. Source-derived layouts satisfy all 26 relevant size/offset assertions
with both 4- and 8-byte UINT64 alignment: effect 52, apply 112, X11 thumb 56,
thumbnail params 8344, Peek params 2120, clock params 24, wait params 16 bytes.
No native pointer/HWND was added to a Unix record. git diff --check passes.
These checks do not substitute for compilation or runtime evidence.

Tooling notes: shell-tool Get-Content, pwd/Get-Item/git status, and the memory rg
commands returned exit 0 with empty output; direct Node filesystem reads and
structured child_process.execFile worked. The initial AGENTS.md discovery hit
"rg: ./unsloth-tmp: Access is denied. (os error 5)"; git ls-files found no tracked
AGENTS.md. An unrestricted hidden/no-ignore root search exceeded the Node tool's
30-second limit ("js execution timed out; kernel reset, rerun your request");
tracked-tree and source/script-filtered hidden/no-ignore searches completed.
tasklist /FI "IMAGENAME eq rg.exe" /FO CSV failed with "ERROR: Access denied".
The first enum checker reported "Enum mismatch" because index 0 is unix_init
but its implementation is unix_init_impl; recognizing that existing alias made
the full table/enum and layout checks pass. All edits used apply_patch; no C
escapes went through shell editing commands. No commit was made.
An unfiltered rg file inventory exceeded the 16-MiB execFile output buffer after
251,343 lines (ERR_CHILD_PROCESS_STDIO_MAXBUFFER). A further root scan with a
10-MiB per-file cap also timed out at 60 seconds and reset the Node kernel;
neither incomplete scan is used as caller-removal evidence. The complete
tracked-tree and source/script scans above are the evidence for that decision.
A memory read using the repository-relative read helper with an absolute path
failed with ENOENT because it prepended the repository root; direct fs.readFile
with the absolute memory path worked. No VM/session state was changed.

## Slices 4-5 static checkpoint (2026-11-03)

Historical checkpoint: all five slices are now committed and runtime verified,
per the user. Its validation backlog describes the original acceptance gates,
not missing work on the committed baseline. See the follow-up review for new work.

- Added PE-only WEX_WINDOW_THUMB and WexWmApplyThumbnails. The shell fetches
  one broker list and fills HWNDs, flags, opacity and Wine-pixel rectangles once.
  The host resolves every pair on every poll through slice 2's effect_identity
  helper, extended with an explicit backend request. Effects retain AUTO's
  XID-first identity; lists request their session backend. Explicit tags, list
  order, omission of unresolved pairs and the 80-record limit remain.
  It uses existing wm_backend_thumbnails dispatch and X11 ops over compose.c;
  no HWND enters a Unix record. Legacy handle and Composition exports retain
  their exact bodies and existing callers remain supported.
- X11 submits every poll for compose-side duplicate suppression. Wayland converts
  destination coordinates nearest/ties away using the destination scale, and
  source bounds outward using the source scale, with unchanged signed/saturating
  arithmetic. Its success-only sent-list comparison includes both scales even
  when rounded coordinates do not change. NULL/0 forces an empty submission and
  invalidates the cache even if the clear fails; an X11 submission also
  invalidates the Wayland cache. Calls are serialized by the shell bridge.
- Bridge start/stop reset the broker revision/list and pending effects; start and
  stop explicitly clear thumbnails on either backend. A failed empty submission
  is returned by the host; bridge teardown still proceeds to WM teardown, as the
  old X11 teardown did. Runtime failure/disconnection coverage is required to
  validate scene cleanup. Failed thread creation closes its stop event. A
  timed-out bridge join preserves handles and aborts shell teardown for retry.
- WexWmPeekWindow(target, picture) moves identity and keep collection into the PE
  host. The shell still produces the iconic bitmap/popup and keeps its
  TaskbarPeek/TaskbarThumbnails gates. Target selection retains the picture first,
  then the non-iconic target; X11 retains its missing-picture-XID fallback while
  Wayland waits for a supplied picture's handle. Only same-process visible
  top-level windows enter the ordered keep lists, capped at 8 X11 or 64 Wayland.
  The old collector's handle-first preference and stop after 8 X11-only windows
  remain, even in mixed Wayland sessions; both identities use the shared helper.
  NULL/NULL ends Peek. The path uses existing wm_backend_peek records/ops.
- dwm_composition_start now owns the Composition=1 eligibility check,
  explicit-Wayland detection, optional X11 enablement and bridge/clock startup.
  desktop.c installs its existing shadow hook only when that function returns
  X11 eligibility. No X redirection is acquired on an adopted Wayland backend.
  DwmService still gates only polling and DwmFrameClock independently gates the
  clock; material, high-contrast, transparency and AltTabThumbnails gates are
  unchanged. Removed bridge_wayland, the Wayland start wrapper and the shell
  thumbnail/Peek identity helpers. Effect scale refresh now uses the nonzero
  applied scale returned by the neutral effect API to identify scaled windows.
- Clock attach/wait/detach now use WexWmSetClock/WexWmWaitFrame. Producer
  ownership, 500-ms waits, Win32 event pulses, Unix generation wait and
  wake-on-disable are unchanged. Detach must succeed and the wait thread must
  finish before unmap/handle release. Failed detach or a timed-out join retains
  resources and aborts shell/WM teardown for a later stop retry. Wait-thread
  creation failure uses the same detach-first cleanup.
- Both Unixlib tables remain identical at 87 entries, unchanged from HEAD,
  including their append-only indices. Unix records, layout assertions, dispatch
  and backend implementations are untouched. New HWND input is a PE-only record
  with native-sized HWNDs and fixed-width UINT32/INT32 scalar/array fields; the
  host copies it into existing 56-byte X11 or 104-byte Wayland records. The
  borrowed X11 pointer is zero-extended into the existing UINT64 slot and
  consumed synchronously. No new Unix call or WoW64 thunk is necessary.

Static verification: re-read edited code, checked declarations/spec signatures
and all changed-symbol callers, and grepped every WexComposition caller in src/
and wine/ before migration. Compatibility bodies remain unchanged; effects
only add AUTO to the shared identity call, retaining its prior resolution order.
The moved coordinate helper is identical to its former shell body. Checked both
87-entry Unix tables and unchanged transport/backends,
fixed-width copy fields and bounds, success-only cache writes, gate locations,
clear/reset ordering and detach-before-unmap control flow. git diff --check
passes. No stock Wine files or new source files changed; no commit.

What remains for slices 4-5: build both WoW64 architectures and run the 32-bit
probe; compare exact thumbnail/Peek records and fresh-prefix visible pixels on
X11 and Wayland. Exercise empty/unresolved/late/destroyed identities, both scale
changes (including unchanged rounded rectangles), both-property mixed identities,
keep ordering/caps/omissions, minimized/iconic pictures and failed flush/resends.
Verify broker disappearance, failed clears, repeated start/stop, thread-creation
failure, all gates off/on, X selection/extension failures and protocol <13/<14.
Re-run dwm_clock/DwmFlush/timing checks, concurrent waits and shutdown/detach
failure/retry before accepting parity. No runtime result for these edits is claimed.

Tooling this session: Get-Content CLAUDE.md, Get-Location, cmd /c type CLAUDE.md,
and the memory rg command through the shell tool returned exit 0 with empty
output. Node dynamic filesystem imports and structured child_process.execFile
calls worked. Repository-wide rg --files encountered "unsloth-tmp: Access is
denied (os error 5)"; targeted source searches worked. An initial caller search
included nonexistent include instead of src/include; the corrected targeted
search worked. Searching wine/include/stdint.h reported that file absent; the
standard header is included by the new host code. The first documentation edit
failed to parse with "[9:17-9:33]: Expected ')'" before editing any file; passing
the section as a JSON-serialized string fixed the nested template-literal issue.
A documentation replacement then failed with "missing helper; limits remain
X11 8 and Wayland 64. X11 uses UINT64 target" because the match omitted the word
"identity"; no file was written, and the corrected exact match succeeded.
A later documentation cleanup also failed before editing with
"[1:362-1:378]: Expected ')'" from nested template literals; serializing all
replacement pairs outside the Node tool call fixed it.
A compose_peek caller search failed with "rg: regex parse error" and
"error: unclosed group": JavaScript consumed the regex parenthesis escape.
Repeating the lookup with rg -F and literal compose_peek( succeeded.
No C escapes were passed through shell tools, and no VM/build/runtime was started.

## Slices 4-5 argument contracts

For each resolved pair in broker order, the logical thumbnail tuple is
(destination identity, source identity, flags, opacity, destination L/T/R/B,
source L/T/R/B). Flags remain VISIBLE=0x1 and HAS_SOURCE=0x4; opacity and all
rectangle fields are retained even when their flags are inactive. Missing or
nonmatching identities omit the pair; capacity remains 80.

- X11: compose_thumb = { UINT64 dest_window, UINT64 src, UINT32 flags,
  UINT32 opacity, INT32 dest[4], INT32 source[4] }, 56 bytes. Both rectangles
  remain Wine pixels, copied without scaling.
- Wayland: WEX_WM_THUMB = { char dest[32], char src[32], DWORD flags,
  DWORD opacity, INT dest_rect[4], INT source_rect[4] }, 104 bytes. Each
  destination coordinate uses Q(value, destination_scale, nearest); source
  L/T use floor and R/B use ceil with source_scale. The two scales are part of
  sent-list comparison even if the wire tuple is unchanged. Compare these
  transformed tuples with the old Wayland path, rather than comparing
  surface-unit numbers directly with X11 Wine-pixel numbers.
- Peek: (target identity, ordered keep identities, count). Keep only visible
  top-level windows owned by GetCurrentProcessId(), resolving through the same
  identity helper; limits remain X11 8 and Wayland 64, including the historical
  handle-first omission and X11-count early stop in mixed sessions. X11 uses
  UINT64 target
  and UINT64 keep[count]; Wayland uses a NUL-terminated 32-byte target handle
  and packed keep[count][32]. End is (0, NULL, 0) on X11 and an empty target
  plus zero-length keep on Wayland. Picture/target selection and iconic
  fallback are described above. Effects use XID-first AUTO identity; thumbnail
  and Peek targets request their session backend. Keep collection requests
  Wayland first and then X11, preserving its former mixed-session behavior.
- Clock: (page address, event handle), each transported in a UINT64 slot;
  detach is (0, 0). Wait is (generation in/out, timeout_ms=500). The Wayland
  producer ignores the event handle; only the Wine wait thread pulses it.

## Slices 2-3 static checkpoint (2026-11-03)

Historical checkpoint; the user-provided runtime baseline above supersedes its
outstanding slice-2/3 validation status, and slices 4-5 supersede its next steps.

- `WEX_WINDOW_EFFECT` is a 52-byte policy record. `effect_identity` in the PE
  `wexhost.c` resolves an XID first, then the atom-backed Wayland handle, without
  requiring a foreign-window inventory entry. The 112-byte Unix record has an
  explicit backend tag, UINT64 XID and separate handle storage. It never carries
  an HWND. USER geometry calls remain on the PE side; only the Wayland region is
  offset there, before signed surface-coordinate conversion in the Unix adapter.
- Both backends supply `apply_effects`. The separate, legacy `effects` slot still
  means the handle-based `WexWmSetEffects` ABI and intentionally stays NULL on X11.
  Wayland's neutral adapter delegates to that unchanged v13 implementation. The
  X11 adapter delegates to the unchanged corner/backdrop implementations, retaining
  two lock/flush boundaries, corner-first writes, first-error precedence, skipped
  backdrop writes with materials off, and no additional shadow write.
- `apply_window` computes policy and calls only `WexWmApplyEffects`; the result
  carries retry intent and applied scale. Missing identity remains retryable,
  every X11 error retries, and only Wayland NOT_READY retries. The existing 40-poll
  limit, destroyed-window handling and `bridge_wayland` scale-refresh condition
  remain. Effect refresh uses the neutral `WexWmGetEffectScale` query; thumbnail
  identity/scale helpers remain in the shell for slice 4.
  Malformed atom names that narrow to short C strings retain the legacy PE-side
  handle-length rejection before checking Unix readiness.
- X11 `thumbnails` and `peek` ops adapt tagged records to the unchanged composition
  calls. `WexCompositionSetThumbnails/SetPeek` now enter those ops through explicit
  X11 routing, independently of WM selection, preserving the former mixed-session
  behavior. Their UINT64 pointer slots borrow the original PE arrays synchronously;
  no list is copied, reordered or clamped at the new boundary. The old Wayland
  record prefixes, handle APIs and selected-backend dispatch remain unchanged;
  calls to those handle APIs on an X11 backend still return NOT_SUPPORTED. X11's
  8-window and Wayland's 64-window Peek limits and the 80-thumbnail capacity stay.
- Both backends supply clock ops and share `wm_wait_frame`, which explicitly
  translates to the existing generation/condition wait record. New
  `WexWmSetClock/WexWmWaitFrame` exports use the selected backend and do not acquire
  the dispatch mutex during backend calls/waits. A selected X11 clock does not
  fall back to Wayland. Existing `WexCompositionSetClock/GetEnabled/WaitFrame`
  semantics and fallback behavior are unchanged; shell clock callers still use
  them. The page producer, Win32 event-pulse thread and detach-before-unmap order
  are unchanged. New clock callers must detach successfully before stopping the WM.
- No startup, Composition, DwmService, DwmMaterials, high-contrast or transparency
  gate changed. In particular, this API never enables X composition. `desktop.c`
  still starts the bridge only after the Composition gate/enablement succeeds.
- All original 82 enum/table entries retain their indices. Both native and WoW64
  tables append exactly `wm_apply_effects`, `wm_backend_thumbnails`,
  `wm_backend_peek`, `wm_clock`, `wm_clock_wait`, reaching 87 entries. Size/offset
  assertions cover the new effects/clock records and appended pointer slots;
  explicit padding keeps 32-bit and 64-bit record sizes equal.

Static verification: re-read every diff and grepped touched symbols; compared both
table prefixes and appended orders/counts against HEAD, resolved all PE
`WEXHOST_CALL` names against the enum, and checked both 22-field backend
initializers. Source comparison against HEAD found identical pending-effect policy
(apart from its public type/names and material bit), scale and signed rounding
arithmetic, material-gate body, Wayland protocol request implementations, and X11
legacy corner/backdrop/thumbnail/Peek/clock bodies. `git diff --check` passed.
Source-derived layout calculations matched all 26 size/offset assertions with
both 4-byte and 8-byte UINT64 alignment; offsets and record sizes were identical.
These are source checks, not compiler or runtime proof.

What remains: compile both WoW64 architectures and run the 32-bit probe; compare
the argument contracts below, including inactive region coordinates at the Unix
boundary, and retry outcomes on missing/destroyed identities, mixed sessions,
disabled composition, missing X extensions, old Wayland protocol and fractional
scale changes. Run fresh-prefix X11/Wayland visual checks before accepting parity.
Then implement slice 4's HWND-based thumbnail/Peek APIs and explicit cache/reset
lifecycle work, and slice 5's shell clock migration/startup centralization with
concurrent wait/shutdown tests. No cache/reset, shadow-ownership or startup-policy
changes were folded into slices 2-3. `wine/` and `docs/STOCK_CHANGES.md` are untouched;
there was no commit. Existing untracked workspace files were left alone.

Tooling failures retained for continuation: shell-tool commands `Get-Content
CLAUDE.md`, `rg -n "DWM|dwm|unification"
C:/Users/Owner/.codex/memories/MEMORY.md`, and `git status --short` returned success
with empty output in this session. Reading files with Node `fs.readFile` and
running `git`/`rg` through structured `child_process.spawnSync` returned real output.
The Node REPL command `var fs = require('fs'); var root =
'D:/DeepThought/Projects/WineExplorer'; console.log(fs.readFileSync(root +
'/CLAUDE.md','utf8'));` failed with `require is not defined`; dynamic
`await import('node:fs/promises')` worked. The patch-script invocation
`console.log(run([process.execPath, 'dwm_slice_patch.cjs']));` failed with
`process is not defined` before executing the script; recreating the written script
and running `run(['node', 'dwm_slice_patch.cjs'])` succeeded. The temporary script
was removed. No VM, build or runtime session was started.
The first layout-check script rejected `keep[64][32]` because its declaration
parser handled only one array dimension; accepting all dimensions and multiplying
their extents fixed the checker, and both alignment runs then passed.

## Follow-up lifecycle and identity fixes (2026-11-04)

- `dwm_bridge.c` now caches the exact `(HWND, identity)` pair: the identity is
  the current XID or the complete 32-character Wayland handle. The broker HWND
  remains a full 32-bit value, so Wine's upper generation bits are not lost.
  Every retry poll compares identity, not only scale, and reapplies effects when
  an X window or Wayland surface is replaced. Native-surface replacement is
  therefore intentionally refreshed through the same path.
- `wm.c` now owns one STOPPED/STARTING/RUNNING/STOPPING state machine and
  condition variable for both backends. Start waits for STOPPING; stop owns the
  transition and never races a backend start. X11 and Wayland each wake their
  reader before joining; their wake descriptors are closed only after join.
  `WexWmStop` retains the PE event-thread handle if its 5-second wait expires
  and reports `ERROR_TIMEOUT` through `GetLastError`; a later stop can join it,
  and start cannot replace it prematurely.
- Mixed-session Peek keep-list collection retains the historical handle-first
  and X11 early-stop behavior. Failed remote thumbnail/scene clears receive one
  bounded retry during bridge reset; a persistent failure is not spun on.

Runtime scenarios required before acceptance:

1. Start and stop X11 concurrently from multiple callers, then repeat rapid
   stop/start; verify no stale thread, closed-pipe poll, X error, or leaked
   thread handle.
2. Repeat the same concurrency and rapid restart test with Wayland; verify the
   reader exits immediately on its wake pipe and the next start gets a fresh
   connection.
3. Keep one HWND alive while replacing its XID, then its Wayland handle/native
   surface; without a new broker attribute revision, verify corner, backdrop,
   tint, region, and shadow are reapplied to the replacement.
4. Destroy and rapidly recreate a window in the same USER slot; verify the
   differing full-generation HWND does not inherit the old applied state.
5. Exercise mixed X11/Wayland sessions and both iconic-picture and target Peek;
   verify the historical keep-list selection and limits remain unchanged.
6. Force the remote scene clear to fail once and then succeed, and fail twice;
   verify exactly one retry, clean shutdown, and no unbounded retry loop.
7. Let the PE event callback block past five seconds during stop; verify stop
   retains the handle, a later stop joins it, and start refuses replacement
   until that join completes.

## Slice 1 exact argument contracts

Let `T = p->tint` (ABGR), `R` be the four signed 16-bit halves of `p->rgn[0..1]`
in left/top/right/bottom order, and `M = materials_on`. Select kind **once**:
Mica if type 2/4 or (mica_effect and type !=1); else Acrylic if type 3; else use
the masked blur mode (none, acrylic, gradient, otherwise blur). Tint/region validity
comes from blur flags only in that last, non-none branch. In particular, mica_effect
can override type 3, and type 1 suppresses that Mica opt-in, not explicit blur.

| Kind | X11 base | Wayland backdrop |
| --- | ---: | ---: |
| none | 0 | 0 |
| blur | 2 | 1 |
| acrylic | 4 | 2 |
| mica | 1 | 3 |
| gradient | 8 | 4 |

The original X11 export argument tuples, preserved at the backend boundary, are:

- `WexCompositionSetCorner(XID, p->radius)` including `WEX_CORNER_SQUARE=0x100`.
- Only if M: `WexCompositionSetBackdrop(XID, base | (dark ? 0x100 : 0) |
  (tinted ? 0x200 : 0) | (has_region ? 0x400 : 0), T, &R)`.
  Dark is retained even for none; T and all four R values are passed even when
  their flags are absent. No extra shadow call. Preserve first-error precedence.
  Transport detail: the old `WexCompositionSetBackdrop` PE wrapper copied R only
  with the region flag; actual `compose_backdrop` coordinates were otherwise all
  zero. `x11_apply_effects` preserves that region zeroing and retains T even when
  the tint flag is absent.

Wayland stays `WexWmSetEffects(handle, Q(radius), backdrop, flags, tint, &S)`:

- Pre-scale radius is 0 for explicit square; otherwise p->radius if nonzero;
  otherwise 6 for frame==2, 8 for another nonzero frame, 0 for no frame.
- `scale = property_120 * 1000 / 120`, default 1000 when that result is zero;
  Q rounds `value * 1000 / scale` nearest, ties away from zero, clamped to INT32.
- Backdrop is the table's value if M, else 0. Flags: shadow `0x1` iff frame!=0,
  dark `0x2` iff M and dark and backdrop!=0, tint `0x4` iff M and tinted, region
  `0x8` iff M and has_region and both GetWindowRect/GetClientRect succeed.
- Tint is `(T & 0xff00ff00) | ((T >> 16) & 0xff) | ((T & 0xff) << 16)` only
  when the tint flag is active; otherwise exactly T, including Mica/type-3 Acrylic.
- S is zero unless the region flag is active. When active, offset R by
  `(client screen origin - window screen origin)` before scaling. Preserve the
  existing unchecked ClientToScreen result. Scale left/top with floor and
  right/bottom with ceil, using signed 64-bit arithmetic and INT32 saturation.
- The call still occurs with M off (corner/shadow survive); only ERROR_NOT_READY
  retries, and all other results record the applied scale.

Compare old/new tuples with conflicting attributes, every masked blur value,
all tint/region/dark combinations, signed region extremes, API lookup failures,
frame 0/1/2/other, square/default/explicit radii, and fractional scales. Also compare
call absence, ordering and retry results, not just the numeric backdrop enum.

## Runtime verification (2026-11-04, WSL `wexbuild`, build `/root/WE-prefetch/build/wow64`)

Lifecycle scenarios from the section above, exercised at runtime on X11
(`/root/p4-visual.sh`) and Wayland (`/root/p5-wayland.sh`). All three PASS.

### A1 — HWND slot reuse: no effect leak onto the recreated window (PASS)

Probe `scripts/dwm-hwnd-reuse-visual-probe.c` (built with both
`x86_64-w64-mingw32-gcc` and `i686-w64-mingw32-gcc`, `.exe` copied to `/root`):
creates a window, applies `DwmSetWindowAttribute` corner + backdrop, destroys it,
then rapidly recreates a window in the same USER slot and checks the new window's
pixels. Command (X11, 64-bit then 32-bit):

```
PROBE=dwm-hwnd-reuse-visual-probe COMP=1 FX=1 MAT=1 TB=1 AT=1 \
    bash /root/p4-visual.sh on
```

Evidence: the recreated window's corner/backdrop pixels match the fresh (no-effect)
reference, not the destroyed window's applied state — the differing full-generation
HWND does not inherit the old applied state. Screenshots `a1-a-run1.png` (64-bit),
`a1-b-run2.png`, `a1-b-run3-32bit.png` (32-bit) show the recreated window without the
previous window's corner/backdrop. PASS for both arches.

### A2 — shell restart: bridge + frame clock return, dwm_clock passes, no stale procs (PASS)

Script `.tmp-g4-unit/a2.sh`: Xvfb `:77`, three `wineserver -k` restarts of the
`/desktop` shell with `WINEDEBUG=trace+wexshell`. Evidence per restart (log lines
25/27, 35/37, 45/47):

```
...:trace:wexshell:dwm_bridge_start bridge thread 00000000000000B0
...:trace:wexshell:dwm_clock_start frame clock on, period 166666
```

Both lines appear exactly 3× (once per restart); zero `err:wexshell`/`err:wexhost`
lines. The `dwm_clock` test still passes after the restarts:

```
MOD=dwmapi TESTS=dwm_clock FX=1 bash /root/p6-test.sh
```

x86_64: `frames 57 -> 83, refresh period 166666`; i386: `frames 126 -> 152`; both
`47 tests executed (0 marked as todo, 0 as flaky, 0 failures), 0 skipped`. No stale
`wineserver`/`explorer.exe`/`Xvfb` remain after the run. PASS.

### A3 — every gate off: shell starts, no err: from wexshell/wexhost (PASS)

Script `.tmp-g4-unit/a3.sh`: prefix with **no** `Composition`/`DwmService`/
`DwmFrameClock` keys at all. Evidence: the shell starts (taskbar window
`1024x30+0+738`; Start button pixel `(10,750)=srgb(95,92,92)`), there are **no**
`bridge thread`/`frame clock on` lines (neither the bridge nor the clock starts),
and zero `err:wexshell`/`err:wexhost` lines (only benign `err:ole`/`err:setupapi`).
Screenshot `a3-off.png`. PASS.
