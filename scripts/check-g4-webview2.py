#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline first-failure diagnostic, NOT a Chromium/G4 acceptance certificate.

Supply a lawfully acquired private fixed-version runtime and existing diagnostic
host/loader. No downloading, installation, sandbox bypass or system prefix use.
Run inside a private mount/network namespace if WSLg owns the X socket directory.
"""
import argparse
import fcntl
import hashlib
import importlib.util
import json
import os
import re
import secrets
from pathlib import Path
import select
import shutil
import signal
import subprocess
import threading
import time
import graphics_resources

VISUAL_STAGES = ('ready', 'input', 'scroll', 'resize', 'minimized', 'restored', 'reload', 'resize_back')


def native_scale_state(text, expected):
    rows = re.findall(r'^NATIVE_WINDOW fixed=(\d+) visible=(\d+) x=(-?\d+) y=(-?\d+) width=(\d+) height=(\d+)\s*$', text, re.MULTILINE)
    if (len(rows) != 1 or int(rows[0][0]) != round(expected * 65536)
            or int(rows[0][1]) != 1 or int(rows[0][4]) <= 0 or int(rows[0][5]) <= 0):
        raise RuntimeError('Fixture did not publish the observed native output scale: ' + text)
    return dict(zip(('fixed', 'visible', 'x', 'y', 'width', 'height'), map(int, rows[0])))


def visual_requests(text, captured=False):
    event = 'visual checkpoint captured' if captured else 'visual checkpoint'
    stages = re.findall(r'^\d+ ms ' + event + r' \{"stage":"([a-z_]+)"\}\s*$', text, re.MULTILINE)
    return [stage for stage in stages if stage in VISUAL_STAGES]


def visual_capture_complete(text, captures, error=None):
    return (error is None and set(captures) == set(VISUAL_STAGES)
            and visual_requests(text) == list(VISUAL_STAGES)
            and visual_requests(text, captured=True) == list(VISUAL_STAGES))


def observe_fail_closed(observer, result):
    """Keep background observer failures in the acceptance result."""
    try:
        observer()
    except Exception as error:
        result['observer_error'] = '%s: %s' % (type(error).__name__, error)


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def lifecycle_result(log):
    """Require every named check, not just a fixture's summary boolean."""
    matches = re.findall(r'^\d+ ms lifecycle result (.+)$', log, re.MULTILINE)
    required = {'input', 'scroll', 'resize', 'minimized', 'restored', 'reload', 'resize_back'}
    if len(matches) != 1:
        return {'passed': False, 'error': 'Expected exactly one lifecycle result'}
    try:
        result = json.loads(matches[0])
        checks = result.get('checks', {})
        result['passed'] = (result.get('passed') is True and set(checks) == required
                            and all(value is True for value in checks.values()))
        return result
    except (ValueError, AttributeError, TypeError):
        return {'passed': False, 'error': 'Invalid lifecycle result'}


def graphics_result(log):
    matches = re.findall(r'^\d+ ms graphics capabilities hr=(0x[\da-fA-F]+) (.+)$', log, re.MULTILINE)
    if len(matches) != 1 or matches[0][0].lower() != '0x00000000':
        return {'passed': False, 'error': 'Expected one successful graphics callback'}
    try:
        capabilities = json.loads(matches[0][1])
        contexts = {}
        for name in ('webgl', 'webgl2'):
            item = capabilities[name]
            contexts[name] = {key: item.get(key) for key in
                              ('available', 'lost', 'defaultError', 'defaultPixel', 'framebufferStatus',
                               'textureError', 'readError', 'texturePixel', 'version', 'unmasked')}
            contexts[name]['passed'] = (item.get('available') is True and item.get('lost') is False
                                        and all(item.get(key) == 0 for key in
                                                ('defaultError', 'textureError', 'readError'))
                                        and item.get('framebufferStatus') == 36053
                                        and item.get('defaultPixel') == [255, 0, 0, 255]
                                        and item.get('texturePixel') == [255, 0, 0, 255])
        return {'passed': all(item['passed'] for item in contexts.values()), 'contexts': contexts}
    except (ValueError, KeyError, AttributeError, TypeError):
        return {'passed': False, 'error': 'Invalid graphics capabilities'}


def profile_result(log, expected, token):
    """A caller-supplied fresh nonce must survive a complete process restart."""
    matches = re.findall(r'^\d+ ms profile persistence (.+)$', log, re.MULTILINE)
    if len(matches) != 1:
        return {'passed': False, 'error': 'Expected exactly one profile persistence result'}
    try:
        result = json.loads(matches[0])
        result['passed'] = (result.get('passed') is True and result.get('expected') == expected
                            and result.get('previous') == expected and result.get('written') == token
                            and result.get('flushed') is True)
        return result
    except (ValueError, AttributeError, TypeError):
        return {'passed': False, 'error': 'Invalid profile persistence result'}


