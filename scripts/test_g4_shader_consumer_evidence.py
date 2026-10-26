# SPDX-License-Identifier: GPL-2.0-or-later
"""Focused offline tests for g4_shader_consumer_evidence.

Synthetic front-window images are generated in-memory with PIL from INDEPENDENT
literal constants (their own rects and colors), so a checker bug is not its own
oracle. The quadrant orientation authority is the CSS #reference grid in
g4-shader-probe/index.html (q-ul blue, q-ur yellow, q-ll red, q-lr green), which is
what scene.js's OpenGL lower-left texture looks like once displayed. These are unit
controls only: they prove the checker logic, NOT a real browser/GPU run.
"""
import json
import unittest

from PIL import Image

import g4_shader_consumer_evidence as ev

NONCE = 'a1b2c3d4e5f60718293a4b5c6d7e8f90'


# --- record builders -----------------------------------------------------------
def _api_record(**over):
    rec = {
        'compileVs': True, 'compileFsSolid': True, 'compileFsTex': True,
        'linkSolid': True, 'linkTex': True,
        'solidMatches': 64, 'textureMatches': 64,
        'corners': {'ll': [255, 0, 0, 255], 'lr': [0, 255, 0, 255],
                    'ul': [0, 0, 255, 255], 'ur': [255, 255, 0, 255]},
        'firstMismatch': None, 'error': 0, 'lost': False,
        'renderer': 'ANGLE (VirGL Renderer)',
    }
    rec.update(over)
    return rec


def _record(nonce=NONCE, **over):
    rec = {
        'v': 1, 'nonce': nonce, 'kind': 'ready', 'passed': True,
        'apis': {'webgl': _api_record(), 'webgl2': _api_record()},
        'frozenAtMs': 123.456,
    }
    rec.update(over)
    return rec


def _title(rec):
    return 'WEXSHADER ' + json.dumps(rec)


# --- synthetic front-window image builders (independent constants) -------------
CLIENT = [0, 0]
SCALE = 1
RECTS = {
    'marker': [20, 20, 48, 48],
    'webgl': [100, 100, 64, 64],
    'webgl2': [190, 100, 64, 64],
    'reference': [280, 100, 64, 64],
    'background': [360, 250, 16, 16],
}
IMG_W, IMG_H = 420, 300
MARKER = (33, 150, 243)
BG = (240, 244, 248)
# Screen/DOM orientation of the four quadrants (authority: CSS #reference grid).
QUADS = {'ul': (0, 0, 255), 'ur': (255, 255, 0), 'll': (255, 0, 0), 'lr': (0, 255, 0)}

# Documented lab-default client-relative CSS-px geometry at scale 1 (see report).
LAB_RECTS = {
    'marker': [32, 32, 48, 48],
    'webgl': [33, 125, 64, 64],
    'webgl2': [123, 125, 64, 64],
    'reference': [213, 125, 64, 64],
    'background': [300, 48, 16, 16],
}


def _paint_quads(img, rect, flip_vertical=False):
    left, top, width, height = rect
    px = img.load()
    qw, qh = width // 2, height // 2
    if not flip_vertical:
        top_row, bot_row = (QUADS['ul'], QUADS['ur']), (QUADS['ll'], QUADS['lr'])
    else:
        top_row, bot_row = (QUADS['ll'], QUADS['lr']), (QUADS['ul'], QUADS['ur'])
    for y in range(top, top + qh):
        for x in range(left, left + qw):
            px[x, y] = top_row[0]
        for x in range(left + qw, left + width):
            px[x, y] = top_row[1]
    for y in range(top + qh, top + height):
        for x in range(left, left + qw):
            px[x, y] = bot_row[0]
        for x in range(left + qw, left + width):
            px[x, y] = bot_row[1]


def _front(include=('marker', 'webgl', 'webgl2', 'reference'), flip=None, size=(IMG_W, IMG_H)):
    img = Image.new('RGB', size, BG)
    px = img.load()
    if 'marker' in include:
        left, top, width, height = RECTS['marker']
        for y in range(top, top + height):
            for x in range(left, left + width):
                px[x, y] = MARKER
    for name in ('webgl', 'webgl2', 'reference'):
        if name in include:
            _paint_quads(img, RECTS[name], flip_vertical=(flip == name))
    return img


def _geometry(**over):
    geo = {'client': CLIENT, 'scale': SCALE, 'bounds': {k: list(v) for k, v in RECTS.items()}}
    geo.update(over)
    return geo


