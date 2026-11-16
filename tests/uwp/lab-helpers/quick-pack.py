# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""host: the XamlShim files of a quick build that differ from the base release (the zip the lab's base prefix was
made from), as a tar to unpack over the installed component; classes.txt is always included.
usage: quick-pack.py PUBLISH_DIR BASE_ZIP OUT_TAR"""
import hashlib, sys, tarfile, zipfile
from pathlib import Path

publish, base_zip, out = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
with zipfile.ZipFile(base_zip) as z:
    # the release zip keeps the files in a XamlShim/ folder; the component is installed without it
    base = {i.filename.split('/', 1)[1]: hashlib.sha256(z.read(i)).hexdigest()
            for i in z.infolist() if not i.is_dir() and '/' in i.filename}
changed = []
for f in sorted(p for p in publish.rglob('*') if p.is_file() and p.suffix != '.pdb'):
    rel = f.relative_to(publish).as_posix()
    if rel == 'classes.txt' or base.get(rel) != hashlib.sha256(f.read_bytes()).hexdigest():
        changed.append((f, rel))
with tarfile.open(out, 'w', format=tarfile.GNU_FORMAT) as t:
    for f, rel in changed:
        t.add(f, arcname=rel)
print(f'{len(changed)} files differ from {base_zip.name}: ' + ' '.join(rel for _, rel in changed[:12]) +
      (' ...' if len(changed) > 12 else ''))
