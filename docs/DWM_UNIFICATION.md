<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->

# Shell DWM backend unification

## Chosen end state

The shell computes one neutral effect description from broker state and submits it
through one backend-dispatched `WexWm*` API. Keep X11 drawing in
`src/dlls/wexhost/compose.c` and Wayland drawing in the existing `wexcomp` scene.
No new compositor, broker transport, or material policy is needed.

Use a new, explicitly named `WexWmApplyEffects(HWND, const WEX_WINDOW_EFFECT *)`
(proposed, not present today), rather than changing the ABI of the handle-based
`WexWmSetEffects`. The public description carries kind none/blur/acrylic/mica/gradient,
ABGR tint plus validity, client-space region plus validity, dark, corner preference
(including default versus explicit square), glass default radius, and shadow intent.
Coordinates remain Wine pixels until the backend adapter converts them. Carry a
separate material-application bit: gate-off currently means skip X11 backdrop writes,
but send no Wayland backdrop; a kind of none alone cannot express both operations.

One PE-side helper in wexhost resolves HWND identity using the selected backend:
X window ID or 32-digit wex handle, plus the scale and client-to-window offset needed
by Wayland. USER/window-property calls stay on the PE side; Unix backends receive
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
  `x11wm.c` leaves those ops NULL; its composition calls already serialize
  `compose_corner/backdrop/shadow/thumbnails/peek` with `begin_call/end_call`.
  Do not hold the dispatch mutex while taking backend locks or waiting for frames.
- Clock backend ops and `WexWm*` clock exports are **not found**. Despite their names,
  `WexCompositionSetClock/GetEnabled` already have Wayland fallbacks in `x11wm.c`.
  `composition_clock_wait` in `compose.c` is a shared generation/condition wait;
  Wayland frame events call `compose_clock_wake`. The shell's Wine thread pulses
  the Win32 event; the Unix producer must not touch Win32 events.
- Thumbnail paths fetch one broker list, resolve identities on every poll, and omit
  unresolved pairs. X11 sends each poll (`compose_thumbnails` suppresses duplicates);
  Wayland caches successfully sent records **and both scales**. Dest coordinates
  round nearest; source bounds round outward. Keep 80-record capacity.
- Peek lives in `taskband_preview.c`, outside the bridge: `start_peek/end_peek` and
  keep-window collection branch too. Preserve iconic-picture fallback, target
  selection, and the different keep limits (X11 8, Wayland 64).
- Current bridge stop clears only X11 thumbnails. Wayland clear/reset of cached
  thumbnails, `bridge_wayland`, and restart state needs an explicit later lifecycle
  change and tests; it is not silently corrected in slice 1.

## Ordered slices

Each slice leaves existing exports/callers working and can be built and tested
independently before proceeding. Only slice 1 is implemented here; this work was
static, with no compiler, WSL, or runtime validation.

1. **Share effect policy locally (implemented).** Add `effect_kind`, `window_effect`
   and `pending_effect` in `dwm_bridge.c`. Compute once, preserve raw tint/region,
   corner sentinel and glass defaults, and translate through the existing adapters.
   Check the argument contracts below against the previous code, then build both
   WoW64 architectures and compare effect traces/pixels when a build is available.
   Risk: precedence, inactive fields, dark-with-none, or premature scale conversion.
2. **Add the neutral host contract and X11 ops.** Add the proposed API, fixed-size
   transport, identity/conversion helper, and X11 effect op above `compose.c`;
   adapt Wayland to the same neutral input. Retain legacy entry points and callers.
   X11 must preserve corner-first writes, material gating and first-error precedence;
   do not add a new shadow write until existing X11 shadow ownership is reconciled
   with `desktop.c`'s hook and compose's glass-frame policy. Verify old/new tuples,
   missing identities, disabled composition, extension failures, and protocol <13.
   Risk: ABI layout, nested locks, HWND/XID/handle confusion, changed retry errors.
3. **Switch bridge effects to the neutral API.** Move conversion/identity knowledge
   out of `apply_window/apply_wayland`; retain retry timing and scale refresh through
   a neutral host revision/scale query. Retire the local adapters only after parity.
   Compare all contracts below, gate-off behavior, delayed X windows/handles, destroyed
   windows, fractional scale changes and broker snapshots/restarts. Risk: lost refresh
   or accidentally changing the historical XID-first dispatch in mixed sessions;
   explicitly validate mixed-session policy before selecting solely by backend.
4. **Unify thumbnails and peek.** Add HWND-based neutral list/peek APIs using the
   same helper, implement X11 backend ops, and migrate bridge plus preview callers.
   Preserve per-backend keep limits, list order, omissions, return codes, geometry
   rounding, and success-only cache updates. Clear lists and caches on stop/restart
   in a separately verified lifecycle change within this slice. Test empty lists,
   late identities, source/destination scale changes, minimization/iconic Peek,
   broker disappearance and repeated start/stop. Risk: stale scenes and handle reuse.
5. **Unify clock and startup last.** Add backend clock ops and proposed
   `WexWmSetClock/WexWmWaitFrame`; keep the existing shared wait primitive and
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

X11 calls stay exactly:

- `WexCompositionSetCorner(XID, p->radius)` including `WEX_CORNER_SQUARE=0x100`.
- Only if M: `WexCompositionSetBackdrop(XID, base | (dark ? 0x100 : 0) |
  (tinted ? 0x200 : 0) | (has_region ? 0x400 : 0), T, &R)`.
  Dark is retained even for none; T and all four R values are passed even when
  their flags are absent. No extra shadow call. Preserve first-error precedence.

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
