# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline shader/texture *consumer* evidence checker for the original WebGL fixture.

This validates two independent pieces of externally captured evidence that a real
browser actually consumed the original shaders and 2x2 texture from
``scripts/g4-shader-probe/scene.js``:

1. The ``WEXSHADER v1`` title record (a strict JSON document emitted by scene.js),
   and
2. A presented front-window snapshot (PIL image or path) showing the marker, page
   background, and the four-quadrant texture in BOTH WebGL canvases plus the DOM
   reference.

It captures nothing itself and fabricates no readiness: it only inspects inputs
supplied by the caller (the eventual parent/Luna browser adapter after R6). The two
verdicts are kept distinct; combined acceptance requires both, and neither one alone
certifies GPU-process isolation, the sandbox, or a hardware backend. Offline-only:
no VM, SSH, Wine, or browser execution happens here.
"""
import json
import math
import re

# --- WEXSHADER v1 record protocol (see scene.js + CHROMIUM_WEBGL_SHADER_FIXTURE_20261025.md) ---
TITLE_PREFIX = 'WEXSHADER '
_TITLE_SUFFIX = ' - Chromium'  # browsers may append the app name to the tab title

_EXPECTED_APIS = ('webgl', 'webgl2')
_TOP_KEYS_PASS = {'v', 'nonce', 'kind', 'passed', 'apis', 'frozenAtMs'}
_API_KEYS = {'compileVs', 'compileFsSolid', 'compileFsTex', 'linkSolid', 'linkTex',
             'solidMatches', 'textureMatches', 'corners', 'firstMismatch', 'error',
             'lost', 'renderer'}
_BOOL_FLAGS = ('compileVs', 'compileFsSolid', 'compileFsTex', 'linkSolid', 'linkTex')
# OpenGL lower-left quadrant colors (RGBA), exactly as reported by scene.js.
_EXPECTED_CORNERS = {'ll': [255, 0, 0, 255], 'lr': [0, 255, 0, 255],
                     'ul': [0, 0, 255, 255], 'ur': [255, 255, 0, 255]}

# Bounded inputs: reject absurdly large strings / deep nesting up front so a hostile or
# corrupt record cannot exhaust memory or the C stack before schema checks run.
_MAX_TITLE_LEN = 65536        # captured title string (WEXSHADER <json>)
_MAX_RENDERER_LEN = 2048      # per-API renderer diagnostic string
_MAX_DEPTH = 64               # maximum JSON nesting depth


def _strict_pairs(items):
    value = {}
    for key, val in items:
        if key in value:
            raise ValueError('Duplicate WEXSHADER key: ' + repr(key))
        value[key] = val
    return value


def _reject_constant(_name):
    raise ValueError('Nonfinite WEXSHADER value')


def _strict_json(text):
    # No duplicate keys, no NaN/Infinity tokens; a bounded finite sweep catches overflow floats.
    value = json.loads(text, object_pairs_hook=_strict_pairs, parse_constant=_reject_constant)
    _assert_finite(value)
    return value


def _assert_finite(value, depth=0):
    if depth > _MAX_DEPTH:
        raise ValueError('WEXSHADER record exceeds maximum nesting depth')
    if isinstance(value, dict):
        for item in value.values():
            _assert_finite(item, depth + 1)
    elif isinstance(value, list):
        for item in value:
            _assert_finite(item, depth + 1)
    elif isinstance(value, float) and not math.isfinite(value):
        raise ValueError('Nonfinite WEXSHADER number')


def _split_title(title):
    if type(title) is not str or not title.startswith(TITLE_PREFIX):
        raise ValueError('Title missing WEXSHADER prefix')
    body = title[len(TITLE_PREFIX):]
    if body.endswith(_TITLE_SUFFIX):
        body = body[:-len(_TITLE_SUFFIX)]
    return body


def _check_api_record(rec):
    if type(rec) is not dict:
        return 'API record is not a JSON object'
    if set(rec) != _API_KEYS:
        return 'unexpected API keys: ' + repr(sorted(set(rec) ^ _API_KEYS))
    for flag in _BOOL_FLAGS:
        if rec[flag] is not True:
            return flag + ' must be true'
    if type(rec['solidMatches']) is not int or rec['solidMatches'] != 64:
        return 'solidMatches must be 64'
    if type(rec['textureMatches']) is not int or rec['textureMatches'] != 64:
        return 'textureMatches must be 64'
    if rec['firstMismatch'] is not None:
        return 'firstMismatch must be null'
    if type(rec['error']) is not int or rec['error'] != 0:
        return 'GL error must be 0'
    if rec['lost'] is not False:
        return 'context lost must be false'
    renderer = rec['renderer']
    if type(renderer) is not str or not renderer or renderer in ('null', 'undefined'):
        return 'renderer must be a non-empty string (diagnostic only)'
    if len(renderer) > _MAX_RENDERER_LEN:
        return 'renderer exceeds %d-char quota' % _MAX_RENDERER_LEN
    corners = rec['corners']
    if type(corners) is not dict or set(corners) != {'ll', 'lr', 'ul', 'ur'}:
        return 'corners must have exactly ll/lr/ul/ur'
    for name in ('ll', 'lr', 'ul', 'ur'):
        corner = corners[name]
        if (type(corner) is not list or len(corner) != 4
                or any(type(c) is not int for c in corner) or corner != _EXPECTED_CORNERS[name]):
            return 'corner ' + name + ' mismatch: ' + repr(corner)
    return None


def _check_record_schema(record, expected_nonce):
    if type(record) is not dict:
        return 'record is not a JSON object'
    if set(record) != _TOP_KEYS_PASS:
        return 'unexpected top-level keys: ' + repr(sorted(set(record) ^ _TOP_KEYS_PASS))
    if type(record['v']) is not int or record['v'] != 1:
        return 'protocol v must be 1'
    if type(record['nonce']) is not str or record['nonce'] != expected_nonce:
        return 'nonce mismatch (stale or wrong-owner record)'
    if record['kind'] != 'ready':
        return "kind must be 'ready'"
    if record['passed'] is not True:
        return 'record passed flag is not true'
    frozen = record['frozenAtMs']
    if type(frozen) is bool or type(frozen) not in (int, float) or not math.isfinite(frozen):
        return 'frozenAtMs must be a finite number'
    apis = record['apis']
    if type(apis) is not dict or set(apis) != set(_EXPECTED_APIS):
        return 'apis must have exactly webgl and webgl2'
    for api in _EXPECTED_APIS:
        reason = _check_api_record(apis[api])
        if reason is not None:
            return api + ': ' + reason
    return None


def validate_shader_record(record_or_title, expected_nonce):
    """Validate a WEXSHADER v1 record. Accepts the captured title string or an
    already-parsed record dict. Returns ``api_verdict`` with status pass/fail and the
    preserved source record (and title when given) as primary failure evidence."""
    if type(expected_nonce) is not str or re.fullmatch('[0-9a-f]{32}', expected_nonce) is None:
        return {'status': 'fail', 'reason': 'expected nonce must be 32 lowercase hex',
                'record': None, 'title': None}
    title = None
    try:
        if isinstance(record_or_title, dict):
            record = record_or_title
            _assert_finite(record)
        else:
            title = record_or_title
            if type(title) is not str:
                raise ValueError('title must be a string')
            if len(title) > _MAX_TITLE_LEN:
                raise ValueError('title exceeds %d-char quota' % _MAX_TITLE_LEN)
            record = _strict_json(_split_title(title))
    except (ValueError, RecursionError) as error:
        return {'status': 'fail', 'reason': str(error), 'record': None, 'title': title}
    reason = _check_record_schema(record, expected_nonce)
    if reason is None:
        return {'status': 'pass', 'reason': None, 'record': record, 'title': title}
    return {'status': 'fail', 'reason': reason, 'record': record, 'title': title}


# --- Presented front-window pixel inspection (PIL/Pillow; lazy import) ---
MARKER_RGB = (33, 150, 243)          # rgb(33,150,243), #marker at client (32,32) size 48x48
BACKGROUND_RGB = (240, 244, 248)     # page background rgb(240,244,248)
# Screen/DOM orientation of the four texture quadrants in each presented 64x64 region.
# Authority: the CSS #reference grid (q-ul blue, q-ur yellow, q-ll red, q-lr green), which
# mirrors scene.js's OpenGL lower-left texture as displayed (GL upper row appears on top).
_QUAD_RGB = {'ul': (0, 0, 255), 'ur': (255, 255, 0), 'll': (255, 0, 0), 'lr': (0, 255, 0)}

# Lab-default client-relative CSS-px geometry at 96 DPI / scale 1, derived from the fixed
# layout in index.html (see report). The real adapter should prefer explicit per-canvas
# bounds; these are the documented fallback so a no-bounds scale-1 capture is checkable.
_LAB_DEFAULT_RECTS = {
    'marker': [32, 32, 48, 48],
    'webgl': [33, 125, 64, 64],
    'webgl2': [123, 125, 64, 64],
    'reference': [213, 125, 64, 64],
    'background': [300, 48, 16, 16],
}
_PIXEL_REGIONS = ('marker', 'webgl', 'webgl2', 'reference', 'background')


def lab_default_geometry(scale=1):
    """Return the documented lab-default client-relative CSS-px rects (scale 1 only)."""
    if scale != 1:
        raise ValueError('lab default geometry is defined for scale 1 only')
    return {name: list(rect) for name, rect in _LAB_DEFAULT_RECTS.items()}


def _load_rgb(image):
    from PIL import Image
    if isinstance(image, Image.Image):
        return image.convert('RGB'), None
    try:
        with Image.open(image) as opened:
            return opened.convert('RGB'), None
    except Exception as error:  # noqa: BLE001 - surface any open/decode failure as a verdict
        return None, 'cannot open front-window image: ' + repr(error)


def _first_mismatch(px, x0, y0, x1, y1, rgb):
    for y in range(y0, y1):
        for x in range(x0, x1):
            actual = px[x, y]
            if tuple(actual) != rgb:
                return (x, y, tuple(actual))
    return None


def _quadrant_interiors(x0, y0, w, h):
    """Interior 25%-75% of each of the four quadrants of a region, avoiding borders/dividers."""
    qw, qh = w // 2, h // 2
    ix0, ix1 = int(round(qw * 0.25)), int(round(qw * 0.75))
    iy0, iy1 = int(round(qh * 0.25)), int(round(qh * 0.75))

    def block(col, row):
        return (x0 + col * qw + ix0, y0 + row * qh + iy0,
                x0 + col * qw + ix1, y0 + row * qh + iy1)

    return {'ul': block(0, 0), 'ur': block(1, 0), 'll': block(0, 1), 'lr': block(1, 1)}


def _solid_interior(x0, y0, w, h, inset=2):
    """Central sub-rect of a solid region, inset from its edges."""
    return (x0 + inset, y0 + inset, x0 + w - inset, y0 + h - inset)


def _check_solid(px, label, x0, y0, x1, y1, rgb):
    """Return a fail reason if the solid sample block is empty or any pixel mismatches.

    An empty block (count 0) is a hard fail, never a silent pass: ``_first_mismatch``
    returns None for an empty range, which would otherwise read as 'no mismatch'."""
    if not (x0 < x1 and y0 < y1):
        return '%s sample block is empty' % label
    hit = _first_mismatch(px, x0, y0, x1, y1, rgb)
    if hit is not None:
        hx, hy, actual = hit
        return '%s interior pixel (%d,%d) is %r, expected %r' % (label, hx, hy, actual, rgb)
    return None


def _check_quadrants(px, label, x0, y0, x1, y1):
    """Return a fail reason if any quadrant sample block is empty or mismatches."""
    interiors = _quadrant_interiors(x0, y0, x1 - x0, y1 - y0)
    for quad in ('ul', 'ur', 'll', 'lr'):
        expected = _QUAD_RGB[quad]
        bx0, by0, bx1, by1 = interiors[quad]
        if not (bx0 < bx1 and by0 < by1):
            return '%s %s quadrant sample block is empty' % (label, quad)
        hit = _first_mismatch(px, bx0, by0, bx1, by1, expected)
        if hit is not None:
            hx, hy, actual = hit
            return ('%s %s quadrant interior pixel (%d,%d) is %r, expected %r'
                    % (label, quad, hx, hy, actual, expected))
    return None


def _is_number(value):
    return type(value) is not bool and type(value) in (int, float)


def _resolve_rects(geometry, img_w, img_h):
    """Map geometry to image-pixel rects. Returns ``(rects, None)`` on success or
    ``(None, (status, reason))`` on failure. Only scale 1 is proven: unprovable geometry
    (nonfinite coords, unsupported scale, incomplete explicit bounds) is ``inconclusive``,
    while a caller-supplied layout that is out of bounds, the wrong size, or aliased/
    overlapping is a concrete ``fail``."""
    client = geometry.get('client') if isinstance(geometry, dict) else None
    scale = geometry.get('scale') if isinstance(geometry, dict) else None
    bounds = (geometry.get('bounds')
              if isinstance(geometry, dict) and isinstance(geometry.get('bounds'), dict)
              else {})

    # client / scale must be present, numeric, and finite BEFORE any arithmetic or int()
    # conversion (a NaN/Infinity coordinate must yield a verdict, never an exception).
    if type(client) is not list or len(client) != 2 or any(not _is_number(c) for c in client):
        return None, ('inconclusive', 'geometry.client must be a [x, y] pair of numbers')
    if not _is_number(scale):
        return None, ('inconclusive', 'geometry.scale must be a number')
    if any(not math.isfinite(v) for v in (client[0], client[1], scale)):
        return None, ('inconclusive', 'nonfinite geometry coordinate (NaN/Infinity)')
    if scale != 1:
        return None, ('inconclusive',
                      'unsupported scale %r; only the lab default 96DPI/scale 1 is proven '
                      '(other scales are TODO)' % (scale,))

    # Explicit bounds must be a COMPLETE mapping of all five regions: never silently fall
    # back to defaults for a subset. An empty/absent bounds dict uses the lab-default rects.
    if bounds and set(bounds) != set(_PIXEL_REGIONS):
        missing = sorted(set(_PIXEL_REGIONS) - set(bounds))
        return None, ('inconclusive',
                      'incomplete explicit bounds; missing %r (all five regions required)'
                      % (missing,))

    rects = {}
    for name in _PIXEL_REGIONS:
        css = bounds.get(name) if name in bounds else _LAB_DEFAULT_RECTS[name]
        if type(css) is not list or len(css) != 4 or any(not _is_number(v) for v in css):
            return None, ('inconclusive', 'geometry rect %r must be a [l, t, w, h] of numbers' % (name,))
        if any(not math.isfinite(v) for v in css):
            return None, ('inconclusive', 'nonfinite geometry coordinate in rect %r' % (name,))
        l, t, w, h = css
        x0, y0 = client[0] + l * scale, client[1] + t * scale
        x1, y1 = client[0] + (l + w) * scale, client[1] + (t + h) * scale
        if any(v != int(v) for v in (x0, y0, x1, y1)):
            return None, ('inconclusive', 'geometry rect %r is not integral at the supported scale' % (name,))
        x0, y0, x1, y1 = int(x0), int(y0), int(x1), int(y1)
        if not (0 <= x0 < x1 <= img_w and 0 <= y0 < y1 <= img_h):
            return None, ('fail', 'crop out of bounds for %r: (%d,%d)-(%d,%d) vs image %dx%d'
                          % (name, x0, y0, x1, y1, img_w, img_h))
        rects[name] = (x0, y0, x1, y1)

    # Supported canvas geometry: the texture regions must be exactly 64x64 and the solid
    # regions large enough to sample; arbitrary caller dimensions are not valid layout.
    for name in ('webgl', 'webgl2', 'reference'):
        x0, y0, x1, y1 = rects[name]
        if (x1 - x0) != 64 or (y1 - y0) != 64:
            return None, ('fail', '%s must be exactly 64x64 at scale 1, got %dx%d'
                          % (name, x1 - x0, y1 - y0))
    x0, y0, x1, y1 = rects['marker']
    if (x1 - x0) < 16 or (y1 - y0) < 16:
        return None, ('fail', 'marker must be at least 16x16, got %dx%d' % (x1 - x0, y1 - y0))
    x0, y0, x1, y1 = rects['background']
    if (x1 - x0) < 8 or (y1 - y0) < 8:
        return None, ('fail', 'background must be at least 8x8, got %dx%d' % (x1 - x0, y1 - y0))

    # Regions must be strictly pairwise disjoint: aliased/overlapping bounds would let one
    # region's pixels stand in for another's and produce a false pass.
    names = list(_PIXEL_REGIONS)
    for i in range(len(names)):
        ax0, ay0, ax1, ay1 = rects[names[i]]
        for j in range(i + 1, len(names)):
            bx0, by0, bx1, by1 = rects[names[j]]
            if ax0 < bx1 and bx0 < ax1 and ay0 < by1 and by0 < ay1:
                return None, ('fail', 'overlapping/aliased regions %r and %r' % (names[i], names[j]))

    return rects, None


def validate_presented_pixels(image, geometry):
    """Inspect the presented front-window snapshot. Returns ``pixel_verdict`` with status
    pass / fail / inconclusive and a specific reason. Both WebGL canvases AND the DOM
    reference must show the correct four-quadrant texture; the DOM reference alone never
    passes, and an unprovable scale/geometry is inconclusive (never success)."""
    rgb_image, open_error = _load_rgb(image)
    if rgb_image is None:
        return {'status': 'inconclusive', 'reason': open_error}
    img_w, img_h = rgb_image.size
    rects, err = _resolve_rects(geometry, img_w, img_h)
    if rects is None:
        status, reason = err
        return {'status': status, 'reason': reason}

    px = rgb_image.load()
    # Marker and background: solid interior must be exactly the expected color (non-empty).
    for name, expected in (('marker', MARKER_RGB), ('background', BACKGROUND_RGB)):
        x0, y0, x1, y1 = rects[name]
        sx0, sy0, sx1, sy1 = _solid_interior(x0, y0, x1 - x0, y1 - y0)
        reason = _check_solid(px, name, sx0, sy0, sx1, sy1, expected)
        if reason is not None:
            return {'status': 'fail', 'reason': reason}

    # Both WebGL canvases and the DOM reference must show the correct quadrant texture.
    for name in ('webgl', 'webgl2', 'reference'):
        x0, y0, x1, y1 = rects[name]
        reason = _check_quadrants(px, name, x0, y0, x1, y1)
        if reason is not None:
            return {'status': 'fail', 'reason': reason}
    return {'status': 'pass', 'reason': None}


def validate_shader_consumer_evidence(record_or_title, expected_nonce, image, geometry):
    """Combined offline consumer-evidence verdict. ``passed`` is True only when BOTH the
    WEXSHADER record and the presented pixels verify; the primary failure (record first)
    is surfaced in ``error`` with its specific reason preserved."""
    api_verdict = validate_shader_record(record_or_title, expected_nonce)
    pixel_verdict = validate_presented_pixels(image, geometry)
    passed = api_verdict['status'] == 'pass' and pixel_verdict['status'] == 'pass'
    error = None
    if api_verdict['status'] != 'pass':
        error = 'API record: ' + str(api_verdict['reason'])
    elif pixel_verdict['status'] != 'pass':
        error = 'presented pixels (%s): %s' % (pixel_verdict['status'], pixel_verdict['reason'])
    return {'passed': passed, 'api_verdict': api_verdict,
            'pixel_verdict': pixel_verdict, 'error': error}