class RecordValidationTests(unittest.TestCase):
    def test_canonical_title_passes(self):
        verdict = ev.validate_shader_record(_title(_record()), NONCE)
        self.assertEqual(verdict['status'], 'pass')
        self.assertIsNone(verdict['reason'])
        self.assertEqual(verdict['record']['nonce'], NONCE)

    def test_dict_input_passes(self):
        verdict = ev.validate_shader_record(_record(), NONCE)
        self.assertEqual(verdict['status'], 'pass')

    def test_title_suffix_chromium_accepted(self):
        verdict = ev.validate_shader_record(_title(_record()) + ' - Chromium', NONCE)
        self.assertEqual(verdict['status'], 'pass')

    def test_wrong_nonce_fails(self):
        other = 'f' * 32
        verdict = ev.validate_shader_record(_title(_record(nonce=other)), NONCE)
        self._fail(verdict, 'nonce')

    def test_invalid_expected_nonce_fails(self):
        verdict = ev.validate_shader_record(_title(_record()), NONCE.upper())
        self._fail(verdict, '32 lowercase hex')

    def test_missing_prefix_fails(self):
        verdict = ev.validate_shader_record('WEXEDGE ' + json.dumps(_record()), NONCE)
        self._fail(verdict, 'prefix')

    def test_malformed_json_fails(self):
        verdict = ev.validate_shader_record('WEXSHADER not-json-at-all', NONCE)
        self._fail(verdict, '')  # any parse failure is a fail

    def test_truncated_json_fails(self):
        title = 'WEXSHADER {"v": 1, "nonce": "' + NONCE + '"'
        verdict = ev.validate_shader_record(title, NONCE)
        self._fail(verdict, '')

    def test_duplicate_key_fails(self):
        title = 'WEXSHADER {"v":1,"v":1}'
        verdict = ev.validate_shader_record(title, NONCE)
        self._fail(verdict, 'Duplicate')

    def test_nonfinite_value_fails(self):
        title = ('WEXSHADER {"v":1,"nonce":"%s","kind":"ready","passed":true,'
                 '"apis":{"webgl":{},"webgl2":{}},"frozenAtMs":Infinity}' % NONCE)
        verdict = ev.validate_shader_record(title, NONCE)
        self._fail(verdict, 'Nonfinite')

    def test_context_loss_fails(self):
        rec = _record()
        rec['apis']['webgl']['lost'] = True
        self._fail(ev.validate_shader_record(_title(rec), NONCE), 'context lost')

    def test_glerror_fails(self):
        rec = _record()
        rec['apis']['webgl2']['error'] = 1285
        self._fail(ev.validate_shader_record(_title(rec), NONCE), 'GL error')

    def test_failed_compile_fails(self):
        rec = _record()
        rec['apis']['webgl']['compileFsTex'] = False
        self._fail(ev.validate_shader_record(_title(rec), NONCE), 'compileFsTex')

    def test_solid_mismatch_count_fails(self):
        rec = _record()
        rec['apis']['webgl']['solidMatches'] = 63
        self._fail(ev.validate_shader_record(_title(rec), NONCE), 'solidMatches')

    def test_texture_mismatch_count_fails(self):
        rec = _record()
        rec['apis']['webgl2']['textureMatches'] = 40
        self._fail(ev.validate_shader_record(_title(rec), NONCE), 'textureMatches')

    def test_corner_mismatch_fails(self):
        rec = _record()
        rec['apis']['webgl']['corners']['ul'] = [0, 0, 0, 255]
        self._fail(ev.validate_shader_record(_title(rec), NONCE), 'corner ul')

    def test_first_mismatch_nonnull_fails(self):
        rec = _record()
        rec['apis']['webgl']['firstMismatch'] = {'draw': 'texture', 'x': 2, 'y': 2,
                                                 'expected': [255, 0, 0, 255], 'actual': [0, 0, 0, 255]}
        self._fail(ev.validate_shader_record(_title(rec), NONCE), 'firstMismatch')

    def test_renderer_empty_or_null_fails(self):
        for bad in ('', 'null', 'undefined'):
            rec = _record()
            rec['apis']['webgl']['renderer'] = bad
            self._fail(ev.validate_shader_record(_title(rec), NONCE), 'renderer')

    def test_missing_api_fails(self):
        rec = _record()
        del rec['apis']['webgl2']
        self._fail(ev.validate_shader_record(_title(rec), NONCE), 'webgl and webgl2')

    def test_extra_top_level_key_fails(self):
        rec = _record()
        rec['error'] = 'should not be present on a ready record'
        self._fail(ev.validate_shader_record(_title(rec), NONCE), 'top-level keys')

    def test_passed_false_fails(self):
        rec = _record()
        rec['passed'] = False
        self._fail(ev.validate_shader_record(_title(rec), NONCE), 'passed flag')

    def _fail(self, verdict, needle):
        self.assertEqual(verdict['status'], 'fail')
        self.assertIsInstance(verdict['reason'], str)
        if needle:
            self.assertIn(needle, verdict['reason'])


