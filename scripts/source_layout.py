#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Resolve historical Wine asset locations to project-owned source."""
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent


def project_path(path):
    relative = path.removeprefix('wine/')
    if relative == 'programs/explorer/rover.bmp':
        return 'src/dlls/wexsearch/rover.bmp'
    if relative.startswith('programs/explorer/res_power/'):
        return 'src/dlls/msgina/res_power/' + relative[len('programs/explorer/res_power/'):]
    if relative.startswith('programs/explorer/'):
        return 'src/dlls/wexres/' + relative[len('programs/explorer/'):]
    parts = relative.split('/')
    if len(parts) > 2 and parts[0] in ('dlls', 'programs'):
        if (ROOT / 'src' / parts[0] / parts[1] / 'Makefile.in').is_file():
            return 'src/' + relative
    if relative.startswith(('include/', 'resources/')) and (ROOT / 'src' / relative).exists():
        return 'src/' + relative
    return 'wine/' + relative
