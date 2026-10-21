"""OOBE coordinator protocol tests for embedded-terms review (Phase D).

Runs the real coordinator loop against a private socket/stage under an owned
/root base directory and exercises the review verbs over the actual AF_UNIX
transport: GET_COMPONENTS 'R:' column, REVIEW_INFO, GET_TERMS cached-only
errors, START_REVIEW/QUERY_REVIEW/CANCEL_REVIEW lifecycle, duplicate and
foreign starts, unknown operations, stage expiry, peer-credential rejection,
install isolation and bounded shutdown. Every download is served by an
in-process fake opener: no public network is touched and no real component
state (/var/lib/wineexplorer, real accounts, installed prefixes) is read or
written.

Requires root: the coordinator enforces uid 0 and the cache security walk
rejects world-writable ancestors such as /tmp."""

import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.request
from pathlib import Path
from unittest.mock import Mock, patch

import test_component_review as tcr

components = tcr.components
URL_PREFIX = tcr.URL_PREFIX


def expected_urls(verb):
    return [URL_PREFIX + name for name, _sha in tcr.FIXTURES[verb]['recipe']]


class CoordinatorReviewTests(unittest.TestCase):
    """Real coordinator loop, private endpoint, fake network only."""

    def setUp(self):
        if os.geteuid() != 0:
            self.fail('OOBE coordinator review tests require root')
        self.base = tcr.private_base()
        self.addCleanup(shutil.rmtree, self.base, ignore_errors=True)
        # The endpoint lives in a root-owned, world-traversable directory like
        # production /run/wex-oobe: any local peer can connect and is then
        # filtered by SO_PEERCRED. Cache/terms stay under the private base.
        self.run_dir = Path(tempfile.mkdtemp(prefix='wex-oobe-review-test-', dir='/run'))
        os.chmod(self.run_dir, 0o755)
        self.addCleanup(shutil.rmtree, self.run_dir, ignore_errors=True)
        self.sock_path = self.run_dir / 'coordinator.sock'
        self.stage_file = self.run_dir / 'stage'
        self.stage_file.write_text('oobe\n', encoding='ascii')
        self.cache_root = self.base / 'review-cache'
        self.terms_dir = self.base / 'terms'
        self.machine = self.base / 'machine'
        self.machine.mkdir(mode=0o700)
        self.addCleanup(patch.stopall)
        patch.object(components, 'COMPONENTS', tcr.COMPONENTS_MAP).start()
        patch.object(components, 'COMPONENTS_METADATA', tcr.METADATA_MAP).start()
        patch.object(components, 'OPERATION_STATE', self.base / 'operation-state').start()
        patch.object(components, 'REVIEW_CACHE', self.cache_root).start()
        patch.object(components, 'TERMS', self.terms_dir).start()
        patch.object(components, 'MACHINE', self.machine).start()
        patch.dict(os.environ, {
            'WEX_OOBE_SOCKET': str(self.sock_path),
            'WEX_OOBE_STAGE_FILE': str(self.stage_file),
        }).start()
        patch.object(components.os, 'getuid', return_value=0).start()
        patch.object(components.pwd, 'getpwnam',
                     return_value=Mock(pw_uid=os.getuid())).start()
        self.coord_thread = None

    def tearDown(self):
        if self.stage_file.exists():
            self.stage_file.write_text('done\n', encoding='ascii')
        if self.sock_path.exists():
            try:
                with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as probe:
                    probe.settimeout(0.5)
                    probe.connect(str(self.sock_path))
            except OSError:
                pass
        if self.coord_thread and self.coord_thread.is_alive():
            self.coord_thread.join(timeout=15)

    def start_coordinator(self):
        self.coord_thread = threading.Thread(target=components.oobe_coordinator, daemon=True)
        self.coord_thread.start()
        # bind creates the path before listen; existence is not readiness.
        for _ in range(100):
            try:
                with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as probe:
                    probe.settimeout(0.1)
                    probe.connect(str(self.sock_path))
                return
            except OSError:
                time.sleep(0.01)
        self.fail('Coordinator did not start listening')

    def send_request(self, command_str):
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
            sock.settimeout(3.0)
            connected = False
            for _ in range(30):
                try:
                    sock.connect(str(self.sock_path))
                    connected = True
                    break
                except OSError:
                    time.sleep(0.05)
            if not connected:
                self.fail('Could not reach the coordinator')
            sock.sendall((command_str.strip() + '\n').encode('utf-8'))
            response = b''
            while True:
                try:
                    chunk = sock.recv(4096)
                except (socket.timeout, ConnectionResetError):
                    break
                if not chunk:
                    break
                response += chunk
            return response.decode('utf-8')

    def try_request(self, command_str):
        """Like send_request but returns None when the endpoint is gone."""
        try:
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
                sock.settimeout(3.0)
                for _ in range(20):
                    try:
                        sock.connect(str(self.sock_path))
                        break
                    except OSError:
                        time.sleep(0.1)
                else:
                    return None
                sock.sendall((command_str.strip() + '\n').encode('utf-8'))
                response = b''
                while True:
                    try:
                        chunk = sock.recv(4096)
                    except (socket.timeout, ConnectionResetError):
                        break
                    if not chunk:
                        break
                    response += chunk
                return response.decode('utf-8')
        except OSError:
            return None

    def start_fake_network(self, verb='synfont', on_read=None, **opener_kwargs):
        opener = tcr.FakeOpener(tcr.FIXTURES[verb]['files'], on_read=on_read,
                                **opener_kwargs)
        patch.object(urllib.request, 'build_opener', return_value=opener).start()
        return opener

    def wait_review(self, op_id, timeout=30.0):
        deadline = time.monotonic() + timeout
        saw_running = False
        while time.monotonic() < deadline:
            reply = self.send_request(f'QUERY_REVIEW {op_id}')
            if reply.startswith('STATUS RUNNING '):
                saw_running = True
            elif reply.startswith(('STATUS SUCCESS ', 'STATUS CANCELLED',
                                   'STATUS FAILED')):
                return reply, saw_running
            else:
                self.fail(f'unexpected query reply: {reply!r}')
            time.sleep(0.1)
        self.fail('review did not reach a terminal state in bounded time')

    def start_review(self, verb='synfont'):
        match = re.fullmatch(r'OK (review_[a-z0-9_]+) STARTED\n',
                             self.send_request(f'START_REVIEW {verb}'))
        if not match:
            self.fail('START_REVIEW was not accepted')
        return match.group(1)

    def cache_dir(self, verb):
        return self.cache_root / verb

    def assert_no_publication(self, verb):
        self.assertFalse((self.terms_dir / f'{verb}.json').exists())
        directory = self.cache_dir(verb)
        if directory.is_dir():
            self.assertEqual(list(directory.glob('*.part')), [])

    def assert_no_install_state(self):
        state = components.OPERATION_STATE
        self.assertFalse((state / 'operations.json').exists())
        self.assertEqual(sorted(p.name for p in self.machine.iterdir()), [])


