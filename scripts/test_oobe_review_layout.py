#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""F3 rendered-height layout proof for the IDD_OOBE_REVIEW info/status controls.

Compiles scripts/oobe_review_layout_probe.c (a native PE) with the mingw
cross-compiler and runs it under wine + Xvfb. The probe reproduces the dialog's
real 8pt "MS Shell Dlg" font, runs GDI DrawText(DT_CALCRECT | DT_WORDBREAK) on
the CURRENT production info/status strings, takes the wrapped line count (which
is font-independent), and asserts that each block fits its control at the
production pixel calibration.

This is a rendered-height assertion -- not "the resource compiles" and not "the
text appears in a source search". The probe holds the worst-case production text
with a keep-in-sync comment; setUpClass additionally guards against silent drift
by asserting those fixed phrases still appear verbatim in oobe.c.

Requires the mingw cross-compiler + wine + an X server (xvfb-run or Xvfb); it is
skipped, not failed, where any of those are absent -- the same toolchain-gate
convention test_oobe_bridge.py uses for its `cc` dependency. Where the toolchain
is present (the reviewer/CI environment) the case runs and asserts FIT.
"""
import os
import shutil
import socket
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PROBE_C = ROOT / 'scripts' / 'oobe_review_layout_probe.c'
OOBE_C = ROOT / 'src' / 'programs' / 'oobe' / 'oobe.c'

# Fixed wording the probe renders; must stay in sync with oobe.c (WM_OOBE_REV_READY
# and WM_OOBE_REV_DONE handlers). setUpClass asserts each still appears verbatim.
PRODUCTION_PHRASES = (
    'Downloaded from: SourceForge (downloads.sourceforge.net/corefonts/)',
    'the pinned Core Fonts distribution archive; not a Microsoft-hosted download.',
    'Free space needed: %s (packages plus 64 MB of working headroom).',
    'Nothing will be executed or installed.',
    "Download cancelled. Nothing was installed. Verified packages already in the local cache are kept; only this run's partial files were removed.",
)


class ReviewLayoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cc = shutil.which('x86_64-w64-mingw32-gcc')
        cls.wine = shutil.which('wine') or shutil.which('wine64')
        cls.xvfb_run = shutil.which('xvfb-run')
        cls.xvfb = shutil.which('Xvfb')
        missing = [name for name, present in (
            ('x86_64-w64-mingw32-gcc', cls.cc),
            ('wine', cls.wine),
            ('xvfb-run or Xvfb', cls.xvfb_run or cls.xvfb)) if not present]
        if missing:
            raise unittest.SkipTest(
                'rendered-height layout check needs %s (not available)' % ', '.join(missing))

        # Sync guard (not the proof): the probe's fixed phrases must still be the
        # production wording, so a future oobe.c reword cannot silently invalidate it.
        src = OOBE_C.read_text(encoding='utf-8', errors='replace')
        for phrase in PRODUCTION_PHRASES:
            if phrase not in src:
                raise AssertionError(
                    'oobe.c no longer contains production string; update the probe: %r' % phrase)

    def _run_probe(self, exe):
        """Run the compiled probe under an X server, returning a CompletedProcess."""
        env = dict(os.environ, WINEDEBUG='-all')
        if self.xvfb_run:
            return subprocess.run([self.xvfb_run, '-a', self.wine, str(exe)],
                                  capture_output=True, text=True, timeout=180, env=env)
        # Only raw Xvfb available: start it on a private display and point wine at it.
        probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        probe.bind(('127.0.0.1', 0))
        display_num = (probe.getsockname()[1] % 100) + 100
        probe.close()
        display = ':%d.0' % display_num
        server = subprocess.Popen([self.xvfb, display], env=env,
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            sock_path = '/tmp/.X11-unix/X%d' % display_num
            for _ in range(100):
                if os.path.exists(sock_path):
                    break
                time.sleep(0.1)
            return subprocess.run([self.wine, str(exe)], capture_output=True, text=True,
                                  timeout=180, env=dict(env, DISPLAY=display))
        finally:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()

    def test_info_and_status_blocks_fit_their_controls(self):
        scratch = tempfile.TemporaryDirectory(prefix='wex-oobe-layout-')
        self.addCleanup(scratch.cleanup)
        exe = Path(scratch.name) / 'probe.exe'
        subprocess.run([self.cc, '-O2', '-Wall', str(PROBE_C), '-o', str(exe),
                        '-lgdi32', '-luser32'], check=True, capture_output=True, timeout=180)
        # Fresh isolated wine prefix per run; no shared state with other tests.
        env = dict(os.environ, WINEPREFIX=tempfile.mkdtemp(prefix='wex-oobe-wine-', dir=scratch.name))
        self.addCleanup(shutil.rmtree, env['WINEPREFIX'], ignore_errors=True)
        result = self._run_probe(exe)

        self.assertEqual(result.returncode, 0, result.stdout + '\n' + result.stderr)
        self.assertIn('RESULT: PASS', result.stdout)
        lines = result.stdout.splitlines()
        info = [l for l in lines if l.startswith('INFO')]
        status = [l for l in lines if l.startswith('STATUS')]
        self.assertEqual(len(info), 1, result.stdout)
        self.assertEqual(len(status), 1, result.stdout)
        # Both blocks must report FIT (no scrolling required at production scale).
        self.assertIn('FIT', info[0], result.stdout)
        self.assertIn('FIT', status[0], result.stdout)


if __name__ == '__main__':
    unittest.main(verbosity=2)
