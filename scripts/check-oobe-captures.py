#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Reject mislabeled capture evidence. Passing is NOT visual/theme acceptance.

This offline check reads PNGs only. It never controls a VM, changes a desktop,
or infers a backend/theme from filenames. Human baseline comparison is required.
Dependencies: Pillow and tesseract. Synthetic tests mock OCR, not screenshots.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
from PIL import Image


def audit(path, ocr=True):
    name = path.name
    with Image.open(path) as image:
        width, height = image.size
        image.verify()
    reasons = []
    claimed = re.search(r'(\d{3,4})x(\d{3,4})', name)
    if not claimed:
        reasons.append('No claimed capture dimensions; document actual framebuffer/viewport separately')
    elif (width, height) != tuple(map(int, claimed.groups())):
        reasons.append('Claimed resolution differs from actual PNG dimensions')
    if ocr:
        # Welcome-screen white text disappears with automatic thresholding.
        # Match vmtest's explicit white-text pass, without modifying the source.
        with Image.open(path) as image:
            prepared = image.convert('L').resize((width * 3, height * 3)).point(lambda p: 255 if p > 153 else 0)
            buffer = io.BytesIO()
            prepared.save(buffer, format='PNG')
        run = subprocess.run(['tesseract', 'stdin', 'stdout', '--psm', '3'],
                             input=buffer.getvalue(), capture_output=True, check=True, timeout=30)
        words = re.sub(r'[^a-z0-9]+', ' ', run.stdout.decode('utf-8').lower())
        if 'to begin' in words and ('user name' in words or 'username' in words):
            reasons.append('Login screen, not a logged-in theme/desktop capture')
    return {'file': name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
            'width': width, 'height': height, 'rejections': reasons,
            'human_theme_backend_and_state_review_required': True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    paths = sorted(args.directory.glob('*.png'))
    if not paths:
        parser.error('No PNG evidence found; no acceptance can be reported')
    records = [audit(path) for path in paths]
    print(json.dumps({'scope': 'capture mislabel rejection only, not visual acceptance',
                      'captures': records}, indent=2))
    return 1 if any(r['rejections'] for r in records) else 0


if __name__ == '__main__':
    raise SystemExit(main())