def recovery_result(log):
    matches = re.findall(r'^\d+ ms gpu recovery result (.+)$', log, re.MULTILINE)
    if len(matches) != 1:
        return {'passed': False, 'error': 'Expected one GPU recovery result'}
    try:
        result = json.loads(matches[0])
        before, after, fault = (result[key] for key in ('before', 'after', 'fault'))
        # Cross-check the whole run, including exits AFTER the result snapshot.
        child_records = [json.loads(value) for value in re.findall(r'^\d+ ms child process gone (.+)$', log, re.MULTILINE)]
        gpu_exits = [item for item in child_records if item.get('type') == 'GPU']
        faults = [json.loads(value) for value in re.findall(r'^\d+ ms gpu recovery fault (.+)$', log, re.MULTILINE)]
        def pixels(item):
            return (item.get('available') is True and item.get('lost') is False
                    and item.get('defaultError') == 0 and item.get('textureError') == 0
                    and item.get('framebufferStatus') == 36053
                    and item.get('defaultPixel') == [255, 0, 0, 255]
                    and item.get('texturePixel') == [0, 255, 0, 255]
                    and isinstance(item.get('renderer'), str) and bool(item['renderer'])
                    and not re.search('swiftshader|software rasterizer', item['renderer'], re.I))
        checks = {
            'pixels': all(pixels(phase['contexts'][name]) for phase in (before, after) for name in ('webgl', 'webgl2')),
            'same_backend': all(before['contexts'][name]['renderer'] == after['contexts'][name]['renderer']
                                for name in ('webgl', 'webgl2')),
            'features': (before['features'].get('gpu_compositing') == 'enabled'
                         and before['features'] == after['features']),
            'owned_fault': (fault.get('requested') is True and fault['pid'] == before['gpu']['pid']
                            and fault['creationTime'] == before['gpu']['creationTime']
                            and type(fault['pid']) is int and fault['pid'] > 0),
            'new_gpu': (type(after['gpu']['pid']) is int and after['gpu']['pid'] > 0
                        and after['gpu']['pid'] != before['gpu']['pid']
                        and after['gpu']['creationTime'] >= before['gpu']['creationTime']),
            'same_renderer': (type(before['rendererPid']) is int and before['rendererPid'] > 0
                              and before['rendererPid'] == after['rendererPid'] and result['rendererExits'] == []),
            'context_loss': all(after['oldLost'][name]['events'] >= 1 for name in ('webgl', 'webgl2')),
            'frames': (type(after['frames']) is int and type(after['laterFrames']) is int
                       and after['laterFrames'] > after['frames']),
            'one_expected_exit': (len(result['exits']) == 1 and result['exits'][0]['type'] == 'GPU'
                                  and result['exits'][0]['reason'] in ('killed', 'crashed')
                                  and type(result['exits'][0]['exitCode']) is int and result['exits'][0]['exitCode'] != 0),
            'whole_run_exits': (gpu_exits == result['exits'] and faults == [fault]
                                and not re.search(r'^\d+ ms renderer gone ', log, re.MULTILINE)),
        }
        result['checks'] = checks
        result['passed'] = result.get('passed') is True and all(checks.values())
        return result
    except (ValueError, AttributeError, TypeError, KeyError):
        return {'passed': False, 'error': 'Invalid/incomplete GPU recovery result'}


def recovery_acceptance(animation, recovery):
    """Allow exactly the injected fault signature, never a blanket crash ignore."""
    errors = animation.get('errors', {})
    return (recovery.get('passed') is True and animation.get('state') == 'animation_progressing'
            and animation.get('navigation_succeeded') is True and animation.get('chromium_log_present') is True
            and errors.get('gpu_process_crash') == 1
            and set(errors) == {'gl_allowlist', 'warp_initialization', 'd3d11_output',
                                'directcomposition_output', 'gpu_process_crash', 'fatal_log',
                                'skia_surface', 'shared_context'}
            and all(value == 0 for name, value in errors.items() if name != 'gpu_process_crash'))


