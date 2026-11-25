#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Static checks of the accounts helper's installation (docs/LOCAL_USERS_DESIGN.md, "Tests of the helper and its link"):
the service unit (its own account, no supplementary group, no capabilities, no network, sandboxed), the socket unit (root,
0660, the group of session 0's account only, a SEQPACKET socket), the sync unit, the polkit rule (the one account and the
two grants, nothing else), and that the packaging and the setup script install them. Needs no build and no root:
    python3 scripts/test_accountsd_units.py"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
failures = 0


def check(cond, name):
    global failures
    print(('ok ' if cond else 'FAIL ') + name)
    if not cond:
        failures += 1


def read(path):
    with open(os.path.join(ROOT, path), encoding='utf-8') as f:
        return f.read()


def keys(text):
    """{key: [values]} of the lines Key=Value of a unit file"""
    out = {}
    for line in text.splitlines():
        if line.startswith('#') or '=' not in line or line.startswith('['):
            continue
        k, v = line.split('=', 1)
        out.setdefault(k.strip(), []).append(v.strip())
    return out


svc = keys(read('installer/systemd/wex-accountsd.service'))
sock = keys(read('installer/systemd/wex-accountsd.socket'))
sync = keys(read('installer/systemd/wex-accounts-sync.service'))
rules = read('installer/polkit/49-wexaccounts.rules')
setup = read('scripts/setup-machine-root.sh')
package = read('installer/package-wine.sh')

# the service
check(svc.get('User') == ['wexaccounts'] and svc.get('Group') == ['wexaccounts'], 'service: runs as wexaccounts')
check(svc.get('SupplementaryGroups') in (None, ['']), 'service: no supplementary groups')
check('wexsys' not in read('installer/systemd/wex-accountsd.service').replace('(above all not the system group)', ''),
      'service: never names the system group')
check(svc.get('CapabilityBoundingSet') == [''] and svc.get('AmbientCapabilities') in (None, ['']), 'service: no capabilities')
check(svc.get('NoNewPrivileges') == ['yes'], 'service: NoNewPrivileges')
check(svc.get('RestrictAddressFamilies') == ['AF_UNIX'], 'service: only unix sockets')
for k in ('ProtectSystem', 'ProtectHome', 'PrivateTmp', 'PrivateDevices', 'ProtectKernelTunables', 'ProtectKernelModules',
          'ProtectControlGroups', 'RestrictSUIDSGID', 'LockPersonality', 'MemoryDenyWriteExecute', 'RestrictNamespaces',
          'RestrictRealtime', 'SystemCallFilter'):
    check(k in svc, 'service: sandbox line ' + k)
check(svc.get('ProtectSystem') == ['strict'], 'service: the file system is read-only (strict)')
check(svc.get('LimitCORE') == ['0'], 'service: no core dumps')
check(svc.get('ExecStart') == ['/usr/local/libexec/wineexplorer/wex-accountsd'], 'service: the helper binary')
check('ReadWritePaths' not in svc and 'BindPaths' not in svc, 'service: writes nowhere')

# the socket
check(sock.get('ListenSequentialPacket') == ['/run/wineexplorer/accounts.sock'], 'socket: a SEQPACKET socket in /run/wineexplorer')
check(sock.get('SocketUser') == ['root'] and sock.get('SocketMode') == ['0660'], 'socket: root-owned, 0660')
check(sock.get('SocketGroup') == ["wexsession0"], "socket: the group of session 0's account only")
check(sock.get('DirectoryMode') == ['0711'], 'socket: the directory is 0711')
check(sock.get('Accept') == ['no'] and sock.get('Service') == ['wex-accountsd.service'], 'socket: one process serves, one connection at a time')

# the sync unit
check(sync.get('ExecStart') == ['/usr/local/libexec/wineexplorer/wex-accounts sync'] and sync.get('Type') == ['oneshot'],
      'sync unit: runs the existing sync, once')

# the polkit rule: one account, two grants, nothing else
check(rules.count('polkit.addRule') == 1, 'rule: one rule')
check(re.findall(r'subject\.user\s*!=\s*"([^"]+)"', rules) == ['wexaccounts'], 'rule: the account wexaccounts and no other')
check(re.findall(r'action\.id\s*==\s*"([^"]+)"', rules) ==
      ['org.freedesktop.accounts.user-administration', 'org.freedesktop.systemd1.manage-units'], 'rule: exactly the two actions')
check('action.lookup("unit") == "wex-accounts-sync.service"' in rules and 'action.lookup("verb") == "start"' in rules,
      'rule: manage-units only for the sync unit and the verb start')
