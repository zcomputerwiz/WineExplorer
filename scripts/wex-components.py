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
import json
import os
from pathlib import Path
import pwd
import re
import shutil
import signal
import socket
import stat
import struct
import subprocess
import sys
import tempfile
import uuid

OPERATION_STATE = Path('/var/lib/wineexplorer/component-operations')


def active_oobe_stage(path):
    try:
        info = path.lstat()
        parent = path.parent.lstat()
        return (stat.S_ISDIR(parent.st_mode) and parent.st_uid == 0 and
                not parent.st_mode & 0o022 and stat.S_ISREG(info.st_mode) and
                info.st_uid == 0 and not info.st_mode & 0o022 and
                path.read_text(encoding='ascii').strip() == 'oobe')
    except (OSError, UnicodeError):
        return False
import threading
import time
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
        'disabled': True,
        'disabled_reason': 'Upstream mutable URL divergence in winetricks recipe (aka.ms hash mismatch)',
        'terms_url': 'https://visualstudio.microsoft.com/wp-content/uploads/2021/09/Visual-C-Runtime-2015-2022-License-1.docx',
        'terms_sha256': 'f1e3d56ceb2ad68aae0711b910375009e651ac5530fa0760f0dea6e81e54fae1',
        'terms_text_sha256': '993abe4df09446971c7a72f0b5c6cd1f0f69e57d574a2c9729582438708f3df0',
        'recipe_files': [
            ('vc_redist.x86.exe', 'dd1a8be03398367745a87a5e35bebdab00fdad080cf42af0c3f20802d08c25d4'),
        ],
    },
    'corefonts': {
        'title': 'Microsoft TrueType Core Fonts',
        'publisher': 'Microsoft Corporation',
        'description': 'Installs original Microsoft TrueType core fonts (Arial, Times New Roman, Courier New, Georgia, Verdana, Comic Sans, Trebuchet, Impact, Andale Mono, Webdings).',
        'size': '~4 MB',
        'reboot': False,
        'disabled': True,
        'disabled_reason': 'Retired upstream by vendor; standalone terms lack first-party vendor provenance',
        'terms_url': None,
        'recipe_files': [
            ('andale32.exe', '0524fe42951237501f3a00e620ecc7446b404f05d166b24597746044a40ec974'),
            ('arial32.exe', '85288a75e478546114eb307406a6cc0536c46a6f0fb32230fed84820eb7706d7'),
            ('comic32.exe', '9c60703a20d84b22acc9d2729e79a9e2cb273b54fa399d00355799138f036025'),
            ('courie32.exe', 'bb511d616912461863174bd3787e37e1aa18d581203a4342d0775036e9f310e0'),
            ('georgi32.exe', '2c00ff85019cfae5bf208910547ad3745a706785833246ebec532c2957f82666'),
            ('impact32.exe', '5e714d521d15e71a473f2759146f411e749294247514a60ea54f5979eb170c9a'),
            ('times32.exe', 'db712061555543cfd720b0800b467d3e4c495cb21ccb5d92131aa1192518e874'),
            ('trebuc32.exe', '5a690d9bb85db97cf473804603099d2b090738349f3fa977156096366d45f9de'),
            ('verdan32.exe', '120c24fb89cf62a7df8e08d660e5fae5ad1935c13e11500f135b1d9bf896deec'),
            ('webdin32.exe', '64585ac6a50c291ab2618ad66c5514d150479228eb97e37be8d115ec7192d345'),
        ],
    },
    'tahoma': {
        'title': 'Microsoft Tahoma font',
        'publisher': 'Microsoft Corporation',
        'description': 'Installs Microsoft Tahoma and Tahoma Bold TrueType fonts from the official Microsoft Internet Explorer font package.',
        'size': '~410 KB',
        'reboot': False,
        'disabled': True,
        'disabled_reason': 'IE language pack payload terms require extraction from redistributable; no standalone vendor terms URL',
        'terms_url': None,
        'recipe_files': [
            ('IELPKTH.CAB', 'c1be3fb8f0042570be76ec6daa03a99142c88367c1bc810240b85827c715961a'),
        ],
    },
    'd3dcompiler_47': {
        'title': 'DirectX Shader Compiler (d3dcompiler_47)',
        'publisher': 'Microsoft Corporation',
        'description': 'DirectX HLSL shader compiler library required by DirectX 11, DirectX 12, and Vulkan translation layers (DXVK/VKD3D). Wine builtin compiler remains default.',
        'size': '~4.5 MB',
        'reboot': False,
        'disabled': True,
        'disabled_reason': 'Redistributable package lacks a standalone first-party vendor terms URL',
        'terms_url': None,
        'recipe_files': [
            ('d3dcompiler_47_32.dll', '2ad0d4987fc4624566b190e747c9d95038443956ed816abfd1e2d389b5ec0851'),
            ('d3dcompiler_47.dll', '4432bbd1a390874f3f0a503d45cc48d346abc3a8c0213c289f4b615bf0ee84f3'),
        ],
    },
    'xact': {
        'title': 'Microsoft XACT audio engine',
        'publisher': 'Microsoft Corporation',
        'description': 'Cross-Platform Audio Creation Tool (XACT) runtime libraries for game audio.',
        'size': '~95 MB',
        'reboot': False,
        'disabled': True,
        'disabled_reason': 'Vendor terms are embedded inside redistributable and cannot be acquired prior to consent',
        'terms_url': None,
        'recipe_files': [
            ('directx_Jun2010_redist.exe', '8746ee1a84a083a90e37899d71d50d5c7c015e69688a466aa80447f011780c0d'),
        ],
    },
    'physx': {
        'title': 'NVIDIA PhysX System Software',
        'publisher': 'NVIDIA Corporation',
        'description': 'Physics simulation engine software required by PhysX-enabled games.',
        'size': '~27 MB',
        'reboot': False,
        'disabled': True,
        'disabled_reason': 'Marked broken upstream in winetricks (WineHQ bug 56606)',
        'terms_url': None,
        'recipe_files': [
            ('PhysX_9.21.0713_SystemSoftware.exe', '26d62c5c347c15cb27c3be92bf10706113511b48b28aecc09f61ee58b3b62778'),
        ],
    },
}