class CatalogAndTermsProtocolTests(CoordinatorReviewTests):
    """N1/N3 protocol surface: metadata-only catalog, cached-only GET_TERMS."""

    def test_catalog_exposes_review_column_without_network(self):
        self.start_coordinator()
        opener = self.start_fake_network()
        reply = self.send_request('GET_COMPONENTS')
        lines = reply.split('\n')
        self.assertEqual(lines[0], 'OK')
        rows = {line.split('\t')[0]: line for line in lines[1:] if line}
        synfont_total = sum(size for size, _l, _s in tcr.FIXTURES['synfont']['table'].values())
        self.assertTrue(rows['synfont'].endswith(f'\tR:{synfont_total}:0'))
        corefonts_total = components.review_total_bytes('corefonts')
        # The real disabled component advertises review availability too.
        self.assertTrue(rows['corefonts'].endswith(f'\tR:{corefonts_total}:0'))
        self.assertTrue(rows['vcrun2022'].endswith('\t0'))
        self.assertEqual(opener.log, [])

    def test_review_info_reports_totals_and_cache_state(self):
        self.start_coordinator()
        self.start_fake_network()
        synfont_total = sum(size for size, _l, _s in tcr.FIXTURES['synfont']['table'].values())
        self.assertEqual(self.send_request('REVIEW_INFO synfont'),
                         f'OK {synfont_total} 3 0\n')
        corefonts_total = components.review_total_bytes('corefonts')
        self.assertEqual(self.send_request('REVIEW_INFO corefonts'),
                         f'OK {corefonts_total} 11 0\n')
        self.assertIn('ERR Unsupported component', self.send_request('REVIEW_INFO nosuch'))

    def test_get_terms_missing_cache_is_zero_network_and_zero_install(self):
        self.start_coordinator()
        opener = self.start_fake_network()
        with patch.object(components.subprocess, 'run',
                          side_effect=AssertionError('no subprocess')):
            reply = self.send_request('GET_TERMS synfont')
            self.assertTrue(reply.startswith('ERR NOT_DOWNLOADED '), reply)
            reply = self.send_request('GET_TERMS corefonts')
            self.assertTrue(reply.startswith('ERR NOT_DOWNLOADED '), reply)
        self.assertEqual(opener.log, [])
        self.assertFalse((self.terms_dir / 'synfont.json').exists())
        self.assert_no_install_state()

    def test_get_terms_stale_is_a_distinct_error(self):
        opener = self.start_fake_network(on_read=lambda _c: time.sleep(0.2))
        self.start_coordinator()
        op_id = self.start_review('synfont')
        reply, _running = self.wait_review(op_id)
        self.assertTrue(reply.startswith('STATUS SUCCESS '), reply)
        record_path = self.terms_dir / 'synfont.json'
        record = json.loads(record_path.read_text(encoding='utf-8'))
        record['text'] = 'TAMPERED\n'
        record_path.write_text(json.dumps(record), encoding='utf-8')
        reply = self.send_request('GET_TERMS synfont')
        self.assertTrue(reply.startswith('ERR STALE_TERMS '), reply)


