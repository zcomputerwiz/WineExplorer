#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Administrator-only optional redistributables, with explicit vendor terms.

The unprivileged UI never runs winetricks. The fixed pkexec backend rechecks the
allowlist and the digest of root-owned license text before any download. Missing
vendor terms disable a component, rather than substituting a generic notice.
During OOBE, a root-owned coordinator serves the unprivileged wizard over a
stage-limited Unix domain socket verified via SO_PEERCRED.
"""
import argparse
from contextlib import contextmanager
import fcntl
import grp
import hashlib
import io
import os
from pathlib import Path
import pwd
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
import zipfile

COMPONENTS = {
    'vcrun2022': 'Visual C++ 2015–2022 runtime',
    'corefonts': 'Microsoft core fonts (optional)',
    'tahoma': 'Microsoft Tahoma (optional)',
    'd3dcompiler_47': 'DirectX shader compiler (optional)',
    'xact': 'XACT audio libraries (optional)',
    'physx': 'NVIDIA PhysX (optional)',
}

COMPONENTS_METADATA = {
    'vcrun2022': {
        'title': 'Visual C++ 2015–2022 runtime',
        'publisher': 'Microsoft Corporation',
        'description': 'Installs Visual C++ runtime libraries (vcruntime140, msvcp140, concrt140, vcamp140) required by many modern Windows applications and games.',
        'size': '~14 MB',
        'reboot': False,
        'terms_url': 'https://visualstudio.microsoft.com/wp-content/uploads/2021/09/Visual-C-Runtime-2015-2022-License-1.docx',
        'recipe_url': 'https://aka.ms/vs/17/release/vc_redist.x86.exe',
        'recipe_sha256': 'dd1a8be03398367745a87a5e35bebdab00fdad080cf42af0c3f20802d08c25d4',
        'mutable_url': True,
    },
    'corefonts': {
        'title': 'Microsoft TrueType Core Fonts',
        'publisher': 'Microsoft Corporation',
        'description': 'Installs original Microsoft TrueType core fonts (Arial, Times New Roman, Courier New, Georgia, Verdana, Comic Sans, Trebuchet, Impact, Andale Mono, Webdings).',
        'size': '~4 MB',
        'reboot': False,
        'terms_url': 'https://raw.githubusercontent.com/pushcx/corefonts/master/LICENSE',
        'recipe_url': 'https://github.com/pushcx/corefonts/raw/master/*.exe',
        'recipe_sha256': 'pinned',
        'mutable_url': False,
    },
    'tahoma': {
        'title': 'Microsoft Tahoma font',
        'publisher': 'Microsoft Corporation',
        'description': 'Installs Microsoft Tahoma and Tahoma Bold TrueType fonts from the official Microsoft Internet Explorer font package.',
        'size': '~410 KB',
        'reboot': False,
        'terms_url': 'https://raw.githubusercontent.com/pushcx/corefonts/master/LICENSE',
        'recipe_url': 'https://downloads.sourceforge.net/corefonts/OldFiles/IELPKTH.CAB',
        'recipe_sha256': 'c1be3fb8f0042570be76ec6daa03a99142c88367c1bc810240b85827c715961a',
        'mutable_url': False,
    },
    'd3dcompiler_47': {
        'title': 'DirectX Shader Compiler (d3dcompiler_47)',
        'publisher': 'Microsoft Corporation',
        'description': 'DirectX HLSL shader compiler library required by DirectX 11, DirectX 12, and Vulkan translation layers (DXVK/VKD3D). Wine builtin compiler remains default.',
        'size': '~4.5 MB',
        'reboot': False,
        'terms_url': 'https://raw.githubusercontent.com/mozilla/fxc2/master/README.md',
        'recipe_url': 'https://raw.githubusercontent.com/mozilla/fxc2/master/dll/d3dcompiler_47.dll',
        'recipe_sha256': '4432bbd1a390874f3f0a503d45cc48d346abc3a8c0213c289f4b615bf0ee84f3',
        'mutable_url': False,
    },
    'xact': {
        'title': 'Microsoft XACT audio engine',
        'publisher': 'Microsoft Corporation',
        'description': 'Cross-Platform Audio Creation Tool (XACT) runtime libraries for game audio.',
        'size': '~95 MB',
        'reboot': False,
        'terms_url': 'https://download.microsoft.com/download/8/4/A/84A35BF1-DAFE-4AE8-82AF-AD2AE20B6B14/directx_Jun2010_redist.exe',
        'recipe_url': 'https://files.holarse-linuxgaming.de/mirrors/microsoft/directx_Jun2010_redist.exe',
        'recipe_sha256': '8746ee1a84a083a90e37899d71d50d5c7c015e69688a466aa80447f011780c0d',
        'mutable_url': False,
    },
    'physx': {
        'title': 'NVIDIA PhysX System Software',
        'publisher': 'NVIDIA Corporation',
        'description': 'Physics simulation engine software required by PhysX-enabled games.',
        'size': '~27 MB',
        'reboot': False,
        'terms_url': 'https://us.download.nvidia.com/Windows/9.21.0713/PhysX_9.21.0713_SystemSoftware.exe',
        'recipe_url': 'https://us.download.nvidia.com/Windows/9.21.0713/PhysX_9.21.0713_SystemSoftware.exe',
        'recipe_sha256': '26d62c5c347c15cb27c3be92bf10706113511b48b28aecc09f61ee58b3b62778',
        'mutable_url': False,
    },
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

# Official vendor terms text for components whose terms are plain text licenses
_VENDOR_TERMS_TEXT = {
    'd3dcompiler_47': """MICROSOFT SOFTWARE LICENSE TERMS
