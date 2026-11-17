#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Root-only native reader tests; no installed configuration or Wine is used.

Run in a disposable Linux guest: python3 scripts/test_sysrun_conf.py.
Compile the actual consumer functions in isolation, including registry.c's
administrator-group path, rather than a second implementation of their logic.
"""
import grp
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent


def function(path, signature):
    text = (ROOT / path).read_text(encoding='utf-8')
    start = text.index(signature)
    return text[start:text.index('\n}', start) + 2] + '\n'


@unittest.skipUnless(os.getuid() == 0, 'requires root and protected /run fixtures')
class TestReaders(unittest.TestCase):
    readers = ('launcher', 'server', 'client', 'gpstore', 'registry')
    config = ('group=root\nloader=/wine\nntdll=/ntdll.so\nwineserver=/wineserver\n'
              'machine_root=/machine\nadmin_group=root\nbuiltin_administrator=administrator\n'
              'session0_socket=/run/session0\nsession0_user=wexsys\n')

    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='wex-conf-test-', dir='/run')
        cls.base = Path(cls.temp.name)
        try:
            source = cls.base / 'readers.c'
            launcher = (ROOT / 'tools/wex-sysrun/wex-sysrun.c').read_text(encoding='utf-8')
            start = launcher.index('struct config\n')
            config_struct = launcher[start:launcher.index('\n};', start) + 3]
            server_header = (ROOT / 'wine/server/sysrun.h').read_text(encoding='utf-8')
            start = server_header.index('enum sysrun_conf\n')
            status_enum = server_header[start:server_header.index('\n};', start) + 3]
            source.write_text(
                '#define _GNU_SOURCE\n#include <stdio.h>\n#include <stdlib.h>\n'
                '#include "wine/wex_sysrun_conf.h"\n'
                'static const char *conf_path;\n#define SYSRUN_CONF conf_path\n'
                '#define BOOL int\n' + config_struct + '\n' + status_enum + '\n'
                'static void refuse(const char *reason, const char *path)\n'
                '{ (void)reason; (void)path; exit(77); }\n'
                # the launcher's path diagnostic (its own suite covers it); the shared reader decides here
                'static void check_components(const char *path) { (void)path; }\n' +
                function('tools/wex-sysrun/wex-sysrun.c', 'static void read_config(') +
                function('wine/server/sysrun.c', 'enum sysrun_conf sysrun_conf_get(') +
                function('wine/dlls/ntdll/unix/sysrun.c', 'static BOOL read_config(').replace(
                    'read_config(', 'client_read_config(') +
                function('src/dlls/wexgpstore/unixlib.c', 'static int read_machine_root(') +
                function('wine/server/registry.c', 'static const char *admin_group_name(').replace(
                    '"/etc/wineexplorer/sysrun.conf"', 'SYSRUN_CONF') +
                'int main(int argc, char **argv)\n{\n'
                '    struct config config; char value[PATH_MAX];\n'
                '    if (argc != 3) return 2;\n    conf_path = argv[2];\n'
                '    if (!strcmp(argv[1], "launcher")) { read_config(&config); return 0; }\n'
                '    if (!strcmp(argv[1], "server")) return sysrun_conf_get("group", value, sizeof(value)) != SYSRUN_CONF_VALUE;\n'
                '    if (!strcmp(argv[1], "client")) return !client_read_config(value, sizeof(value));\n'
                '    if (!strcmp(argv[1], "gpstore")) return !read_machine_root(value, sizeof(value));\n'
                '    if (!strcmp(argv[1], "registry")) { const char *group = admin_group_name();\n'
                '        if (!group) return 1;\n        puts(group); return 0; }\n'
                '    return 2;\n}\n', encoding='ascii')
            cls.binary = cls.base / 'readers'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'wine/include'),
                            str(source), '-o', str(cls.binary)], check=True)
            cls.helper = cls.base / 'wex-is-admin'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror',
                            str(ROOT / 'tools/wex-is-admin/wex-is-admin.c'), '-o', str(cls.helper)], check=True)
        except BaseException:
            cls.temp.cleanup()
            raise

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.directory = self.base / 'config'
        self.directory.mkdir(mode=0o755)
        self.path = self.directory / 'sysrun.conf'
        self.write(self.config)

    def tearDown(self):
        self.directory.chmod(0o755)
        shutil.rmtree(self.directory)

    def write(self, text):
        self.path.write_bytes(text.encode('ascii'))
        self.path.chmod(0o644)

    def check_readers(self, accepted):
        for reader in self.readers:
            with self.subTest(reader=reader):
                result = subprocess.run([str(self.binary), reader, str(self.path)], capture_output=True)
                self.assertEqual(result.returncode == 0, accepted, (reader, result))

    def test_valid(self):
        self.check_readers(True)

    def test_duplicates_including_unrequested_keys(self):
        for line in self.config.splitlines():
            with self.subTest(key=line.split('=')[0]):
                self.write(self.config + line + '\n')
                self.check_readers(False)
        self.write(self.config + 'future_key=one\nfuture_key=two\n')
        self.check_readers(False)

    def test_size_boundary_and_oversize(self):
        self.write(self.config + '#' + 'x' * (4095 - len(self.config) - 2) + '\n')
        self.check_readers(True)
        for size in (4096, 8192):
            self.write(self.config + '#' + 'x' * (size - len(self.config) - 2) + '\n')
            self.check_readers(False)

    def test_malformed(self):
        for text in ('', self.config[:-1], self.config + 'broken\n', self.config + '=value\n',
                     self.config + 'bad_key=\n', self.config + 'bad key=value\n',
                     self.config + 'key=bad\x00value\n', self.config.replace('\n', '\r\n')):
            with self.subTest(text=text):
                self.write(text)
                self.check_readers(False)

    def test_protection(self):
        self.path.chmod(0o664)
        self.check_readers(False)
        self.path.chmod(0o644)
        os.chown(self.path, 1, 0)
        self.check_readers(False)
        os.chown(self.path, 0, 0)
        link = self.directory / 'hardlink'
        os.link(self.path, link)
        self.check_readers(False)
        link.unlink()
        self.path.rename(link)
        self.path.symlink_to(link)
        self.check_readers(False)
        self.path.unlink()
        link.rename(self.path)
        self.directory.chmod(0o775)
        self.check_readers(False)
        self.directory.chmod(0o755)
        parent_link = self.base / 'parent-link'
        parent_link.symlink_to(self.directory, target_is_directory=True)
        original = self.path
        try:
            self.path = parent_link / 'sysrun.conf'
            self.check_readers(False)
        finally:
            self.path = original
            parent_link.unlink()

    def test_acl(self):
        if not shutil.which('setfacl'):
            self.skipTest('requires setfacl')
        for target in (self.path, self.directory):
            subprocess.run(['setfacl', '-m', 'u:1:r-x', str(target)], check=True)
            self.check_readers(False)
            subprocess.run(['setfacl', '-b', str(target)], check=True)

    def test_registry_default_and_invalid_group(self):
        self.write('group=root\n')
        result = subprocess.run([str(self.binary), 'registry', str(self.path)], capture_output=True, text=True)
        self.assertEqual((result.returncode, result.stdout), (0, 'sudo\n'))
        self.write('admin_group=-bad\n')
        result = subprocess.run([str(self.binary), 'registry', str(self.path)])
        self.assertNotEqual(result.returncode, 0)
        self.path.unlink()
        result = subprocess.run([str(self.binary), 'registry', str(self.path)])
        self.assertNotEqual(result.returncode, 0)

    def test_print_group(self):
        group = grp.getgrgid(0).gr_name
        self.write(f'group={group}\nadmin_group={group}\n')
        def run():
            return subprocess.run([str(self.helper), '--print-group', str(self.path)],
                                  capture_output=True, text=True)
        result = run()
        self.assertEqual((result.returncode, result.stdout), (0, group + '\n'))
        self.write(f'group={group}\nadmin_group={group}\nadmin_group={group}\n')
        result = run()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, '')
        self.write(f'group={group}\nadmin_group={group}\n')
        self.path.chmod(0o664)
        self.assertNotEqual(run().returncode, 0)

    def test_print_value(self):
        """The components read session 0's settings through this, never parsing the file themselves."""
        def run(key, path=None):
            return subprocess.run([str(self.helper), '--print-value', key, str(path or self.path)],
                                  capture_output=True, text=True)
        self.write('group=wexsys\nsession0_socket=/run/s0/session0\nsession0_user=wexsys\n')
        self.assertEqual((run('session0_user').returncode, run('session0_user').stdout), (0, 'wexsys\n'))
        self.assertEqual(run('session0_socket').stdout, '/run/s0/session0\n')
        self.assertEqual((run('builtin_administrator').returncode, run('builtin_administrator').stdout), (3, ''))
        self.assertEqual(run('session0_user', self.directory / 'missing' / 'sysrun.conf').returncode, 3)
        for key in ('Bad', '0key', 'a-b', ''):
            with self.subTest(key=key):
                self.assertEqual(run(key).returncode, 2)
        # an invalid or unprotected file is never "absent": callers must not fall back to the defaults
        self.write('group=wexsys\nsession0_user=wexsys\nsession0_user=root\n')
        self.assertEqual((run('session0_user').returncode, run('session0_user').stdout), (1, ''))
        self.write('group=wexsys\nsession0_user=wexsys\n')
        self.path.chmod(0o664)
        self.assertEqual(run('session0_user').returncode, 1)


if __name__ == '__main__':
    unittest.main()