class ReviewLifecycleProtocolTests(CoordinatorReviewTests):
    """N3/L1 protocol: explicit start, progress, cancel, retry, cached reopen."""

    def test_explicit_start_downloads_exactly_the_pinned_packages(self):
        opener = self.start_fake_network()
        self.start_coordinator()
        op_id = self.start_review('synfont')
        reply, _running = self.wait_review(op_id)
        match = re.fullmatch(r'STATUS SUCCESS ([0-9a-f]{64})\n', reply)
        self.assertIsNotNone(match, reply)
        fingerprint = match.group(1)
        # Exactly the pinned packages, in recipe order, each requested once.
        self.assertEqual(opener.log, expected_urls('synfont'))
        for name, data in tcr.FIXTURES['synfont']['files'].items():
            self.assertEqual((self.cache_dir('synfont') / name).read_bytes(), data)
        # Success is visible through the cached-only surface with no new traffic.
        before = len(opener.log)
        info = self.send_request('REVIEW_INFO synfont')
        total = sum(size for size, _l, _s in tcr.FIXTURES['synfont']['table'].values())
        self.assertEqual(info, f'OK {total} 3 1\n')
        terms = self.send_request('GET_TERMS synfont')
        self.assertTrue(terms.startswith(f'OK {fingerprint} '), terms[:80])
        self.assertIn('Nothing has been installed by reviewing these terms.', terms)
        self.assertEqual(len(opener.log), before)
        # A repeated start reuses the verified cache: zero further requests.
        op_id2 = self.start_review('synfont')
        reply2, _running = self.wait_review(op_id2)
        self.assertEqual(reply2, f'STATUS SUCCESS {fingerprint}\n')
        self.assertEqual(len(opener.log), before)

    def test_duplicate_start_reuses_id_and_foreign_verb_is_rejected(self):
        self.start_fake_network(on_read=lambda _c: time.sleep(0.3))
        self.start_coordinator()
        first = self.start_review('synfont')
        reply = self.send_request('START_REVIEW synfont')
        self.assertEqual(reply, f'OK {first} STARTED\n')
        reply = self.send_request('START_REVIEW synfont2')
        self.assertEqual(reply, 'ERR Another review download is active\n')
        _reply, _running = self.wait_review(first)
        # After completion the id is no longer cancellable.
        self.assertEqual(self.send_request(f'CANCEL_REVIEW {first}'),
                         'ERR No running review operation\n')

    def test_unknown_operations_are_rejected(self):
        self.start_coordinator()
        self.start_fake_network()
        self.assertEqual(self.send_request('QUERY_REVIEW review_nosuch_deadbeef'),
                         'ERR Unknown review operation\n')
        self.assertEqual(self.send_request('CANCEL_REVIEW review_nosuch_deadbeef'),
                         'ERR No running review operation\n')

    def test_cancel_mid_download_removes_partials_and_reports_cancelled(self):
        opener = self.start_fake_network(on_read=lambda _c: time.sleep(0.3))
        self.start_coordinator()
        op_id = self.start_review('synfont')
        # Wait until a partial file proves the transfer is in flight.
        deadline = time.monotonic() + 15.0
        while not list(self.cache_dir('synfont').glob('*.part')):
            if time.monotonic() >= deadline:
                self.fail('download never reached an in-flight state')
            time.sleep(0.05)
        self.assertEqual(self.send_request(f'CANCEL_REVIEW {op_id}'), 'OK CANCELLING\n')
        reply, _running = self.wait_review(op_id)
        self.assertEqual(reply, 'STATUS CANCELLED\n')
        self.assert_no_publication('synfont')
        # A retry after cancellation starts fresh and completes.
        before = len(opener.log)
        op_id2 = self.start_review('synfont')
        reply2, _running = self.wait_review(op_id2)
        self.assertTrue(reply2.startswith('STATUS SUCCESS '), reply2)
        self.assertGreater(len(opener.log), before)

    def test_failed_download_reports_error_without_receipts_or_journal(self):
        files = dict(tcr.FIXTURES['synfont']['files'])
        corrupted = bytearray(files['a1.exe'])
        corrupted[0x40] ^= 0xFF
        files['a1.exe'] = bytes(corrupted)
        opener = tcr.FakeOpener(files)
        patch.object(urllib.request, 'build_opener', return_value=opener).start()
        self.start_coordinator()
        with patch.object(components.subprocess, 'run',
                          side_effect=AssertionError('no subprocess')):
            op_id = self.start_review('synfont')
            reply, _running = self.wait_review(op_id)
        self.assertTrue(reply.startswith('STATUS FAILED '), reply)
        self.assertIn('does not match the reviewed package hash', reply)
        self.assert_no_publication('synfont')
        self.assertEqual(self.send_request('GET_RECEIPTS'), 'OK \n')
        self.assert_no_install_state()

    def test_review_success_is_not_acceptance_and_disabled_install_stays_rejected(self):
        self.start_fake_network()
        self.start_coordinator()
        with patch.object(components.subprocess, 'run',
                          side_effect=AssertionError('no subprocess')):
            op_id = self.start_review('synfont')
            reply, _running = self.wait_review(op_id)
            match = re.fullmatch(r'STATUS SUCCESS ([0-9a-f]{64})\n', reply)
        fingerprint = match.group(1)
        # No acceptance exists anywhere: the record has no accepted marker and
        # the install journal was never created by review activity.
        record = json.loads((self.terms_dir / 'synfont.json').read_text(encoding='utf-8'))
        self.assertNotIn('accepted_sha256', record)
        self.assert_no_install_state()
        # The disabled fixture is rejected for install through the protocol too.
        reply = self.send_request(f'START_INSTALL synfont {fingerprint}')
        self.assertIn('ERR', reply)
        self.assertIn('disabled', reply)
        self.assert_no_install_state()


