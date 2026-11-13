#!/usr/bin/env python3
#
# Copyright 2026 WineExplorer contributors
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <https://www.gnu.org/licenses/>.

"""Write a registry policy file (Registry.pol, PReg) for tests.

usage: make-preg.py OUT KEY VALUE NUMBER [KEY VALUE NUMBER ...]

Every entry sets a REG_DWORD value. A value name that starts with ** is a
PReg instruction (for example **del.Name) and is written as given.
"""

import struct
import sys


def text(s):
    return (s + chr(0)).encode('utf-16-le')


def entry(key, value, number):
    sep = ';'.encode('utf-16-le')
    data = struct.pack('<I', number)
    return ('['.encode('utf-16-le') + text(key) + sep + text(value) + sep + struct.pack('<I', 4) + sep +
            struct.pack('<I', len(data)) + sep + data + ']'.encode('utf-16-le'))


def main(argv):
    if len(argv) < 5 or (len(argv) - 2) % 3:
        sys.stderr.write(__doc__)
        return 2
    out = b'PReg' + struct.pack('<I', 1)
    for i in range(2, len(argv), 3):
        out += entry(argv[i], argv[i + 1], int(argv[i + 2], 0))
    with open(argv[1], 'wb') as f:
        f.write(out)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