code = '\n'.join(l for l in rules.splitlines() if not l.strip().startswith('//'))
check(not re.search(r'isInGroup|subject\.groups|subject\.isInGroup|"wexsys"|Result\.AUTH', code), 'rule: no group test, no authentication, no system group')
check(code.count('polkit.Result.YES') == 2, 'rule: two grants')

# the installation
check('wex-accountsd.bin' in package and '-lsystemd' in package, 'packaging: the helper is built with libsystemd')
for f in ('wex-accounts-sync.service', 'wex-accountsd.socket', 'wex-accountsd.service'):
    check(f in package, 'packaging: ' + f)
check('49-wexaccounts.rules' in package and 'etc/polkit-1/rules.d/49-wexaccounts.rules' in package, 'packaging: the polkit rule')
check('useradd --system' in setup and 'wexaccounts' in setup and "usermod -G '' wexaccounts" in setup,
      'setup: the account has no other group')
check('gpasswd -M "$S0_ACCOUNT" wexsession0' in setup, "setup: the socket's group has session 0's account as its only member")
check('session0_user=$S0_ACCOUNT' in setup and 'sync_unit=wex-accounts-sync.service' in setup, 'setup: writes accountsd.conf')
check('enable --now wex-accountsd.socket' in setup, 'setup: enables the socket')
check('libsystemd-dev' in read('scripts/lab/wexgpo-iso.sh'), 'the ISO build machine has libsystemd-dev')

# F3: the shipped binary has none of the lab options that bypass the peer check (they exist only with
# -DWEX_ACCOUNTSD_LAB_TEST); F4: the hash and the receive buffer are wiped
src = read('tools/wex-accountsd/wex-accountsd.c')
check(src.count('explicit_bzero') >= 3 and 'memset( buf, 0, 0 )' not in src, 'F4: the hash and the receive buffer are wiped with explicit_bzero')
if shutil.which('gcc'):
    tmp = tempfile.mkdtemp(prefix='accountsd-ship-')
    exe = os.path.join(tmp, 'wex-accountsd')
    r = subprocess.run(['gcc', '-O2', '-Wall', '-Wextra', '-Werror', '-o', exe,
                        os.path.join(ROOT, 'tools/wex-accountsd/wex-accountsd.c'), '-lsystemd'], capture_output=True, text=True)
    if r.returncode == 0:
        with open(exe, 'rb') as f:
            blob = f.read()
        lab = [o for o in (b'--mainpid', b'--listen', b'--bus', b'--any-dumpable', b'--admin-members', b'--login-defs',
                           b'--group-file', b'--passwd-file', b'--no-idle-exit', b'WEX_ACCOUNTSD_TEST') if o in blob]
        check(not lab, 'F3: the shipped binary has no lab option in it (%s)' % ', '.join(x.decode() for x in lab))
        for opt in ('--mainpid', '--listen', '--bus', '--any-dumpable'):
            p = subprocess.run([exe, opt, '1'], env=dict(os.environ, WEX_ACCOUNTSD_TEST='1'), capture_output=True, text=True)
            check(p.returncode == 2 and 'usage' in p.stderr, 'F3: the shipped binary refuses ' + opt + ' even with WEX_ACCOUNTSD_TEST=1')
    else:
        print('SKIP F3 (libsystemd is not available to build the shipped binary): ' + r.stderr.strip()[:120])
    shutil.rmtree(tmp, ignore_errors=True)

# systemd's own parser, when there is one (the executables are missing here: those complaints do not count)
if shutil.which('systemd-analyze'):
    tmp = tempfile.mkdtemp(prefix='accountsd-units-')
    for f in ('wex-accountsd.service', 'wex-accountsd.socket', 'wex-accounts-sync.service'):
        shutil.copy(os.path.join(ROOT, 'installer/systemd', f), tmp)
    r = subprocess.run(['systemd-analyze', 'verify', '--man=no', os.path.join(tmp, 'wex-accountsd.service'),
                        os.path.join(tmp, 'wex-accountsd.socket'), os.path.join(tmp, 'wex-accounts-sync.service')],
                       capture_output=True, text=True)
    bad = [l for l in (r.stdout + r.stderr).splitlines()
           if re.search(r'Unknown (key|section|lvalue)|Failed to parse|Invalid|not a valid|Unit .* has a bad', l)]
    check(not bad, "systemd's parser accepts the units" + (': ' + '; '.join(bad) if bad else ''))
    shutil.rmtree(tmp, ignore_errors=True)

print('%d failure(s)' % failures if failures else 'accountsd_units=PASS')
sys.exit(1 if failures else 0)
