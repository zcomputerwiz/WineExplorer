# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Generate an original, deliberately rounded caption for bridge acceptance.

The source stays readable; no third-party style or binary fixture is stored.
Compile rounded.rc with windres, then link its object as rounded.msstyles.
"""
from pathlib import Path
import struct
import sys

target = Path(sys.argv[1])
target.mkdir(parents=True, exist_ok=True)
width, height, radius = 24, 16, 7
stride = (width * 3 + 3) & ~3
pixels = bytearray()
for y in reversed(range(height)):
    row = bytearray()
    for x in range(width):
        dx = max(radius - x, x - (width - 1 - radius), 0)
        dy = max(radius - y, 0)
        transparent = dx * dx + dy * dy > radius * radius
        row.extend((255, 0, 255) if transparent else (180, 90, 30))
    pixels.extend(row + bytes(stride - len(row)))
header = struct.pack('<2sIHHI', b'BM', 54 + len(pixels), 0, 0, 54)
header += struct.pack('<IiiHHIIiiII', 40, width, height, 1, 24, 0, len(pixels), 0, 0, 0, 0)
(target / 'caption.bmp').write_bytes(header + pixels)

style = """[SysMetrics]
CaptionFont = Tahoma, 10, bold
CaptionBarHeight = 25
SMCaptionBarHeight = 17
SizingBorderWidth = 4
[Window]
BgType = BorderFill
FillColor = 30 90 180
BorderColor = 20 60 120
BorderSize = 1
[Window.Caption]
BgType = ImageFile
ImageFile = caption.bmp
SizingType = Stretch
SizingMargins = 8, 8, 8, 0
ImageCount = 1
Transparent = True
TransparentColor = 255 0 255
TextColor = 255 255 255
"""

def text_resource(name, resource_type, value):
    lines = value.splitlines(keepends=True)
    strings = ['L"' + line.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\r\\n') + '"'
               for line in lines]
    return f'{name} {resource_type}\n{{\n' + '\n'.join(strings) + '\n}\n'

rc = 'LANGUAGE 0, 0\n1 PACKTHEM_VERSION {0x3}\n'
rc += '1 COLORNAMES {L"Blue\\0\\0"}\n1 SIZENAMES {L"NormalSize\\0\\0"}\n'
rc += '1 FILERESNAMES {L"BLUE_INI\\0\\0"}\n'
rc += text_resource('THEMES_INI', 'TEXTFILE', '[documentation]\nDisplayName = Rounded frame acceptance\n')
rc += text_resource('BLUE_INI', 'TEXTFILE', style)
rc += 'CAPTION_BMP BITMAP "caption.bmp"\n'
(target / 'rounded.rc').write_text(rc, encoding='ascii')
