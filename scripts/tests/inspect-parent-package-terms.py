#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Offline, exact-package terms provenance inventory; never executes vendor code.

Prints hashes/manifest relationships only, not vendor text or legal conclusions.
Requires cabextract and wrestool. This is not a production terms renderer.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET


def digest(data, algorithm='sha256'):
    return hashlib.new(algorithm, data).hexdigest()


def checked(path, expected):
    data = path.read_bytes()
    if digest(data) != expected:
        raise ValueError('Unreviewed package identity: ' + path.name)
    return data


def command(*args):
    return subprocess.run(args, check=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=30).stdout


def cabinet(path, name):
    return command('cabextract', '-p', '-F', name, str(path))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cache', type=Path, required=True)
    args = parser.parse_args()
    cache = args.cache.resolve()
    sdk = cache / 'sdksetup.exe'
    sdk_bytes = checked(sdk, '5107822a5a99bcdef4c7b7c7eea218425692f0185750b0d4fafd441034a7486b')
    manifest_bytes = cabinet(sdk, '0')
    root = ET.fromstring(manifest_bytes)
    ns = {'b': 'http://schemas.microsoft.com/wix/2008/Burn'}
    payloads = root.findall('.//b:Payload', ns)
    license_payload, = [p for p in payloads if p.get('FilePath') == 'license.rtf']
    assert license_payload.get('SourcePath') == 'u1'
    license_bytes = cabinet(sdk, 'u1')
    assert license_bytes.startswith(b'{')
    assert digest(license_bytes, 'sha1').upper() == license_payload.get('Hash')
    assert len(license_bytes) == int(license_payload.get('FileSize'))
    assert digest(license_bytes) == '7606715231cab510135bb271836677797189c64cfacc59b8b444e427fecace78'
    ux = ET.fromstring(cabinet(sdk, 'u12'))
    fields = {e.tag.rsplit('}', 1)[-1]: e.text for e in ux.iter()
              if e.tag.rsplit('}', 1)[-1] in ('Eula', 'EulaVersion')}
    assert fields['Eula'] == 'license.rtf'
    assert fields['EulaVersion'] == '6.3.0.0'
    cabinets = []
    expected = {
        '2630bae9681db6a9f6722366f47d055c.cab':
        'd0440eb81c532dc23639c0c63f2fcde9deddb23bb4cce01c19ac6b96cc3e269d',
        '61d57a7a82309cd161a854a6f4619e52.cab':
        'f736e161547095bb8d98c636b85fdfeb4070fefeee3c3745db3ce88f6eb1d9de',
    }
    for name, sha in expected.items():
        data = checked(cache / name, sha)
        payload, = [p for p in payloads if p.get('FilePath', '').endswith(name)]
        assert digest(data, 'sha1').upper() == payload.get('Hash')
        assert len(data) == int(payload.get('FileSize'))
        cabinets.append({'name': name, 'sha256': sha, 'manifest_sha1_matches': True})
    directx = cache / 'directx_Jun2010_redist.exe'
    dx_bytes = checked(directx, '053f76dcbb28802e23341b6a787e3b0791c0fa5c8d4d011b1044172dbf89c73b')
    carrier = cabinet(directx, 'dsetup32.dll')
    assert digest(carrier) == 'fb0e534f9b0926e518f1c2980640dfd29f14217cdfa37cf3a0c13349127ed9a8'
    with tempfile.TemporaryDirectory(prefix='wex-parent-terms-') as temp:
        dll = Path(temp) / 'dsetup32.dll'
        dll.write_bytes(carrier)
        runtime = command('wrestool', '--extract', '--raw', '--type=DATA',
                          '--name=SETUP_AGREEMENT_ENG', '--language=1033', str(dll))
    assert len(runtime) == 4849
    assert digest(runtime) == '55eaf19bbbbcc80e36e22e3bac1842413376874505b4927abd7eb0c203987771'
    text = runtime.decode('cp1252', errors='strict')
    assert 'MICROSOFT DIRECTX END USER RUNTIME' in text
    assert chr(0) not in text
    print(json.dumps({
        'license': 'SPDX-License-Identifier: GPL-2.0-or-later',
        'scope': 'Static exact-package provenance, not license clearance or installation acceptance',
        'sdk': {
            'url': 'https://download.microsoft.com/download/B/0/C/B0C80BA3-8AD6-4958-810B-6882485230B5/standalonesdk/sdksetup.exe',
            'sha256': digest(sdk_bytes), 'manifest_sha256': digest(manifest_bytes),
            'license_member': 'u1', 'manifest_filename': 'license.rtf',
            'license_bytes': len(license_bytes), 'license_sha256': digest(license_bytes),
            'eula_version': fields['EulaVersion'], 'compiler_cabinets': cabinets,
        },
        'directx': {
            'package_sha256': digest(dx_bytes), 'carrier': 'dsetup32.dll',
            'carrier_sha256': digest(carrier),
            'resource': {'type': 'DATA', 'name': 'SETUP_AGREEMENT_ENG', 'language': 1033},
            'resource_bytes': len(runtime), 'resource_sha256': digest(runtime),
            'decoding': 'cp1252 strict; raw bytes unchanged',
            'title': 'Microsoft DirectX End User Runtime',
        },
        'vendor_executables_run': False,
    }, indent=2))


if __name__ == '__main__':
    main()
