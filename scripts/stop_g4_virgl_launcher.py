#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate and stop only the launcher recorded for one owned VirGL run."""
import argparse
import json
import os
from pathlib import Path
import signal
import sys
import time


def proc_identity(pid, proc_root=Path('/proc')):
    entry = Path(proc_root) / str(pid)
    stat = (entry / 'stat').read_text()
    close = stat.rfind(')')
    if close < 0:
        raise RuntimeError(f'Malformed proc stat for PID {pid}')
    fields = stat[close + 2:].split()  # field 3 onwards
    cmdline = (entry / 'cmdline').read_bytes().split(b'\0')
    return {'pid': pid, 'start_ticks': int(fields[19]),
            'cmdline': [part.decode(errors='replace') for part in cmdline if part]}


def validate_identity(identity, identity_path, proc_root=Path('/proc')):
    identity_path = Path(identity_path).resolve(strict=True)
    run_dir = Path(identity['run_dir']).resolve(strict=True)
    launcher_path = Path(identity['launcher_path']).resolve(strict=True)
    if (identity.get('identity_record') != str(identity_path)
            or identity_path != run_dir / 'launcher-identity.json'):
        raise RuntimeError('Launcher identity path does not match its recorded run directory')
    current = proc_identity(identity['pid'], proc_root)
    if current['start_ticks'] != identity.get('start_ticks'):
        raise RuntimeError('Launcher PID start_ticks changed; refusing to signal')
    if current['cmdline'] != identity.get('cmdline'):
        raise RuntimeError('Launcher cmdline differs from its identity record; refusing to signal')
    if not current['cmdline'] or Path(current['cmdline'][-1]).resolve() != launcher_path:
        raise RuntimeError('Launcher cmdline does not name the recorded launcher path')
    if identity.get('run_name') != run_dir.name:
        raise RuntimeError('Launcher run name does not match its output path')
    return current


def wait_for_owned_child_exit(identity, key, proc_root, deadline):
    pid = identity.get(key + '_pid')
    ticks = identity.get(key + '_start_ticks')
    if type(pid) is not int or type(ticks) is not int:
        raise RuntimeError(f'Missing {key} process identity in launcher record')
    while time.monotonic() < deadline:
        try:
            current = proc_identity(pid, proc_root)
        except FileNotFoundError:
            return
        if current['start_ticks'] != ticks:
            return
        time.sleep(0.1)
    raise TimeoutError(f'Owned {key} process {pid} is still running')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--identity', type=Path, required=True)
    parser.add_argument('--timeout', type=float, default=60.0)
    args = parser.parse_args(argv)
    identity_path = args.identity.resolve(strict=True)
    identity = json.loads(identity_path.read_text(encoding='utf-8'))
    validate_identity(identity, identity_path)
    # Validate both child identities before signaling their recorded parent.
    for key in ('qemu', 'xvfb'):
        child = proc_identity(identity[key + '_pid'])
        if child['start_ticks'] != identity[key + '_start_ticks']:
            raise RuntimeError(f'{key} PID has been reused; refusing launcher signal')
    os.kill(identity['pid'], signal.SIGINT)

    result_path = Path(identity['run_dir']) / 'results.json'
    deadline = time.monotonic() + args.timeout
    result = None
    while time.monotonic() < deadline:
        try:
            result = json.loads(result_path.read_text(encoding='utf-8'))
        except FileNotFoundError:
            time.sleep(0.1)
            continue
        break
    if result is None:
        raise TimeoutError(f'Launcher did not write final result: {result_path}')
    recorded = result.get('launcher_identity', {})
    if any(recorded.get(key) != identity.get(key)
           for key in ('pid', 'start_ticks', 'run_dir', 'identity_record')):
        raise RuntimeError('Final result belongs to a different launcher identity')
    if (result.get('stop_reason') != 'signal' or result.get('graceful_powerdown') is not True
            or result.get('cleanup') != {'qemu_stopped': True, 'xvfb_stopped': True}):
        raise RuntimeError('Launcher final result does not attest signal-driven graceful cleanup')
    for key in ('qemu', 'xvfb'):
        wait_for_owned_child_exit(identity, key, Path('/proc'), deadline)
    print(json.dumps({'result': str(result_path), 'stop_reason': result['stop_reason'],
                      'graceful_powerdown': result['graceful_powerdown'],
                      'cleanup': result['cleanup'],
                      'qemu_exit_status': result.get('qemu_exit_status')}, sort_keys=True))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
