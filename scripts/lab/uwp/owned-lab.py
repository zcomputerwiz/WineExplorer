#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Codex-owned U1 development guest; never controls another lab's VM."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import socket
import subprocess
import time

ROOT = Path('/var/tmp/wex-uwp-u1-20261111')
DISK = Path('/var/tmp/wex-uwp-u1-images-20261111/standalone.qcow2')
VERIFY = DISK.parent
KEY = Path('/var/tmp/wex-g4-lab-credentials/vm_key')
OWNER = 'codex-uwp-u1-20261111'
PORT = 2237


def run(argv, timeout=15, check=True):
    result = subprocess.run(argv, timeout=timeout, check=False, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if check and result.returncode:
        raise RuntimeError('Command exited %s: %s' % (result.returncode, result.stderr.strip()))
    return result


def save(name, value):
    temporary = ROOT / (name + '.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(ROOT / name)


def identity(pid):
    proc = Path('/proc') / str(pid)
    stat = (proc / 'stat').read_text().rsplit(')', 1)[1].split()
    return {'pid': pid, 'start_ticks': int(stat[19]),
            'argv': [x.decode() for x in (proc / 'cmdline').read_bytes().split(b'\0') if x]}


def owned_process(record):
    try:
        current = identity(record['pid'])
    except FileNotFoundError:
        return False
    if current != record:
        raise RuntimeError('Process identity changed; refusing control')
    if not any(str(DISK) in arg for arg in current['argv']):
        raise RuntimeError('Owned disk missing from actual process command')
    return True


def ssh(argv):
    record = json.loads((ROOT / 'process.json').read_text())
    if not owned_process(record):
        raise RuntimeError('Owned QEMU is not running')
    return ['ssh', '-i', str(KEY), '-p', str(PORT), '-o', 'BatchMode=yes',
            '-o', 'StrictHostKeyChecking=yes', '-o', 'UserKnownHostsFile=' + str(ROOT / 'known_hosts'),
            '-o', 'ConnectTimeout=5', 'debian@127.0.0.1', *argv]


def start(restart=False):
    if (ROOT / 'process.json').exists():
        previous = json.loads((ROOT / 'process.json').read_text())
        if not restart or owned_process(previous):
            raise RuntimeError('Existing run is not stopped or restart was not explicit')
        save('previous-' + str(previous['start_ticks']) + '.json', previous)
        for name in ('qemu.pid', 'qmp.sock'):
            path = ROOT / name
            if path.exists():
                path.unlink()
    check = json.loads((VERIFY / 'check.json').read_text())
    if check.get('check-errors') != 0 or check.get('corruptions', 0) or check.get('leaks', 0):
        raise RuntimeError('Disk consistency check did not pass')
    if (VERIFY / 'compare.txt').read_text().strip() != 'Images are identical.':
        raise RuntimeError('Disk comparison did not establish a complete clone')
    info = json.loads(run(['qemu-img', 'info', '--output=json', str(DISK)]).stdout)
    if info.get('backing-filename') or info.get('format') != 'qcow2':
        raise RuntimeError('Expected self-contained QCOW2 disk')
    if not KEY.is_file():
        raise RuntimeError('Expected lab SSH credential is missing')
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', PORT))
    command = ['qemu-system-x86_64', '-accel', 'kvm', '-machine', 'q35',
               '-cpu', 'host', '-smp', '8', '-m', '16384', '-device', 'virtio-vga',
               '-display', 'none', '-drive', 'file=' + str(DISK) + ',format=qcow2,if=none,id=u1-root',
               '-device', 'virtio-blk-pci,drive=u1-root,bootindex=1',
               '-nic', 'user,model=virtio-net-pci,hostfwd=tcp:127.0.0.1:2237-:22',
               '-serial', 'file:' + str(ROOT / 'serial.log'),
               '-qmp', 'unix:' + str(ROOT / 'qmp.sock') + ',server=on,wait=off',
               '-pidfile', str(ROOT / 'qemu.pid'), '-daemonize', '-no-reboot']
    if (ROOT / 'workdisk.json').exists():
        work = json.loads((ROOT / 'workdisk.json').read_text())
        if work.get('owner') != OWNER or work.get('file') != str(VERIFY / 'work.qcow2'):
            raise RuntimeError('Foreign work disk record')
        command.extend(['-drive', 'file=' + work['file'] + ',format=qcow2,if=none,id=u1-work-startup',
                        '-device', 'virtio-blk-pci,drive=u1-work-startup,serial=WEX_U1_20261111'])
    save('launch.json', {'owner': OWNER, 'disk': str(DISK), 'argv': command})
    result = run(command)
    pid = int((ROOT / 'qemu.pid').read_text())
    record = identity(pid)
    save('process.json', record)
    if not owned_process(record):
        raise RuntimeError('QEMU exited at startup: ' + result.stderr)
    print(json.dumps({'state': 'started', **record}))


def readiness():
    record = json.loads((ROOT / 'process.json').read_text())
    if not owned_process(record):
        raise RuntimeError('Owned QEMU is not running')
    hosts = ROOT / 'known_hosts'
    if not hosts.exists():
        scan = run(['ssh-keyscan', '-T', '5', '-p', str(PORT), '-t', 'ed25519', '127.0.0.1'], check=False)
        expected = '[127.0.0.1]:2237 ssh-ed25519 '
        keys = [line for line in scan.stdout.splitlines() if line.startswith(expected)]
        if len(keys) != 1:
            raise RuntimeError('Owned endpoint is not ready for host-key pinning')
        with hosts.open('x') as stream:
            stream.write(keys[0] + '\n')
    response = run(ssh(['printf "U1_GUEST_READY\\n"; uname -m; cat /proc/sys/kernel/random/boot_id']))
    if not response.stdout.startswith('U1_GUEST_READY\n'):
        raise RuntimeError('Guest readiness response missing')
    save('readiness.json', {'owner': OWNER, 'response': response.stdout,
                            'host_key_fingerprint': run(['ssh-keygen', '-lf', str(hosts)]).stdout.strip()})
    print(response.stdout, end='')


def add_workdisk():
    record = json.loads((ROOT / 'process.json').read_text())
    if not owned_process(record):
        raise RuntimeError('Owned QEMU is not running')
    disk = VERIFY / 'work.qcow2'
    if disk.exists() or (ROOT / 'workdisk.json').exists():
        raise RuntimeError('Work disk state already exists; refusing overwrite or reattach')
    run(['qemu-img', 'create', '-f', 'qcow2', str(disk), '160G'])
    save('workdisk.json', {'owner': OWNER, 'file': str(disk), 'serial': 'WEX_U1_20261111',
                           'attachment': 'pending; may require restart on a non-hotpluggable root bus'})
    commands = [
        ('qmp_capabilities', {}),
        ('blockdev-add', {'driver': 'file', 'filename': str(disk), 'node-name': 'u1-work-file'}),
        ('blockdev-add', {'driver': 'qcow2', 'file': 'u1-work-file', 'node-name': 'u1-work-disk'}),
        ('device_add', {'driver': 'virtio-blk-pci', 'drive': 'u1-work-disk',
                        'id': 'u1-work-device', 'serial': 'WEX_U1_20261111'}),
    ]
    with socket.socket(socket.AF_UNIX) as connection:
        connection.settimeout(5)
        connection.connect(str(ROOT / 'qmp.sock'))
        stream = connection.makefile('rwb', buffering=0)
        json.loads(stream.readline())
        for command, arguments in commands:
            stream.write(json.dumps({'execute': command, 'arguments': arguments, 'id': command}).encode() + b'\n')
            for _ in range(64):
                reply = json.loads(stream.readline())
                if reply.get('id') == command:
                    if 'error' in reply:
                        raise RuntimeError(reply['error'])
                    break
            else:
                raise RuntimeError('QMP reply limit exceeded')
    save('workdisk.json', {'owner': OWNER, 'file': str(disk), 'serial': 'WEX_U1_20261111'})
    print('Attached exclusively owned blank work disk WEX_U1_20261111')


def recover_workdisk():
    # A failed Q35 root-bus hotplug can leave our empty image without a device.
    record = json.loads((ROOT / 'process.json').read_text())
    if owned_process(record) or (ROOT / 'workdisk.json').exists():
        raise RuntimeError('Recovery requires the owned guest stopped and no work disk record')
    disk = VERIFY / 'work.qcow2'
    info = json.loads(run(['qemu-img', 'info', '--output=json', str(disk)]).stdout)
    extents = json.loads(run(['qemu-img', 'map', '--output=json', str(disk)]).stdout)
    if info.get('format') != 'qcow2' or info.get('virtual-size') != 160 * 1024**3 or info.get('backing-filename'):
        raise RuntimeError('Work disk metadata is unexpected')
    if any(extent.get('data') or not extent.get('zero') for extent in extents):
        raise RuntimeError('Work disk is not demonstrably blank; refusing recovery')
    save('workdisk.json', {'owner': OWNER, 'file': str(disk), 'serial': 'WEX_U1_20261111',
                           'attachment': 'verified blank; attach at next explicit restart'})
    print('Recovered owned blank work disk for startup attachment')


def stop(quit_unbooted=False):
    record = json.loads((ROOT / 'process.json').read_text())
    if not owned_process(record):
        print('Owned QEMU already stopped')
        return
    # This endpoint and process identity belong exclusively to this run.
    with socket.socket(socket.AF_UNIX) as connection:
        connection.settimeout(5)
        connection.connect(str(ROOT / 'qmp.sock'))
        stream = connection.makefile('rwb', buffering=0)
        json.loads(stream.readline())
        for command in ('qmp_capabilities', 'quit' if quit_unbooted else 'system_powerdown'):
            stream.write(json.dumps({'execute': command, 'id': command}).encode() + b'\n')
            for _ in range(64):
                reply = json.loads(stream.readline())
                if reply.get('id') == command:
                    if 'error' in reply:
                        raise RuntimeError(reply['error'])
                    break
            else:
                raise RuntimeError('QMP reply limit exceeded')
    deadline = time.monotonic() + 50
    while time.monotonic() < deadline:
        if not owned_process(record):
            state = 'QEMU quit (unbooted setup recovery)' if quit_unbooted else 'stopped after powerdown request'
            save('shutdown.json', {'owner': OWNER, 'state': state})
            print('Owned ' + state)
            return
        time.sleep(1)
    raise RuntimeError('Owned guest did not stop; retained for inspection, no global kill')


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('action', choices=['start', 'restart', 'status', 'ready', 'ssh', 'push', 'add-workdisk', 'recover-workdisk', 'stop', 'quit-unbooted'])
parser.add_argument('command', nargs='*')
args = parser.parse_args()
if ROOT.exists():
    if ROOT.is_symlink() or not ROOT.is_dir() or (ROOT / 'owner').read_text().strip() != OWNER:
        raise RuntimeError('Foreign or unmarked U1 run directory')
else:
    if args.action != 'start':
        raise RuntimeError('Owned lab has not been initialized')
    ROOT.mkdir(mode=0o700)
    (ROOT / 'owner').write_text(OWNER + '\n')
with (ROOT / 'control.lock').open('a') as lock:
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    if args.action in ('start', 'restart'):
        start(restart=args.action == 'restart')
    elif args.action == 'ready':
        readiness()
    elif args.action == 'add-workdisk':
        add_workdisk()
    elif args.action == 'recover-workdisk':
        recover_workdisk()
    elif args.action == 'ssh':
        completed = run(ssh(args.command), timeout=55)
        print(completed.stdout, end='')
    elif args.action == 'push':
        if len(args.command) != 2:
            raise RuntimeError('push requires a local file and an absolute guest destination')
        source, destination = args.command
        if not Path(source).is_file() or not destination.startswith('/home/debian/uwp-u1-20261111/'):
            raise RuntimeError('push is restricted to the owned guest workspace')
        ssh([])  # Validate the owned QEMU identity before using the endpoint.
        completed = run(['scp', '-i', str(KEY), '-P', str(PORT), '-o', 'BatchMode=yes',
                         '-o', 'StrictHostKeyChecking=yes',
                         '-o', 'UserKnownHostsFile=' + str(ROOT / 'known_hosts'),
                         '-o', 'ConnectTimeout=5', source,
                         'debian@127.0.0.1:' + destination], timeout=55)
        print(completed.stdout, end='')
    elif args.action in ('stop', 'quit-unbooted'):
        stop(quit_unbooted=args.action == 'quit-unbooted')
    else:
        record = json.loads((ROOT / 'process.json').read_text())
        print(json.dumps({'running': owned_process(record), 'owner': OWNER, **record}))
