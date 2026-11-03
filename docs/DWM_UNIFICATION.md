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
- Clock backend ops and `WexWmSetClock/WexWmWaitFrame` now exist. Despite their names,
  `WexCompositionSetClock/GetEnabled` already have Wayland fallbacks in `x11wm.c`.
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
independently before proceeding. Slices 1-3 are committed and runtime verified
(user-provided baseline: X11 pixel-identical, Wayland probes, dwm_clock 47/47).
Slices 4-5 below are implemented statically only; their build/runtime acceptance
is outstanding. No compiler or WSL was used for this change.

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

## Slices 4-5 static checkpoint (2026-11-03)

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
