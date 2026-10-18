#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Opt-in graphics for split prefixes; never write through shared Windows links.

on/off configure the next login. prepare is called by the session launcher before
starting Explorer. Only DLL load order in that session's environment is changed;
existing registry overrides are neither edited nor discarded.
"""
import argparse
import fcntl
import os
from pathlib import Path
import re
import shutil
import stat
import struct
import subprocess
import sys
import tempfile

DLLS = ('d3d8', 'd3d9', 'd3d10core', 'd3d11', 'dxgi', 'd3d12', 'd3d12core')
OVERRIDES = ','.join(DLLS) + '=n,b'


def check_device():
    result = subprocess.run(['vulkaninfo', '--summary'], capture_output=True, text=True, timeout=20)
    versions = [tuple(map(int, pair)) for pair in re.findall(r'apiVersion\s*=\s*(\d+)\.(\d+)', result.stdout)]
    if (result.returncode or not re.search(r'^\s*GPU\d+:', result.stdout, re.M)
            or not any(version >= (1, 3) for version in versions)):
        raise ValueError('No usable Vulkan 1.3 device; leaving configuration unchanged')


def check_libraries(root):
    for arch, machine in (('x32', 0x14c), ('x64', 0x8664)):
        for name in DLLS:
            path = root / arch / (name + '.dll')
            with path.open('rb') as stream:
                if stream.read(2) != b'MZ':
                    raise ValueError(f'Not a PE DLL: {path}')
                stream.seek(0x3c)
                offset, = struct.unpack('<I', stream.read(4))
                stream.seek(offset)
                if stream.read(4) != b'PE\0\0' or struct.unpack('<H', stream.read(2))[0] != machine:
                    raise ValueError(f'Wrong architecture: {path}')


def restore(prefix, state):
    windows = prefix / 'drive_c/windows'
    saved = state / 'shared-windows'
    if saved.is_symlink():
        if not windows.is_symlink() or windows.resolve() != (state / 'windows').resolve():
            # Already restored (e.g. interrupted prepare), otherwise refuse.
            if windows.is_symlink() and windows.resolve() == saved.resolve():
                return
            raise ValueError('Windows link changed outside wex-graphics; refusing to replace it')
        temporary = state / 'restore-link'
        temporary.unlink(missing_ok=True)
        temporary.symlink_to(saved.resolve(), target_is_directory=True)
        os.replace(temporary, windows)


def prepare(prefix, state, libraries):
    restore(prefix, state)
    if not (state / 'enabled').exists():
        return
    # A previously enabled machine may lose its device/driver. Fail back to
    # Wine at login rather than installing native DLLs that cannot initialize.
    check_device()
    check_libraries(libraries)
    windows = prefix / 'drive_c/windows'
    if not windows.is_symlink():
        raise ValueError('Requires a split prefix with a shared Windows symlink')
    shared = windows.resolve(strict=True)
    if shared == (state / 'windows').resolve():
        raise ValueError('Cannot determine shared Windows directory')
    # Rebuild the shallow view every login, so new shared files remain visible.
    # Only system32/syswow64 entries are shadowed; all other folders stay shared.
    shadow = state / 'windows'
    if shadow.exists():
        if shadow.is_symlink():
            raise ValueError('Unexpected symlink at private graphics view')
        shutil.rmtree(shadow)
    with tempfile.TemporaryDirectory(prefix='view-', dir=state) as scratch:
        view = Path(scratch) / 'windows'
        view.mkdir()
        for item in shared.iterdir():
            if item.name.lower() not in ('system32', 'syswow64'):
                (view / item.name).symlink_to(item, target_is_directory=item.is_dir())
        for directory, arch in (('system32', 'x64'), ('syswow64', 'x32')):
            target = view / directory
            target.mkdir()
            source = shared / directory
            for item in source.iterdir():
                (target / item.name).symlink_to(item, target_is_directory=item.is_dir())
            for name in DLLS:
                # Remove case-insensitive matches too (Wine filenames may vary).
                for item in target.iterdir():
                    if item.name.lower() == name + '.dll':
                        item.unlink()
                (target / (name + '.dll')).symlink_to(libraries / arch / (name + '.dll'))
        os.replace(view, shadow)
    saved = state / 'shared-windows'
    saved.unlink(missing_ok=True)
    saved.symlink_to(shared, target_is_directory=True)
    temporary = state / 'activate-link'
    temporary.unlink(missing_ok=True)
    temporary.symlink_to(shadow, target_is_directory=True)
    os.replace(temporary, windows)
    print(OVERRIDES)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('feature', choices=['vulkan'])
    parser.add_argument('action', choices=['on', 'off', 'status', 'prepare', 'restore'])
    args = parser.parse_args()
    prefix = Path(os.environ.get('WINEPREFIX', str(Path.home() / '.wine'))).absolute()
    if prefix.is_symlink() or not prefix.is_dir() or prefix.stat().st_uid != os.getuid():
        raise ValueError('Prefix must be an existing directory owned by the current user')
    state = prefix / '.wex-graphics'
    if state.is_symlink():
        raise ValueError('Refusing symlinked graphics state')
    state.mkdir(mode=0o700, exist_ok=True)
    libraries = Path(os.environ.get('WEX_DXVK_DIR', '/usr/local/share/wineexplorer/dxvk')).resolve()
    # Never truncate a symlink target supplied through stale/untrusted state.
    descriptor = os.open(state / 'lock', os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    with os.fdopen(descriptor, 'a') as lock:
        if not stat.S_ISREG(os.fstat(lock.fileno()).st_mode):
            raise ValueError('Graphics lock must be a regular file')
        fcntl.flock(lock, fcntl.LOCK_EX)
        if args.action == 'on':
            if not (prefix / 'drive_c/windows').is_symlink():
                raise ValueError('Requires a split prefix with a shared Windows symlink')
            check_libraries(libraries)
            check_device()
            (state / 'enabled').touch(mode=0o600)
            print('Vulkan requested. Log out and back in to apply; running applications are unchanged.')
        elif args.action == 'off':
            (state / 'enabled').unlink(missing_ok=True)
            print('Wine graphics requested. Log out and back in to apply.')
        elif args.action == 'status':
            active = (prefix / 'drive_c/windows').resolve() == (state / 'windows').resolve()
            print(f"Next login: {'Vulkan' if (state / 'enabled').exists() else 'Wine'}; private view: {'active' if active else 'inactive'}")
        elif args.action == 'restore':
            restore(prefix, state)
        else:
            prepare(prefix, state, libraries)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, subprocess.SubprocessError, struct.error) as error:
        print(f'wex-graphics: {error}', file=sys.stderr)
        sys.exit(1)
