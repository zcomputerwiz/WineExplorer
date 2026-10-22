# SPDX-License-Identifier: GPL-2.0-or-later
import copy
import importlib.util
import json
from pathlib import Path
import unittest
from unittest import mock
from PIL import Image, ImageDraw
from graphics_frame_pixels import (VISIBLE_PALETTES, recovery_pixels, recovery_page_pixels,
                                   visible_canvas_geometry, visible_canvas_pixels)

spec = importlib.util.spec_from_file_location('visible_consumer', Path(__file__).with_name('check-g4-webview2.py'))
consumer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(consumer)


def observation(stage='pre_fault'):
    phase = 'pre_fault' if stage == 'pre_fault' else 'recovered'
    return {'windowBounds': dict(x=25, y=25, width=740, height=520),
            'clientBounds': dict(x=29, y=79, width=732, height=462),
            'viewport': dict(width=732, height=462), 'marker': dict(x=192, y=177.59375, width=40, height=40),
            'background': [232, 245, 233],
            'visibleCanvases': {api: {'api': api, 'stage': phase,
                'rect': dict(x=x, y=300, width=64, height=64)} for api, x in (('webgl', 360), ('webgl2', 456))},
            'canvasExclusions': {key: dict(x=32, y=y, width=200, height=24)
                                 for key, y in (('h1', 45), ('#counter', 130), ('input', 222),
                                                ('button', 222), ('#scroll-label', 248))}}


def images(stage='pre_fault', scale=1.25):
    meta = observation(stage)
    page = Image.new('RGB', (round(732*scale), round(462*scale)), tuple(meta['background']))
    def paint(image, r, color, offset=(0, 0)):
        x, y = r['x']+offset[0], r['y']+offset[1]
        ImageDraw.Draw(image).rectangle((round(x*scale), round(y*scale),
            round((x+r['width'])*scale)-1, round((y+r['height'])*scale)-1), fill=color)
    paint(page, meta['marker'], (21, 101, 192))
    for api, value in meta['visibleCanvases'].items():
        paint(page, value['rect'], VISIBLE_PALETTES[value['stage']][api])
    front = Image.new('RGB', (round(1280*scale), round(720*scale)), 'black')
    paint(front, meta['windowBounds'], 'white')
    front.paste(page, (round(29*scale), round(79*scale)))
    state = {**meta['windowBounds'], 'client_bounds': meta['clientBounds']}
    return front, page, state, meta


