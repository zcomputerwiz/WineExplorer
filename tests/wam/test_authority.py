#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""A local OAuth 2.0 test authority for the token broker (docs/WAM_BROKER_DESIGN.md, "Tests").

It implements the v2.0 endpoints the broker uses, /{authority}/oauth2/v2.0/authorize and /token, with PKCE (S256)
checked, and issues test tokens only (nothing here is a real credential). What the authorize endpoint does depends on
the login hint, so that each test case can ask for one behaviour:

    ok@...        signs in at once, through one hop on the authority (as a sign-in page would)
    cancel@...    answers with a page for the user (the test hook closes the window: UserCancel)
    denied@...    redirects with error=access_denied (UserCancel)
    badstate@...  redirects with a state that is not the one sent (refused)
    hostile@...   redirects to another host with the redirect URI's path (refused)

The token endpoint redeems a code once, with the verifier, client id and redirect URI of its authorize request, and
takes refresh tokens; a refresh whose scope names "refuse" is answered invalid_grant. GET /stats gives the counters.

Usage: test_authority.py PORT_FILE   (listens on 127.0.0.1, an ephemeral port written to PORT_FILE)
"""
import base64
import hashlib
import http.server
import json
import secrets
import sys
import threading
import urllib.parse

REDIRECT_PREFIX = 'ms-appx-web://Microsoft.AAD.BrokerPlugin/'

lock = threading.Lock()
codes = {}           # code -> request parameters
refresh_tokens = {}  # refresh token -> (client id, account)
stats = {'authorize': 0, 'token_code': 0, 'token_refresh': 0, 'refused': 0, 'issued': 0}


def b64url(data):
    return base64.urlsafe_b64encode(data).rstrip(b'=').decode()


def account_for(hint):
    name = hint or 'ok@wine.test'
    local = name.split('@')[0]
    return {'oid': 'oid-' + local, 'sub': 'sub-' + local, 'preferred_username': name,
            'tid': '9188040d-6c67-4c5b-b112-36a304b66dad', 'name': 'Test ' + local}


def id_token(account, client_id, nonce):
    header = b64url(json.dumps({'alg': 'none', 'typ': 'JWT'}).encode())
    claims = dict(account, aud=client_id, nonce=nonce or '', iss='https://login.wine.test/v2.0')
    return header + '.' + b64url(json.dumps(claims).encode()) + '.test-signature'


def issue(client_id, account, scope, nonce=None):
    with lock:
        stats['issued'] += 1
        n = stats['issued']
        refresh = 'rt-%d-%s' % (n, secrets.token_urlsafe(8))
        refresh_tokens[refresh] = (client_id, account)
    return {'token_type': 'Bearer', 'access_token': 'at-%d' % n, 'refresh_token': refresh, 'expires_in': 3600,
            'scope': scope, 'id_token': id_token(account, client_id, nonce),
            'client_info': b64url(json.dumps({'uid': account['oid'], 'utid': account['tid']}).encode())}


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        sys.stderr.write('authority: ' + (fmt % args) + '\n')

    def reply(self, status, body=b'', ctype='application/json', location=None):
        self.send_response(status)
        if location:
            self.send_header('Location', location)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def error(self, status, error, description):
        with lock:
            stats['refused'] += 1
        self.reply(status, json.dumps({'error': error, 'error_description': description}).encode())

    def do_GET(self):
        url = urllib.parse.urlsplit(self.path)
        q = dict(urllib.parse.parse_qsl(url.query))
        parts = url.path.strip('/').split('/')
        if url.path == '/stats':
            with lock:
                return self.reply(200, json.dumps(stats).encode())
        if len(parts) == 4 and parts[1:] == ['oauth2', 'v2.0', 'authorize']:
            return self.authorize(parts[0], q)
        if len(parts) == 4 and parts[1:] == ['oauth2', 'v2.0', 'login']:
            # the hop of a sign-in page: on to the redirect URI
            with lock:
                entry = codes.get(q.get('sid', ''))
            if not entry:
                return self.reply(400, b'unknown sign-in', 'text/plain')
            target = entry['redirect_uri'] + '?' + urllib.parse.urlencode({'code': q['sid'], 'state': entry['state']})
            return self.reply(302, location=target)
        self.reply(404, b'not found', 'text/plain')

    def authorize(self, authority, q):
        with lock:
            stats['authorize'] += 1
        client_id = q.get('client_id', '')
        redirect = q.get('redirect_uri', '')
        scope = q.get('scope', '').split()
        if (not client_id or q.get('response_type') != 'code' or redirect != REDIRECT_PREFIX + client_id or
                q.get('code_challenge_method') != 'S256' or not q.get('code_challenge') or not q.get('state') or
                'openid' not in scope or 'offline_access' not in scope or q.get('response_mode') != 'query'):
            return self.reply(400, b'bad authorize request', 'text/plain')
        hint = q.get('login_hint', 'ok@wine.test')
        kind = hint.split('@')[0]
        state = q['state']
        if kind == 'cancel':
            return self.reply(200, b'<html><body>Sign in</body></html>', 'text/html')
        if kind == 'denied':
            return self.reply(302, location=redirect + '?' + urllib.parse.urlencode(
                {'error': 'access_denied', 'error_description': 'The user cancelled.', 'state': state}))
        code = secrets.token_urlsafe(16)
        with lock:
            codes[code] = {'client_id': client_id, 'redirect_uri': redirect, 'challenge': q['code_challenge'],
                           'state': state, 'nonce': q.get('nonce'), 'account': account_for(hint),
                           'scope': ' '.join(scope), 'authority': authority}
        if kind == 'badstate':
            return self.reply(302, location=redirect + '?' + urllib.parse.urlencode({'code': code, 'state': 'wrong'}))
        if kind == 'hostile':
            evil = 'ms-appx-web://evil.example/' + client_id
            return self.reply(302, location=evil + '?' + urllib.parse.urlencode({'code': code, 'state': state}))
        self.reply(302, location='/%s/oauth2/v2.0/login?sid=%s' % (authority, code))

    def do_POST(self):
        url = urllib.parse.urlsplit(self.path)
        parts = url.path.strip('/').split('/')
        if len(parts) != 4 or parts[1:] != ['oauth2', 'v2.0', 'token']:
            return self.reply(404, b'not found', 'text/plain')
        if self.headers.get('Content-Type', '').split(';')[0] != 'application/x-www-form-urlencoded':
            return self.error(400, 'invalid_request', 'not a form')
        body = self.rfile.read(int(self.headers.get('Content-Length', '0'))).decode()
        f = dict(urllib.parse.parse_qsl(body))
        grant = f.get('grant_type')
        if grant == 'authorization_code':
            with lock:
                stats['token_code'] += 1
                entry = codes.pop(f.get('code', ''), None)
            if not entry:
                return self.error(400, 'invalid_grant', 'unknown or used code')
            verifier = f.get('code_verifier', '')
            if b64url(hashlib.sha256(verifier.encode()).digest()) != entry['challenge']:
                return self.error(400, 'invalid_grant', 'PKCE verification failed')
            if f.get('client_id') != entry['client_id'] or f.get('redirect_uri') != entry['redirect_uri']:
                return self.error(400, 'invalid_grant', 'client or redirect URI differ')
            if parts[0] != entry['authority']:
                return self.error(400, 'invalid_grant', 'another authority')
            return self.reply(200, json.dumps(issue(entry['client_id'], entry['account'], entry['scope'],
                                                    entry['nonce'])).encode())
        if grant == 'refresh_token':
            with lock:
                stats['token_refresh'] += 1
                known = refresh_tokens.get(f.get('refresh_token', ''))
            if not known or known[0] != f.get('client_id'):
                return self.error(400, 'invalid_grant', 'unknown refresh token')
            if 'refuse' in f.get('scope', '').split():
                with lock:
                    refresh_tokens.pop(f['refresh_token'], None)
                return self.error(400, 'invalid_grant', 'AADSTS70000: the refresh token was revoked (test)')
            return self.reply(200, json.dumps(issue(known[0], known[1], f.get('scope', ''))).encode())
        self.error(400, 'unsupported_grant_type', 'unsupported grant type')


def main():
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    with open(sys.argv[1], 'w') as port_file:
        port_file.write(str(server.server_address[1]))
    server.serve_forever()


if __name__ == '__main__':
    main()
