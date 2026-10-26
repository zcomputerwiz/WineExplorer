<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium Shader Consumer Evidence Validation (offline)

**Date:** 2026-10-26
**Branch:** `codex/chromium-shader-consumer-validation`
**Worktree:** `D:/DeepThought/Worktrees/chromium-shader-consumer-validation/WineExplorer`
**Base commit:** `fbbbe032` (parent work order) · **clean HEAD at start:** `fe5952fb`
**Accepted fixture reference:** `docs/CHROMIUM_WEBGL_SHADER_FIXTURE_20261025.md` (`0d96719a`)

**Status: `PREPARED_OFFLINE` — validation logic complete and tested offline. No live run.**
This is complementary, *consumer-evidence* work: it proves that a real browser's
captured title record **and** presented front-window pixels agree with the original
shaders/texture. It does **not** boot a VM, open SSH, run Wine/browser/probe, or add
any startup/token authority. Luna's R6 host/guest preparation and its comparison
fixture are untouched.

---

## 1. Scope and constraints (offline only)

- Zero VM boot, zero SSH, zero Wine/browser/probe execution, zero package install,
  zero runtime/DLL build.
- Disjoint write scope: the accepted fixture files (`scripts/g4-shader-probe/{index.html,scene.js}`,
  `scripts/test_g4_shader_scene.js`, the fixture doc) plus **new**
  `scripts/g4_shader_consumer_evidence.py`, its focused test file, and this report.
- Not modified: R6 native runner/helper/contract, module-view/probe scripts, R5/R6
  adapters, private preparation, launcher/token code, or the existing general HTTP
  server (`g4_edge_http_fixture.py`). The fixture's GL/texture workload in `scene.js`
  is preserved **byte-for-byte** (only a CSS layout rule was added to `index.html`).

## 2. Deliverable 1 — minimal layout polish (deterministic offsets)

`scripts/g4-shader-probe/index.html` gains one rule so the label row has a fixed,
font-independent height. Without it the `<span>` height depends on font metrics and
the canvas Y-offset is not deterministic across renderers:

```css
.card span { display:block; width:64px; height:16px; line-height:16px;
             font-size:12px; margin:0; padding:0; text-align:center }
```

`scene.js` is unchanged. `node scripts/test_g4_shader_scene.js` still passes
**35 negative tests / 28 mutation regressions** (see §7). The original GL and texture
workloads are preserved.

## 3. WEXSHADER v1 record schema (validated)

The captured browser title is `WEXSHADER <json>` (optionally suffixed ` - Chromium`).
`validate_shader_record` enforces, with a strict decoder (no duplicate keys, no
NaN/Infinity, bounded finite sweep):

- Top-level exact key set `{ v, nonce, kind, passed, apis, frozenAtMs }` — a ready
  record has **no** `error` key. `v == 1`, `nonce == expected_nonce` (32 lowercase
  hex), `kind == 'ready'`, `passed is True`, `frozenAtMs` finite number.
- `apis` exact key set `{ webgl, webgl2 }`. Per API, exact key set of the 12 fields
  emitted by `scene.js`; required:
  - `compileVs`, `compileFsSolid`, `compileFsTex`, `linkSolid`, `linkTex` all `True`;
  - `solidMatches == 64`, `textureMatches == 64`;
  - `firstMismatch is None`, `error == 0`, `lost is False`;
  - `renderer` a non-empty string (not `'null'`/`'undefined'`) — **diagnostic only**,
    it does not certify hardware acceleration or sandbox isolation;
  - `corners`: `ll [255,0,0,255]`, `lr [0,255,0,255]`, `ul [0,0,255,255]`,
    `ur [255,255,0,255]` (OpenGL lower-left orientation).

Any non-success / incomplete / malformed record fails with its specific reason and the
source record preserved. A stale or wrong-owner nonce is rejected.

## 4. Presented-pixel geometry and coordinates

The checker inspects a front-window snapshot (PIL image or path) at an explicit
`geometry = { client:[x,y], scale, bounds? }`:

