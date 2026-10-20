#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline first-failure diagnostic, NOT a Chromium/G4 acceptance certificate.

Supply a lawfully acquired private fixed-version runtime and existing diagnostic
host/loader. No downloading, installation, sandbox bypass or system prefix use.
Run inside a private mount/network namespace if WSLg owns the X socket directory.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import select
import shutil
import signal
import subprocess
import threading
import time


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--wine-build', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--host', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--trace-exceptions', action='store_true',
                        help='Trace exception addresses; does not change browser/security flags')
    parser.add_argument('--renderer', choices=('gl', 'vulkan'), default='gl')
    parser.add_argument('--vulkan-icd', type=Path,
                        help='Required explicit software/hardware ICD for a Vulkan control')
    args = parser.parse_args()
    if args.renderer == 'vulkan' and not args.vulkan_icd:
        parser.error('--renderer vulkan requires --vulkan-icd')
    icd = args.vulkan_icd.resolve(strict=True) if args.vulkan_icd else None
    build = args.wine_build.resolve(strict=True)
    runtime = args.runtime.resolve(strict=True)
    host = args.host.resolve(strict=True)
    loader = host.parent / 'WebView2Loader.dll'
    browser = runtime / 'msedgewebview2.exe'
    wine, server = build / 'wine', build / 'server/wineserver'
    payloads = (browser, host, loader)
    for path in (wine, server) + payloads:
        if not path.is_file():
            parser.error('Required file missing: ' + str(path))
    sockets = Path('/tmp/.X11-unix')
    if sockets.exists() and os.statvfs(sockets).f_flag & os.ST_RDONLY:
        parser.error('Use an owned X socket directory in a private mount namespace')
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    prefix = output / 'prefix'
    profile = output / 'profile'
    profile.mkdir()
    spec = importlib.util.spec_from_file_location('g4_runner', Path(__file__).with_name('check-g3-correctness.py'))
    helpers = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helpers)
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(('WINE', 'VK_', 'WEBVIEW2_', 'WEX_WEBVIEW2_', 'CHROME_'))
                   and key not in ('LD_PRELOAD', 'LD_LIBRARY_PATH', 'DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY')}
    environment.update(WINE=str(wine), WINESERVER=str(server), WINEPREFIX=str(prefix),
                       WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d;d3d11,dxgi,wined3d,dcomp=b',
                       WINEDEBUG='+timestamp,+pid,+tid,err+all,warn+all,fixme+all',
                       LIBGL_ALWAYS_SOFTWARE='1', WINE_D3D_CONFIG='renderer=' + args.renderer)
    if icd:
        environment.update(VK_DRIVER_FILES=str(icd), VK_ICD_FILENAMES=str(icd))
    winpath = lambda path: 'Z:' + str(path).replace('/', '\\')
    environment.update(WEBVIEW2_BROWSER_EXECUTABLE_FOLDER=winpath(runtime),
                       WEX_WEBVIEW2_PROFILE=winpath(profile), WEX_WEBVIEW2_LOG=winpath(output / 'host.log'),
                       WEX_WEBVIEW2_SECONDS='25', CHROME_LOG_FILE=winpath(output / 'chromium.log'))
    environment['WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS'] = '--enable-logging --v=1'
    if args.trace_exceptions:
        environment['WINEDEBUG'] += ',+seh'
    result = {'classification': 'Xvfb offline first-failure diagnostic; not G4 acceptance',
              'renderer': args.renderer,
              'wine_build': str(build),
              'environment': {key: value for key, value in environment.items()
                              if key.startswith(('WINE', 'WEBVIEW2_', 'WEX_WEBVIEW2_', 'CHROME_'))
                              or key == 'LIBGL_ALWAYS_SOFTWARE'},
              'sha256': {str(path): digest(path) for path in payloads},
              'sandbox_bypass_requested': False, 'acceptance': 'NOT_PROVEN'}
    if icd:
        result.update(vulkan_icd=str(icd), vulkan_icd_sha256=digest(icd))
    display = None
    read_fd, write_fd = os.pipe()
    monitor = None
    stop = threading.Event()

    def observe():
        seen = set()
        started = time.monotonic()
        screenshots = [8, 18]
        with (output / 'processes.jsonl').open('w') as log:
            while not stop.wait(.25):
                if screenshots and time.monotonic() - started >= screenshots[0]:
                    moment = screenshots.pop(0)
                    if shutil.which('import'):
                        try:
                            with (output / 'capture.log').open('ab') as capture_log:
                                subprocess.run(['import', '-window', 'root', str(output / ('screen-%d.png' % moment))],
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
                                if any(name in line.lower() for name in ('d3d11', 'dxgi', 'dcomp', 'wined3d'))]
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
        with (output / 'xvfb.log').open('wb') as log:
            display = subprocess.Popen(['Xvfb', '-displayfd', str(write_fd), '-screen', '0',
                                        '1024x768x24', '-nolisten', 'tcp'], pass_fds=(write_fd,),
                                       stdout=log, stderr=log, start_new_session=True)
        os.close(write_fd)
        write_fd = None
        if not select.select([read_fd], [], [], 10)[0]:
            raise TimeoutError('Owned Xvfb did not allocate a display')
        number = os.read(read_fd, 64).strip().decode('ascii')
        if not number.isdigit() or display.poll() is not None:
            raise RuntimeError('Owned Xvfb failed')
        environment['DISPLAY'] = ':' + number
        result['display'] = environment['DISPLAY']
        boot = helpers.run_logged([str(wine), 'wineboot', '-u'], environment, output, 'wineboot', 90)
        result['wineboot_exit'] = boot.returncode
        if boot.returncode:
            raise RuntimeError('Fresh prefix initialization failed')
        monitor = threading.Thread(target=observe, daemon=True)
        monitor.start()
        command = [str(wine), str(host)]
        result['command'] = command
        run = helpers.run_logged(command, environment, output, 'wine', 70)
        result['host_exit'] = run.returncode
        hostlog = output / 'host.log'
        if hostlog.exists():
            analyzer_spec = importlib.util.spec_from_file_location('analyzer', Path(__file__).with_name('analyze-webview2-run.py'))
            analyzer = importlib.util.module_from_spec(analyzer_spec)
            analyzer_spec.loader.exec_module(analyzer)
            result['animation_diagnostic'] = analyzer.analyze(output)
        else:
            result['first_failure'] = 'Host did not create its diagnostic log'
        print(json.dumps(result, indent=2), flush=True)
        passed = result.get('animation_diagnostic', {}).get('diagnostic_passed', False)
        result['diagnostic_exit'] = 0 if run.returncode == 0 and passed else 4
        return result['diagnostic_exit']
    except Exception as error:
        result['failure'] = repr(error)
        raise
    finally:
        stop.set()
        if monitor:
            monitor.join(timeout=5)
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
        if display and display.poll() is None:
            os.killpg(display.pid, signal.SIGTERM)
            try:
                display.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(display.pid, signal.SIGKILL)
                display.wait(timeout=5)
        (output / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
        if 'cleanup_error' in result:
            raise RuntimeError(result['cleanup_error'])


if __name__ == '__main__':
    raise SystemExit(main())
