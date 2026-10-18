#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the actual Unix transport functions against an isolated socket peer."""
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parent.parent


class BridgeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('cc')
        if not compiler:
            raise unittest.SkipTest('C compiler unavailable')
        cls.scratch = tempfile.TemporaryDirectory(prefix='wex-bridge-')
        cls.addClassCleanup(cls.scratch.cleanup)
        directory = Path(cls.scratch.name)
        source = (ROOT / 'src/dlls/wexhost/unixlib.c').read_text()
        helpers = source[source.index('static BOOL oobe_write_all('):
                         source.index('static NTSTATUS oobe_get_components(')]
        install = source[source.index('static NTSTATUS oobe_install('):
                         source.index('static NTSTATUS oobe_get_receipts(')]
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
struct oobe_install_params { char verb[32]; char sha256[65]; char err_buf[256]; DWORD err; };
'''
        harness += helpers + install + r'''
int main(void) {
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
        cls.binary = directory / 'bridge'
        subprocess.run([compiler, '-Wall', '-Wextra', '-Werror',
                        '-Wno-sign-compare', str(path), '-o', str(cls.binary)], check=True)

    def exercise(self, frames, timeout=10):
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
                result = subprocess.run([str(self.binary)], capture_output=True, text=True,
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


if __name__ == '__main__':
    unittest.main(verbosity=2)
