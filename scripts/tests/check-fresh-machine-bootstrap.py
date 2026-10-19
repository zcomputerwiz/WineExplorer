#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Fresh owned-prefix diagnostic, not installed-image or desktop acceptance.

On WSL run in a private mount AND network namespace with an owned /tmp.
Never uses a pre-existing prefix/display or changes installed Wine files.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import select
import signal
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--wine-build', type=Path)
    parser.add_argument('--wine', type=Path)
    parser.add_argument('--wineserver', type=Path)
    parser.add_argument('--exercise-regedit', type=Path, help='Repaired backend source; tests a deliberately empty regedit in this NEW prefix only')
    parser.add_argument('--native-probe32', type=Path)
    parser.add_argument('--native-probe64', type=Path)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    if bool(args.native_probe32) != bool(args.native_probe64):
        parser.error('Supply both native probe architectures')
    if args.wine_build:
        if args.wine or args.wineserver:
            parser.error('Choose a build OR explicit installed Wine/server paths')
        build = args.wine_build.resolve(strict=True)
        wine, server = build / 'wine', build / 'server/wineserver'
    elif args.wine and args.wineserver:
        wine, server = args.wine.resolve(strict=True), args.wineserver.resolve(strict=True)
    else:
        parser.error('An explicit build or matching Wine/server pair is required')
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    if os.environ.get('WSL_DISTRO_NAME') and os.readlink('/proc/self/ns/net') == os.readlink('/proc/1/ns/net'):
        raise RuntimeError('Use a private network namespace for WSL X sockets')
    result = {'scope': 'fresh bootstrap only; no desktop acceptance', 'commands': []}
    with tempfile.TemporaryDirectory(prefix='wex-machine-bootstrap-') as scratch:
        root = Path(scratch)
        env = {k: v for k, v in os.environ.items() if not k.startswith(('WINE', 'WEX_', 'XDG_'))
               and k not in ('DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY', 'LD_PRELOAD', 'LD_LIBRARY_PATH')}
        env.update(WINEPREFIX=str(root / 'prefix'), HOME=str(root), WINESERVER=str(server),
                   WINE=str(wine), WINEDEBUG='-all', WINELINKFAKEDLLS='1',
                   WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d')
        read_fd, write_fd = os.pipe()
        display = None
        try:
            with (output / 'xvfb.log').open('wb') as stream:
                display = subprocess.Popen(['Xvfb', '-displayfd', str(write_fd), '-screen', '0',
                                            '1024x768x24', '-nolisten', 'tcp'],
                                           pass_fds=(write_fd,), stdout=stream, stderr=stream)
            os.close(write_fd)
            write_fd = None
            if not select.select([read_fd], [], [], 10)[0]:
                raise TimeoutError('Owned Xvfb failed to become ready')
            number = os.read(read_fd, 64).strip().decode('ascii')
            if not number.isdigit():
                raise RuntimeError('Invalid allocated display')
            env['DISPLAY'] = ':' + number
            commands = [
                    ('boot', [str(wine), 'wineboot.exe', '-i'], 0),
                    ('cmd64', [str(wine), r'C:\windows\system32\cmd.exe', '/c', 'exit', '37'], 37),
                    ('cmd32', [str(wine), r'C:\windows\syswow64\cmd.exe', '/c', 'exit', '37'], 37)]
            if args.native_probe32:
                commands.extend((label, [str(wine), str(path.resolve(strict=True))], 0) for label, path in
                                (('native32', args.native_probe32), ('native64', args.native_probe64)))
            for label, command, expected in commands:
                with (output / (label + '.log')).open('wb') as stream:
                    run = subprocess.run(command, env=env, stdout=stream, stderr=stream, timeout=120)
                result['commands'].append({'label': label, 'command': command,
                                           'exit': run.returncode, 'expected': expected})
            windows = root / 'prefix/drive_c/windows'
            if args.exercise_regedit:
                backend = args.exercise_regedit.resolve(strict=True)
                spec = importlib.util.spec_from_file_location('components', backend)
                module = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(module)
                target = windows / 'syswow64/regedit.exe'
                if not target.parent.resolve().is_relative_to(root):
                    raise RuntimeError('Test mutation must stay inside its NEW prefix')
                target.unlink(missing_ok=True)
                target.touch()  # Controlled regression fixture, not a damaged user installation.
                module.ensure_installer_regedit(root / 'prefix')
                result['repaired_regedit32_target'] = str(target.resolve())
                registry = root / 'probe.reg'
                registry.write_text('Windows Registry Editor Version 5.00\n\n'
                                    '[HKEY_CURRENT_USER\\Software\\WineExplorer\\OobeRepairProbe]\n'
                                    '"Native32"=dword:00000025\n', encoding='utf-16')
                command = [str(wine), r'C:\windows\syswow64\regedit.exe', '/S',
                           'Z:' + str(registry).replace('/', '\\')]
                with (output / 'regedit32.log').open('wb') as stream:
                    run = subprocess.run(command, env=env, stdout=stream, stderr=stream, timeout=30)
                result['commands'].append({'label': 'regedit32', 'command': command, 'exit': run.returncode, 'expected': 0})
                command = [str(wine), 'reg.exe', 'query', r'HKCU\Software\WineExplorer\OobeRepairProbe', '/v', 'Native32']
                with (output / 'regedit-query.log').open('wb') as stream:
                    run = subprocess.run(command, env=env, stdout=stream, stderr=stream, timeout=30)
                result['commands'].append({'label': 'regedit-query', 'command': command,
                                           'exit': run.returncode, 'expected': 0})
            result['empty_files'] = [str(p.relative_to(windows)) for p in windows.rglob('*')
                                     if p.suffix.lower() in ('.dll', '.exe') and p.is_file() and not p.is_symlink()
                                     and p.stat().st_size == 0]
            result['passed'] = not result['empty_files'] and all(c['exit'] == c['expected'] for c in result['commands'])
        finally:
            os.close(read_fd)
            if write_fd is not None:
                os.close(write_fd)
            subprocess.run([str(server), '-k'], env=env, timeout=15)
            subprocess.run([str(server), '-w'], env=env, timeout=15, check=True)
            if display and display.poll() is None:
                display.send_signal(signal.SIGTERM)
                display.wait(timeout=10)
            result['cleanup'] = 'owned Wine server waited and Xvfb reaped; temporary prefix removed on context exit'
            (output / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 0 if result.get('passed') else 1


if __name__ == '__main__':
    raise SystemExit(main())