TERMS = Path('/etc/wineexplorer/component-licenses')
MACHINE = Path('/var/lib/wineexplorer/machine')
CACHE = Path('/var/cache/winetricks')
BACKEND = '/usr/local/bin/wex-components'
WINETRICKS_PATH = Path('/usr/bin/winetricks')
APPROVED_WINETRICKS_VERSION = '20250102'
APPROVED_WINETRICKS_SHA256 = '9df4af038f04c753f3b0b02817cf60a51d8eac32357d070c45dfd139b93bd3f5'

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


def validate_winetricks_integrity(path=None):
    winetricks = path or WINETRICKS_PATH
    if not winetricks.is_file():
        raise ValueError(f'Winetricks executable missing at {winetricks}')
    if winetricks.is_symlink():
        raise ValueError('Refusing symlinked winetricks executable')
    stat = winetricks.stat()
    if stat.st_uid != 0 and (os.getuid() == 0 or stat.st_uid != os.getuid()):
        raise ValueError('Winetricks executable must be root-owned and not writable by others')
    if stat.st_mode & 0o022:
        raise ValueError('Winetricks executable must not be writable by group or others')
    digest = hashlib.sha256(winetricks.read_bytes()).hexdigest()
    if digest != APPROVED_WINETRICKS_SHA256:
        raise ValueError(f'Winetricks integrity violation: expected {APPROVED_WINETRICKS_SHA256}, got {digest}')


def validate_recipe_files(verb, path=None):
    winetricks = path or WINETRICKS_PATH
    meta = COMPONENTS_METADATA.get(verb, {})
    expected_files = meta.get('recipe_files', [])
    if not expected_files:
        raise ValueError(f'No approved recipe files configured for {verb}')
    content = winetricks.read_text(encoding='utf-8', errors='replace')
    for fname, sha in expected_files:
        if fname not in content or sha not in content:
            raise ValueError(f'Recipe integrity violation: {verb} requires {fname} with hash {sha}')


def is_admin():
    return os.getuid() == 0 or grp.getgrnam('sudo').gr_gid in (*os.getgroups(), os.getgid())


def protected_name(name):
    name = name.lower()
    return name.endswith('.dll') and (name[:-4] in PROTECTED or name.startswith(('msxml', 'mf')))


MAX_TERMS_BYTES = 2 * 1024 * 1024


