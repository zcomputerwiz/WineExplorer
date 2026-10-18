#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Administrator-only optional redistributables, with explicit vendor terms.

The unprivileged UI never runs winetricks. The fixed pkexec backend rechecks the
allowlist and the digest of root-owned license text before any download. Missing
vendor terms disable a component, rather than substituting a generic notice.
"""
import argparse
from contextlib import contextmanager
import fcntl
import grp
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

COMPONENTS = {
    'vcrun2022': 'Visual C++ 2015–2022 runtime',
    'corefonts': 'Microsoft core fonts (optional)',
    'tahoma': 'Microsoft Tahoma (optional)',
    'd3dcompiler_47': 'DirectX shader compiler (optional)',
    'xact': 'XACT audio libraries (optional)',
    'physx': 'NVIDIA PhysX (optional)',
}
TERMS = Path('/etc/wineexplorer/component-licenses')
MACHINE = Path('/var/lib/wineexplorer/machine')
CACHE = Path('/var/cache/winetricks')
BACKEND = '/usr/local/bin/wex-components'
# Winetricks must not replace these Wine implementations, even as an indirect
# side effect of an allowed verb such as vcrun2022. Native d3dcompiler is optional.
PROTECTED = ('dwrite', 'gdiplus', 'usp10', 'riched20', 'mf', 'mfplat', 'mfreadwrite',
             'msxml', 'msxml2', 'msxml3', 'msxml4', 'msxml6', 'ucrtbase', 'wininet',
             'crypt32', 'kernel32', 'kernelbase', 'ntdll', 'user32', 'gdi32',
             'advapi32', 'shell32', 'ole32', 'combase')


def validate_verb(verb):
    if verb not in COMPONENTS:
        raise ValueError(f'Unsupported component: {verb}')
    return verb


def is_admin():
    return os.getuid() == 0 or grp.getgrnam('sudo').gr_gid in (*os.getgroups(), os.getgid())


def protected_name(name):
    name = name.lower()
    return name.endswith('.dll') and (name[:-4] in PROTECTED or name.startswith(('msxml', 'mf')))


def license_text(verb, directory=TERMS):
    path = directory / (validate_verb(verb) + '.txt')
    if not path.is_file():
        raise ValueError(f'{verb} is disabled: an administrator must provision the full vendor terms at {path}. No download was started.')
    # No caller-controlled text or replaceable ancestor can authorize a root
    # download. The installed directory and notices must be root-owned.
    for entry in (path, *path.parents):
        if entry.is_symlink() or entry.stat().st_uid != 0 or entry.stat().st_mode & 0o022:
            raise ValueError(f'Insecure vendor license path: {entry}')
    text = path.read_text(encoding='utf-8')
    if len(text.strip()) < 500:
        raise ValueError(f'Full vendor terms required for {verb}')
    return text, hashlib.sha256(path.read_bytes()).hexdigest()


def reg(wine, environment, *args, **kwargs):
    return subprocess.run([wine, 'reg.exe', *args], env=environment,
                          capture_output=True, timeout=90, **kwargs)


@contextmanager
def protect_builtins(machine, environment):
    """Preserve core files and override keys in the isolated maintenance prefix."""
    wine = '/usr/local/bin/wine'
    # Keep the root-only recovery snapshot even if restoration itself fails.
    # Debian 13's Python supports delete=False; these can be removed by an
    # administrator after validating the installed component.
    with tempfile.TemporaryDirectory(prefix='components-protected-', dir=machine.parent, delete=False) as scratch:
        saved = Path(scratch)
        print(f'Protected builtin recovery snapshot: {saved}', file=sys.stderr)
        files = []
        for directory in ('system32', 'syswow64'):
            system = machine / 'drive_c/windows' / directory
            names = {name + '.dll' for name in PROTECTED}
            names.update(path.name for path in system.iterdir() if protected_name(path.name))
            for name in names:
                target = system / name
                backup = saved / directory / target.name
                backup.parent.mkdir(exist_ok=True)
                existed = target.exists() or target.is_symlink()
                if existed:
                    if target.is_dir() and not target.is_symlink():
                        raise ValueError(f'Unexpected protected DLL directory: {target}')
                    shutil.copy2(target, backup, follow_symlinks=False)
                files.append((target, backup, existed))
        keys = []
        for hive in ('HKCU', 'HKLM'):
            for suffix in ('DllOverrides', 'AppDefaults'):
                key = hive + '\\Software\\Wine\\' + suffix
                backup = saved / (hive + '-' + suffix + '.reg')
                query = reg(wine, environment, 'query', key)
                if query.returncode:
                    # Distinguish "key absent" from a broken Wine invocation.
                    reg(wine, environment, 'query', hive + '\\Software', check=True)
                    existed = False
                else:
                    reg(wine, environment, 'export', key, str(backup), '/y', check=True)
                    existed = True
                keys.append((key, backup, existed))
        try:
            yield
        finally:
            known = {target for target, _, _ in files}
            for directory in ('system32', 'syswow64'):
                for target in (machine / 'drive_c/windows' / directory).iterdir():
                    if target not in known and protected_name(target.name):
                        if target.is_dir() and not target.is_symlink():
                            raise ValueError(f'Unexpected protected DLL directory: {target}')
                        target.unlink()
            for target, backup, existed in files:
                if target.is_dir() and not target.is_symlink():
                    raise ValueError(f'Installer replaced protected file with directory: {target}')
                target.unlink(missing_ok=True)
                if existed:
                    shutil.copy2(backup, target, follow_symlinks=False)
            # The machine root's HKCU is not a user's HKCU. Restore all installer
            # override side effects instead of propagating them into templates.
            for key, backup, existed in keys:
                result = reg(wine, environment, 'delete', key, '/f')
                if result.returncode:
                    reg(wine, environment, 'query', key.split('\\Software')[0] + '\\Software', check=True)
                if existed:
                    reg(wine, environment, 'import', str(backup), check=True)


def install(verb, accepted_digest):
    validate_verb(verb)
    if os.getuid() != 0:
        raise ValueError('The installer backend requires authenticated elevation')
    _, current = license_text(verb)
    if current != accepted_digest:
        raise ValueError('Vendor terms changed; review and accept the current text')
    if MACHINE.is_symlink() or MACHINE.stat().st_uid != 0:
        raise ValueError('Machine prefix must be a root-owned real directory')
    # Neither arbitrary prefixes, DLL overrides, executables, display sockets nor
    # user-controlled caches are inherited across the privilege boundary.
    environment = {
        'PATH': '/usr/local/bin:/usr/bin:/bin', 'HOME': '/root', 'USER': 'root',
        'LOGNAME': 'root', 'LANG': 'C.UTF-8', 'WINEPREFIX': str(MACHINE),
        'WINE': '/usr/local/bin/wine', 'WINESERVER': '/usr/local/bin/wineserver',
        'WINEDLLOVERRIDES': 'winemenubuilder.exe=d', 'WINEDEBUG': '-all',
        'W_CACHE': str(CACHE), 'XDG_CACHE_HOME': str(CACHE.parent),
    }
    lock_path = MACHINE.parent / '.components.lock'
    descriptor = os.open(lock_path, os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    with os.fdopen(descriptor, 'a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        # Do not kill somebody else's maintenance client or wait for a desktop.
        subprocess.run(['/usr/local/bin/wineserver', '-w'], env=environment,
                       timeout=2, check=True)
        cache = CACHE
        if cache.is_symlink():
            raise ValueError('Refusing symlinked component cache')
        cache.mkdir(mode=0o700, exist_ok=True)
        if cache.stat().st_uid != 0 or cache.stat().st_mode & 0o022:
            raise ValueError('Component cache must be root-owned and not writable by others')
        # Prefix ownership and default ACLs stay intact: do not recursively
        # chown live user prefixes. Record the real verb result, never success
        # from a progress-dialog pipeline. All package terms were accepted above.
        with protect_builtins(MACHINE, environment):
            result = subprocess.run(['/usr/bin/xvfb-run', '-a', '/usr/bin/winetricks',
                                     '-q', '-f', verb], env=environment)
        subprocess.run(['/usr/local/bin/wineserver', '-w'], env=environment,
                       timeout=120, check=True)
        if result.returncode:
            raise ValueError(f'{verb} failed (exit {result.returncode}); installation may be partial; see console output')
        receipt = MACHINE.parent / 'component-receipts'
        receipt.mkdir(mode=0o700, exist_ok=True)
        (receipt / (verb + '.sha256')).write_text(current + '\n', encoding='ascii')


def consent(verb, gui):
    text, sha = license_text(verb)
    if gui:
        result = subprocess.run(['zenity', '--text-info', '--title=' + COMPONENTS[verb],
                                 '--width=760', '--height=560',
                                 '--checkbox=I accept these vendor license terms'],
                                input=text, text=True)
        return sha if result.returncode == 0 else None
    print(text)
    return sha if input(f'Accept these terms for {verb}? Type yes: ').strip() == 'yes' else None


def frontend(verbs, gui):
    if not is_admin():
        raise ValueError('Windows Components is available to administrators only')
    # Validate the whole request before displaying terms or invoking elevation.
    for verb in verbs:
        validate_verb(verb)
    if gui:
        command = ['zenity', '--list', '--checklist', '--title=Windows Components',
                   '--column=Install', '--column=Component', '--column=Description',
                   '--print-column=2', '--separator=|', '--width=720', '--height=360']
        for verb, name in COMPONENTS.items():
            command.extend(['FALSE', verb, name])
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode or not result.stdout.strip():
            return
        verbs = result.stdout.strip().split('|')
    for verb in verbs:
        validate_verb(verb)
        sha = consent(verb, gui)
        if sha is None:
            return
        # pkexec authenticates an administrator and sanitizes the environment.
        # No broad passwordless policy is installed.
        command = [BACKEND, '--install', verb, sha]
        if os.getuid() != 0:
            command.insert(0, '/usr/bin/pkexec')
        result = subprocess.run(command)
        if result.returncode:
            raise ValueError(f'{verb} was not installed successfully (exit {result.returncode})')
    if gui:
        subprocess.run(['zenity', '--info', '--text=Selected components installed successfully.'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', nargs=2, metavar=('VERB', 'ACCEPTED_SHA256'), help=argparse.SUPPRESS)
    parser.add_argument('action', nargs='?', choices=['install', 'list'])
    parser.add_argument('verbs', nargs='*')
    args = parser.parse_args()
    if args.install:
        install(*args.install)
    elif args.action == 'list':
        for verb, name in COMPONENTS.items():
            print(verb + ': ' + name)
    else:
        if args.action == 'install' and not args.verbs:
            parser.error('install requires at least one supported component')
        frontend(args.verbs, gui=args.action is None)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f'Windows Components: {error}', file=sys.stderr)
        if len(sys.argv) == 1:
            subprocess.run(['zenity', '--error', '--text=' + str(error)])
        sys.exit(1)
