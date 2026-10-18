#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Verify exact target families, not fc-match's always-successful fallback."""
import argparse
from pathlib import Path
import re
import subprocess
import sys


def missing_families(mappings, listing):
    targets = set(re.findall(r'^"[^"]+"="([^"]+)"', mappings, re.M))
    families = {family.strip() for line in listing.splitlines() for family in line.split(',')}
    return sorted(targets - families)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mappings', nargs='?', type=Path,
                        default=Path('/usr/local/share/wineexplorer/fonts/font-substitutes.reg'))
    args = parser.parse_args()
    listing = subprocess.run(['fc-list', ':', 'family'], check=True, text=True, capture_output=True).stdout
    missing = missing_families(args.mappings.read_text(), listing)
    if missing:
        print('Missing exact font families: ' + ', '.join(missing), file=sys.stderr)
        return 1
    print('All font-substitute target families are installed (exact fc-list match).')
    return 0


if __name__ == '__main__':
    sys.exit(main())
