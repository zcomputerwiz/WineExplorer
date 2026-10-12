#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise native directory copying, including an empty service-account home."""
import os
from pathlib import Path
import stat
import subprocess
import tempfile
import unittest


class DirectoryCopyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="wex-copydirs-")
        directory = Path(cls.build.name)
        driver = directory / "driver.c"
        driver.write_text("""
#include "copydirs.h"
int main(int argc, char **argv) {
    if (argc != 4) return 2;
    FILE *file = fopen(argv[1], "r");
    if (!file) return 2;
    int result = copy_layout_directories(file, argv[2], argv[3]) ? 0 : 1;
    fclose(file);
    return result;
}
""")
        source = Path(__file__).resolve().parents[1] / "textsetup"
        cls.program = directory / "copydirs-test"
        subprocess.run(["gcc", "-Wall", "-Wextra", "-Werror", "-I", str(source),
                        str(driver), str(source / "copydirs.c"), "-o", str(cls.program)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def test_empty_service_home_ownership_and_modes(self):
        with tempfile.TemporaryDirectory(prefix="wex-copydirs-root-") as directory:
            root = Path(directory)
            source, target = root / "source", root / "target"
            home = source / "var/lib/lightdm"
            home.mkdir(parents=True)
            target.mkdir()
            (source / "var").chmod(0o751)
            home.chmod(0o2750)
            if os.geteuid() == 0:
                os.chown(home, 213, 214)
                home.chmod(0o2750)
            layout = root / "layout.inf"
            layout.write_text('[WinntDirectories]\n1 = "/"\n2 = "/var"\n'
                              '3 = "/var/lib"\n4 = "/var/lib/lightdm"\n[SourceDisksFiles]\n')
            subprocess.run([str(self.program), str(layout), str(source), str(target)], check=True)
            for path in ("var", "var/lib", "var/lib/lightdm"):
                original, copied = (source / path).stat(), (target / path).stat()
                self.assertEqual((copied.st_uid, copied.st_gid, stat.S_IMODE(copied.st_mode)),
                                 (original.st_uid, original.st_gid, stat.S_IMODE(original.st_mode)))
            self.assertEqual(list((target / "var/lib/lightdm").iterdir()), [])

    def test_traversal_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix="wex-copydirs-invalid-") as directory:
            root = Path(directory)
            layout = root / "layout.inf"
            layout.write_text('[WinntDirectories]\n1 = "/../outside"\n')
            result = subprocess.run([str(self.program), str(layout), str(root), str(root / "target")])
            self.assertEqual(result.returncode, 1)
            self.assertFalse((root / "outside").exists())


if __name__ == "__main__":
    unittest.main()
