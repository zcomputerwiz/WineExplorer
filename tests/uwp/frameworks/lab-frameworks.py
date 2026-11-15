#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Lab: the uwp-frameworks install and uninstall steps of wex-components.py against a Wine prefix standing in for the
machine root, as the unprivileged lab user. Only the download location differs from setup: the pinned containers are
fetched into DOWNLOADS (once) and checked against the same sizes and hashes; extraction, identity checks, unpack,
catalog records, verification and the receipt are the component's own code.

  lab-frameworks.py install COMPONENTS_PY PREFIX DOWNLOADS
  lab-frameworks.py uninstall COMPONENTS_PY PREFIX"""
import hashlib
import importlib.util
import os
from pathlib import Path
import sys
import urllib.request

action, components, prefix = sys.argv[1], sys.argv[2], Path(sys.argv[3]).resolve()
spec = importlib.util.spec_from_file_location('components', components)
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)
c.UWP_XAML_OWNER = os.getuid()


def lab_download(verb, payload, cache, redirects, label):
    target = cache / payload['name']
    if not target.exists():
        if not payload['url'].startswith(redirects):
            raise ValueError('unapproved download location')
        partial = target.with_suffix('.partial')
        with urllib.request.urlopen(payload['url'], timeout=60) as response, open(partial, 'wb') as out:
            if not response.url.startswith(redirects):
                raise ValueError('unexpected redirect ' + response.url)
            while chunk := response.read(1 << 20):
                out.write(chunk)
        partial.rename(target)
    if target.stat().st_size != payload['size'] or c.file_sha256(target) != payload['sha256']:
        raise ValueError('pinned container mismatch: ' + payload['name'])
    return target


if action == 'install':
    downloads = Path(sys.argv[4]).resolve()
    downloads.mkdir(exist_ok=True)
    c.cache_pinned_download = lab_download
    # the cache argument is where the work directory goes: <cache>/<verb>/.work-*
    (downloads / 'uwp-frameworks').mkdir(exist_ok=True)
    c.install_uwp_frameworks_into('uwp-frameworks', prefix, downloads, 'lab')
    receipt = prefix.parent / 'component-receipts' / 'uwp-frameworks.json'
    import json
    for name, files in json.loads(receipt.read_text())['packages'].items():
        print(f'installed {name} ({len(files)} files)')
    c.verify_uwp_frameworks(prefix, json.loads(receipt.read_text()), full=True)
    print('verify full: ok')
elif action == 'uninstall':
    c.uninstall_uwp_frameworks('uwp-frameworks', prefix)
    print('uninstalled')
else:
    sys.exit(__doc__)