def component_identity(verb):
    record = {'schema': 1, 'verb': validate_verb(verb),
              'metadata': COMPONENTS_METADATA[verb],
              'winetricks_sha256': APPROVED_WINETRICKS_SHA256,
              'architecture': 'wow64'}
    return hashlib.sha256(json.dumps(record, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def atomic_json(path, data, mode=0o600):
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=path.parent,
                                         prefix='.' + path.name, delete=False) as out:
            temporary = Path(out.name)
            os.fchmod(out.fileno(), mode)
            json.dump(data, out, sort_keys=True, indent=2)
            out.write('\n')
            out.flush()
            os.fsync(out.fileno())
        os.replace(temporary, path)
        descriptor = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def extract_docx_terms(data):
    """Render supported Word paragraphs/lists/links; reject unhandled content.

    Only independently hash-pinned documents reach this parser in production.
    No proprietary document contents are embedded in the program.
    """
    w = '{http://schemas.openxmlformats.org/wordprocessingml/2006/main}'
    rel = '{http://schemas.openxmlformats.org/officeDocument/2006/relationships}'
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        names = archive.namelist()
        if len(set(names)) != len(names) or sum(i.file_size for i in archive.infolist()) > MAX_TERMS_BYTES:
            raise ValueError('Oversized or ambiguous terms document')
        document = ET.fromstring(archive.read('word/document.xml'))
        if any(document.find('.//' + w + tag) is not None for tag in
               ('altChunk', 'object', 'drawing', 'del', 'fldSimple', 'instrText', 'footnoteReference', 'endnoteReference')):
            raise ValueError('Terms contain unsupported embedded or conditional content')
        # Do not silently discard substantive text in auxiliary document parts.
        for name in names:
            if re.fullmatch(r'word/(?:header\d+|footer\d+|footnotes|endnotes)\.xml', name):
                part = ET.fromstring(archive.read(name))
                if any((node.text or '').strip() for node in part.iter(w + 't')):
                    raise ValueError('Terms contain auxiliary text requiring explicit review')
        links = {}
        if 'word/_rels/document.xml.rels' in names:
            for item in ET.fromstring(archive.read('word/_rels/document.xml.rels')):
                if item.get('TargetMode') == 'External':
                    if not item.get('Type', '').endswith('/hyperlink'):
                        raise ValueError('Unsupported external terms dependency')
                    url = item.get('Target', '')
                    if not url.startswith(('https://', 'http://')):
                        raise ValueError('Unsupported terms hyperlink')
                    links[item.get('Id')] = url
        numbering = ET.fromstring(archive.read('word/numbering.xml')) if 'word/numbering.xml' in names else None
        abstracts, numbers, counters = {}, {}, {}
        if numbering is not None:
            abstracts = {n.get(w + 'abstractNumId'): n for n in numbering.findall(w + 'abstractNum')}
            for number in numbering.findall(w + 'num'):
                if number.find(w + 'lvlOverride') is not None:
                    raise ValueError('Numbering overrides require explicit review')
                abstract = number.find(w + 'abstractNumId')
                if abstract is not None:
                    numbers[number.get(w + 'numId')] = abstract.get(w + 'val')
        paragraphs = []
        styles = {}
        if 'word/styles.xml' in names:
            styles = {s.get(w + 'styleId'): s for s in
                      ET.fromstring(archive.read('word/styles.xml')).findall(w + 'style')}

        def list_properties(properties, seen=()):
            result = {}
            if properties is None:
                return result
            style = properties.find(w + 'pStyle')
            if style is not None:
                style_id = style.get(w + 'val')
                if style_id in seen or len(seen) > 16:
                    raise ValueError('Cyclic legal document style')
                definition = styles.get(style_id)
                if definition is not None:
                    base = definition.find(w + 'basedOn')
                    if base is not None:
                        inherited = ET.Element(w + 'pPr')
                        ET.SubElement(inherited, w + 'pStyle', {w + 'val': base.get(w + 'val', '')})
                        result.update(list_properties(inherited, (*seen, style_id)))
                    result.update(list_properties(definition.find(w + 'pPr'), (*seen, style_id)))
            num = properties.find(w + 'numPr')
            if num is not None:
                for field in ('numId', 'ilvl'):
                    item = num.find(w + field)
                    if item is not None:
                        result[field] = item.get(w + 'val')
            return result

        def format_counter(value, fmt):
            if value < 1 or value > 3999:
                raise ValueError('Unsupported legal numbering range')
            if fmt == 'decimal':
                return str(value)
            if fmt in ('lowerLetter', 'upperLetter'):
                marker = ''
                while value:
                    value, digit = divmod(value - 1, 26)
                    marker = chr(ord('a') + digit) + marker
                return marker.upper() if fmt == 'upperLetter' else marker
            if fmt in ('lowerRoman', 'upperRoman'):
                marker = ''
                for amount, symbol in ((1000, 'M'), (900, 'CM'), (500, 'D'), (400, 'CD'),
                                       (100, 'C'), (90, 'XC'), (50, 'L'), (40, 'XL'),
                                       (10, 'X'), (9, 'IX'), (5, 'V'), (4, 'IV'), (1, 'I')):
                    count, value = divmod(value, amount)
                    marker += symbol * count
                return marker.lower() if fmt == 'lowerRoman' else marker
            raise ValueError('Unsupported legal list format')

        for paragraph in document.iter(w + 'p'):
            pieces = []
            for node in paragraph.iter():
                if node.tag == w + 't':
                    pieces.append(node.text or '')
                elif node.tag == w + 'tab':
                    pieces.append('\t')
                elif node.tag in (w + 'br', w + 'cr'):
                    pieces.append('\n')
                elif node.tag == w + 'hyperlink':
                    link_id = node.get(rel + 'id')
                    if link_id and link_id not in links:
                        raise ValueError('Unresolved document hyperlink')
            text = ''.join(pieces)
            for link in paragraph.iter(w + 'hyperlink'):
                url = links.get(link.get(rel + 'id'))
                if url and url not in text:
                    text += ' [' + url + ']'
            properties = paragraph.find(w + 'pPr')
            num = list_properties(properties)
            if num:
                num_id = num.get('numId', '0')
                if num_id != '0':
                    level_id = num.get('ilvl', '0')
                    depth = int(level_id)
                    abstract = abstracts.get(numbers.get(num_id))
                    level = next((l for l in abstract.findall(w + 'lvl') if l.get(w + 'ilvl') == level_id), None) if abstract is not None else None
                    if level is None or not 0 <= depth <= 8:
                        raise ValueError('Unsupported legal list nesting')
                    fmt, label = level.find(w + 'numFmt'), level.find(w + 'lvlText')
                    if fmt is None or label is None:
                        raise ValueError('Incomplete legal list numbering')
                    if fmt.get(w + 'val') == 'bullet':
                        prefix = '\u2022 '
                    else:
                        start = level.find(w + 'start')
                        key = (num_id, depth)
                        counters[key] = counters.get(key, int(start.get(w + 'val', '1')) - 1 if start is not None else 0) + 1
                        for deeper in range(depth + 1, 9):
                            counters.pop((num_id, deeper), None)
                        if level.find(w + 'lvlRestart') is not None:
                            raise ValueError('Custom list restart requires explicit review')
                        def replace_level(match):
                            referenced = int(match.group(1)) - 1
                            ref_level = next((l for l in abstract.findall(w + 'lvl') if l.get(w + 'ilvl') == str(referenced)), None)
                            if ref_level is None or (num_id, referenced) not in counters:
                                raise ValueError('Unresolved parent legal list counter')
                            ref_fmt = ref_level.find(w + 'numFmt')
                            return format_counter(counters[(num_id, referenced)], ref_fmt.get(w + 'val', ''))
                        prefix = re.sub(r'%([1-9])', replace_level, label.get(w + 'val', '')) + ' '
                    text = prefix + text
            if text.strip():
                paragraphs.append(text)
        text = '\n\n'.join(paragraphs) + '\n'
        if len(text.strip()) < 500 or len(text.encode('utf-8')) > MAX_TERMS_BYTES or '\x00' in text:
            raise ValueError('Incomplete or oversized extracted terms')
        return text


def fetch_vendor_terms(verb, directory=TERMS):
    """Acquire authentic vendor terms over network and cache under directory."""
    validate_verb(verb)
    meta = COMPONENTS_METADATA.get(verb, {})
    if meta.get('disabled'):
        raise ValueError(f'{verb} is disabled: {meta.get("disabled_reason")}')

    target = directory / (verb + '.json')
    if target.is_file():
        return license_text(verb, directory)

    terms_url = meta.get('terms_url')
    if not terms_url:
        raise FileNotFoundError(f'Authentic vendor terms for {verb} must be provisioned in {directory}/{verb}.txt')
    if not terms_url.startswith('https://') or not meta.get('terms_sha256') or not meta.get('terms_text_sha256'):
        raise ValueError('No reviewed document and extraction identity for ' + verb)

    headers = {'User-Agent': 'Wget/1.21.3 (linux-gnu)'}
    req = urllib.request.Request(terms_url, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=10) as response:
            if not response.url.startswith('https://'):
                raise ValueError('Insecure vendor document redirect')
            data = response.read(MAX_TERMS_BYTES + 1)
    except urllib.error.URLError as err:
        raise ConnectionError(f'Failed to retrieve vendor terms for {verb} from {terms_url}: {err.reason}')

    if len(data) > MAX_TERMS_BYTES or hashlib.sha256(data).hexdigest() != meta['terms_sha256']:
        raise ValueError('Vendor document changed or exceeded its size limit; review required')
    if not terms_url.endswith('.docx'):
        raise ValueError('Unreviewed vendor document format')
    text = extract_docx_terms(data)
    if hashlib.sha256(text.encode()).hexdigest() != meta['terms_text_sha256']:
        raise ValueError('Terms extraction changed; full document review required')

    if len(text.strip()) < 500:
        raise ValueError(f'Retrieved terms for {verb} too short ({len(text.strip())} chars); full vendor terms required')

    # If running as root, cache into directory securely
    if os.getuid() == 0:
        directory.mkdir(parents=True, exist_ok=True)
        for entry in (directory, *directory.parents):
            if entry.is_symlink() or entry.stat().st_uid != 0 or entry.stat().st_mode & 0o022:
                raise ValueError(f'Insecure vendor license path: {entry}')
        atomic_json(target, {'schema': 1, 'component_identity': component_identity(verb),
                             'document_sha256': meta['terms_sha256'], 'text': text}, 0o644)

    sha = hashlib.sha256((component_identity(verb) + '\n' + text).encode('utf-8')).hexdigest()
    return text, sha


def prepare_terms(verb, directory=TERMS):
    """Privileged action: fetch and cache authentic vendor terms into root TERMS dir."""
    validate_verb(verb)
    if os.getuid() != 0:
        raise PermissionError('Preparing vendor terms requires root elevation')
    meta = COMPONENTS_METADATA.get(verb, {})
    if meta.get('disabled'):
        raise ValueError(f'{verb} is disabled: {meta.get("disabled_reason")}')
    target = directory / (verb + '.json')
    if target.is_file():
        return license_text(verb, directory)
    return fetch_vendor_terms(verb, directory)


def license_text(verb, directory=TERMS):
    path = directory / (validate_verb(verb) + '.json')
    if not path.is_file():
        raise ValueError(f'{verb} terms have not been prepared. Choose Review terms; no payload was downloaded.')
    # No caller-controlled text or replaceable ancestor can authorize a root
    # download. The installed directory and notices must be root-owned.
    for entry in (path, *path.parents):
        if entry.is_symlink() or entry.stat().st_uid != 0 or entry.stat().st_mode & 0o022:
            raise ValueError(f'Insecure vendor license path: {entry}')
    if path.stat().st_size > MAX_TERMS_BYTES:
        raise ValueError('Oversized cached terms')
    record = json.loads(path.read_text(encoding='utf-8'))
    meta = COMPONENTS_METADATA[verb]
    text = record.get('text', '')
    if (record.get('schema') != 1 or record.get('component_identity') != component_identity(verb) or
            record.get('document_sha256') != meta.get('terms_sha256') or
            hashlib.sha256(text.encode()).hexdigest() != meta.get('terms_text_sha256')):
        raise ValueError('Cached document/package identity changed; review required')
    if len(text.strip()) < 500 or '\x00' in text:
        raise ValueError(f'Full vendor terms required for {verb}')
    return text, hashlib.sha256((component_identity(verb) + '\n' + text).encode()).hexdigest()


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
        recovery = {'safe_to_restore': True}
        try:
            yield recovery
        finally:
            if not recovery['safe_to_restore']:
                marker = machine.parent / 'component-recovery-required'
                marker.write_text(str(saved) + '\n', encoding='utf-8')
                raise RuntimeError('Cannot establish an idle machine prefix; protected snapshot retained; administrator recovery required: ' + str(saved))
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


def is_installed(verb, accepted_digest=None, machine=MACHINE):
    receipts_dir = machine.parent / 'component-receipts'
    json_file = receipts_dir / (verb + '.json')
    if json_file.is_file():
        try:
            data = json.loads(json_file.read_text(encoding='utf-8'))
            if (data.get('verb') == verb and
                data.get('winetricks_sha256') == APPROVED_WINETRICKS_SHA256 and
                data.get('winetricks_version') == APPROVED_WINETRICKS_VERSION and
                data.get('architecture') == 'wow64' and
                data.get('recipe_files') == [list(item) for item in COMPONENTS_METADATA[verb]['recipe_files']] and
                (accepted_digest is None or data.get('terms_sha256') == accepted_digest)):
                return True
        except Exception:
            pass
    # Legacy text hashes lack package/recipe/architecture identity.
    return False


def get_installed_receipts(machine=MACHINE):
    receipts_dir = machine.parent / 'component-receipts'
    if not receipts_dir.is_dir():
        return set()
    installed = set()
    for verb in COMPONENTS:
        if is_installed(verb, machine=machine):
            installed.add(verb)
    return installed


def install(verb, accepted_digest):
    validate_verb(verb)
    meta = COMPONENTS_METADATA.get(verb, {})
    if meta.get('disabled'):
        raise ValueError(f'{verb} is disabled: {meta.get("disabled_reason")}')
    if os.getuid() != 0:
        raise ValueError('The installer backend requires authenticated elevation')
    validate_winetricks_integrity()
    validate_recipe_files(verb)
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
        if (MACHINE.parent / 'component-recovery-required').exists():
            raise ValueError('Machine prefix requires administrator recovery before another installation')
        if is_installed(verb, accepted_digest, machine=MACHINE):
            return
        # Do not kill somebody else's maintenance client or wait for a desktop.
        subprocess.run(['/usr/local/bin/wineserver', '-w'], env=environment,
                       timeout=2, check=True)
        regedit_src = MACHINE / 'drive_c/windows/regedit.exe'
        if regedit_src.is_file():
            for directory in ('system32', 'syswow64'):
                target = MACHINE / f'drive_c/windows/{directory}/regedit.exe'
                if not target.exists() and not target.is_symlink():
                    target.symlink_to('../regedit.exe')
        cache = CACHE
        if cache.is_symlink():
            raise ValueError('Refusing symlinked component cache')
        cache.mkdir(mode=0o700, exist_ok=True)
        if cache.stat().st_uid != 0 or cache.stat().st_mode & 0o022:
            raise ValueError('Component cache must be root-owned and not writable by others')
        # Prefix ownership and default ACLs stay intact: do not recursively
        # chown live user prefixes. Record the real verb result, never success
        # from a progress-dialog pipeline. All package terms were accepted above.
        with protect_builtins(MACHINE, environment) as recovery:
            if recovery is not None:
                recovery['safe_to_restore'] = False
            # timeout owns a separate process group (do not use --foreground).
            # Keep the lock and builtin snapshots until Wine has also stopped.
            result = subprocess.run(['/usr/bin/timeout', '--signal=TERM',
                                     '--kill-after=10s', '1800s',
                                     '/usr/bin/xvfb-run', '-a', '/usr/bin/winetricks',
                                     '-q', verb], env=environment)
            try:
                if result.returncode in (124, 137):
                    subprocess.run(['/usr/local/bin/wineserver', '-k'],
                                   env=environment, timeout=15, check=True)
                subprocess.run(['/usr/local/bin/wineserver', '-w'], env=environment,
                               timeout=120, check=True)
            except subprocess.TimeoutExpired:
                subprocess.run(['/usr/local/bin/wineserver', '-k'],
                               env=environment, timeout=15, check=True)
                subprocess.run(['/usr/local/bin/wineserver', '-w'],
                               env=environment, timeout=30, check=True)
                if recovery is not None:
                    recovery['safe_to_restore'] = True
                raise ValueError('Installer did not finish; machine-prefix Wine stopped; installation may be partial')
            if recovery is not None:
                recovery['safe_to_restore'] = True
        if result.returncode:
            raise ValueError(f'{verb} failed (exit {result.returncode}); installation may be partial; see console output')
        receipt = MACHINE.parent / 'component-receipts'
        receipt.mkdir(mode=0o700, exist_ok=True)
        receipt_data = {
            'verb': verb,
            'title': meta.get('title', verb),
            'sha256': current,
            'terms_sha256': current,
            'winetricks_version': APPROVED_WINETRICKS_VERSION,
            'winetricks_sha256': APPROVED_WINETRICKS_SHA256,
            'architecture': 'wow64',
            'installed_at': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
            'recipe_files': meta.get('recipe_files', []),
        }
        # Publish complete records atomically; a truncated JSON is not success.
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=receipt,
                                         prefix='.' + verb, delete=False) as out:
            temporary_receipt = Path(out.name)
            json.dump(receipt_data, out, indent=2)
            out.write('\n')
            out.flush()
            os.fsync(out.fileno())
        try:
            os.replace(temporary_receipt, receipt / (verb + '.json'))
        finally:
            temporary_receipt.unlink(missing_ok=True)
        (receipt / (verb + '.sha256')).write_text(current + '\n', encoding='ascii')


