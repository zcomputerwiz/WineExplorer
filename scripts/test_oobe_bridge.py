#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the actual Unix transport functions against an isolated socket peer."""
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import Mock, patch
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
if str(ROOT / 'scripts') not in sys.path:
    sys.path.insert(0, str(ROOT / 'scripts'))

import test_component_review as tcr


def _compile_bridge_binary(directory):
    compiler = shutil.which('cc')
    if not compiler:
        raise unittest.SkipTest('C compiler unavailable')
    source = (ROOT / 'src/dlls/wexhost/unixlib.c').read_text()
    helpers = source[source.index('static BOOL oobe_write_all('):
                     source.index('static NTSTATUS oobe_get_components(')]
    install = source[source.index('static NTSTATUS oobe_install('):
                     source.index('static NTSTATUS oobe_get_receipts(')]
    terms = source[source.index('static NTSTATUS oobe_get_terms('):
                   source.index('static NTSTATUS oobe_install(')]
    review = source[source.index('/* BEGIN review-wire strict parsing (Q6) */'):
                    source.index('const unixlib_entry_t __wine_unix_call_funcs[] =')]
    harness = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
typedef unsigned int DWORD;
typedef int BOOL;
typedef int NTSTATUS;
#define TRUE 1
#define FALSE 0
#define STATUS_SUCCESS 0
#define ERROR_SUCCESS 0
#define ERROR_NOT_SUPPORTED 50
#define ERROR_GEN_FAILURE 31
#define ERROR_TIMEOUT 1460
#define ERROR_NOT_ENOUGH_MEMORY 8
#define ERROR_INVALID_DATA 13
#define ERROR_MORE_DATA 234
typedef unsigned long long ULONG64;
typedef unsigned long ULONG_PTR;
struct oobe_terms_params { char verb[32]; char sha256[65]; ULONG64 buffer; DWORD size; DWORD needed; DWORD err; };
struct oobe_install_params { char verb[32]; char sha256[65]; char err_buf[256]; DWORD err; };
struct oobe_review_info_params { char verb[32]; ULONG64 total; DWORD packages; BOOL cached; DWORD err; };
struct oobe_review_start_params { char verb[32]; char op_id[96]; char err_buf[256]; DWORD err; };
struct oobe_review_query_params { char op_id[96]; char status[16]; ULONG64 done; ULONG64 total; char fingerprint[65]; char err_buf[256]; DWORD err; };
struct oobe_review_cancel_params { char op_id[96]; char err_buf[256]; DWORD err; };
static inline int lstrcpynA( char *dest, const char *src, unsigned int max )
{
    unsigned int i;
    for (i = 0; i + 1 < max && src[i]; ++i)
        dest[i] = src[i];
    dest[i] = '\0';
    return i;
}
'''
    harness += helpers + terms + install + review + r'''
