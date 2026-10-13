#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Stream private resource overlays to their assembled source locations."""
import io
from pathlib import Path
import sys
import tarfile
from source_layout import project_path

source = Path(sys.argv[1]).resolve()
with tarfile.open(fileobj=sys.stdout.buffer, mode='w|') as archive:
    for name in Path(sys.argv[2]).read_text().splitlines():
        path = (source / name).resolve()
        target = project_path(name)
        if not path.is_relative_to(source) or not path.is_file() or Path(target).is_absolute() or '..' in Path(target).parts:
            raise ValueError('Invalid private overlay location: ' + name)
        data = path.read_bytes()
        info = tarfile.TarInfo(target)
        info.size = len(data)
        info.mode = 0o644
        archive.addfile(info, io.BytesIO(data))