def consent(verb, gui):
    validate_verb(verb)
    meta = COMPONENTS_METADATA.get(verb, {})
    if meta.get('disabled'):
        raise ValueError(f'{verb} is disabled: {meta.get("disabled_reason")}')
    try:
        text, sha = license_text(verb)
    except ValueError:
        if not is_admin():
            raise ValueError('Administrator elevation required to prepare vendor terms')
        command = [BACKEND, '--prepare-terms', verb]
        if os.getuid() != 0:
            command.insert(0, '/usr/bin/pkexec')
        res = subprocess.run(command)
        if res.returncode != 0:
            raise ValueError(f'Failed to prepare authentic vendor terms for {verb}')
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
        meta = COMPONENTS_METADATA.get(verb, {})
        if meta.get('disabled'):
            raise ValueError(f'{verb} is disabled: {meta.get("disabled_reason")}')
    if gui:
        command = ['zenity', '--list', '--checklist', '--title=Windows Components',
                   '--column=Install', '--column=Component', '--column=Description',
                   '--print-column=2', '--separator=|', '--width=720', '--height=360']
        installed = get_installed_receipts()
        for verb, name in COMPONENTS.items():
            checked = 'TRUE' if verb in installed else 'FALSE'
            meta = COMPONENTS_METADATA[verb]
            description = name
            if meta.get('disabled'):
                description += ' [Unavailable: ' + meta.get('disabled_reason', 'verification pending') + ']'
                checked = 'FALSE'
            command.extend([checked, verb, description])
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode or not result.stdout.strip():
            return
        verbs = [verb for verb in result.stdout.strip().split('|') if verb]
        if not verbs:
            return
    for verb in verbs:
        validate_verb(verb)
        meta = COMPONENTS_METADATA.get(verb, {})
        if meta.get('disabled'):
            raise ValueError(f'{verb} is disabled: {meta.get("disabled_reason")}')
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
    stage_file_str = os.environ.get('WEX_OOBE_STAGE_FILE', '/run/wex-oobe/stage')
    stage_file = Path(stage_file_str)
    if not active_oobe_stage(stage_file):
        raise PermissionError('OOBE coordinator only permitted during active OOBE stage')

    sock_path_str = os.environ.get('WEX_OOBE_SOCKET', '/run/wex-oobe/coordinator.sock')
    sock_path = Path(sock_path_str)
    sock_path.parent.mkdir(parents=True, exist_ok=True)
    parent_info = sock_path.parent.lstat()
    if not stat.S_ISDIR(parent_info.st_mode) or parent_info.st_uid != 0 or parent_info.st_mode & 0o022:
        raise PermissionError('OOBE endpoint directory must be root-owned and not writable by others')
    os.chmod(sock_path.parent, 0o755)
    if sock_path.exists():
        sock_path.unlink()

    try:
        wexsetup_uid = pwd.getpwnam('wexsetup').pw_uid
    except KeyError:
        raise RuntimeError('Required setup account "wexsetup" does not exist')

    running = True
    active_operations = {}
    operations_lock = threading.Lock()
    OPERATION_STATE.mkdir(mode=0o700, parents=True, exist_ok=True)
    state_info = OPERATION_STATE.lstat()
    if not stat.S_ISDIR(state_info.st_mode) or state_info.st_uid != 0 or state_info.st_mode & 0o077:
        raise PermissionError('OOBE operation directory must be private and root-owned')
    journal = OPERATION_STATE / 'operations.json'

    def save_operations():
        records = {key: {k: v for k, v in value.items() if k != 'thread'}
                   for key, value in active_operations.items()}
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8',
                                         dir=OPERATION_STATE, delete=False) as out:
            temporary = Path(out.name)
            json.dump(records, out)
            out.flush()
            os.fsync(out.fileno())
        try:
            os.replace(temporary, journal)
        finally:
            temporary.unlink(missing_ok=True)

    if journal.exists():
        if journal.is_symlink() or journal.stat().st_uid != 0 or journal.stat().st_mode & 0o022:
            raise PermissionError('Untrusted OOBE operation journal')
        active_operations = json.loads(journal.read_text(encoding='utf-8'))
        for info in active_operations.values():
            info['thread'] = None
            if info['status'] == 'RUNNING':
                info['status'] = 'UNKNOWN'
                info['error'] = 'Coordinator restarted during installation; administrator reconciliation required'
        save_operations()

    # Validate/recover persistent state before publishing a live endpoint.
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server.bind(str(sock_path))
    os.chmod(str(sock_path), 0o666)
    server.listen(5)
    server.settimeout(2.0)

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
        if not active_oobe_stage(stage_file):
            print('OOBE stage expired; coordinator shutting down.', file=sys.stderr)
            break
        try:
            conn, _ = server.accept()
        except socket.timeout:
            continue
        except OSError:
            break

        conn.settimeout(15.0)
        try:
            # Enforce peer credentials: only wexsetup (or root in tests/CLI)
            creds = conn.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, struct.calcsize('3i'))
            pid, uid, gid = struct.unpack('3i', creds)
            if uid != wexsetup_uid and uid != 0:
                print(f'Rejecting unauthorized client UID {uid}', file=sys.stderr)
                conn.close()
                continue

            # Read bounded request line (max 1024 bytes)
            raw_req = b''
            deadline = time.monotonic() + 15.0
            while len(raw_req) < 1024:
                conn.settimeout(max(0.001, deadline - time.monotonic()))
                b = conn.recv(1)
                if b == b'\n':
                    break
                if not b or time.monotonic() >= deadline:
                    raise ValueError('Incomplete coordinator request')
                raw_req += b
            else:
                raise ValueError('Coordinator request exceeds limit')

            line = raw_req.decode('utf-8', errors='replace').strip()
            if not line:
                conn.close()
                continue

            parts = line.split()
            cmd = parts[0] if parts else ''

            if cmd == 'GET_COMPONENTS' and len(parts) == 1:
                installed = get_installed_receipts()
                lines = ['OK']
                for v, meta in COMPONENTS_METADATA.items():
                    inst = '1' if v in installed else '0'
                    reb = '1' if meta['reboot'] else '0'
                    dis = '1' if meta.get('disabled') else '0'
                    dis_reason = meta.get('disabled_reason', '')
                    # Format: verb \t title \t publisher \t description \t size \t reboot \t installed \t disabled \t disabled_reason
                    row = f"{v}\t{meta['title']}\t{meta['publisher']}\t{meta['description']}\t{meta['size']}\t{reb}\t{inst}\t{dis}\t{dis_reason}"
                    lines.append(row)
                conn.sendall(('\n'.join(lines) + '\n').encode('utf-8'))

            elif cmd == 'GET_TERMS' and len(parts) == 2:
                verb = parts[1]
                try:
                    validate_verb(verb)
                    meta = COMPONENTS_METADATA.get(verb, {})
                    if meta.get('disabled'):
                        raise ValueError(f'{verb} is disabled: {meta.get("disabled_reason")}')
                    try:
                        text, sha = license_text(verb)
                    except ValueError:
                        text, sha = fetch_vendor_terms(verb)
                    response = f"OK {sha} {len(text.encode('utf-8'))}\n{text}"
                    conn.sendall(response.encode('utf-8'))
                except Exception as err:
                    err_msg = f"ERR {err}\n"
                    conn.sendall(err_msg.encode('utf-8'))

            elif (cmd == 'START_INSTALL' or cmd == 'INSTALL') and len(parts) == 3:
                # Revalidate stage immediately before dispatching privileged install
                if not active_oobe_stage(stage_file):
                    conn.sendall(b"ERR Setup OOBE stage expired; installation rejected\n")
                    try:
                        conn.shutdown(socket.SHUT_WR)
                    except OSError:
                        pass
                    continue
                verb, accepted_sha = parts[1], parts[2]
                try:
                    validate_verb(verb)
                    meta = COMPONENTS_METADATA.get(verb, {})
                    if meta.get('disabled'):
                        raise ValueError(f'{verb} is disabled: {meta.get("disabled_reason")}')
                    _, current_sha = license_text(verb)
                    if current_sha != accepted_sha:
                        raise ValueError('Vendor terms changed; review and accept the current text')
                except Exception as err:
                    conn.sendall(f"ERR {err}\n".encode('utf-8'))
                    continue

                if is_installed(verb, accepted_sha):
                    conn.sendall(b"OK ALREADY_INSTALLED\n")
                    continue

                with operations_lock:
                    existing_op = None
                    other_running = False
                    for op_id, op_info in active_operations.items():
                        if op_info['status'] == 'UNKNOWN':
                            other_running = True
                        if op_info['status'] == 'RUNNING':
                            if op_info['verb'] == verb:
                                existing_op = (op_id, op_info)
                            else:
                                other_running = True
                    if existing_op:
                        op_id, op_info = existing_op
                        if op_info['sha'] == accepted_sha:
                            conn.sendall(f"OK {op_id} STARTED\n".encode('utf-8'))
                            continue
                        else:
                            conn.sendall(b"ERR Another operation running with different terms\n")
                            continue
                    if other_running:
                        conn.sendall(b"ERR Another installation is active or requires administrator reconciliation\n")
                        continue

                    op_id = f"op_{verb}_{uuid.uuid4().hex}"
                    op_state = {
                        'verb': verb,
                        'sha': accepted_sha,
                        'status': 'RUNNING',
                        'error': '',
                        'thread': None,
                        'cancelled': False,
                    }
                    active_operations[op_id] = op_state
                    save_operations()

                    def run_op(current_op_id, v, s):
                        try:
                            install(v, s)
                            with operations_lock:
                                if current_op_id in active_operations:
                                    active_operations[current_op_id]['status'] = 'SUCCESS'
                                    save_operations()
                        except Exception as ex:
                            with operations_lock:
                                if current_op_id in active_operations:
                                    active_operations[current_op_id]['status'] = 'FAILED'
                                    active_operations[current_op_id]['error'] = str(ex)
                                    save_operations()

                    op_thread = threading.Thread(target=run_op, args=(op_id, verb, accepted_sha), daemon=False)
                    op_state['thread'] = op_thread
                    op_thread.start()

                    conn.sendall(f"OK {op_id} STARTED\n".encode('utf-8'))

            elif cmd == 'QUERY_INSTALL' and len(parts) == 2:
                q_op_id = parts[1]
                with operations_lock:
                    if q_op_id in active_operations:
                        info = active_operations[q_op_id]
                        if info['status'] == 'RUNNING':
                            conn.sendall(b"STATUS RUNNING\n")
                        elif info['status'] == 'SUCCESS':
                            conn.sendall(b"STATUS SUCCESS\n")
                        elif info['status'] in ('FAILED', 'UNKNOWN'):
                            err_str = info['error'].replace('\n', ' ').strip()
                            conn.sendall(f"STATUS {info['status']} {err_str}\n".encode('utf-8'))
                    else:
                        conn.sendall(b"ERR Unknown operation; reconcile outcome before retry\n")

            elif cmd == 'CANCEL_INSTALL' and len(parts) == 2:
                c_op_id = parts[1]
                with operations_lock:
                    if c_op_id in active_operations:
                        info = active_operations[c_op_id]
                        if info['status'] == 'RUNNING':
                            # Do not unlock/retry while an MSI is still running.
                            info['cancelled'] = True
                            conn.sendall(b"OK DEFERRED Installer running; waiting for safe completion\n")
                        else:
                            conn.sendall(b"ERR Operation already completed\n")
                    else:
                        conn.sendall(b"ERR No such operation\n")

            elif cmd == 'GET_RECEIPTS' and len(parts) == 1:
                installed = get_installed_receipts()
                conn.sendall(('OK ' + ' '.join(installed) + '\n').encode('utf-8'))

            else:
                conn.sendall(b"ERR Unknown or malformed command\n")

        except Exception as err:
            print('Coordinator client error:', err, file=sys.stderr)
        finally:
            try:
                conn.shutdown(socket.SHUT_WR)
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
    # Revoke new requests before waiting for workers at their safe completion.
    for info in active_operations.values():
        if info['thread'] and info['thread'].ident is not None:
            info['thread'].join()
    print('OOBE coordinator stopped.', file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', nargs=2, metavar=('VERB', 'ACCEPTED_SHA256'), help=argparse.SUPPRESS)
    parser.add_argument('--prepare-terms', metavar='VERB', help=argparse.SUPPRESS)
    parser.add_argument('--oobe-coordinator', action='store_true', help='Run OOBE setup coordinator daemon')
    parser.add_argument('action', nargs='?', choices=['install', 'list'])
    parser.add_argument('verbs', nargs='*')
    args = parser.parse_args()
    if args.oobe_coordinator:
        oobe_coordinator()
    elif args.prepare_terms:
        prepare_terms(args.prepare_terms)
    elif args.install:
        install(*args.install)
    elif args.action == 'list':
        for verb, meta in COMPONENTS_METADATA.items():
            status = f" (disabled: {meta['disabled_reason']})" if meta.get('disabled') else ''
            print(f"{verb}: {meta['title']}{status}")
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
