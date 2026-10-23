# SPDX-License-Identifier: GPL-2.0-or-later
"""Two immutable original-fixture routes, not a file/proxy server.

Caller must prove private loopback before constructing/starting this diagnostic.
One HTTP/1.0 worker, bounded accepted sockets, no keepalive/handler threads.
Receipt/write is NOT evidence of browser consumption or renderer readiness.
Request status is attempted; only response_completed confirms the complete write.
"""
import hashlib
from http.server import BaseHTTPRequestHandler, HTTPServer
import re
import socket
import sys
import threading
import time


class FixtureServer:
    def __init__(self, fixture, nonce):
        if not re.fullmatch('[0-9a-f]{32}', nonce): raise ValueError('Invalid HTTP fixture nonce')
        self.routes = {}
        self.record = {'classification': 'HTTP writes only; browser consumption NOT_PROVEN',
                       'requests': [], 'errors': [], 'sources': {}, 'connection_timeout_seconds': .5,
                       'maximum_requests': 64}
        for name, mime in (('index.html', 'text/html; charset=utf-8'),
                           ('scene.js', 'application/javascript; charset=utf-8')):
            path = fixture.with_name(name)
            with path.open('rb') as stream: data = stream.read(1024**2 + 1)
            if not data or len(data) > 1024**2: raise ValueError('Invalid bounded original fixture size')
            digest = hashlib.sha256(data).hexdigest()
            self.routes['/' + nonce + '/' + name] = (data, mime, digest)
            self.record['sources'][name] = {'path': str(path), 'bytes': len(data), 'sha256': digest}
        self.nonce = nonce
        self.server = self.thread = None
        self.active = None
        self.lock = threading.Lock()
        self.stopping = threading.Event()
        self.ready = threading.Event()

    def start(self, timeout=2):
        owner = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = 'HTTP/1.0'
            def handle(self):
                if len(owner.record['requests']) >= owner.record['maximum_requests']:
                    owner.record['errors'].append('HTTP request quota exceeded')
                    owner.stopping.set()
                    self.close_connection = True
                    return
                self.entry = {'started_monotonic': time.monotonic(), 'status': None, 'written_bytes': 0,
                              'response_completed': False, 'client_address': list(self.client_address)}
                try: super().handle()
                except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, TimeoutError) as error:
                    self.entry['client_event'] = repr(error)
                except OSError as error:
                    if not owner.stopping.is_set(): raise
                    self.entry['client_event'] = 'Closed during owned cleanup: ' + repr(error)
                finally:
                    if self.entry['status'] is None and 'client_event' not in self.entry:
                        self.entry['client_event'] = 'EOF before request/response'
                    self.entry.update(method=getattr(self, 'command', None), path=getattr(self, 'path', ''),
                                      requestline=getattr(self, 'requestline', ''),
                                      finished_monotonic=time.monotonic())
                    owner.record['requests'].append(self.entry)

            def reply(self, status, data=b'', mime='text/plain; charset=utf-8', digest=None):
                self.close_connection = True
                self.entry.update(status=status, content_length=len(data), content_type=mime, sha256=digest)
                self.send_response(status)
                self.send_header('Content-Type', mime)
                self.send_header('Content-Length', str(len(data)))
                self.send_header('Connection', 'close')
                self.end_headers()
                if data: self.entry['written_bytes'] = self.wfile.write(data)
                self.entry['response_completed'] = True

            def do_GET(self):
                # BaseHTTPRequestHandler normalizes a leading //; accept only
                # the exact wire target, never its normalized/path-decoded alias.
                target = self.requestline.split()[1]
                payload = owner.routes.get(target) if target == self.path else None
                if payload is None: self.reply(404)
                else:
                    data, mime, digest = payload
                    self.reply(200, data, mime, digest)

            def send_error(self, code, message=None, explain=None):
                # Unknown methods are rejected, never inherited file/CGI behavior.
                self.reply(405 if code == 501 else code)

            def log_message(self, *_): pass
            def log_error(self, fmt, *args):
                if 'timed out' in fmt.lower():
                    self.entry['client_event'] = fmt % args

        class Server(HTTPServer):
            def get_request(self):
                try: connection, address = super().get_request()
                except OSError as error:
                    # TCPServer normally swallows accept errors; retain them.
                    if not owner.stopping.is_set(): owner.record['errors'].append('Listener: ' + repr(error))
                    raise
                connection.settimeout(.5)
                with owner.lock:
                    if owner.stopping.is_set():
                        connection.close()
                        raise OSError('HTTP fixture stopping')
                    owner.active = connection
                return connection, address

            def shutdown_request(self, request):
                try: super().shutdown_request(request)
                finally:
                    with owner.lock:
                        if owner.active is request: owner.active = None

            def handle_error(self, request, address):
                owner.record['errors'].append(repr(sys.exc_info()[1]))

        self.server = Server(('127.0.0.1', 0), Handler)
        self.server.timeout = .1
        host, port = self.server.server_address
        self.record.update(listener={'host': host, 'port': port}, started_monotonic=time.monotonic(),
                           url=f'http://127.0.0.1:{port}/{self.nonce}/index.html#nonce={self.nonce}')
        def serve():
            self.ready.set()
            try:
                while not self.stopping.is_set() and not self.record['errors']: self.server.handle_request()
            except Exception as error:
                if not self.stopping.is_set(): self.record['errors'].append(repr(error))
        self.thread = threading.Thread(target=serve, name='g4-original-http-fixture', daemon=True)
        self.thread.start()
        if not self.ready.wait(timeout): raise TimeoutError('HTTP fixture startup deadline')
        self.check()
        return self.record['url']

    def check(self):
        if self.record['errors']: raise RuntimeError('HTTP fixture failure: ' + repr(self.record['errors']))
        if self.thread is None or not self.thread.is_alive(): raise RuntimeError('HTTP fixture thread stopped')

    def close(self, timeout=2):
        self.stopping.set()
        try:
            with self.lock:
                if self.active is not None:
                    try: self.active.shutdown(socket.SHUT_RDWR)
                    except OSError: pass  # peer may already have disconnected
                    self.active.close()
        except Exception as error: self.record['errors'].append('Connection cleanup: ' + repr(error))
        try:
            if self.server is not None: self.server.server_close()
        except Exception as error: self.record['errors'].append('Listener cleanup: ' + repr(error))
        try:
            if self.thread is not None and self.thread.ident is not None: self.thread.join(timeout=max(0, timeout))
        except Exception as error: self.record['errors'].append('Worker cleanup: ' + repr(error))
        self.record.update(stopped_monotonic=time.monotonic(),
                           thread_stopped=self.thread is None or not self.thread.is_alive())
        if not self.record['thread_stopped']: self.record['errors'].append('HTTP fixture join deadline')
        if self.record['errors']: raise RuntimeError('HTTP fixture cleanup: ' + repr(self.record['errors']))