class VisiblePixels(unittest.TestCase):
    def verdicts(self, front, page, state, meta, stage='pre_fault', scale=1.25):
        return (recovery_pixels(front, state, scale, meta['marker'], meta['background'], visible=meta, stage=stage),
                recovery_page_pixels(page, meta['viewport'], scale, meta['marker'], meta['background'],
                                     visible=meta, stage=stage))

    def test_decoded_front_page_fractional_scale_and_caption_origin(self):
        import io
        for stage in ('pre_fault', 'recovered', 'settled_2s', 'settled_4s'):
            front, page, state, meta = images(stage)
            decoded = []
            for image in (front, page):
                data = io.BytesIO(); image.save(data, format='PNG'); data.seek(0)
                decoded.append(Image.open(data))
            for verdict in self.verdicts(*decoded, state, meta, stage):
                self.assertTrue(verdict['passed'])
                for item in verdict['visible_webgl']['canvases'].values():
                    self.assertEqual(item['matched_pixels'], item['sampled_pixels'])
                    self.assertGreaterEqual(item['sampled_pixels'], 64)

    def test_each_missing_black_wrong_swapped_partial_canvas_fails_independently(self):
        for api in ('webgl', 'webgl2'):
            for color in ((0,0,0), (232,245,233), (255,255,255), VISIBLE_PALETTES['pre_fault']['webgl2' if api=='webgl' else 'webgl']):
                front, page, state, meta = images()
                r = meta['visibleCanvases'][api]['rect']
                for image, origin in ((front, (29,79)), (page, (0,0))):
                    ImageDraw.Draw(image).rectangle((round((origin[0]+r['x'])*1.25), round((origin[1]+r['y'])*1.25),
                        round((origin[0]+r['x']+64)*1.25)-1, round((origin[1]+r['y']+64)*1.25)-1), fill=color)
                for verdict in self.verdicts(front, page, state, meta):
                    self.assertFalse(verdict['passed'])
                    self.assertFalse(verdict['visible_webgl']['canvases'][api]['passed'])
            front, page, state, meta = images()
            r = meta['visibleCanvases'][api]['rect']
            page.putpixel((round((r['x']+32)*1.25), round((r['y']+32)*1.25)), (0,0,0))
            self.assertFalse(self.verdicts(front, page, state, meta)[1]['passed'])
            self.assertTrue(self.verdicts(front, page, state, meta)[0]['passed'])

    def test_stale_pre_fault_palette_cannot_pass_claimed_recovered_colors(self):
        front, page, state, _ = images('pre_fault')
        meta = observation('recovered')
        for item in meta['visibleCanvases'].values():
            item['claimedColor'] = [255,0,255]
        for verdict in self.verdicts(front, page, state, meta, 'recovered'):
            self.assertFalse(verdict['passed'])

    def test_invalid_geometry_clipping_overlap_metadata_or_small_regions_fail(self):
        _, page, _, meta = images()
        for key, value in (('x', float('nan')), ('y', float('inf')), ('width', 0), ('width', 15),
                           ('x', -1), ('x', 730), ('y', 460), ('x', 456)):
            bad = copy.deepcopy(meta)
            bad['visibleCanvases']['webgl']['rect'][key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                visible_canvas_pixels(page, bad, 'pre_fault', 1.25)
        bad = copy.deepcopy(meta)
        bad['canvasExclusions']['input'] = bad['visibleCanvases']['webgl']['rect'].copy()
        with self.assertRaises(ValueError): visible_canvas_geometry(bad, 'pre_fault')
        for change in ({'visibleCanvases': {}}, {'canvasExclusions': {}}, {'viewport': None}):
            with self.assertRaises(ValueError):
                visible_canvas_geometry({**meta, **change}, 'pre_fault')
        bad = copy.deepcopy(meta); bad['visibleCanvases']['webgl']['api'] = 'webgl2'
        with self.assertRaises(ValueError): visible_canvas_geometry(bad, 'pre_fault')
        with self.assertRaises(ValueError): visible_canvas_pixels(page, meta, 'pre_fault', .1)
        with self.assertRaises(ValueError): visible_canvas_pixels(page, meta, 'pre_fault', float('nan'))
        with self.assertRaises(ValueError): visible_canvas_pixels(page, meta, 'recovered', 1.25)
        self.assertFalse(visible_canvas_pixels(page, meta, 'pre_fault', 1.25, (32,32))['passed'])
        self.assertFalse(visible_canvas_pixels(page, meta, 'pre_fault', 1)['passed'])


class VisibleIntegration(unittest.TestCase):
    def test_timing_requires_and_matches_canvas_metadata_only_when_enabled(self):
        meta = {**observation(), 'runId': 'a'*32, 'remainingMs': 30000}
        clock = [0.]
        def front(stage, request, timeout, record): return {'pixels': {'passed': False}, 'observed_scale': 1}
        def collector(visible):
            return consumer.PresentationTimingCollector('a'*32, front, lambda *a: {'pixels': {'passed': True}},
                lambda *a: None, now=lambda: clock[0], sleep=lambda n: clock.__setitem__(0, clock[0]+n),
                visible_webgl=visible)
        event = lambda name, obj: '1 ms presentation timing '+name+' '+json.dumps(obj)+'\n'
        missing = {k:v for k,v in meta.items() if k not in ('visibleCanvases','canvasExclusions')}
        collector(False).poll(event('requested', missing))
        with self.assertRaises(ValueError): collector(True).poll(event('requested', missing))
        active = collector(True); text = event('requested', meta); active.poll(text)
        changed = copy.deepcopy(meta); changed['visibleCanvases']['webgl']['rect']['x'] += 1
        with self.assertRaises(ValueError): active.poll(text+event('page capture', changed))

    def test_recovery_visible_ledger_geometry_and_existing_identity_nonce_gates(self):
        identity, nonce = {'pid': 7, 'start_ticks': 8}, 'a'*32
        text, captures, pixels = '', {}, {}
        for stage in consumer.RECOVERY_VISUAL_STAGES:
            f,p,state,meta = images(stage)
            verdicts = VisiblePixels().verdicts(f,p,state,meta,stage)
            pixels[stage] = dict(zip(('front','page'), verdicts))
            captures[stage] = {'sha256':'b'*64, 'bytes':100, 'width':1600, 'height':900,
                'geometry':state, 'client_bounds':meta['clientBounds'], 'compositor_identity':identity}
            for event, value in (
                ('front capture requested', {**meta,'stage':stage,'runId':nonce}),
                ('front capture acknowledged', {'stage':stage,'runId':nonce,'captured':True,'pixelPassed':True}),
                ('page capture', {**meta,'stage':stage,'empty':False,'bytes':100,'size':{'width':915,'height':578}}),
                ('page capture acknowledged', {'stage':stage,'runId':nonce,'captured':True,'pixelPassed':True})):
                text += '1 ms gpu recovery '+event+' '+json.dumps(value)+'\n'
        assess = lambda log=text, after=identity: consumer.recovery_front_capture_result(
            log,captures,nonce,None,identity,after,pixels,visible_webgl=True)
        self.assertTrue(assess()['passed'])
        self.assertFalse(assess(text.replace('"api": "webgl"','"api": "wrong"',1))['passed'])
        self.assertFalse(assess(text.replace(nonce,'c'*32))['passed'])
        self.assertFalse(assess(after={'pid':9,'start_ticks':8})['passed'])
        lines = text.splitlines()
        for index, line in enumerate(lines):
            if 'gpu recovery page capture {' in line:
                prefix, raw = line.split('page capture ', 1)
                changed = json.loads(raw)
                changed['visibleCanvases']['webgl']['rect']['x'] += 1
                lines[index] = prefix + 'page capture ' + json.dumps(changed)
                break
        self.assertFalse(assess('\n'.join(lines))['passed'])
        stripped = []
        for line in text.splitlines():
            prefix, raw = line.split(' {', 1)
            value = json.loads('{'+raw)
            value.pop('visibleCanvases', None); value.pop('canvasExclusions', None)
            stripped.append(prefix+' '+json.dumps(value))
        missing = '\n'.join(stripped)
        self.assertFalse(assess(missing)['passed'])
        self.assertTrue(consumer.recovery_front_capture_result(
            missing,captures,nonce,None,identity,identity,pixels)['passed'])

    def test_cli_rejects_visible_outside_explicit_modes_before_launch(self):
        args=['runner','--wine-build','/absent','--runtime','/absent','--host','/absent',
              '--output-dir','/absent','--electron-app','/absent','--visible-webgl']
        with mock.patch('sys.argv', args), mock.patch.object(consumer.subprocess,'Popen') as launch:
            with self.assertRaises(SystemExit): consumer.main()
            launch.assert_not_called()


if __name__ == '__main__': unittest.main()