- `client` — the browser **content-area origin in screenshot pixels** (the top-left of
  the page's client area). Bounds are client-relative CSS pixels.
- `scale` — device scale factor. **Only the lab default 96 DPI / `scale == 1` is
  proven**; any other scale returns `inconclusive` (never success) and is a TODO.
- `bounds` — optional per-element rects `[l,t,w,h]`. When present they are authoritative
  (the real adapter should supply real per-canvas capture bounds). When absent, the
  documented lab-default rects (§4.1) are used at scale 1.

Elements checked and expected colors:

| Element | Expected | Source of truth |
|---------|----------|-----------------|
| `marker` | solid `rgb(33,150,243)` | `#marker` CSS (client `left:32 top:32`, 48×48) |
| `background` | solid `rgb(240,244,248)` | page background |
| `webgl`, `webgl2`, `reference` | four quadrants, each 64×64 | texture draw / `#reference` grid |

**Quadrant orientation (screen/DOM coordinates).** The authority is the CSS
`#reference` grid (`q-ul` blue, `q-ur` yellow, `q-ll` red, `q-lr` green), which is how
`scene.js`'s OpenGL lower-left texture appears once displayed (the GL upper row renders
on top):

| region corner | color |
|---------------|-------|
| top-left (`ul`) | blue `[0,0,255]` |
| top-right (`ur`) | yellow `[255,255,0]` |
| bottom-left (`ll`) | red `[255,0,0]` |
| bottom-right (`lr`) | green `[0,255,0]` |

Each quadrant is sampled on its **interior 25%–75%** (the central half of each 32×32
quadrant), avoiding borders, the center divider, and any label text. Every interior
pixel must exactly equal the expected color (lossless, `image-rendering:pixelated`
nearest-neighbor upscale at scale 1). **Both** WebGL canvases (`webgl` and `webgl2`)
**and** the DOM `reference` must match; a front where only the DOM reference is correct
never passes. Blank / stale / missing-one-canvas / flipped-quadrant fronts fail with a
specific region+quadrant+pixel reason.

### 4.1 Lab-default client-relative CSS-px rects (scale 1)

Derived from the fixed `index.html` layout after the §2 polish (panel at
`top:100 left:32`, card gap 24, label row 16px + 8px gap + 1px canvas border):

| element | `[l, t, w, h]` |
|---------|----------------|
| `marker` | `[32, 32, 48, 48]` |
| `webgl` | `[33, 125, 64, 64]` |
| `webgl2` | `[123, 125, 64, 64]` |
| `reference` | `[213, 125, 64, 64]` |
| `background` | `[300, 48, 16, 16]` |

These are a documented fallback so a no-bounds scale-1 capture is checkable. The real
adapter should prefer explicit per-canvas `bounds`; do not rely on guessed desktop
coordinates for anything but this known lab default.

## 5. Public reusable entry point

Module: `scripts/g4_shader_consumer_evidence.py`. Three functions, distinct verdicts:

```python
import g4_shader_consumer_evidence as ev

api_verdict   = ev.validate_shader_record(record_or_title, expected_nonce)
pixel_verdict = ev.validate_presented_pixels(image, geometry)
combined      = ev.validate_shader_consumer_evidence(record_or_title, expected_nonce, image, geometry)
```

- `validate_shader_record(record_or_title, expected_nonce)` → `api_verdict`
  `{ 'status': 'pass'|'fail', 'reason', 'record', 'title' }`. Accepts the captured title
  **string** or an already-parsed record **dict**.
- `validate_presented_pixels(image, geometry)` → `pixel_verdict`
  `{ 'status': 'pass'|'fail'|'inconclusive', 'reason' }`. `image` is a PIL image or a path.
- `validate_shader_consumer_evidence(...)` → `{ 'passed': bool, 'api_verdict',
  'pixel_verdict', 'error' }`. `passed` is True only when **both** verdicts pass; the
  primary failure (record first) is surfaced in `error` with its specific reason preserved.

Verdict semantics: a record that fails to parse or mismatches the schema is `fail`; a
presented-pixel check that finds a concrete mismatch or an out-of-bounds crop is `fail`;
an **unprovable scale or unopenable image** is `inconclusive` — never reported as success.
Neither verdict alone certifies GPU-process isolation, the sandbox, or a hardware backend;
combined acceptance additionally requires external actual backend/ownership proof.

## 6. Serving the fixture (existing server, unchanged)

The two original files are served by the **existing** `g4_edge_http_fixture.FixtureServer`
(no new routes, no server modification). It nonce-scopes the two exact files:

```python
from pathlib import Path
from g4_edge_http_fixture import FixtureServer

nonce = 'a1b2c3d4e5f60718293a4b5c6d7e8f90'          # 32 lowercase hex
server = FixtureServer(Path('scripts/g4-shader-probe/index.html'), nonce)
url = server.start()   # http://127.0.0.1:<port>/<nonce>/index.html#nonce=<nonce>
# ... the browser adapter navigates to `url`; the routes served are:
#     GET /<nonce>/index.html   (text/html)
#     GET /<nonce>/scene.js     (application/javascript)
server.close()
```