int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "terms")) {
        char body[16384];
        struct oobe_terms_params t = {0};
        lstrcpynA(t.verb, (argc > 2) ? argv[2] : "corefonts", sizeof(t.verb));
        t.buffer = (ULONG_PTR)body;
        t.size = sizeof(body);
        oobe_get_terms(&t);
        if (argc > 2)
            printf("%u|%u|%s\n", t.err, t.needed, t.sha256);
        else
            printf("%u %u\n", t.err, t.needed);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "info")) {
        struct oobe_review_info_params p = {0};
        lstrcpynA(p.verb, (argc > 2) ? argv[2] : "corefonts", sizeof(p.verb));
        oobe_review_info(&p);
        printf("%u|%llu|%u|%u\n", p.err, p.total, p.packages, p.cached);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "start")) {
        struct oobe_review_start_params p = {0};
        lstrcpynA(p.verb, (argc > 2) ? argv[2] : "corefonts", sizeof(p.verb));
        oobe_review_start(&p);
        printf("%u|%s|%s\n", p.err, p.op_id, p.err_buf);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "start_retry")) {
        struct oobe_review_start_params p = {0};
        strcpy(p.verb, "corefonts");
        oobe_review_start(&p);
        printf("%u|%s|%s\n", p.err, p.op_id, p.err_buf);
        memset(&p, 0, sizeof(p));
        strcpy(p.verb, "corefonts");
        oobe_review_start(&p);
        printf("%u|%s|%s\n", p.err, p.op_id, p.err_buf);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "query")) {
        struct oobe_review_query_params p = {0};
        lstrcpynA(p.op_id, (argc > 2) ? argv[2] : "review_corefonts_deadbeef", sizeof(p.op_id));
        oobe_review_query(&p);
        printf("%u|%s|%llu|%llu|%s|%s\n", p.err, p.status, p.done, p.total, p.fingerprint, p.err_buf);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "query_retry")) {
        struct oobe_review_query_params p = {0};
        strcpy(p.op_id, "review_corefonts_deadbeef");
        oobe_review_query(&p);
        printf("%u|%s|%llu|%llu|%s|%s\n", p.err, p.status, p.done, p.total, p.fingerprint, p.err_buf);
        if (p.err == ERROR_TIMEOUT) {
            memset(&p, 0, sizeof(p));
            strcpy(p.op_id, "review_corefonts_deadbeef");
            oobe_review_query(&p);
            printf("%u|%s|%llu|%llu|%s|%s\n", p.err, p.status, p.done, p.total, p.fingerprint, p.err_buf);
        }
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "cancel")) {
        struct oobe_review_cancel_params p = {0};
        lstrcpynA(p.op_id, (argc > 2) ? argv[2] : "review_corefonts_deadbeef", sizeof(p.op_id));
        oobe_review_cancel(&p);
        printf("%u|%s\n", p.err, p.err_buf);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "install")) {
        struct oobe_install_params p = {0};
        lstrcpynA(p.verb, (argc > 2) ? argv[2] : "corefonts", sizeof(p.verb));
        if (argc > 3)
            lstrcpynA(p.sha256, argv[3], sizeof(p.sha256));
        else
            memset(p.sha256, 'a', 64);
        oobe_install(&p);
        printf("%u|%s\n", p.err, p.err_buf);
        return 0;
    }
    struct oobe_install_params p = {0};
    strcpy(p.verb, "corefonts");
    memset(p.sha256, 'a', 64);
    oobe_install(&p);
    printf("%u %s\n", p.err, p.err_buf);
    return 0;
}
'''
    path = directory / 'bridge.c'
    path.write_text(harness)
    binary = directory / 'bridge'
    subprocess.run([compiler, '-Wall', '-Wextra', '-Werror',
                    '-Wno-sign-compare', str(path), '-o', str(binary)], check=True)
    return binary


class BridgeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.scratch = tempfile.TemporaryDirectory(prefix='wex-bridge-')
        cls.addClassCleanup(cls.scratch.cleanup)
        cls.binary = _compile_bridge_binary(Path(cls.scratch.name))

    def exercise(self, frames, timeout=10, mode=None):
        endpoint = str(Path(self.scratch.name) / 'peer.sock')
        Path(endpoint).unlink(missing_ok=True)
        errors, requests = [], []
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as server:
            server.bind(endpoint)
            server.listen(5)
            server.settimeout(5)
            def peer():
                try:
                    for frame in frames:
                        conn, _ = server.accept()
                        with conn:
                            conn.settimeout(3)
                            request = b''
                            while not request.endswith(b'\n'):
                                chunk = conn.recv(1024)
                                if not chunk:
                                    break
                                request += chunk
                            requests.append(request)
                            conn.sendall(frame)
                except Exception as error:
                    errors.append(error)
            worker = threading.Thread(target=peer)
            worker.start()
            try:
                result = subprocess.run([str(self.binary)] + ([mode] if mode else []), capture_output=True, text=True,
                                        env=dict(os.environ, WEX_OOBE_SOCKET=endpoint), timeout=timeout)
            finally:
                worker.join(6)
            self.assertFalse(worker.is_alive())
            self.assertFalse(errors, errors)
            self.assertEqual(result.returncode, 0, result.stderr)
            return result.stdout, requests

    def test_empty_and_partial_queries_reconnect_without_resubmitting(self):
        output, requests = self.exercise([b'OK op_fixture STARTED\n', b'',
                                         b'STATUS SUCCESS', b'STATUS RUNNING\n',
                                         b'STATUS SUCCESS\n'])
        self.assertEqual(output, '0 \n')
        self.assertEqual(sum(r.startswith(b'START_INSTALL ') for r in requests), 1)
        self.assertEqual(sum(r == b'QUERY_INSTALL op_fixture\n' for r in requests), 4)

    def test_partial_acknowledgement_is_not_success(self):
        output, _ = self.exercise([b'OK ALREADY_INSTALLED'])
        self.assertTrue(output.startswith('1460 '), output)
        self.assertIn('outcome unknown', output)

    def test_installer_failure_is_preserved(self):
        output, _ = self.exercise([b'OK op_fixture STARTED\n', b'STATUS FAILED fixture failure\n'])
        self.assertTrue(output.startswith('1603 fixture failure'), output)

    def test_running_install_can_exceed_socket_timeout(self):
        output, requests = self.exercise([b'OK op_fixture STARTED\n'] +
                                         [b'STATUS RUNNING\n'] * 32 +
                                         [b'STATUS SUCCESS\n'], timeout=25)
        self.assertEqual(output, '0 \n')
        self.assertEqual(len(requests), 34)

    def test_terms_require_exact_complete_frame(self):
        digest = b'a' * 64
        frames = [b'OK ' + digest + b' 3\nabc',
                  b'OK ' + digest + b' 4\nabc',
                  b'OK ' + digest + b' 2\nabc',
                  b'OK ' + digest + b'\nabc',
                  b'OK ' + digest + b' 3 EXTRA\nabc',
                  b'OK ' + b'z' * 64 + b' 3\nabc',
                  b'OK ' + digest + b' 3\na\x00c']
        for index, frame in enumerate(frames):
            with self.subTest(index=index):
                output, requests = self.exercise([frame], mode='terms')
                self.assertEqual(requests, [b'GET_TERMS corefonts\n'])
                if index == 0:
                    self.assertEqual(output, '0 4\n')
                else:
                    self.assertNotEqual(output.split()[0], '0')

    # B1: the real C transport for the four review verbs. Each case drives one
    # fresh connection against a scripted peer; frames model what the Python
    # coordinator (or its absence) can actually send back.

    def test_review_info_parses_totals_and_cache_state(self):
        output, requests = self.exercise([b'OK 123456 3 0\n'], mode='info')
        self.assertEqual(output, '0|123456|3|0\n', output)
        self.assertEqual(requests, [b'REVIEW_INFO corefonts\n'])
        # The exact catalog maximum (16 GiB) is a valid boundary value.
        output, _ = self.exercise([b'OK 17179869184 11 1\n'], mode='info')
        self.assertEqual(output, '0|17179869184|11|1\n', output)

    def test_review_info_rejects_malformed_oversize_and_partial(self):
        # (frame, expected err). \n-terminated malformed frames are GEN_FAILURE;
        # missing-newline / empty / oversized frames time out.
        cases = [
            (b'OK 123456 3 2\n', '31'),                    # cached flag out of range
            (b'OK 123456 3 0 x\n', '31'),                  # trailing junk after fields
            (b'ERR Unsupported component: nosuch\n', '31'),
            (b'OK -5 3 0\n', '31'),                        # negative sign is not decimal
            (b'OK +5 3 0\n', '31'),                        # plus sign unsupported
            (b'OK 05 3 0\n', '31'),                        # leading zero not canonical
            (b'OK 17179869185 3 0\n', '31'),               # total over the 16 GiB cap
            (b'OK 5 4294967296 0\n', '31'),                # package count over cap
            (b'OK 18446744073709551615 3 0\n', '31'),      # unsigned overflow boundary
            (b'OK 123456 3 0', '1460'),                    # no terminating newline
            (b'', '1460'),                                 # peer closed without data
            (b'OK ' + b'9' * 200 + b' 3 0\n', '1460'),     # reply exceeds the C buffer
        ]
        for index, (frame, expected) in enumerate(cases):
            with self.subTest(index=index):
                output, _ = self.exercise([frame], mode='info')
                err = output.split('|')[0]
                self.assertEqual(err, expected, output)

    def test_review_start_acknowledgement_and_idempotent_retry(self):
        op = b'review_corefonts_0123456789abcdef0123456789abcdef'
        ack = b'OK ' + op + b' STARTED\n'
        output, requests = self.exercise([ack], mode='start')
        self.assertEqual(output.strip().split('|'), ['0', op.decode(), ''], output)
        self.assertEqual(requests, [b'START_REVIEW corefonts\n'])
        # Lost acknowledgement: the transport must not claim success, and the
        # same-verb retry reconnects with an identical request (the coordinator
        # treats a repeated start as idempotent).
        output, requests = self.exercise([b'', ack], mode='start_retry')
        lines = output.strip().splitlines()
        self.assertEqual(lines[0].split('|')[0], '1460', output)
        self.assertIn('outcome unknown', lines[0])
        self.assertEqual(lines[1].split('|'), ['0', op.decode(), ''], output)
        self.assertEqual(requests, [b'START_REVIEW corefonts\n'] * 2)

    def test_review_start_reports_coordinator_errors(self):
        frames = [b'ERR Setup OOBE stage expired; review download rejected\n',
                  b'ERR Another review download is active\n',
                  b'OK review_x\n',                     # missing STARTED keyword
                  b'OK ' + b'r' * 96 + b' STARTED\n']   # operation id over field limit
        for index, frame in enumerate(frames):
            with self.subTest(index=index):
                output, _ = self.exercise([frame], mode='start')
                err, _, message = output.strip().split('|', 2)
                self.assertEqual(err, '31', output)     # ERROR_GEN_FAILURE
                if index == 0:
                    self.assertEqual(message, 'Setup OOBE stage expired; review download rejected')
                elif index == 1:
                    self.assertEqual(message, 'Another review download is active')
                else:
                    self.assertEqual(message, 'Malformed coordinator acknowledgement')

    # F1 (Round 2): the STARTED acknowledgement's operation id must match the
    # coordinator's exact START_REVIEW grammar for the requested verb —
    # "review_" <verb> "_" + exactly 32 lowercase hex digits (distinct from
    # START_INSTALL's "op_" prefix). A framed-but-wrong token (wrong verb,
    # install "op_" prefix, uppercase, short/long, non-hex, or an extra
    # trailing token) must neither succeed nor populate an accepted id that
    # later rides into QUERY/CANCEL.
    def test_review_start_rejects_malformed_operation_ids(self):
        hex32 = b'0123456789abcdef0123456789abcdef'
        bad_frames = [
            b'OK review_synfont_' + hex32 + b' STARTED\n',            # wrong verb
            b'OK op_corefonts_' + hex32 + b' STARTED\n',              # install op_ prefix rejected for review
            b'OK review_corefonts_0123456789ABCDEF0123456789ABCDEF STARTED\n',  # uppercase hex
            b'OK review_corefonts_' + hex32[:31] + b' STARTED\n',     # one byte short
            b'OK review_corefonts_' + hex32 + b'0 STARTED\n',         # one byte long
            b'OK review_corefonts_' + b'g' * 32 + b' STARTED\n',      # non-hex digits
            b'OK corefonts_' + hex32 + b' STARTED\n',                 # missing "review_" prefix
            b'OK review_corefonts_' + hex32 + b' extra STARTED\n',    # trailing token
        ]
        for index, frame in enumerate(bad_frames):
            with self.subTest(index=index):
                output, _ = self.exercise([frame], mode='start')
                parts = output.strip().split('|')
                self.assertEqual(parts[0], '31', output)          # ERROR_GEN_FAILURE
                self.assertEqual(parts[1], '', output)            # no accepted id populated
                self.assertEqual(parts[2], 'Malformed coordinator acknowledgement', output)
        # Positive control: the exact grammar for the requested verb is accepted and
        # populates the id that later rides into QUERY_REVIEW/CANCEL_REVIEW.
        good = b'review_corefonts_' + hex32
        output, _ = self.exercise([b'OK ' + good + b' STARTED\n'], mode='start')
        self.assertEqual(output.strip().split('|'), ['0', good.decode(), ''], output)

    def test_review_query_states_and_fingerprint_validation(self):
        digest = b'0123456789abcdef' * 4
        output, requests = self.exercise([b'STATUS RUNNING 1024 4096\n'], mode='query')
        self.assertEqual(output.strip(), '0|RUNNING|1024|4096||', output)
        self.assertEqual(requests, [b'QUERY_REVIEW review_corefonts_deadbeef\n'])
        output, _ = self.exercise([b'STATUS CANCELLED\n'], mode='query')
        self.assertEqual(output.strip(), '0|CANCELLED|0|0||', output)
        output, _ = self.exercise([b'STATUS SUCCESS ' + digest + b'\n'], mode='query')
        self.assertEqual(output.strip(), '0|SUCCESS|0|0|' + digest.decode() + '|', output)

    def test_review_query_rejects_invalid_fingerprints_and_frames(self):
        frames = [b'STATUS SUCCESS ' + b'A' * 64 + b'\n',   # uppercase hex
                  b'STATUS SUCCESS ' + b'a' * 63 + b'\n',   # one byte short
                  b'STATUS SUCCESS ' + b'a' * 65 + b'\n',   # one byte long
                  b'STATUS RUNNING 1024 4096',              # no terminating newline
                  b'',                                      # peer closed without data
                  b'ERR Unknown review operation\n',        # unknown operation id
                  b'GARBAGE FRAME\n']                       # unrecognised reply
        for index, frame in enumerate(frames):
            with self.subTest(index=index):
                output, _ = self.exercise([frame], mode='query')
                parts = output.strip().split('|')
                if index in (3, 4):
                    self.assertEqual(parts[0], '1460', output)   # ERROR_TIMEOUT
                else:
                    self.assertEqual(parts[0], '13', output)     # ERROR_INVALID_DATA
                    self.assertEqual(parts[1], '', output)       # no status reported
        output, _ = self.exercise([b'STATUS FAILED  hash mismatch detail\n'], mode='query')
        parts = output.strip().split('|')
        self.assertEqual(parts[0], '31', output)                 # ERROR_GEN_FAILURE
        self.assertEqual(parts[1], 'FAILED')
        self.assertEqual('|'.join(parts[5:]), 'hash mismatch detail')

    def test_review_query_running_rejects_bad_progress(self):
        # Progress counters must be exact, bounded decimals with done <= total;
        # a malformed RUNNING frame is INVALID_DATA with no status reported.
        frames = [b'STATUS RUNNING -5 4096\n',              # negative done
                  b'STATUS RUNNING 1024 +4096\n',           # plus sign unsupported
                  b'STATUS RUNNING 01024 4096\n',           # leading zero not canonical
                  b'STATUS RUNNING 8192 4096\n',            # done exceeds total
                  b'STATUS RUNNING 0 0\n',                  # zero total: no bytes to transfer
                  b'STATUS RUNNING 17179869185 17179869185\n',  # over-cap counters
                  b'STATUS RUNNING 1024 4096 x\n']          # trailing junk
        for index, frame in enumerate(frames):
            with self.subTest(index=index):
                output, _ = self.exercise([frame], mode='query')
                parts = output.strip().split('|')
                self.assertEqual(parts[0], '13', output)     # ERROR_INVALID_DATA
                self.assertEqual(parts[1], '', output)       # no status reported
        # The exact 16 GiB boundary with done == total is a valid terminal frame.
        output, _ = self.exercise([b'STATUS RUNNING 17179869184 17179869184\n'], mode='query')
        self.assertEqual(output.strip(), '0|RUNNING|17179869184|17179869184||', output)
        # done == 0 is a valid start-of-transfer state whenever total is positive.
        output, _ = self.exercise([b'STATUS RUNNING 0 4096\n'], mode='query')
        self.assertEqual(output.strip(), '0|RUNNING|0|4096||', output)

    def test_review_query_retry_is_read_only(self):
        output, requests = self.exercise([b'', b'STATUS CANCELLED\n'], mode='query_retry')
        lines = output.strip().splitlines()
        self.assertEqual(lines[0].split('|')[0], '1460', output)
        self.assertEqual(lines[1], '0|CANCELLED|0|0||', output)
        # A timed-out query is retried as the same read-only request; it can
        # never resubmit a start or an install.
        self.assertEqual(requests, [b'QUERY_REVIEW review_corefonts_deadbeef\n'] * 2)

    def test_review_cancel_ack_and_errors(self):
        output, requests = self.exercise([b'OK CANCELLING\n'], mode='cancel')
        self.assertEqual(output.strip(), '0|', output)
        self.assertEqual(requests, [b'CANCEL_REVIEW review_corefonts_deadbeef\n'])
        frames = [b'ERR No running review operation\n', b'OK DONE\n', b'OK CANCELLING']
        for index, frame in enumerate(frames):
            with self.subTest(index=index):
                output, _ = self.exercise([frame], mode='cancel')
                parts = output.strip().split('|')
                if index == 2:
                    self.assertEqual(parts[0], '1460', output)   # ERROR_TIMEOUT
                else:
                    self.assertEqual(parts[0], '13', output)     # ERROR_INVALID_DATA
        output, _ = self.exercise([b'ERR No running review operation\n'], mode='cancel')
        self.assertEqual(output.strip(), '13|No running review operation', output)

    # F1: a well-formed first line followed by extra bytes (a second frame or an
    # embedded NUL) must be rejected as a whole, not parsed to its C-string
    # prefix. The C readers stop at the first NUL, so only validating the full
    # byte-counted reply catches these. All-zero review-info totals are also
    # rejected: a real catalog always has bytes and at least one package.
    def test_review_rejects_trailing_frames_and_embedded_nuls(self):
        info_frames = [b'OK 123456 3 0\nEXTRA\n',      # trailing second frame
                       b'OK 123456 3 0\n\x00EXTRA\n',  # embedded NUL then junk
                       b'OK 0 0 0\n']                   # zero total and count
        for index, frame in enumerate(info_frames):
            with self.subTest(index=index):
                output, _ = self.exercise([frame], mode='info')
                # Nonzero error (GEN_FAILURE) and no successful metadata leaked.
                self.assertEqual(output.strip(), '31|0|0|0', output)
        query_frames = [b'STATUS RUNNING 1 2\nEXTRA\n',     # trailing second frame
                        b'STATUS RUNNING 1 2\n\x00EXTRA\n'] # embedded NUL then junk
        for index, frame in enumerate(query_frames):
            with self.subTest(index=index):
                output, _ = self.exercise([frame], mode='query')
                # Nonzero error (INVALID_DATA), no status and no progress reported.
                self.assertEqual(output.strip(), '13||0|0||', output)

    def test_review_unreachable_socket_is_not_supported(self):
        endpoint = str(Path(self.scratch.name) / 'missing.sock')
        for mode in ('info', 'start', 'query', 'cancel'):
            with self.subTest(mode=mode):
                result = subprocess.run([str(self.binary), mode], capture_output=True, text=True,
                                        env=dict(os.environ, WEX_OOBE_SOCKET=endpoint), timeout=10)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.split('|')[0], '50', result.stdout)


class LiveCoordinatorBridgeTests(unittest.TestCase):
    """End-to-end verification of compiled C unixlib.c functions against live oobe_coordinator()."""

    @classmethod
    def setUpClass(cls):
        cls.scratch = tempfile.TemporaryDirectory(prefix='wex-live-bridge-bin-')
        cls.addClassCleanup(cls.scratch.cleanup)
        cls.binary = _compile_bridge_binary(Path(cls.scratch.name))

    def setUp(self):
        self.base = tcr.private_base()
        self.addCleanup(shutil.rmtree, self.base, ignore_errors=True)
        run_parent = '/run' if (os.geteuid() == 0 and os.access('/run', os.W_OK)) else None
        self.run_dir = Path(tempfile.mkdtemp(prefix='wex-live-bridge-', dir=run_parent))
        os.chmod(self.run_dir, 0o755)
        self.addCleanup(shutil.rmtree, self.run_dir, ignore_errors=True)
        self.sock_path = self.run_dir / 'coordinator.sock'
        self.stage_file = self.run_dir / 'stage'
        self.stage_file.write_text('oobe\n', encoding='ascii')
        self.cache_root = self.base / 'review-cache'
        self.terms_dir = self.base / 'terms'
        self.ops_dir = self.base / 'operation-state'
        self.receipts_dir = self.base / 'receipts'
        self.machine = self.base / 'machine'
        self.receipts_dir.mkdir(mode=0o700, parents=True, exist_ok=True)
        self.machine.mkdir(mode=0o700, parents=True, exist_ok=True)

        self.addCleanup(patch.stopall)
        patch.object(tcr.components, 'COMPONENTS', tcr.COMPONENTS_MAP).start()
        patch.object(tcr.components, 'COMPONENTS_METADATA', tcr.METADATA_MAP).start()
        patch.object(tcr.components, 'OPERATION_STATE', self.ops_dir).start()
        patch.object(tcr.components, 'REVIEW_CACHE', self.cache_root).start()
        patch.object(tcr.components, 'TERMS', self.terms_dir).start()
        patch.object(tcr.components, 'MACHINE', self.machine).start()
        patch.dict(os.environ, {
            'WEX_OOBE_SOCKET': str(self.sock_path),
            'WEX_OOBE_STAGE_FILE': str(self.stage_file),
            'WEX_COMPONENTS_DIR': str(self.receipts_dir),
            'WEX_MACHINE_PREFIX': str(self.machine),
        }).start()
        patch.object(tcr.components.os, 'getuid', return_value=0).start()
        patch.object(tcr.components.pwd, 'getpwnam',
                     return_value=Mock(pw_uid=os.getuid())).start()
        if os.geteuid() != 0:
            orig_stat, orig_lstat = Path.stat, Path.lstat

            class _RootStat:
                def __init__(self, st):
                    self._st = st

                def __getattr__(self, name):
                    if name == 'st_uid':
                        return 0
                    if name == 'st_mode':
                        return self._st.st_mode & ~0o022
                    return getattr(self._st, name)

            patch.object(Path, 'stat', lambda p, *a, **kw: _RootStat(orig_stat(p, *a, **kw))).start()
            patch.object(Path, 'lstat', lambda p, *a, **kw: _RootStat(orig_lstat(p, *a, **kw))).start()
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

    def _start_coordinator(self, verb='synfont', on_read=None):
        opener = tcr.FakeOpener(tcr.FIXTURES[verb]['files'], on_read=on_read)
        patch.object(urllib.request, 'build_opener', return_value=opener).start()
        self.coord_thread = threading.Thread(target=tcr.components.oobe_coordinator, daemon=True)
        self.coord_thread.start()
        for _ in range(100):
            try:
                with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as probe:
                    probe.settimeout(0.1)
                    probe.connect(str(self.sock_path))
                return opener
            except OSError:
                time.sleep(0.01)
        self.fail('Coordinator did not start listening')

    def _run_bridge(self, *args, timeout=15):
        result = subprocess.run([str(self.binary), *args], capture_output=True, text=True,
                                env=dict(os.environ, WEX_OOBE_SOCKET=str(self.sock_path)),
                                timeout=timeout)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout.strip()

    def test_live_coordinator_review_and_terms_roundtrip(self):
        self._start_coordinator('synfont')
        expected_total = sum(size for size, _l, _s in tcr.FIXTURES['synfont']['table'].values())

        # 1. Pre-review REVIEW_INFO reports exact byte total, package count, and cached=0.
        self.assertEqual(self._run_bridge('info', 'synfont'), f'0|{expected_total}|3|0')

        # 2. Pre-review GET_TERMS is rejected (no implicit download).
        self.assertEqual(self._run_bridge('terms', 'synfont').split('|')[0], '31')

        # 3. START_REVIEW returns a valid review_synfont_<32hex> operation ID accepted by C validator.
        start_parts = self._run_bridge('start', 'synfont').split('|')
        self.assertEqual(start_parts[0], '0', start_parts)
        op_id = start_parts[1]
        self.assertRegex(op_id, r'^review_synfont_[0-9a-f]{32}$')

        # 4. QUERY_REVIEW reaches SUCCESS and returns the 64-hex consent fingerprint.
        fingerprint = None
        deadline = time.monotonic() + 10.0
        while time.monotonic() < deadline:
            q_parts = self._run_bridge('query', op_id).split('|')
            self.assertEqual(q_parts[0], '0', q_parts)
            if q_parts[1] == 'SUCCESS':
                fingerprint = q_parts[4]
                break
            self.assertEqual(q_parts[1], 'RUNNING', q_parts)
            time.sleep(0.02)
        self.assertIsNotNone(fingerprint, 'Review did not reach SUCCESS')
        self.assertRegex(fingerprint, r'^[0-9a-f]{64}$')

        # 5. Post-review REVIEW_INFO reports cached=1 and GET_TERMS returns the exact fingerprint + body.
        self.assertEqual(self._run_bridge('info', 'synfont'), f'0|{expected_total}|3|1')
        terms_parts = self._run_bridge('terms', 'synfont').split('|')
        self.assertEqual(terms_parts[0], '0', terms_parts)
        self.assertGreater(int(terms_parts[1]), 200)
        self.assertEqual(terms_parts[2], fingerprint)

        # 6. Review-only component remains rejected on START_INSTALL with zero install state created.
        inst_parts = self._run_bridge('install', 'synfont', fingerprint).split('|', 1)
        self.assertEqual(inst_parts[0], '1603')
        self.assertIn('disabled', inst_parts[1])
        self.assertFalse((self.ops_dir / 'operations.json').exists())

    def test_live_coordinator_idempotent_start_running_and_cancel(self):
        gate = threading.Event()
        entered_read = threading.Event()

        def slow_read(_chunk):
            entered_read.set()
            gate.wait(timeout=5.0)

        self._start_coordinator('synfont', on_read=slow_read)
        expected_total = sum(size for size, _l, _s in tcr.FIXTURES['synfont']['table'].values())

        # Duplicate START_REVIEW while in flight returns the exact same review_<verb>_<32hex> ID.
        first = self._run_bridge('start', 'synfont').split('|')
        self.assertEqual(first[0], '0', first)
        op_id = first[1]
        self.assertTrue(entered_read.wait(timeout=5.0))
        second = self._run_bridge('start', 'synfont').split('|')
        self.assertEqual(second, first)

        # In-flight QUERY_REVIEW reports RUNNING with positive total.
        q_running = self._run_bridge('query', op_id).split('|')
        self.assertEqual(q_running[0], '0', q_running)
        self.assertEqual(q_running[1], 'RUNNING', q_running)
        self.assertEqual(int(q_running[3]), expected_total)

        # CANCEL_REVIEW acknowledges OK CANCELLING and transitions to CANCELLED.
        self.assertEqual(self._run_bridge('cancel', op_id), '0|')
        gate.set()
        status = None
        deadline = time.monotonic() + 10.0
        while time.monotonic() < deadline:
            q_parts = self._run_bridge('query', op_id).split('|')
            self.assertEqual(q_parts[0], '0', q_parts)
            if q_parts[1] == 'CANCELLED':
                status = q_parts[1]
                break
            time.sleep(0.02)
        self.assertEqual(status, 'CANCELLED')

    def test_live_coordinator_eligible_install_and_already_installed(self):
        self._start_coordinator('synfont2')
        start_parts = self._run_bridge('start', 'synfont2').split('|')
        self.assertEqual(start_parts[0], '0', start_parts)
        op_id = start_parts[1]
        self.assertRegex(op_id, r'^review_synfont2_[0-9a-f]{32}$')

        fingerprint = None
        deadline = time.monotonic() + 10.0
        while time.monotonic() < deadline:
            q_parts = self._run_bridge('query', op_id).split('|')
            if q_parts[1] == 'SUCCESS':
                fingerprint = q_parts[4]
                break
            time.sleep(0.02)
        self.assertIsNotNone(fingerprint)

        installed_state = {'done': False}

        def fake_install(v, s):
            self.assertEqual((v, s), ('synfont2', fingerprint))
            installed_state['done'] = True

        with patch.object(tcr.components, 'install', side_effect=fake_install), \
             patch.object(tcr.components, 'is_installed',
                          side_effect=lambda v, s=None: installed_state['done']):
            # First call drives START_INSTALL (op_synfont2_<32hex>) + QUERY_INSTALL (STATUS SUCCESS).
            self.assertEqual(self._run_bridge('install', 'synfont2', fingerprint), '0|')
            self.assertTrue(installed_state['done'])
            # Second call hits OK ALREADY_INSTALLED directly.
            self.assertEqual(self._run_bridge('install', 'synfont2', fingerprint), '0|')


if __name__ == '__main__':
    unittest.main(verbosity=2)
