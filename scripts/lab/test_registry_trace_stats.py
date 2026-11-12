#!/usr/bin/env python3
# WineExplorer Desktop Environment - tests for registry-trace-stats.py
#
# Copyright 2026 WineExplorer Desktop Environment Project
#
# SPDX-License-Identifier: GPL-2.0-or-later
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
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
"""Unit tests for scripts/lab/registry-trace-stats.py (ST-05 --writes)."""
import importlib.util
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "registry-trace-stats.py")
HEADER = os.path.normpath(os.path.join(HERE, "..", "..", "wine", "include", "wine", "wex_session0.h"))


def load_module():
    spec = importlib.util.spec_from_file_location("registry_trace_stats", SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


mod = load_module()

# A small synthetic trace in the WINEDEBUG=+server format: one request line then its reply line.
# The create_key requests use the objattr form (rootdir = a parent handle, name = a relative subkey),
# so the full key path is reconstructed from the parent handle path plus the name.
TRACE = [
    r'0024: open_key( parent=0000, access=00020019, attributes=00000040, name=L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Video" )',
    r'0024: open_key() = 0 { hkey=0004 }',
    r'0024: create_key( access=0002001f, options=00000000, objattr={rootdir=0004,attributes=000000c0,sd={},name=L"X"}, class=L"" )',
    r'0024: create_key() = 0 { hkey=0008 }',
    r'0024: set_key_value( hkey=0008, type=1, namelen=3, name=L"Val", data={...} )',
    r'0024: set_key_value() = 0',
    r'0024: open_key( parent=0000, access=00020019, attributes=00000040, name=L"\\Registry\\Machine\\Software\\Foo" )',
    r'0024: open_key() = 0 { hkey=000c }',
    r'0024: create_key( access=0002001f, options=00000000, objattr={rootdir=000c,attributes=000000c0,sd={},name=L"Bar"}, class=L"" )',
    r'0024: create_key() = OBJECT_NAME_EXISTS { hkey=0010 }',
    r'0024: set_key_value( hkey=0010, type=1, namelen=3, name=L"Val", data={...} )',
    r'0024: set_key_value() = 0',
]


def run_script(args, trace):
    proc = subprocess.run([sys.executable, SCRIPT] + args,
                          input="\n".join(trace) + "\n",
                          capture_output=True, text=True, encoding="utf-8")
    return proc


class HeaderParsingTest(unittest.TestCase):
    def test_real_header(self):
        machine, default_user = mod.load_session_keys(HEADER)
        self.assertIn("Hardware", machine)
        self.assertIn("System\\CurrentControlSet\\Control\\Video", machine)
        self.assertIn("Software\\Wine\\Drives", machine)
        self.assertEqual(len(machine), 23)
        self.assertEqual(default_user, [
            "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders",
            "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\User Shell Folders",
        ])

    def test_synthetic_header(self):
        text = (
            "#define WEX_SESSION_MACHINE_KEYS \\\n"
            '    "Hardware", \\\n'
            '    "System\\\\CurrentControlSet\\\\Control\\\\Video"\n'
            "\n"
            "#define WEX_SESSION_DEFAULT_USER_KEYS \\\n"
            '    "Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Explorer\\\\Shell Folders"\n'
        )
        fd, path = tempfile.mkstemp(suffix=".h")
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as f:
                f.write(text)
            machine, default_user = mod.load_session_keys(path)
            self.assertEqual(machine, ["Hardware", "System\\CurrentControlSet\\Control\\Video"])
            self.assertEqual(default_user,
                             ["Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders"])
        finally:
            os.unlink(path)


class ClassifyTest(unittest.TestCase):
    def setUp(self):
        self.machine, self.default_user = mod.load_session_keys(HEADER)

    def test_session_video(self):
        path = "\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Video\\X"
        self.assertEqual(mod.classify(path, self.machine, self.default_user), "session")

    def test_session0_software_foo(self):
        path = "\\Registry\\Machine\\Software\\Foo"
        self.assertEqual(mod.classify(path, self.machine, self.default_user), "session0")

    def test_case_insensitive(self):
        path = "\\Registry\\Machine\\system\\currentcontrolset\\control\\video\\x"
        self.assertEqual(mod.classify(path, self.machine, self.default_user), "session")

    def test_default_user(self):
        path = ("\\Registry\\User\\.Default\\Software\\Microsoft\\Windows\\CurrentVersion"
                "\\Explorer\\Shell Folders\\Sub")
        self.assertEqual(mod.classify(path, self.machine, self.default_user), "session")


class ParseTraceTest(unittest.TestCase):
    def test_summary(self):
        stats = mod.parse_trace(TRACE)
        self.assertEqual(stats["total"], 6)
        self.assertEqual(stats["registry"], 6)
        self.assertEqual(stats["by_tree"], {"machine": 6, "user": 0, "other": 0})
        self.assertEqual(stats["writes"], {"machine": 4, "user": 0, "other": 0})
        self.assertEqual(stats["by_request"], {"open_key": 2, "create_key": 2, "set_key_value": 2})

    def test_write_events(self):
        stats = mod.parse_trace(TRACE)
        events = stats["write_events"]
        # created vs existing create_key, with the full path from the parent handle plus the name
        self.assertIn(("create_key created",
                       "\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Video\\X", None), events)
        self.assertIn(("create_key existed",
                       "\\Registry\\Machine\\Software\\Foo\\Bar", None), events)
        # set_key_value carries the value name
        self.assertIn(("set_key_value",
                       "\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Video\\X", "Val"), events)
        self.assertIn(("set_key_value",
                       "\\Registry\\Machine\\Software\\Foo\\Bar", "Val"), events)
        self.assertEqual(len(events), 4)


class OutputTest(unittest.TestCase):
    def test_no_writes_unchanged(self):
        proc = run_script(["label"], TRACE)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        lines = proc.stdout.strip().split("\n")
        self.assertEqual(len(lines), 4)
        self.assertEqual(lines[0],
                         "label: processes=0 requests=6 registry=6 (100.0%) machine=6 user=0 other=0")
        self.assertEqual(lines[1], "label: writes machine=4 user=0 other=0")
        self.assertEqual(lines[2], "label: key handles duplicated=0 opened inheritable=0 watched=0 (machine 0)")
        self.assertEqual(lines[3], "label: by request open_key=2 create_key=2 set_key_value=2")

    def test_writes(self):
        proc = run_script(["label", "--writes"], TRACE)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        out = proc.stdout
        lines = out.strip().split("\n")
        # The four summary lines are unchanged and come first.
        self.assertEqual(lines[0],
                         "label: processes=0 requests=6 registry=6 (100.0%) machine=6 user=0 other=0")
        self.assertEqual(lines[1], "label: writes machine=4 user=0 other=0")
        self.assertEqual(lines[2], "label: key handles duplicated=0 opened inheritable=0 watched=0 (machine 0)")
        self.assertEqual(lines[3], "label: by request open_key=2 create_key=2 set_key_value=2")
        # The writes section follows.
        self.assertIn("machine writes by key", lines[4])
        # created vs existing create_key
        self.assertTrue(any("create_key created" in l and
                            "\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Video\\X" in l
                            for l in lines[5:]), out)
        self.assertTrue(any("create_key existed" in l and
                            "\\Registry\\Machine\\Software\\Foo\\Bar" in l
                            for l in lines[5:]), out)
        # relative open under a parent handle: the full path is reconstructed, and the value name is listed
        self.assertTrue(any("set_key_value" in l and
                            "\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Video\\X" in l and
                            "Val" in l
                            for l in lines[5:]), out)
        # classification: session for the per-session Video key, session0 for Software\Foo
        self.assertTrue(any(l.strip().startswith("1 session ") and
                            "\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Video\\X" in l
                            for l in lines[5:]), out)
        self.assertTrue(any(l.strip().startswith("1 session0 ") and
                            "\\Registry\\Machine\\Software\\Foo" in l
                            for l in lines[5:]), out)


if __name__ == "__main__":
    unittest.main()