class PixelValidationTests(unittest.TestCase):
    def test_canonical_front_passes(self):
        verdict = ev.validate_presented_pixels(_front(), _geometry())
        self.assertEqual(verdict['status'], 'pass', verdict.get('reason'))

    def test_marker_only_fails(self):
        verdict = ev.validate_presented_pixels(_front(include=('marker',)), _geometry())
        self._pixel_fail(verdict, 'webgl')

    def test_dom_reference_only_never_passes(self):
        # Marker and the DOM reference are both correct, but BOTH WebGL canvases are blank:
        # sampling only the DOM reference must never pass.
        verdict = ev.validate_presented_pixels(_front(include=('marker', 'reference')), _geometry())
        self._pixel_fail(verdict, 'webgl')

    def test_blank_canvas_fails(self):
        verdict = ev.validate_presented_pixels(
            _front(include=('marker', 'webgl2', 'reference')), _geometry())
        self._pixel_fail(verdict, 'webgl ')

    def test_one_canvas_missing_webgl1_only(self):
        # WebGL1 present; WebGL2 and reference blank.
        verdict = ev.validate_presented_pixels(_front(include=('marker', 'webgl')), _geometry())
        self._pixel_fail(verdict, 'webgl2')

    def test_flipped_quadrants_fails(self):
        verdict = ev.validate_presented_pixels(_front(flip='webgl'), _geometry())
        self._pixel_fail(verdict, 'webgl ul')

    def test_wrong_marker_color_fails(self):
        img = _front()
        px = img.load()
        left, top, width, height = RECTS['marker']
        for y in range(top, top + height):
            for x in range(left, left + width):
                px[x, y] = (10, 20, 30)
        self._pixel_fail(ev.validate_presented_pixels(img, _geometry()), 'marker')

    def test_background_wrong_fails(self):
        img = _front()
        px = img.load()
        left, top, width, height = RECTS['background']
        for y in range(top, top + height):
            for x in range(left, left + width):
                px[x, y] = (200, 10, 10)
        self._pixel_fail(ev.validate_presented_pixels(img, _geometry()), 'background')

    def test_unsupported_scale_inconclusive(self):
        verdict = ev.validate_presented_pixels(_front(), _geometry(scale=2))
        self.assertEqual(verdict['status'], 'inconclusive')
        self.assertIn('scale', verdict['reason'])

    def test_crop_out_of_bounds_fails(self):
        tiny = Image.new('RGB', (50, 50), BG)
        verdict = ev.validate_presented_pixels(tiny, _geometry())
        self.assertEqual(verdict['status'], 'fail')
        self.assertIn('out of bounds', verdict['reason'])

    def test_missing_image_inconclusive(self):
        verdict = ev.validate_presented_pixels('/nonexistent/front.png', _geometry())
        self.assertEqual(verdict['status'], 'inconclusive')

    def _pixel_fail(self, verdict, needle):
        self.assertEqual(verdict['status'], 'fail', verdict.get('reason'))
        self.assertIn(needle, verdict['reason'])


class GeometryTests(unittest.TestCase):
    def test_lab_default_geometry_matches_documented_values(self):
        self.assertEqual(ev.lab_default_geometry(1), LAB_RECTS)

    def test_lab_default_rejects_other_scales(self):
        with self.assertRaises(ValueError):
            ev.lab_default_geometry(2)

    def test_derived_geometry_path_passes_without_bounds(self):
        # Paint the lab-default (scale-1) layout using the independent literal rects, then
        # validate WITHOUT explicit bounds so the checker derives them. Non-circular because
        # test_lab_default_geometry_matches_documented_values pins derivation == literals.
        img = Image.new('RGB', (400, 250), BG)
        px = img.load()
        left, top, width, height = LAB_RECTS['marker']
        for y in range(top, top + height):
            for x in range(left, left + width):
                px[x, y] = MARKER
        for name in ('webgl', 'webgl2', 'reference'):
            _paint_quads(img, LAB_RECTS[name])
        verdict = ev.validate_presented_pixels(img, {'client': [0, 0], 'scale': 1})
        self.assertEqual(verdict['status'], 'pass', verdict.get('reason'))


