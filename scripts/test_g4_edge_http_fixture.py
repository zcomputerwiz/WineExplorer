# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual local sockets, immutable original bytes, no browser/VM/vendor code."""
import hashlib
import http.client
from pathlib import Path
import socket
import socketserver
import threading
import time
import unittest
from unittest.mock import patch

from g4_edge_http_fixture import FixtureServer


class HttpFixtureTests(unittest.TestCase):
    nonce='a'*32
    fixture=Path(__file__).with_name('g4-edge-probe')/'index.html'

    def setUp(self):
        self.server=FixtureServer(self.fixture,self.nonce)
        self.url=self.server.start()
        self.host,self.port=self.server.server.server_address
        self.addCleanup(self.close_if_running)

    def close_if_running(self):
        if not self.server.stopping.is_set():
            try: self.server.close()
            except RuntimeError:
                if self.server.thread.is_alive(): raise

    def request(self,path,method='GET'):
        connection=http.client.HTTPConnection(self.host,self.port,timeout=2)
        try:
            connection.request(method,path)
            response=connection.getresponse()
            return response.status,dict(response.getheaders()),response.read()
        finally: connection.close()

    def wait_records(self,count):
        until=time.monotonic()+2
        while len(self.server.record['requests'])<count and time.monotonic()<until: time.sleep(.005)
        self.assertGreaterEqual(len(self.server.record['requests']),count)

    def test_actual_original_routes_headers_bytes_hashes(self):
        self.assertEqual(self.url,f'http://127.0.0.1:{self.port}/{self.nonce}/index.html#nonce={self.nonce}')
        for name,mime in [('index.html','text/html; charset=utf-8'),('scene.js','application/javascript; charset=utf-8')]:
            status,headers,body=self.request('/'+self.nonce+'/'+name)
            source=self.fixture.with_name(name).read_bytes()
            self.assertEqual(status,200);self.assertEqual(body,source)
            self.assertEqual(headers['Content-Type'],mime);self.assertEqual(headers['Content-Length'],str(len(source)))
            self.assertEqual(headers['Connection'],'close')
            self.assertEqual(self.server.record['sources'][name]['sha256'],hashlib.sha256(source).hexdigest())
        self.wait_records(2)
        for r in self.server.record['requests']:
            self.assertTrue(r['response_completed']);self.assertEqual(r['written_bytes'],r['content_length'])
            self.assertEqual(r['sha256'],self.server.record['sources'][r['path'].split('/')[-1]]['sha256'])

    def test_wrong_nonce_path_queries_encoded_traversal_and_proxy_rejected(self):
        for path in ['/', '/favicon.ico','/'+('b'*32)+'/index.html','/'+self.nonce+'/index.html?x=1',
                     '/'+self.nonce+'/../scene.js','/'+self.nonce+'/%2e%2e/scene.js',
                     '/'+self.nonce+'/index.html#fragment','/etc/passwd','http://example.com/',
                     '/'+self.nonce+'/scene.js/extra','//'+self.nonce+'/index.html']:
            with self.subTest(path=path):
                status,headers,body=self.request(path)
                self.assertEqual(status,404);self.assertEqual(body,b'');self.assertEqual(headers['Content-Length'],'0')
        self.server.check()

    def test_non_get_methods_never_serve_bytes(self):
        for method in ('HEAD','POST','PUT','DELETE','OPTIONS','CONNECT','PATCH','BREW'):
            with self.subTest(method=method):
                status,headers,body=self.request('/'+self.nonce+'/index.html',method)
                self.assertEqual(status,405);self.assertEqual(body,b'');self.assertEqual(headers['Content-Length'],'0')

    def test_idle_preconnect_times_out_then_queued_get_succeeds(self):
        with socket.create_connection((self.host,self.port),timeout=2) as idle:
            start=time.monotonic()
            status,_,body=self.request('/'+self.nonce+'/index.html')
            self.assertEqual(status,200);self.assertTrue(body);self.assertLess(time.monotonic()-start,2)
            self.wait_records(2)
            idle_record=next(r for r in self.server.record['requests'] if r['status'] is None)
            self.assertIn('timed out',idle_record['client_event']);self.assertFalse(idle_record['response_completed'])
            self.server.check();self.assertEqual(self.server.record['errors'],[])

    def test_cancelled_write_not_internal_error_or_completion(self):
        with patch.object(socketserver._SocketWriter,'write',side_effect=BrokenPipeError('client cancelled')):
            with self.assertRaises(http.client.RemoteDisconnected): self.request('/'+self.nonce+'/scene.js')
            self.wait_records(1)
        r=self.server.record['requests'][0]
        self.assertIn('client cancelled',r['client_event']);self.assertFalse(r['response_completed'])
        self.server.check();self.assertEqual(self.request('/'+self.nonce+'/scene.js')[0],200)

    def test_close_active_idle_connection_and_no_listener_or_worker_left(self):
        with socket.create_connection((self.host,self.port),timeout=2) as idle:
            until=time.monotonic()+1
            while self.server.active is None and time.monotonic()<until: time.sleep(.005)
            self.assertIsNotNone(self.server.active)
            start=time.monotonic();self.server.close(timeout=1)
            self.assertLess(time.monotonic()-start,1.5);self.assertFalse(self.server.thread.is_alive())
            self.assertEqual(idle.recv(1),b'')
        with self.assertRaises(OSError): socket.create_connection((self.host,self.port),timeout=.2)

    def test_http11_keepalive_does_not_retain_handler(self):
        with socket.create_connection((self.host,self.port),timeout=2) as client:
            client.sendall(('GET /'+self.nonce+'/index.html HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: keep-alive\r\n\r\n').encode())
            chunks=[]
            while True:
                part=client.recv(8192)
                if not part: break
                chunks.append(part)
        data=b''.join(chunks)
        self.assertTrue(data.startswith(b'HTTP/1.0 200'))
        self.assertIn(b'Connection: close\r\n',data)
        self.assertEqual(data.split(b'\r\n\r\n',1)[1],self.fixture.read_bytes())
        self.wait_records(1);self.server.check()

    def test_internal_handler_error_failclosed_and_cleanup_records_error(self):
        with patch.object(socketserver._SocketWriter,'write',side_effect=ValueError('PROGRAMMING FAILURE')):
            with self.assertRaises(http.client.RemoteDisconnected): self.request('/'+self.nonce+'/scene.js')
            self.wait_records(1)
        with self.assertRaisesRegex(RuntimeError,'PROGRAMMING FAILURE'): self.server.check()
        with self.assertRaisesRegex(RuntimeError,'PROGRAMMING FAILURE'): self.server.close()
        self.assertFalse(self.server.thread.is_alive())

    def test_request_quota_fails_closed_bounded_records(self):
        self.server.record['maximum_requests']=1
        self.request('/'+self.nonce+'/index.html');self.wait_records(1)
        with self.assertRaises(http.client.RemoteDisconnected): self.request('/'+self.nonce+'/scene.js')
        with self.assertRaisesRegex(RuntimeError,'quota'): self.server.check()
        with self.assertRaisesRegex(RuntimeError,'quota'): self.server.close()
        self.assertEqual(len(self.server.record['requests']),1)

    def test_startup_bind_error_and_timeout_close_without_worker_leak(self):
        other=FixtureServer(self.fixture,self.nonce)
        with patch('g4_edge_http_fixture.HTTPServer.server_bind',side_effect=OSError('BIND FAIL')):
            with self.assertRaisesRegex(OSError,'BIND FAIL'): other.start()
        other.close();self.assertTrue(other.record['thread_stopped'])
        other=FixtureServer(self.fixture,self.nonce)
        with patch.object(other.ready,'wait',return_value=False):
            with self.assertRaisesRegex(TimeoutError,'startup deadline'): other.start(timeout=.01)
        other.close();self.assertFalse(other.thread.is_alive())

    def test_nonce_and_missing_fixture_rejected_before_bind(self):
        with patch('g4_edge_http_fixture.HTTPServer') as server:
            with self.assertRaises(ValueError): FixtureServer(self.fixture,'../bad')
            with self.assertRaises(FileNotFoundError): FixtureServer(self.fixture.with_name('absent')/'index.html',self.nonce)
            server.assert_not_called()

    def test_join_deadline_retained(self):
        # Exercise the actual close deadline failure with a controlled worker.
        other=FixtureServer(self.fixture,self.nonce)
        release=threading.Event()
        other.thread=threading.Thread(target=release.wait,daemon=True);other.thread.start()
        try:
            with self.assertRaisesRegex(RuntimeError,'join deadline'): other.close(timeout=0)
        finally:
            release.set();other.thread.join(1)

    def test_listener_close_error_still_joins_and_preserves_failure(self):
        original=self.server.server.server_close
        def close():
            original()
            raise OSError('CLOSE FAIL')
        with patch.object(self.server.server,'server_close',side_effect=close):
            with self.assertRaisesRegex(RuntimeError,'CLOSE FAIL'): self.server.close()
        self.assertFalse(self.server.thread.is_alive())

    def test_accept_error_not_silently_swallowed(self):
        # Actual worker/connection, inject only the stdlib accept boundary.
        with patch('g4_edge_http_fixture.HTTPServer.get_request',side_effect=OSError('ACCEPT FAIL')):
            client=socket.create_connection((self.host,self.port),timeout=2)
            try:
                until=time.monotonic()+1
                while not self.server.record['errors'] and time.monotonic()<until: time.sleep(.005)
                with self.assertRaisesRegex(RuntimeError,'ACCEPT FAIL'): self.server.check()
            finally:
                client.close()
                try: self.server.close()
                except RuntimeError: pass


if __name__=='__main__': unittest.main()
