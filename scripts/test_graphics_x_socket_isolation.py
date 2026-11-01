#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check filesystem X sockets, not only abstract sockets, are private per run.

Run as the same privileged lab user that owns the isolated mount/net namespaces.
--shared is an expected-failure control using an owned directory, never WSLg.
No Wine process, live prefix or global X socket is touched.
"""
import argparse
import json
import os
from pathlib import Path
import select
import signal
import socket
import struct
import subprocess
import sys


def inspect(display, number):
    path = Path('/tmp/.X11-unix') / ('X' + number)
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(5)
        connection.connect(str(path))
        peer, _, _ = struct.unpack('3i', connection.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
    return {'display': number, 'own_xvfb_pid': display.pid, 'filesystem_peer_pid': peer,
            'inode': path.stat().st_ino, 'isolated': peer == display.pid}


def child(sockets, log):
    subprocess.run(['mount', '--bind', sockets, '/tmp/.X11-unix'], check=True)
    read, write = os.pipe()
    display = None
    try:
        with open(log, 'wb') as output:
            display = subprocess.Popen(['Xvfb', '-displayfd', str(write), '-screen', '0',
                                        '320x240x24', '-nolisten', 'tcp'], pass_fds=(write,),
                                       stdin=subprocess.DEVNULL, stdout=output, stderr=output,
                                       start_new_session=True)
        os.close(write)
        write = None
        if not select.select([read], [], [], 15)[0]:
            raise TimeoutError('Xvfb startup')
        number = os.read(read, 64).strip().decode('ascii')
        if not number.isdigit() or display.poll() is not None:
            raise RuntimeError('Xvfb failed')
        print(json.dumps(inspect(display, number)), flush=True)
        for command in sys.stdin:
            if command.strip() == 'quit':
                break
            if command.strip() != 'inspect':
                raise ValueError('Unknown command')
            print(json.dumps(inspect(display, number)), flush=True)
    finally:
        os.close(read)
        if write is not None:
            os.close(write)
        if display and display.poll() is None:
            os.killpg(display.pid, signal.SIGTERM)
            try:
                display.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(display.pid, signal.SIGKILL)
                display.wait(timeout=5)


def receive(process):
    if not select.select([process.stdout], [], [], 20)[0]:
        raise TimeoutError('Namespace child response')
    line = process.stdout.readline()
    if not line:
        raise RuntimeError('Namespace child exited: ' + str(process.poll()))
    return json.loads(line)


def main():
    if len(sys.argv) == 4 and sys.argv[1] == '--child':
        child(sys.argv[2], sys.argv[3])
        return 0
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--shared', action='store_true')
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    processes, errors = [], []
    result = {'classification': 'Owned X socket isolation check', 'shared_control': args.shared}
    try:
        for name in ('first', 'second'):
            sockets = output / ('shared' if args.shared else name)
            sockets.mkdir(exist_ok=args.shared)
            sockets.chmod(0o1777)
            error = (output / (name + '.stderr.log')).open('wb')
            errors.append(error)
            process = subprocess.Popen(['unshare', '--mount', '--net', '--propagation', 'private',
                                        sys.executable, str(Path(__file__).resolve()), '--child',
                                        str(sockets), str(output / (name + '.xvfb.log'))],
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=error,
                                       text=True, start_new_session=True)
            processes.append(process)
            result[name] = receive(process)
        processes[0].stdin.write('inspect\n')
        processes[0].stdin.flush()
        result['first_after_second_start'] = receive(processes[0])
        result['passed'] = all(result[key]['isolated'] for key in
                               ('first', 'second', 'first_after_second_start'))
    finally:
        for process in processes:
            if process.poll() is None:
                try:
                    process.stdin.write('quit\n')
                    process.stdin.flush()
                    process.wait(timeout=10)
                except (BrokenPipeError, subprocess.TimeoutExpired):
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait(timeout=5)
            if process.returncode:
                result['passed'] = False
            process.stdin.close()
            process.stdout.close()
        for error in errors:
            error.close()
        (output / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 0 if result.get('passed') else 1


if __name__ == '__main__':
    raise SystemExit(main())