def restart_state(previous, build, renderer, payload_hashes, stack_hashes, icd_hash=None, gl_driver='software'):
    """Only reuse a successful, stopped fresh run's exact private state."""
    previous = previous.resolve(strict=True)
    prior = json.loads((previous / 'results.json').read_text())
    if (prior.get('diagnostic_exit') != 0 or 'cleanup_error' in prior
            or prior.get('cleanup', {}).get('remaining_servers') != []
            or prior.get('consumer') != 'Electron original fixture'
            or prior.get('wine_build') != str(build) or prior.get('renderer') != renderer
            or prior.get('gl_driver', 'software') != gl_driver
            or (prior.get('desktop') or {}).get('mode', 'none') != 'none'
            or prior.get('sha256') != payload_hashes or prior.get('stack_sha256') != stack_hashes
            or prior.get('vulkan_icd_sha256') != icd_hash
            or prior.get('state_root') != str(previous) or prior.get('restart_from') is not None
            or prior.get('profile_diagnostic', {}).get('passed') is not True
            or not re.fullmatch('[0-9a-f]{32}', prior.get('profile_token', ''))):
        raise ValueError('Restart requires a successful cleaned fresh proof run with identical binaries/backend')
    for name in ('prefix', 'profile'):
        path = previous / name
        if not path.is_dir() or path.resolve(strict=True) != path:
            raise ValueError('Restart state must be the original private nonsymlink directory')
    return previous, prior['profile_token']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--wine-build', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--host', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--trace-exceptions', action='store_true',
                        help='Trace exception addresses; does not change browser/security flags')
    parser.add_argument('--graphics-diagnostic', action='store_true',
                        help='Opt-in WebGL capability/pixel readback script in the original host')
    parser.add_argument('--trace-graphics', action='store_true',
                        help='Trace builtin D3D11/DXGI calls; retained logs may be large')
    parser.add_argument('--seconds', type=int, default=25, choices=range(5, 301), metavar='5..300')
    parser.add_argument('--electron-app', type=Path,
                        help='Separate original Electron fixture directory; use electron.exe as --host')
    parser.add_argument('--freeze-scene', action='store_true',
                        help='Electron fixture only: stop DOM changes at x=160 after eight seconds')
    parser.add_argument('--lifecycle', action='store_true',
                        help='Electron fixture only: injected input, wheel scroll, resize, minimize/restore and reload')
    parser.add_argument('--lifecycle-visuals', action='store_true',
                        help='Hold eight lifecycle stages until actual owned-display capture is acknowledged')
    parser.add_argument('--resize-border', action='store_true',
                        help='Native scale1 only: freeze DOM before final growth and inspect held frame rims over four seconds')
    parser.add_argument('--trace-native', action='store_true',
                        help='Owned native control only: retain Wayland wire and Wine driver lifecycle logs')
    parser.add_argument('--profile-proof', action='store_true',
                        help='Electron only: store/flush a fresh persistent Chromium cookie')
    parser.add_argument('--restart-from', type=Path,
                        help='Restart a successful fresh --profile-proof run using its owned prefix/profile')
    parser.add_argument('--gpu-recovery', action='store_true',
                        help='Separate Electron owned-GPU termination/context-recreation diagnostic')
    parser.add_argument('--renderer', choices=('gl', 'vulkan'), default='gl')
    parser.add_argument('--resource-profile', action='store_true',
                        help='Read-only one-second owned-process CPU/RSS samples; not GPU telemetry')
    parser.add_argument('--gl-driver', choices=('software', 'd3d12', 'svga', 'virgl'), default='software',
                        help='Explicit software/WSL D3D12/VM SVGA driver, independently verified')
    parser.add_argument('--desktop', choices=('none', 'openbox', 'composited'), default='none')
    parser.add_argument('--wexcomp-bin', type=Path, help='Explicit pinned compositor: owned headless native Wayland control')
    parser.add_argument('--wexcomp-config', type=Path, help='Project Wayland config directory for --wexcomp-bin')
    parser.add_argument('--compositor-renderer', choices=('pixman', 'gles2'), default='pixman')
    parser.add_argument('--output-scale', type=float, choices=(1.0, 1.25, 1.5, 2.0),
                        help='Explicit initial scale of the owned native headless output (requires wlr-randr)')
    parser.add_argument('--transition-scale', type=float, choices=(1.0, 1.25, 1.5, 2.0),
                        help='Change the owned output at the held input stage; require matching published HWND scale')
    parser.add_argument('--vulkan-icd', type=Path,
                        help='Required explicit software/hardware ICD for a Vulkan control')
    args = parser.parse_args()
    if args.resize_border:
        # Fail before launching Wine rather than killing the observer halfway
        # through the fixture's frozen-frame acknowledgement handshake.
        from graphics_frame_pixels import frame_pixels
    if bool(args.wexcomp_bin) != bool(args.wexcomp_config):
        parser.error('--wexcomp-bin and --wexcomp-config must be specified together')
    if args.compositor_renderer == 'gles2' and (not args.wexcomp_bin or args.gl_driver == 'software'):
        parser.error('GLES2 outer control requires owned wexcomp and an explicit accelerated driver')
    if args.output_scale is not None and (not args.wexcomp_bin or not shutil.which('wlr-randr')):
        parser.error('--output-scale requires owned wexcomp and wlr-randr')
    if args.transition_scale is not None and (args.output_scale is None or not args.lifecycle_visuals):
        parser.error('--transition-scale requires --output-scale and --lifecycle-visuals')
    if args.resize_border and (not args.wexcomp_bin or not args.lifecycle_visuals
                               or args.output_scale not in (None, 1.0) or args.transition_scale is not None):
        parser.error('--resize-border requires native held lifecycle visuals at fixed scale1')
    if args.trace_native and not args.wexcomp_bin:
        parser.error('--trace-native requires owned native wexcomp')
    if args.wexcomp_bin and (args.desktop != 'none' or args.restart_from):
        parser.error('Native Wayland control cannot use an X11 desktop or restart reuse')
    capture_tool = 'grim' if args.wexcomp_bin else 'import'
    if args.lifecycle_visuals and (not args.lifecycle or args.seconds < 45 or not shutil.which(capture_tool)):
        parser.error('--lifecycle-visuals requires --lifecycle, >=45 seconds and the selected display capture tool')
    if args.restart_from and args.desktop != 'none':
        parser.error('Desktop-mode restart controls are not yet supported')
    if args.gl_driver != 'software' and args.renderer != 'gl':
        parser.error('Accelerated GL driver controls require --renderer gl')
    if args.freeze_scene and not args.electron_app:
        parser.error('--freeze-scene requires --electron-app')
    if args.lifecycle and (not args.electron_app or args.freeze_scene or args.seconds < 25):
        parser.error('--lifecycle requires --electron-app, no --freeze-scene, and at least 25 seconds')
    if args.graphics_diagnostic and args.electron_app:
        parser.error('--graphics-diagnostic is supported only by the original WebView2 host')
    if (args.profile_proof or args.restart_from) and not args.electron_app:
        parser.error('--profile-proof/--restart-from require --electron-app')
    if args.gpu_recovery and (not args.electron_app or args.lifecycle or args.freeze_scene
                              or args.restart_from or args.seconds < 30):
        parser.error('--gpu-recovery requires Electron, >=30 seconds, and no lifecycle/freeze/restart')
    if args.renderer == 'vulkan' and not args.vulkan_icd:
        parser.error('--renderer vulkan requires --vulkan-icd')
    icd = args.vulkan_icd.resolve(strict=True) if args.vulkan_icd else None
    build = args.wine_build.resolve(strict=True)
    wexcomp_binary = args.wexcomp_bin.resolve(strict=True) if args.wexcomp_bin else None
    wexcomp_config = args.wexcomp_config.resolve(strict=True) if args.wexcomp_config else None
    runtime = args.runtime.resolve(strict=True)
    host = args.host.resolve(strict=True)
    loader = host.parent / 'WebView2Loader.dll'
    browser = runtime / ('electron.exe' if args.electron_app else 'msedgewebview2.exe')
    wine, server = build / 'wine', build / 'server/wineserver'
    payloads = ((browser, host, args.electron_app.resolve(strict=True) / 'main.js',
                 args.electron_app.resolve(strict=True) / 'package.json')
                if args.electron_app else (browser, host, loader))
    for path in (wine, server) + payloads:
        if not path.is_file():
            parser.error('Required file missing: ' + str(path))
    sockets = Path('/tmp/.X11-unix')
    if sockets.exists() and os.statvfs(sockets).f_flag & os.ST_RDONLY:
        parser.error('Use an owned X socket directory in a private mount namespace')
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    payload_hashes = {str(path): digest(path) for path in payloads}
    stack_files = [wine, server, build / 'dlls/win32u/win32u.so',
                   build / 'dlls/winex11.drv/winex11.so']
    if wexcomp_binary:
        stack_files.append(build / 'dlls/winewayland.drv/winewayland.so')
    stack_files += [build / 'dlls' / module / (arch + '-windows') / (module + '.dll')
                    for module in ('d3d11', 'dxgi', 'dcomp', 'wined3d', 'win32u')
                    for arch in ('x86_64', 'i386')]
    stack_hashes = {str(path.relative_to(build)): digest(path) for path in stack_files}
    state_root, expected = (restart_state(args.restart_from, build, args.renderer,
                                         payload_hashes, stack_hashes, digest(icd) if icd else None, args.gl_driver)
                            if args.restart_from else (output, None))
    # Refuse concurrent reuse; never kill a test running on someone else's state.
    state_lock = (state_root / '.consumer.lock').open('a')
    fcntl.flock(state_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    prefix = state_root / 'prefix'
    profile = state_root / 'profile'
    if not args.restart_from:
        profile.mkdir()
    proof = args.profile_proof or bool(args.restart_from)
    token = secrets.token_hex(16) if proof else None
    spec = importlib.util.spec_from_file_location('g4_runner', Path(__file__).with_name('check-g3-correctness.py'))
    helpers = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helpers)
    if helpers.matching_servers(server, prefix):
        raise RuntimeError('Owned state still has a running server; refusing reuse')
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(('WINE', 'VK_', 'WEBVIEW2_', 'WEX_WEBVIEW2_', 'CHROME_'))
                   and key not in ('LD_PRELOAD', 'LD_LIBRARY_PATH', 'DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY')}
    environment.update(WINE=str(wine), WINESERVER=str(server), WINEPREFIX=str(prefix),
                       WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d;d3d11,dxgi,wined3d,dcomp=b',
                       WINEDEBUG='+timestamp,+pid,+tid,err+all,warn+all,fixme+all',
                       LIBGL_ALWAYS_SOFTWARE='1', WINE_D3D_CONFIG='renderer=' + args.renderer)
    if args.trace_native:
        environment.update(WINEDEBUG=environment['WINEDEBUG'] + ',+waylanddrv', WAYLAND_DEBUG='client')
    helpers.configure_gl_driver(environment, args.gl_driver)
    if icd:
        environment.update(VK_DRIVER_FILES=str(icd), VK_ICD_FILENAMES=str(icd))
    winpath = lambda path: 'Z:' + str(path).replace('/', '\\')
    environment.update(WEBVIEW2_BROWSER_EXECUTABLE_FOLDER=winpath(runtime),
                       WEX_WEBVIEW2_PROFILE=winpath(profile), WEX_WEBVIEW2_LOG=winpath(output / 'host.log'),
                       WEX_WEBVIEW2_SECONDS=str(args.seconds), CHROME_LOG_FILE=winpath(output / 'chromium.log'))
    environment['WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS'] = '--enable-logging --v=1'
    if args.graphics_diagnostic:
        environment['WEX_WEBVIEW2_GRAPHICS_DIAGNOSTIC'] = '1'
    if args.freeze_scene:
        environment['WEX_WEBVIEW2_FREEZE'] = '1'
    if args.lifecycle:
        environment['WEX_WEBVIEW2_LIFECYCLE'] = '1'
    if args.resize_border:
        environment['WEX_WEBVIEW2_RESIZE_BORDER'] = '1'
    if args.lifecycle_visuals:
        environment['WEX_WEBVIEW2_VISUAL_CHECKPOINTS'] = '1'
    if args.gpu_recovery:
        environment['WEX_WEBVIEW2_GPU_RECOVERY'] = '1'
        environment['WEX_WEBVIEW2_RECOVERY_CAPTURE_DIR'] = winpath(output)
    if proof:
        environment['WEX_WEBVIEW2_PROFILE_EXPECT'] = expected or ''
        environment['WEX_WEBVIEW2_PROFILE_WRITE'] = token
    if args.trace_exceptions:
        environment['WINEDEBUG'] += ',+seh'
    if args.trace_graphics:
        environment['WINEDEBUG'] += ',+d3d11,+dxgi'
    result = {'classification': 'Xvfb offline first-failure diagnostic; not G4 acceptance',
              'renderer': args.renderer,
              'gl_driver': args.gl_driver,
              'consumer': 'Electron original fixture' if args.electron_app else 'WebView2',
              'wine_build': str(build),
              'environment': {key: value for key, value in environment.items()
                              if key.startswith(('WINE', 'WEBVIEW2_', 'WEX_WEBVIEW2_', 'CHROME_'))
                              or key in ('LIBGL_ALWAYS_SOFTWARE', 'GALLIUM_DRIVER', 'LD_LIBRARY_PATH')},
              'sha256': payload_hashes, 'stack_sha256': stack_hashes,
              'state_root': str(state_root), 'profile_token': token,
              'restart_from': str(args.restart_from.resolve()) if args.restart_from else None,
              'sandbox_bypass_requested': False, 'acceptance': 'NOT_PROVEN'}
    if icd:
        result.update(vulkan_icd=str(icd), vulkan_icd_sha256=digest(icd))
    if args.gpu_recovery:
        result['classification'] = 'Fault-injected owned GPU-process recovery; not normal/G4 acceptance'
    display = None
    desktop = []
    wexcomp = None
    read_fd, write_fd = os.pipe()
    monitor = None
    stop = threading.Event()
    capture_command = lambda path: (['grim', str(path)] if wexcomp_binary
                                    else ['import', '-window', 'root', str(path)])
    scale_probe = None
    if (args.output_scale is not None or args.resize_border) and args.lifecycle_visuals:
        scale_probe = output / 'native-window-state.exe'
        subprocess.run(['x86_64-w64-mingw32-g++', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-static-libgcc', '-static-libstdc++',
                        str(Path(__file__).with_name('test_g4_native_window_state.cpp')),
                        '-o', str(scale_probe), '-luser32'], check=True, timeout=30)

    def observe():
        seen = set()
        started = time.monotonic()
        next_resources = started
        screenshots = ([4, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32]
                       if args.gpu_recovery or args.freeze_scene else [8, 18])
        with (output / 'processes.jsonl').open('w') as log:
            while not stop.wait(.25):
                now = time.monotonic()
                if args.resource_profile and now >= next_resources:
                    next_resources = now + 1
                    settled = result.get('resize_border_diagnostic', {}).get('passed') is True
                    point = {'seconds': now - started, 'phase': 'frozen-settled' if settled else 'active-lifecycle',
                             'processes': graphics_resources.sample(prefix)}
                    with (output / 'resources.jsonl').open('a') as resources:
                        resources.write(json.dumps(point) + '\n')
                if args.lifecycle_visuals and (output / 'host.log').exists():
                    host_text = (output / 'host.log').read_text(errors='replace')
                    for stage in visual_requests(host_text):
                        if stage in result.setdefault('visual_captures', {}):
                            continue
                        try:
                            if (args.transition_scale is not None and stage == 'input'
                                    and 'output_scale_transition' not in result):
                                wexcomp.set_output_scale(args.transition_scale)
                                result['output_scale_transition'] = {'stage': stage, 'scale': args.transition_scale}
                                stop.wait(1)  # hold the fixture while native scale/repaint messages are delivered
                            if scale_probe is not None and stage != 'minimized':
                                state = helpers.run_logged([str(wine), str(scale_probe)], environment, output,
                                                           'stage-' + stage + '-native-window', 10)
                                if state.returncode:
                                    raise RuntimeError('Fixture HWND state query failed')
                                wanted = (args.transition_scale if result.get('output_scale_transition') else (args.output_scale or 1.0))
                                result.setdefault('native_window_states', {})[stage] = native_scale_state(state.stdout, wanted)
                            capture = output / ('stage-' + stage + '.png')
                            with (output / 'capture.log').open('ab') as capture_log:
                                subprocess.run(capture_command(capture), env=environment,
                                               stdout=capture_log, stderr=capture_log, timeout=5, check=True)
                            if not capture.is_file() or not capture.stat().st_size:
                                raise RuntimeError('Empty lifecycle capture')
                            inspect = (['wayland-info'] if wexcomp_binary else
                                       ['python3', '-B', str(Path(__file__).with_name('snapshot-graphics-x11.py'))])
                            tree = helpers.run_logged(inspect, environment, output, 'stage-' + stage + '-display', 10)
                            if tree.returncode:
                                raise RuntimeError('Lifecycle X11 snapshot failed')
                            result['visual_captures'][stage] = {'path': str(capture), 'time': time.time()}
                            if args.resize_border and stage == 'resize_back':
                                from PIL import Image
                                samples = []
                                begin = time.monotonic()
                                for offset in (0, .25, 1, 2, 4):
                                    stop.wait(max(0, begin + offset - time.monotonic()))
                                    path = output / ('resize-border-%s.png' % str(offset))
                                    subprocess.run(capture_command(path), env=environment, check=True, timeout=5)
                                    with Image.open(path) as pixels:
                                        sample = frame_pixels(pixels, result['native_window_states'][stage])
                                    sample.update(offset=offset, elapsed=time.monotonic() - begin)
                                    samples.append(sample)
                                result['resize_border_diagnostic'] = {'samples': samples,
                                    'passed': all(item['passed'] for item in samples[-2:]),
                                    'note': 'Bounded settled-frame proof; early mismatches retained, not atomic-resize certification'}
                            (output / 'visual-checkpoint.ack').write_text(stage)
                        except (OSError, subprocess.SubprocessError, RuntimeError) as error:
                            result['visual_capture_error'] = str(error)
                if screenshots and time.monotonic() - started >= screenshots[0]:
                    moment = screenshots.pop(0)
                    if shutil.which(capture_tool):
                        try:
                            with (output / 'capture.log').open('ab') as capture_log:
                                subprocess.run(capture_command(output / ('screen-%d.png' % moment)),
                                               env=environment, stdout=capture_log, stderr=capture_log,
                                               timeout=5, check=True)
                        except (OSError, subprocess.SubprocessError) as error:
                            log.write(json.dumps({'capture_error': str(error)}) + '\n')
                for process in Path('/proc').glob('[0-9]*'):
                    try:
                        if b'WINEPREFIX=' + os.fsencode(prefix) not in (process / 'environ').read_bytes().split(b'\0'):
                            continue
                        argv = (process / 'cmdline').read_bytes().replace(b'\0', b' ').decode(errors='replace')
                        maps = [line for line in (process / 'maps').read_text().splitlines()
                                if any(name in line.lower() for name in
                                       ('d3d11', 'dxgi', 'dcomp', 'wined3d', 'd3d12', 'dxcore', 'libgallium',
                                        'winewayland', 'winex11'))]
                        identity = (process.name, argv, tuple(maps))
                        if identity in seen:
                            continue
                        seen.add(identity)
                        log.write(json.dumps({'time': time.time(), 'pid': int(process.name),
                                              'argv': argv, 'graphics_maps': maps}) + '\n')
                        log.flush()
                    except (OSError, ProcessLookupError):
                        continue
    try:
        if wexcomp_binary:
            spec = importlib.util.spec_from_file_location('graphics_wexcomp', Path(__file__).with_name('graphics_wexcomp.py'))
            compositor = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(compositor)
            wexcomp = compositor.OwnedWexcomp(wexcomp_binary, wexcomp_config, output, environment, args.compositor_renderer)
            result['wexcomp'] = wexcomp.start()
            if args.compositor_renderer == 'gles2':
                result['outer_gl_driver_verification'] = wexcomp.verify_gpu_renderer(args.gl_driver)
            result['gl_driver_verification'] = compositor.verify_native_gl_driver(environment, output, args.gl_driver)
            if args.output_scale is not None:
                wexcomp.set_output_scale(args.output_scale)
            result['classification'] = 'Owned pinned wexcomp native Wayland/private endpoint functional control; not installed-shell acceptance'
            result['display'] = environment['WAYLAND_DISPLAY']
            result['desktop'] = None
            environment['WINEDLLOVERRIDES'] += ';winex11.drv=d;winewayland.drv=b'
            result['environment'].update({key: environment[key] for key in
                                         ('XDG_RUNTIME_DIR', 'WAYLAND_DISPLAY', 'WINEDLLOVERRIDES')})
        else:
            with (output / 'xvfb.log').open('wb') as log:
                display = subprocess.Popen(['Xvfb', '-displayfd', str(write_fd), '-screen', '0',
                                            '1024x768x24', '-nolisten', 'tcp'], pass_fds=(write_fd,),
                                           stdout=log, stderr=log, start_new_session=True)
        os.close(write_fd)
        write_fd = None
        if not wexcomp_binary:
            if not select.select([read_fd], [], [], 10)[0]:
                raise TimeoutError('Owned Xvfb did not allocate a display')
            number = os.read(read_fd, 64).strip().decode('ascii')
            if not number.isdigit() or display.poll() is not None:
                raise RuntimeError('Owned Xvfb failed')
            environment['DISPLAY'] = ':' + number
            result['display'] = environment['DISPLAY']
            result['gl_driver_verification'] = helpers.verify_gl_driver(environment, output, args.gl_driver)
            desktop, result['desktop'] = helpers.start_desktop(environment, output, args.desktop)
        if not args.restart_from:
            boot = helpers.run_logged([str(wine), 'wineboot', '-u'], environment, output, 'wineboot', 90)
            result['wineboot_exit'] = boot.returncode
            if boot.returncode:
                raise RuntimeError('Fresh prefix initialization failed')
        else:
            result['wineboot_exit'] = None  # preserve existing state; no wineboot -u rewrite
        monitor = threading.Thread(target=observe_fail_closed, args=(observe, result), daemon=True)
        monitor.start()
        command = [str(wine), str(host)]
        if args.electron_app:
            command.extend([winpath(args.electron_app.resolve(strict=True)), '--enable-logging', '--v=1',
                            '--log-file=' + winpath(output / 'chromium.log')])
        result['command'] = command
        run = helpers.run_logged(command, environment, output, 'wine', args.seconds + 45)
        result['host_exit'] = run.returncode
        stop.set()
        monitor.join(timeout=5)
        if monitor.is_alive():
            result['observer_error'] = 'Observer did not stop within 5 seconds'
        hostlog = output / 'host.log'
        if hostlog.exists():
            analyzer_spec = importlib.util.spec_from_file_location('analyzer', Path(__file__).with_name('analyze-webview2-run.py'))
            analyzer = importlib.util.module_from_spec(analyzer_spec)
            analyzer_spec.loader.exec_module(analyzer)
            result['animation_diagnostic'] = analyzer.analyze(output)
        else:
            result['first_failure'] = 'Host did not create its diagnostic log'
        passed = result.get('animation_diagnostic', {}).get('diagnostic_passed', False)
        if args.gpu_recovery:
            result['gpu_recovery_diagnostic'] = recovery_result(hostlog.read_text(errors='replace') if hostlog.exists() else '')
            passed = recovery_acceptance(result.get('animation_diagnostic', {}), result['gpu_recovery_diagnostic'])
            passed = passed and 'observer_error' not in result
        if args.lifecycle:
            result['lifecycle_diagnostic'] = lifecycle_result(hostlog.read_text(errors='replace') if hostlog.exists() else '')
            passed = passed and result['lifecycle_diagnostic']['passed']
        if args.lifecycle_visuals:
            passed = passed and visual_capture_complete(hostlog.read_text(errors='replace') if hostlog.exists() else '',
                                                       result.get('visual_captures', {}), result.get('visual_capture_error'))
            result['visual_inspection'] = 'MANUAL_REVIEW_REQUIRED; capture completion is not a pixel verdict'
        if args.resize_border:
            frozen = hostlog.exists() and 'resize border frozen ' in hostlog.read_text(errors='replace')
            passed = passed and frozen and result.get('resize_border_diagnostic', {}).get('passed') is True
        if args.graphics_diagnostic:
            result['graphics_diagnostic'] = graphics_result(hostlog.read_text(errors='replace') if hostlog.exists() else '')
            passed = passed and result['graphics_diagnostic']['passed']
        if proof:
            result['profile_diagnostic'] = profile_result(hostlog.read_text(errors='replace') if hostlog.exists() else '',
                                                         expected, token)
            passed = passed and result['profile_diagnostic']['passed']
        helpers.verify_desktop(environment, desktop, result['desktop'])
        if wexcomp:
            wexcomp.verify()
            maps = (output / 'processes.jsonl').read_text(errors='replace')
            result['native_wayland_driver_observed'] = 'winewayland.so' in maps and 'winex11.so' not in maps
            passed = passed and result['native_wayland_driver_observed']
        print(json.dumps(result, indent=2), flush=True)
        result['diagnostic_exit'] = 0 if run.returncode == 0 and passed else 4
        return result['diagnostic_exit']
    except Exception as error:
        result['failure'] = repr(error)
        raise
    finally:
        stop.set()
        if monitor:
            monitor.join(timeout=5)
        if args.resource_profile and (output / 'resources.jsonl').exists():
            try:
                samples = [json.loads(line) for line in (output / 'resources.jsonl').read_text().splitlines()]
                result['resource_profile'] = graphics_resources.summarize(samples, os.sysconf('SC_CLK_TCK'))
            except (OSError, ValueError, KeyError) as error:
                result['resource_profile_error'] = str(error)  # never skip owned-process cleanup
        os.close(read_fd)
        if write_fd is not None:
            os.close(write_fd)
        try:
            killed = subprocess.run([str(server), '-k'], env=environment, timeout=15)
            if killed.returncode not in (0, 1):
                raise RuntimeError('Unexpected owned-server kill result')
            subprocess.run([str(server), '-w'], env=environment, check=True, timeout=15)
            remaining = helpers.matching_servers(server, prefix)
            if remaining:
                raise RuntimeError('Owned servers remain: ' + str(remaining))
            result['cleanup'] = {'remaining_servers': [], 'prefix_retained_for_diagnosis': str(prefix)}
        except (OSError, subprocess.SubprocessError, RuntimeError) as error:
            result['cleanup_error'] = str(error)
        helpers.stop_desktop(desktop)
        if wexcomp:
            wexcomp.stop()
        if display and display.poll() is None:
            os.killpg(display.pid, signal.SIGTERM)
            try:
                display.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(display.pid, signal.SIGKILL)
                display.wait(timeout=5)
        (output / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
        state_lock.close()
        if 'cleanup_error' in result:
            raise RuntimeError(result['cleanup_error'])


if __name__ == '__main__':
    raise SystemExit(main())