MICROSOFT WINDOWS SOFTWARE DEVELOPMENT KIT (SDK)
REDISTRIBUTABLE CODE

These license terms are an agreement between Microsoft Corporation (or based on where you live, one of its affiliates) and you. Please read them. They apply to the software named above, which includes the media on which you received it, if any.

REDISTRIBUTABLE CODE. The software contains code that you are permitted to distribute in programs you develop if you comply with the terms below.
a. Right to Use and Distribute. The code and text files listed below are "Redistributable Code."
- d3dcompiler_47.dll: You may copy and distribute the object code form of the DirectX HLSL shader compiler library.
b. Distribution Requirements. For any Redistributable Code you distribute, you must:
- add significant primary functionality to it in your programs;
- require distributors and external end users to agree to terms that protect it at least as much as this agreement;
- display your valid copyright notice on your programs; and
- indemnify, defend, and hold harmless Microsoft from any claims, including attorneys' fees, related to the distribution or use of your programs.
c. Distribution Restrictions. You may not:
- alter any copyright, trademark or patent notice in the Redistributable Code;
- use Microsoft's trademarks in your programs' names or in a way that suggests your programs come from or are endorsed by Microsoft;
- distribute Redistributable Code to run on a platform other than the Windows platform;
- include Redistributable Code in malicious, deceptive or unlawful programs; or
- modify or distribute the source code of any Redistributable Code so that any part of it becomes subject to an Excluded License.

DISCLAIMER OF WARRANTY. THE SOFTWARE IS LICENSED "AS-IS." YOU BEAR THE RISK OF USING IT. MICROSOFT GIVES NO EXPRESS WARRANTIES, GUARANTEES OR CONDITIONS.
LIMITATION ON AND EXCLUSION OF REMEDIES AND DAMAGES. YOU CAN RECOVER FROM MICROSOFT AND ITS SUPPLIERS ONLY DIRECT DAMAGES UP TO U.S. $5.00. YOU CANNOT RECOVER ANY OTHER DAMAGES, INCLUDING CONSEQUENTIAL, LOST PROFITS, SPECIAL, INDIRECT OR INCIDENTAL DAMAGES.
""",
    'xact': """MICROSOFT SOFTWARE LICENSE TERMS
MICROSOFT DIRECTX END USER RUNTIME

These license terms are an agreement between Microsoft Corporation (or based on where you live, one of its affiliates) and you. Please read them. They apply to the software named above, which includes the media on which you received it, if any. The terms also apply to any Microsoft updates, supplements, Internet-based services, and support services for this software, unless other terms accompany those items.

BY USING THE SOFTWARE, YOU ACCEPT THESE TERMS. IF YOU DO NOT ACCEPT THEM, DO NOT USE THE SOFTWARE.

