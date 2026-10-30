#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Exercise real owned Wine/display resources; sentinel is independently owned.

Requires the same private X-socket namespace as check-token-security on WSLg.
This is runner lifecycle evidence, NOT token or authorization acceptance.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


def identity(pid):
    # comm may contain spaces, so split after its closing parenthesis.
    return (Path('/proc') / str(pid) / 'stat').read_text().rsplit(')', 1)[1].split()[19]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--wine-build', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    build = args.wine_build.resolve(strict=True)
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=False)
    runner = Path(__file__).resolve().with_name('check-token-security.py')
    spec = importlib.util.spec_from_file_location('token_runner', runner)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    processes = []
    streams = []
    result = {'scope': 'actual runner resource isolation, not security feature acceptance', 'cases': {}}

    def interrupted(signum, frame):
        raise InterruptedError('Isolation harness received signal ' + str(signum))

    for signum in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(signum, interrupted)

    def start(label, mode):
        stream = (output / (label + '.log')).open('wb')
        streams.append(stream)
        command = [sys.executable, '-B', str(runner), '--wine-build', str(build),
                   '--output-dir', str(output / label), '--runner-fixture', mode]
        process = subprocess.Popen(command, stdout=stream, stderr=stream, start_new_session=True)
        processes.append(process)
        return process

    def ready(label, process):
        deadline = time.monotonic() + 150
        path = output / label / 'ready.json'
        heartbeat = output / label / 'fixture.stdout.log'
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError('Runner exited before live resources were ready: ' + label)
            if path.is_file() and heartbeat.is_file() and 'fixture_ready' in heartbeat.read_text():
                return json.loads(path.read_text())
            time.sleep(0.05)
        raise TimeoutError('Live resource fixture did not become ready: ' + label)

    try:
        sentinel = start('sentinel', 'signal')
        owned = ready('sentinel', sentinel)
        identities = {pid: identity(pid) for pid in (sentinel.pid, owned['display_pid'])}
        server = build / 'server/wineserver'
        server_ids = module.matching_servers(server, Path(owned['prefix']))
        if not server_ids:
            raise RuntimeError('Sentinel has no live matching Wine server')
        identities.update({pid: identity(pid) for pid in server_ids})
        result['sentinel_identities'] = identities
        for mode in ('success', 'failure', 'timeout', 'signal'):
            child = start(mode, mode)
            if mode == 'signal':
                ready(mode, child)
                child.send_signal(signal.SIGTERM)
            code = child.wait(timeout=180)
            data = json.loads((output / mode / 'results.json').read_text())
            if (code == 0) != (mode == 'success'):
                raise RuntimeError('Wrong runner fixture exit: ' + mode)
            if not data.get('cleanup') or data.get('cleanup_errors') or Path(data['prefix_root']).exists():
                raise RuntimeError('Owned state not cleaned: ' + mode)
            if sentinel.poll() is not None:
                raise RuntimeError('Sentinel runner exited: ' + mode)
            for pid, original in identities.items():
                if identity(pid) != original:
                    raise RuntimeError('Sentinel identity changed: ' + str(pid))
            heartbeat = output / 'sentinel/fixture.stdout.log'
            if time.time() - heartbeat.stat().st_mtime > 3:
                raise RuntimeError('Sentinel Wine heartbeat stopped: ' + mode)
            result['cases'][mode] = {'runner_exit': code, 'sentinel_unchanged': True,
                                      'cleanup': data['cleanup'], 'passed': True}
            print(mode, 'PASS', flush=True)
    except BaseException as error:
        result['failure'] = repr(error)
        raise
    finally:
        for signum in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
            signal.signal(signum, signal.SIG_IGN)
        for process in processes:
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
                try:
                    process.wait(timeout=40)
                except subprocess.TimeoutExpired:
                    # Only our exact process group. Preserve diagnostics on error.
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait(timeout=5)
                    result.setdefault('cleanup_errors', []).append('Runner did not clean up: ' + str(process.pid))
        for stream in streams:
            stream.close()
        sentinel_result = output / 'sentinel/results.json'
        if sentinel_result.is_file():
            data = json.loads(sentinel_result.read_text())
            result['sentinel_cleanup'] = data.get('cleanup', [])
            if (not data.get('cleanup') or data.get('cleanup_errors')
                    or Path(data['prefix_root']).exists()):
                result.setdefault('cleanup_errors', []).append('Sentinel owned resources not cleaned')
        else:
            result.setdefault('cleanup_errors', []).append('Missing sentinel cleanup evidence')
        (output / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
    if result.get('cleanup_errors'):
        raise RuntimeError('Fixture cleanup needs inspection')


if __name__ == '__main__':
    main()