class StagePeerAndShutdownProtocolTests(CoordinatorReviewTests):
    """B1 stage expiry / wrong peer; L1 bounded shutdown and cache preservation."""

    def _wait_running(self, op_id, timeout=10.0):
        deadline = time.monotonic() + timeout
        while True:
            if self.send_request(f'QUERY_REVIEW {op_id}').startswith('STATUS RUNNING'):
                return
            if time.monotonic() >= deadline:
                self.fail('review did not enter the running state')
            time.sleep(0.05)

    def test_expired_stage_stops_new_reviews_and_shuts_down_bounded(self):
        self.start_fake_network(on_read=lambda _c: time.sleep(0.5))
        self.start_coordinator()
        op_id = self.start_review('synfont')
        self._wait_running(op_id)
        started = time.monotonic()
        self.stage_file.write_text('done\n', encoding='ascii')
        # A new review must never start after expiry: either the handler
        # rejects it or the endpoint is already gone (EOF / refused).
        reply = self.try_request('START_REVIEW synfont2')
        if reply is not None and reply.strip():
            self.assertEqual(reply,
                             'ERR Setup OOBE stage expired; review download rejected\n')
        self.coord_thread.join(timeout=15)
        self.assertFalse(self.coord_thread.is_alive())
        self.assertLessEqual(time.monotonic() - started, 14)
        self.assertFalse(self.sock_path.exists())
        # The running review was cancelled at shutdown: nothing was published.
        self.assert_no_publication('synfont')
        self.assertFalse((self.cache_root / 'synfont2').exists())
        self.assert_no_install_state()

    def test_shutdown_preserves_valid_cache_and_removes_partials(self):
        # Pre-seed the first package as fully valid: the download skips it and
        # is in flight on the second when shutdown cancels it.
        directory = self.cache_dir('synfont')
        directory.mkdir(mode=0o700, parents=True)
        first = 'a1.exe'
        target = directory / first
        target.write_bytes(tcr.FIXTURES['synfont']['files'][first])
        os.chmod(target, 0o600)
        self.start_fake_network(on_read=lambda _c: time.sleep(0.5))
        self.start_coordinator()
        op_id = self.start_review('synfont')
        deadline = time.monotonic() + 15.0
        while not (directory / 'a2.exe.part').exists():
            if time.monotonic() >= deadline:
                self.fail('second package never reached an in-flight state')
            time.sleep(0.05)
        started = time.monotonic()
        self.stage_file.write_text('done\n', encoding='ascii')
        self.coord_thread.join(timeout=15)
        self.assertFalse(self.coord_thread.is_alive())
        self.assertLessEqual(time.monotonic() - started, 14)
        # The valid pre-existing package survives; the partial and any
        # publication are gone.
        self.assertEqual(target.read_bytes(), tcr.FIXTURES['synfont']['files'][first])
        self.assertFalse((directory / 'a2.exe.part').exists())
        self.assert_no_publication('synfont')

    def test_wrong_peer_uid_is_rejected_without_processing(self):
        self.start_coordinator()
        opener = self.start_fake_network()
        code = (
            "import os, socket\n"
            "os.setuid(65534)\n"
            f"s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)\n"
            "s.settimeout(3.0)\n"
            f"s.connect({str(self.sock_path)!r})\n"
            "s.sendall(b'REVIEW_INFO synfont\\n')\n"
            "try:\n"
            "    data = s.recv(4096)\n"
            "except ConnectionResetError:\n"
            "    data = b''  # server closed with the request unread: rejected\n"
            "print('EOF' if not data else 'DATA')\n"
        )
        result = subprocess.run([sys.executable, '-I', '-c', code],
                                capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), 'EOF')
        self.assertEqual(opener.log, [])


if __name__ == '__main__':
    unittest.main(verbosity=2)
