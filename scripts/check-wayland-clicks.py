# SPDX-License-Identifier: GPL-2.0-or-later
"""Check actual pixels after foreground, NoActivate and desktop clicks."""
import sys
from pathlib import Path
from PIL import Image
root = Path(sys.argv[1])
for name, color in [('red',(255,0,0)), ('blue',(0,0,255)),
                    ('desktop',(0,0,255)), ('restored-notepad',(255,255,255))]:
    with Image.open(root / ('click-' + name + '.png')) as image:
        actual = image.convert('RGB').getpixel((250,250))
    assert actual == color, (name,actual,color)
print('CLICK_RAISES_AND_DESKTOP_BOTTOM_PASSED')
