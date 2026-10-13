#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Resolve historical Wine asset locations to project-owned source."""
from pathlib import Path
import argparse
import sys
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


def legacy_path(path):
    if path == 'src/dlls/wexsearch/rover.bmp':
        return 'wine/programs/explorer/rover.bmp'
    if path.startswith('src/dlls/msgina/res_power/'):
        return 'wine/programs/explorer/res_power/' + path[len('src/dlls/msgina/res_power/'):]
    if path.startswith('src/dlls/wexres/'):
        return 'wine/programs/explorer/' + path[len('src/dlls/wexres/'):]
    return 'wine/' + path[4:] if path.startswith('src/') else path


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reverse', action='store_true')
    parser.add_argument('--checksum', action='store_true')
    args = parser.parse_args()
    transform = legacy_path if args.reverse else project_path
    for raw in sys.stdin:
        line = raw.rstrip('\r\n')
        if not line:
            continue
        if args.checksum:
            digest, separator, path = line[:32], line[32:34], line[34:]
            if len(digest) != 32 or any(c not in '0123456789abcdefABCDEF' for c in digest) or separator not in ('  ', ' *'):
                raise ValueError('Expected GNU MD5 text or binary checksum line')
            print(digest + '  ' + transform(path))
        else:
            print(transform(line))
