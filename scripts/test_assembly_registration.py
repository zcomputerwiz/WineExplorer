# SPDX-License-Identifier: GPL-2.0-or-later
"""A clean upstream build list must include the assembled desktop overlay."""
import tempfile
import unittest
from pathlib import Path
from assemble import register_project_sources

class Registration(unittest.TestCase):
    def test_clean_base_and_repeat(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'project'
            wine = Path(directory) / 'wine'
            for name in ('dlls/wexhost', 'programs/oobe'):
                path = root / 'src' / name
                path.mkdir(parents=True)
                (path / 'Makefile.in').write_text('SOURCES = example.c\n')
            (root / 'src/include').mkdir()
            (root / 'src/include/wexhost.h').write_text('/* fixture */\n')
            (wine / 'include').mkdir(parents=True)
            (wine / 'configure.ac').write_text('WINE_CONFIG_MAKEFILE(dlls/user32)\nAC_OUTPUT\n')
            (wine / 'include/Makefile.in').write_text('SOURCES = ' + chr(92) + '\n\twindows.h\n')
            register_project_sources(root, wine)
            first = ((wine / 'configure.ac').read_text(), (wine / 'include/Makefile.in').read_text())
            register_project_sources(root, wine)
            self.assertEqual(first, ((wine / 'configure.ac').read_text(), (wine / 'include/Makefile.in').read_text()))
            for name in ('dlls/wexhost', 'programs/oobe', 'dlls/user32'):
                self.assertEqual(first[0].count('WINE_CONFIG_MAKEFILE(' + name + ')'), 1)
            self.assertEqual(first[1].count('wexhost.h'), 1)

if __name__ == '__main__':
    unittest.main()