`FixtureServer` requires the caller to have already proven private loopback, serves one
HTTP/1.0 worker with bounded sockets, and its receipt is **not** evidence of browser
consumption or renderer readiness — that is exactly what this checker validates from the
captured title + front snapshot.

## 7. Tests and results (offline)

Run in WSL Debian (Python 3.13.5, Pillow 11.1.0) and Windows Node v24.19.0:

```bash
# Python consumer-evidence checker (from scripts/)
python3 -B -m unittest test_g4_shader_consumer_evidence -v
# Accepted fixture suite (from repo root), re-verified after the CSS polish
node --check scripts/g4-shader-probe/scene.js
node scripts/test_g4_shader_scene.js
```

Results:

- `test_g4_shader_consumer_evidence` — **Ran 39 tests … OK** (0 failures / 0 errors /
  0 skips). Coverage: canonical two-canvas success; marker-only; DOM-reference-only
  (never passes); blank canvas; one canvas missing; flipped quadrants; wrong/invalid
  nonce; truncated/malformed/duplicate-key/nonfinite record; context loss / glError /
  failed compile; corner & count mismatches; empty/null renderer; missing API; extra top
  key; unsupported scale (inconclusive); crop out of bounds (fail); unopenable image
  (inconclusive); lab-default geometry values + derived no-bounds path; combined verdict
  with primary-failure evidence retained. Synthetic images are generated in-memory from
  **independent** literal constants (their own rects/colors), so the checker's geometry
  is not its own oracle.
- `node scripts/test_g4_shader_scene.js` — **PASS … 35 negative tests, 28 mutation
  regressions** (unchanged; confirms the CSS polish preserved the fixture).

## 8. Example invocation reading files (no new VM authority)

```python
import json
from pathlib import Path
from PIL import Image
import g4_shader_consumer_evidence as ev

nonce = 'a1b2c3d4e5f60718293a4b5c6d7e8f90'
title = Path('captured/title.txt').read_text().strip()          # "WEXSHADER {...}"
front = Image.open('captured/front.png')                         # presented front window
geometry = {
    'client': [12, 40],            # content-area origin in screenshot pixels
    'scale': 1,                    # lab default 96 DPI
    # optional real per-canvas bounds from the adapter (client-relative CSS px):
    'bounds': {'webgl': [33, 125, 64, 64], 'webgl2': [123, 125, 64, 64],
               'reference': [213, 125, 64, 64]},
}
result = ev.validate_shader_consumer_evidence(title, nonce, front, geometry)
print(json.dumps({'passed': result['passed'], 'error': result['error'],
                  'api': result['api_verdict']['status'],
                  'pixel': result['pixel_verdict']['status']}))
```

This reads already-captured artifacts and validates them; it starts nothing.

## 9. Minimal integration touchpoints for parent / Luna (post-R6)

1. **Serve** the two files via `FixtureServer(Path('scripts/g4-shader-probe/index.html'), nonce)`
   (§6) after proving private loopback; navigate the owned browser to the returned URL.
2. **Capture** the tab title (the `WEXSHADER` record) and one presented front-window
   snapshot, plus the content-area origin (`client`) and `scale`; if available, real
   per-canvas bounds.
3. **Validate** with `ev.validate_shader_consumer_evidence(title, nonce, front, geometry)`;
   gate on `result['passed']` and retain `result['error']` as primary evidence.
4. Keep R6's existing startup fixture for a controlled comparison; this shader workload
   is a later opt-in trial, not a replacement.

## 10. Limitations

- **Synthetic unit controls only.** The Python tests prove the checker logic against
  generated images and records; they are NOT real browser/GPU proof. Real-browser
  validation (actual ANGLE/VirGL rendering, actual presented pixels) remains a future
  **live** observation deferred to the parent's authorized gate.
- **Scale.** Only `scale == 1` (96 DPI lab default) is proven; other scales are
  `inconclusive` (TODO). Per-pixel matching assumes a lossless snapshot of a
  pixelated, scale-1 render; a tolerance option is a documented extension point for
  lossy captures.
- **Not security proof.** No screenshot or record here certifies browser sandboxing,
  GPU-process isolation, or the hardware backend; those need separate external evidence.
