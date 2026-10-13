#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Stream the selected working files to Linux, with Git modes and LF text."""
import io
from pathlib import Path
import subprocess
import sys
import tarfile

root = Path(__file__).resolve().parent.parent
text_extensions = {'.c', '.h', '.rc', '.in', '.ac', '.m4', '.idl', '.spec', '.sh',
                   '.py', '.md', '.manifest', '.theme', '.txt'}
modes = {}
for entry in subprocess.check_output(['git', 'ls-files', '--stage', '-z'], cwd=root).split(b'\0'):
    if entry:
        metadata, path = entry.split(b'\t', 1)
        modes[path.decode()] = int(metadata.split()[0], 8) & 0o777
with tarfile.open(fileobj=sys.stdout.buffer, mode='w|') as archive:
    for name in Path(sys.argv[1]).read_text().splitlines():
        path = (root / name).resolve()
        if not path.is_relative_to(root) or not path.is_file():
            raise ValueError('Invalid sync file: ' + name)
        data = path.read_bytes()
        if path.suffix in text_extensions:
            data = data.replace(b'\r\n', b'\n')
        info = tarfile.TarInfo(name)
        info.size = len(data)
        info.mode = modes.get(name, 0o644)
        archive.addfile(info, io.BytesIO(data))
