#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Remove only explicitly listed old source files within the supplied root."""
from pathlib import Path
import sys
root = Path(sys.argv[1]).resolve()
if root == Path('/'):
    raise ValueError('Refusing filesystem root')
for name in sys.stdin:
    relative = Path(name.rstrip('\r\n'))
    if relative.is_absolute() or '..' in relative.parts:
        raise ValueError('Invalid source deletion')
    path = root / relative
    if not path.parent.resolve().is_relative_to(root):
        raise ValueError('Deletion escapes source root')
    if path.is_symlink() or path.is_file():
        path.unlink()