class CombinedVerdictTests(unittest.TestCase):
    def test_combined_pass(self):
        result = ev.validate_shader_consumer_evidence(_title(_record()), NONCE, _front(), _geometry())
        self.assertTrue(result['passed'])
        self.assertIsNone(result['error'])
        self.assertEqual(result['api_verdict']['status'], 'pass')
        self.assertEqual(result['pixel_verdict']['status'], 'pass')

    def test_combined_api_failure_is_primary(self):
        # Bad nonce, but a perfectly good front: the record failure is surfaced first and the
        # pixel verdict is independently still pass (primary-failure evidence retained).
        result = ev.validate_shader_consumer_evidence(
            _title(_record(nonce='f' * 32)), NONCE, _front(), _geometry())
        self.assertFalse(result['passed'])
        self.assertTrue(result['error'].startswith('API record:'))
        self.assertIn('nonce', result['error'])
        self.assertEqual(result['pixel_verdict']['status'], 'pass')

    def test_combined_pixel_failure(self):
        result = ev.validate_shader_consumer_evidence(
            _title(_record()), NONCE, _front(include=('reference',)), _geometry())
        self.assertFalse(result['passed'])
        self.assertTrue(result['error'].startswith('presented pixels (fail):'))
        self.assertEqual(result['api_verdict']['status'], 'pass')

    def test_combined_both_fail_reports_api_first(self):
        result = ev.validate_shader_consumer_evidence(
            'WEXSHADER not-json', NONCE, _front(include=('marker',)), _geometry())
        self.assertFalse(result['passed'])
        self.assertTrue(result['error'].startswith('API record:'))
        self.assertEqual(result['pixel_verdict']['status'], 'fail')


class ReworkRegressionTests(unittest.TestCase):
    """Regressions for the parent-review false-pass / robustness defects (R1 rework)."""

    def test_overlapping_aliased_bounds_fail(self):
        # webgl/webgl2 bounds copied onto the reference rect: without a disjointness check,
        # sampling the (correct) reference pixels would stand in for both canvases and pass.
        geo = _geometry(bounds={
            'marker': list(RECTS['marker']),
            'webgl': list(RECTS['reference']),
            'webgl2': list(RECTS['reference']),
            'reference': list(RECTS['reference']),
            'background': list(RECTS['background']),
        })
        verdict = ev.validate_presented_pixels(_front(include=('marker', 'reference')), geo)
        self.assertEqual(verdict['status'], 'fail', verdict.get('reason'))
        self.assertIn('overlapping', verdict['reason'])

    def test_blank_image_tiny_bounds_fail(self):
        # All five regions collapsed to 1x1: the texture regions are not 64x64, so this is a
        # supported-geometry fail (not a pass, and not an unprovable-scale inconclusive).
        geo = _geometry(bounds={name: [0, 0, 1, 1] for name in
                                ('marker', 'webgl', 'webgl2', 'reference', 'background')})
        blank = Image.new('RGB', (IMG_W, IMG_H), BG)
        verdict = ev.validate_presented_pixels(blank, geo)
        self.assertEqual(verdict['status'], 'fail', verdict.get('reason'))
        self.assertIn('64x64', verdict['reason'])

    def test_renderer_over_quota_fails(self):
        rec = _record()
        rec['apis']['webgl']['renderer'] = 'x' * 2000000
        verdict = ev.validate_shader_record(rec, NONCE)  # dict path isolates the renderer bound
        self.assertEqual(verdict['status'], 'fail')
        self.assertIn('quota', verdict['reason'])

    def test_title_over_quota_fails(self):
        rec = _record()
        rec['apis']['webgl']['renderer'] = 'x' * 70000  # serialized title exceeds 65536 chars
        verdict = ev.validate_shader_record(_title(rec), NONCE)
        self.assertEqual(verdict['status'], 'fail')
        self.assertIn('quota', verdict['reason'])

    def test_nonfinite_client_returns_verdict(self):
        geo = _geometry(client=[float('nan'), 0])
        verdict = ev.validate_presented_pixels(_front(), geo)  # must not raise
        self.assertIn(verdict['status'], ('fail', 'inconclusive'))
        self.assertIsInstance(verdict['reason'], str)

    def test_partial_explicit_bounds_inconclusive(self):
        partial = {name: list(RECTS[name]) for name in ('marker', 'webgl', 'webgl2', 'reference')}
        geo = _geometry(bounds=partial)  # omits 'background'
        verdict = ev.validate_presented_pixels(_front(), geo)
        self.assertEqual(verdict['status'], 'inconclusive')
        self.assertIn('incomplete explicit bounds', verdict['reason'])

    def test_empty_sample_block_is_not_a_pass(self):
        img = Image.new('RGB', (IMG_W, IMG_H), BG)
        px = img.load()
        # A zero-width solid block must be reported empty, not silently pass.
        self.assertIsNotNone(ev._check_solid(px, 'marker', 5, 5, 5, 9, MARKER))


if __name__ == '__main__':
    unittest.main()
