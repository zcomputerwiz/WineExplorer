#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Tests of scripts/wex-timesync1.py: the server list, the drop-in and the refusals (no bus needed)."""

import importlib.util
import os
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("wex_timesync1", os.path.join(HERE, "wex-timesync1.py"))
ts = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ts)


class TimeSyncTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        with open(os.path.join(self.dir, "timesyncd.conf"), "w") as f:
            f.write("[Time]\nNTP=system.example\n")

    def test_system_servers_are_read(self):
        self.assertEqual(ts.servers(self.dir), "system.example")

    def test_ours_wins_and_empty_removes_it(self):
        ts.set_servers("time.example.org  1.2.3.4 2001:db8::1", self.dir)
        self.assertEqual(ts.servers(self.dir), "time.example.org 1.2.3.4 2001:db8::1")
        ts.set_servers("", self.dir)
        self.assertEqual(ts.servers(self.dir), "system.example")
        ts.set_servers("", self.dir)   # nothing to remove is not an error

    def test_refusals(self):
        for bad in ("a;b", "-x.example", "x" * 300, " ".join(["a"] * 9), "a\nNTP=evil", "[Time]", "a=b"):
            with self.assertRaises(ts.Refused, msg=bad):
                ts.check_servers(bad)

    def test_nothing_written_on_refusal(self):
        with self.assertRaises(ts.Refused):
            ts.set_servers("good.example bad;host", self.dir)
        self.assertFalse(os.path.exists(os.path.join(self.dir, ts.DROPIN)))

    @staticmethod
    def fake_systemctl(active, log):
        class R:
            def __init__(self, code):
                self.returncode = code
                self.stderr = ""
                self.stdout = ""

        def run(argv, **kw):
            log.append(argv[1:])
            return R(0 if (argv[1] != "is-active" or active) else 3)
        return run

    def test_reload_uses_try_restart_so_a_stopped_unit_stays_stopped(self):
        log = []
        ts.reload_servers(run=self.fake_systemctl(False, log))
        self.assertEqual(log, [["try-restart", ts.UNIT]])
        self.assertFalse(any(v[0] in ("restart", "start") for v in log))

    def test_sync_now_restarts_a_running_unit_only(self):
        log = []
        ts.sync_now(run=self.fake_systemctl(True, log))
        self.assertEqual([v[0] for v in log], ["is-active", "try-restart"])

    def test_sync_now_is_refused_while_network_time_is_off(self):
        log = []
        with self.assertRaises(ts.Failed):
            ts.sync_now(run=self.fake_systemctl(False, log))
        self.assertEqual([v[0] for v in log], ["is-active"])   # nothing was started or restarted

    def test_a_failing_systemctl_is_reported(self):
        class R:
            returncode = 1
            stderr = "Unit not found"
            stdout = ""
        with self.assertRaises(ts.Failed):
            ts.reload_servers(run=lambda *a, **k: R())


if __name__ == "__main__":
    unittest.main()
