# SPDX-License-Identifier: GPL-2.0-or-later
"""Unit tests for the read-only Chromium child-exit observer.
Tests command line role classification, stat parsing, mock /proc lifecycle tracking,
and Chromium log event parsing."""
import io
from pathlib import Path
import tempfile
import unittest

from g4_child_observer import parse_cmdline, parse_stat, ChildObserver


class TestParseCmdline(unittest.TestCase):
    def test_browser_main(self):
        args, role = parse_cmdline(b'chrome.exe\0--user-data-dir=C:\\profile\0')
        self.assertEqual(role, 'browser_main')
        self.assertEqual(len(args), 2)

    def test_content_renderer(self):
        args, role = parse_cmdline(b'chrome.exe\0--type=renderer\0--lang=en-US\0')
        self.assertEqual(role, 'content_renderer')

    def test_extension_renderer(self):
        args, role = parse_cmdline(b'chrome.exe\0--type=renderer\0--extension-process\0')
        self.assertEqual(role, 'extension_renderer')

    def test_gpu_process(self):
        args, role = parse_cmdline(b'chrome.exe\0--type=gpu-process\0--use-gl=angle\0')
        self.assertEqual(role, 'gpu_process')

    def test_network_service(self):
        args, role = parse_cmdline(b'chrome.exe\0--type=utility\0--utility-sub-type=network.mojom.NetworkService\0')
        self.assertEqual(role, 'network_service')

    def test_storage_service(self):
        args, role = parse_cmdline(b'chrome.exe\0--type=utility\0--utility-sub-type=storage.mojom.StorageService\0')
        self.assertEqual(role, 'storage_service')

    def test_crashpad_handler(self):
        args, role = parse_cmdline(b'chrome.exe\0--type=crashpad-handler\0')
        self.assertEqual(role, 'crashpad_handler')

    def test_foreign_process(self):
        args, role = parse_cmdline(b'notepad.exe\0file.txt\0')
        self.assertEqual(role, 'foreign')


class TestParseStat(unittest.TestCase):
    def test_parse_stat_running(self):
        stat_line = "1234 (chrome.exe) S 1000 1234 1000 0 -1 4194304 100 0 0 0"
        parsed = parse_stat(stat_line)
        self.assertIsNotNone(parsed)
        self.assertEqual(parsed['pid'], 1234)
        self.assertEqual(parsed['comm'], 'chrome.exe')
        self.assertEqual(parsed['state'], 'S')
        self.assertEqual(parsed['ppid'], 1000)

    def test_parse_stat_zombie(self):
        stat_line = "5678 (chrome.exe) Z 1234 5678 1234 0 -1 0 0 0 0 0"
        parsed = parse_stat(stat_line)
        self.assertIsNotNone(parsed)
        self.assertEqual(parsed['pid'], 5678)
        self.assertEqual(parsed['state'], 'Z')


class TestChildObserverMockProc(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = Path(self.temp_dir.name)
        self.proc_dir = self.root / 'proc'
        self.proc_dir.mkdir()
        self.output_dir = self.root / 'output'
        self.output_dir.mkdir()
        self.simulated_time = 100.0

    def tearDown(self):
        self.temp_dir.cleanup()

    def _now(self):
        return self.simulated_time

    def _create_proc_entry(self, pid, cmdline_bytes, stat_str):
        p = self.proc_dir / str(pid)
        p.mkdir(exist_ok=True)
        (p / 'cmdline').write_bytes(cmdline_bytes)
        (p / 'stat').write_text(stat_str)

    def _remove_proc_entry(self, pid):
        p = self.proc_dir / str(pid)
        if p.exists():
            (p / 'cmdline').unlink(missing_ok=True)
            (p / 'stat').unlink(missing_ok=True)
            p.rmdir()

    def test_observer_lifecycle(self):
        obs = ChildObserver(self.output_dir, now=self._now, proc_root=self.proc_dir)

        # 1. Spawn browser and renderer
        self._create_proc_entry(220, b'chrome.exe\0', '220 (chrome.exe) S 1 220 1 0')
        self._create_proc_entry(708, b'chrome.exe\0--type=renderer\0', '708 (chrome.exe) S 220 708 220 0')
        obs.sample()

        self.assertEqual(len(obs.observed), 2)
        self.assertIn(220, obs.observed)
        self.assertIn(708, obs.observed)
        self.assertEqual(obs.observed[708]['role'], 'content_renderer')
        self.assertTrue(obs.observed[708]['alive'])

        # 2. Advance time: renderer becomes zombie
        self.simulated_time += 5.0
        self._create_proc_entry(708, b'chrome.exe\0--type=renderer\0', '708 (chrome.exe) Z 220 708 220 0')
        obs.sample()

        self.assertEqual(obs.observed[708]['last_state'], 'Z')
        self.assertIn('zombie_seen', obs.observed[708])

        # 3. Advance time: renderer vanishes
        self.simulated_time += 2.0
        self._remove_proc_entry(708)
        obs.sample()

        self.assertFalse(obs.observed[708]['alive'])
        self.assertEqual(obs.observed[708]['survival_seconds'], 7.0)

        # 4. Finish
        summary = obs.finish()
        self.assertEqual(summary['total_processes_observed'], 2)
        self.assertTrue(summary['processes'][0]['alive_at_finish'])  # browser still alive
        self.assertFalse(summary['processes'][1]['alive'])          # renderer dead

        # Verify JSON artifact written
        artifact = self.output_dir / 'child-exit-diagnostic.json'
        self.assertTrue(artifact.exists())

    def test_chromium_log_parsing(self):
        # Write mock chromium.log
        log_path = self.output_dir / 'chromium.log'
        log_path.write_text(
            "[1026/235404.196:ERROR:network_service_instance_impl.cc:652] Network service crashed, restarting service.\n"
            "[1026/235420.830:ERROR:sad_tab_view.cc:123] SadTab: Tab Killed: http://127.0.0.1:33071/\n"
            "[1026/235421.100:VERBOSE1:dispatcher.cc:456] Created script context for extension\n"
        )

        obs = ChildObserver(self.output_dir, now=self._now, proc_root=self.proc_dir)
        events = obs.parse_chromium_log()
        self.assertEqual(len(events), 3)
        self.assertEqual(events[0]['type'], 'network_service_event')
        self.assertEqual(events[1]['type'], 'sad_tab')
        self.assertEqual(events[2]['type'], 'script_context')


if __name__ == '__main__':
    unittest.main()