If you comply with these license terms, you have the rights below:
1. INSTALLATION AND USE RIGHTS. You may install and use any number of copies of the software on your devices.
2. SCOPE OF LICENSE. The software is licensed, not sold. This agreement only gives you some rights to use the software. Microsoft reserves all other rights. Unless applicable law gives you more rights despite this limitation, you may use the software only as expressly permitted in this agreement. You may not:
- work around any technical limitations in the software;
- reverse engineer, decompile or disassemble the software, except and only to the extent that applicable law expressly permits, despite this limitation;
- make more copies of the software than specified in this agreement or allowed by applicable law, despite this limitation;
- publish the software for others to copy;
- rent, lease or lend the software; or
- transfer the software or this agreement to any third party.
3. BACKUP COPY. You may make one backup copy of the software. You may use it only to reinstall the software.
4. EXPORT RESTRICTIONS. The software is subject to United States export laws and regulations. You must comply with all domestic and international export laws and regulations that apply to the software.
5. DISCLAIMER OF WARRANTY. THE SOFTWARE IS LICENSED "AS-IS." YOU BEAR THE RISK OF USING IT. MICROSOFT GIVES NO EXPRESS WARRANTIES, GUARANTEES OR CONDITIONS. TO THE EXTENT PERMITTED UNDER YOUR LOCAL LAWS, MICROSOFT EXCLUDES THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
6. LIMITATION ON AND EXCLUSION OF REMEDIES AND DAMAGES. YOU CAN RECOVER FROM MICROSOFT AND ITS SUPPLIERS ONLY DIRECT DAMAGES UP TO U.S. $5.00. YOU CANNOT RECOVER ANY OTHER DAMAGES, INCLUDING CONSEQUENTIAL, LOST PROFITS, SPECIAL, INDIRECT OR INCIDENTAL DAMAGES.
""",
    'physx': """NVIDIA SOFTWARE LICENSE AGREEMENT
NVIDIA PHYSX SYSTEM SOFTWARE

This Software License Agreement ("Agreement") is between you (either an individual or an entity) and NVIDIA Corporation ("NVIDIA") and governs the use of the NVIDIA PhysX System Software and associated drivers and runtime components.

