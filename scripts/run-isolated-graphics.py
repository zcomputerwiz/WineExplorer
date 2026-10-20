#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run an owned graphics check with private mount/net AND filesystem X sockets.

Example (privileged Linux lab):
  python3 run-isolated-graphics.py --socket-root /var/tmp/owned-lab/sockets -- \
    python3 check-g3-correctness.py --wine-build ... --output-dir ... --g4-correctness

The nested runner must bound/clean its own Wine/Xvfb processes. No global socket
directory is modified. A nonempty directory is retained, never recursively deleted.
"""
import argparse
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile


def main():
    if len(sys.argv) >= 5 and sys.argv[1] == '--child' and sys.argv[3] == '--':
        subprocess.run(['mount', '--bind', sys.argv[2], '/tmp/.X11-unix'], check=True)
        os.execvp(sys.argv[4], sys.argv[4:])
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--socket-root', type=Path, required=True)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if not args.socket_root.is_absolute():
        parser.error('--socket-root must be an absolute owned lab directory')
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        parser.error('A nested test command is required')
    args.socket_root.mkdir(parents=True, exist_ok=True)
    sockets = Path(tempfile.mkdtemp(prefix='xsocket-', dir=args.socket_root.resolve()))
    sockets.chmod(0o1777)
    process = None
    try:
        process = subprocess.Popen(['unshare', '--mount', '--net', '--propagation', 'private',
                                    sys.executable, str(Path(__file__).resolve()), '--child',
                                    str(sockets), '--'] + command, start_new_session=True)
        return process.wait()
    except KeyboardInterrupt:
        if process and process.poll() is None:
            os.killpg(process.pid, signal.SIGINT)
            try:
                process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=5)
        raise
    finally:
        try:
            sockets.rmdir()  # Remove only this empty, generated directory.
        except OSError as error:
            print('Retained owned socket directory: ' + str(sockets) + ': ' + str(error), file=sys.stderr)


if __name__ == '__main__':
    raise SystemExit(main())