1. LICENSE GRANT. NVIDIA grants you a non-exclusive, non-transferable, revocable license to install and use the PhysX System Software on computers with supported operating systems for running applications and games utilizing NVIDIA PhysX technology.
2. RESTRICTIONS. You may not:
- modify, reverse engineer, decompile, or disassemble the software, except and only to the extent permitted by applicable law;
- distribute, rent, sub-license, lease, or lend the software to any third party;
- remove any proprietary notices or labels on the software;
- bypass any technical protection measures in the software.
3. OWNERSHIP. NVIDIA and its licensors own all title, copyright, and other intellectual property rights in the software. The software is licensed, not sold.
4. TERMINATION. This Agreement terminates automatically if you fail to comply with any of its terms and conditions. Upon termination, you must destroy all copies of the software.
5. DISCLAIMER OF WARRANTIES. THE SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, NVIDIA AND ITS SUPPLIERS DISCLAIM ALL WARRANTIES, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NONINFRINGEMENT.
6. LIMITATION OF LIABILITY. IN NO EVENT SHALL NVIDIA OR ITS SUPPLIERS BE LIABLE FOR ANY SPECIAL, INCIDENTAL, INDIRECT, OR CONSEQUENTIAL DAMAGES WHATSOEVER ARISING OUT OF THE USE OF OR INABILITY TO USE THE SOFTWARE, EVEN IF NVIDIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
"""
}


def validate_verb(verb):
    if verb not in COMPONENTS:
        raise ValueError(f'Unsupported component: {verb}')
    return verb


def is_admin():
    return os.getuid() == 0 or grp.getgrnam('sudo').gr_gid in (*os.getgroups(), os.getgid())


def protected_name(name):
    name = name.lower()
    return name.endswith('.dll') and (name[:-4] in PROTECTED or name.startswith(('msxml', 'mf')))


def fetch_vendor_terms(verb, directory=TERMS):
    """Acquire authentic vendor terms over network and cache under directory."""
    validate_verb(verb)
    headers = {'User-Agent': 'Wget/1.21.3 (linux-gnu)'}

    if verb == 'vcrun2022':
        url = COMPONENTS_METADATA[verb]['terms_url']
        req = urllib.request.Request(url, headers=headers)
        try:
            data = urllib.request.urlopen(req, timeout=25).read()
        except urllib.error.URLError as err:
            raise ConnectionError(f'Failed to retrieve vendor terms for {verb} from {url}: {err.reason}')
        try:
            with zipfile.ZipFile(io.BytesIO(data)) as zf:
                xml_content = zf.read('word/document.xml')
            tree = ET.fromstring(xml_content)
            paragraphs = []
            ns = {'w': 'http://schemas.openxmlformats.org/wordprocessingml/2006/main'}
            for p in tree.iterfind('.//w:p', ns):
                texts = [node.text for node in p.iterfind('.//w:t', ns) if node.text]
                if texts:
                    paragraphs.append(''.join(texts))
            text = '\n\n'.join(paragraphs)
        except Exception as parse_err:
            raise ValueError(f'Failed to extract authentic terms from {verb} vendor document: {parse_err}')
    elif verb in ('corefonts', 'tahoma'):
        url = COMPONENTS_METADATA[verb]['terms_url']
        req = urllib.request.Request(url, headers=headers)
        try:
            data = urllib.request.urlopen(req, timeout=25).read()
            text = data.decode('utf-8', errors='replace')
        except urllib.error.URLError as err:
            raise ConnectionError(f'Failed to retrieve vendor terms for {verb} from {url}: {err.reason}')
    elif verb in _VENDOR_TERMS_TEXT:
        text = _VENDOR_TERMS_TEXT[verb]
    else:
        raise ValueError(f'No terms acquisition recipe defined for {verb}')

    if len(text.strip()) < 500:
        raise ValueError(f'Retrieved terms for {verb} too short ({len(text.strip())} chars); full vendor terms required')

    # If running as root, cache into directory securely
    if os.getuid() == 0:
        directory.mkdir(parents=True, exist_ok=True)
        for entry in (directory, *directory.parents):
            if entry.is_symlink() or entry.stat().st_uid != 0 or entry.stat().st_mode & 0o022:
                raise ValueError(f'Insecure vendor license path: {entry}')
        target = directory / (verb + '.txt')
        temp_file = directory / (verb + '.tmp')
        temp_file.write_text(text, encoding='utf-8')
        os.chmod(temp_file, 0o644)
        os.replace(temp_file, target)

    sha = hashlib.sha256(text.encode('utf-8')).hexdigest()
    return text, sha


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

    # In win64/wow64 prefixes, ensure regedit exists in system dirs for winetricks
    regedit_src = MACHINE / 'drive_c/windows/regedit.exe'
    if regedit_src.is_file():
        for d in ('system32', 'syswow64'):
            tgt = MACHINE / f'drive_c/windows/{d}/regedit.exe'
            if not tgt.exists() and not tgt.is_symlink():
                try:
                    tgt.symlink_to('../regedit.exe')
                except OSError:
                    pass

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


def get_installed_receipts(machine=MACHINE):
    receipts_dir = machine.parent / 'component-receipts'
    if not receipts_dir.is_dir():
        return set()
    installed = set()
    for verb in COMPONENTS:
        receipt_file = receipts_dir / (verb + '.sha256')
        if receipt_file.is_file():
            installed.add(verb)
    return installed


def consent(verb, gui):
    try:
        text, sha = license_text(verb)
    except ValueError:
        text, sha = fetch_vendor_terms(verb)
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
        installed = get_installed_receipts()
        for verb, name in COMPONENTS.items():
            checked = 'TRUE' if verb in installed else 'FALSE'
            command.extend([checked, verb, name])
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


def oobe_coordinator():
    """Privileged coordinator daemon during Setup Out-of-Box Experience."""
    if os.getuid() != 0:
        raise PermissionError('OOBE coordinator requires root execution')
    stage_file_str = os.environ.get('WEX_OOBE_STAGE_FILE', '/etc/wineexplorer/setup/stage')
    stage_file = Path(stage_file_str)
    if not stage_file.is_file() or stage_file.read_text(encoding='ascii').strip() != 'oobe':
        raise PermissionError('OOBE coordinator only permitted during active OOBE stage')

    sock_path_str = os.environ.get('WEX_OOBE_SOCKET', '/run/wex-oobe/coordinator.sock')
    sock_path = Path(sock_path_str)
    sock_path.parent.mkdir(parents=True, exist_ok=True)
    os.chmod(sock_path.parent, 0o755)
    if sock_path.exists():
        sock_path.unlink()

    try:
        wexsetup_uid = pwd.getpwnam('wexsetup').pw_uid
    except KeyError:
        wexsetup_uid = None

    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(str(sock_path))
    os.chmod(str(sock_path), 0o666)
    server.listen(5)
    server.settimeout(2.0)

    running = True
    def stop_handler(signum, frame):
        nonlocal running
        running = False
    try:
        signal.signal(signal.SIGTERM, stop_handler)
        signal.signal(signal.SIGINT, stop_handler)
    except ValueError:
        pass

    print('OOBE coordinator listening on', sock_path, file=sys.stderr)

    while running:
        if not stage_file.is_file() or stage_file.read_text(encoding='ascii').strip() != 'oobe':
            print('OOBE stage expired; coordinator shutting down.', file=sys.stderr)
            break
        try:
            conn, _ = server.accept()
        except socket.timeout:
            continue
        except OSError:
            break

        try:
            # Enforce peer credentials: only wexsetup (or root in tests)
            creds = conn.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, struct.calcsize('3i'))
            pid, uid, gid = struct.unpack('3i', creds)
            if wexsetup_uid is not None and uid != wexsetup_uid and uid != 0:
                print(f'Rejecting unauthorized client UID {uid}', file=sys.stderr)
                conn.close()
                continue

            # Read single request line
            file_in = conn.makefile('r', encoding='utf-8')
            line = file_in.readline()
            if not line:
                conn.close()
                continue

            parts = line.strip().split()
            cmd = parts[0] if parts else ''

            if cmd == 'GET_COMPONENTS':
                installed = get_installed_receipts()
                lines = ['OK']
                for v, meta in COMPONENTS_METADATA.items():
                    inst = '1' if v in installed else '0'
                    reb = '1' if meta['reboot'] else '0'
                    # Format: verb \t title \t publisher \t description \t size \t reboot \t installed
                    row = f"{v}\t{meta['title']}\t{meta['publisher']}\t{meta['description']}\t{meta['size']}\t{reb}\t{inst}"
                    lines.append(row)
                conn.sendall(('\n'.join(lines) + '\n').encode('utf-8'))

            elif cmd == 'GET_TERMS' and len(parts) >= 2:
                verb = parts[1]
                try:
                    validate_verb(verb)
                    try:
                        text, sha = license_text(verb)
                    except ValueError:
                        text, sha = fetch_vendor_terms(verb)
                    response = f"OK {sha} {len(text.encode('utf-8'))}\n{text}"
                    conn.sendall(response.encode('utf-8'))
                except Exception as err:
                    err_msg = f"ERR {err}\n"
                    conn.sendall(err_msg.encode('utf-8'))

            elif cmd == 'INSTALL' and len(parts) >= 3:
                verb, accepted_sha = parts[1], parts[2]
                try:
                    validate_verb(verb)
                    install(verb, accepted_sha)
                    conn.sendall(b"OK\n")
                except Exception as err:
                    conn.sendall(f"ERR {err}\n".encode('utf-8'))

            elif cmd == 'GET_RECEIPTS':
                installed = get_installed_receipts()
                conn.sendall(('OK ' + ' '.join(installed) + '\n').encode('utf-8'))

            else:
                conn.sendall(b"ERR Unknown or malformed command\n")

        except Exception as err:
            print('Coordinator client error:', err, file=sys.stderr)
        finally:
            try:
                file_in.close()
            except Exception:
                pass
            try:
                conn.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            try:
                conn.close()
            except OSError:
                pass

    try:
        server.close()
    except OSError:
        pass
    sock_path.unlink(missing_ok=True)
    print('OOBE coordinator stopped.', file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', nargs=2, metavar=('VERB', 'ACCEPTED_SHA256'), help=argparse.SUPPRESS)
    parser.add_argument('--oobe-coordinator', action='store_true', help='Run OOBE setup coordinator daemon')
    parser.add_argument('action', nargs='?', choices=['install', 'list'])
    parser.add_argument('verbs', nargs='*')
    args = parser.parse_args()
    if args.oobe_coordinator:
        oobe_coordinator()
    elif args.install:
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
