# SPDX-License-Identifier: GPL-2.0-or-later
"""One bounded Wine thread snapshot, isolated to the current private prefix."""
import hashlib
from pathlib import Path
import subprocess


def observe(output, env, build, prefix, *, maximum_seconds=8):
    output, build, prefix = Path(output), Path(build), Path(prefix)
    if env.get('WINEPREFIX') != str(prefix) or env.get('WINESERVER') != str(build/'server/wineserver'):
        raise ValueError('Wait snapshot requires exact candidate prefix/server environment')
    if prefix.name != 'chromium-token-wait-r9-20261031-prefix':
        raise ValueError('Wait snapshot requires dedicated fixed diagnostic prefix')
    if not 0 < maximum_seconds <= 8:
        raise ValueError('Wait snapshot exceeds bounded diagnostic budget')
    debugger = build/'programs/winedbg/x86_64-windows/winedbg.exe'
    if not debugger.is_file():
        raise FileNotFoundError(debugger)
    snapshot_env = dict(env, WINEDEBUG='-all')
    command = [str(build/'wine'), str(debugger), '--command', 'bt all']
    record = {'command': command, 'maximum_seconds': maximum_seconds,
              'classification': 'One debugger thread snapshot; not uninstrumented browser acceptance',
              'candidate_server_sha256': hashlib.sha256((build/'server/wineserver').read_bytes()).hexdigest()}
    with (output/'wait-snapshot.stdout.log').open('wb') as out, (output/'wait-snapshot.stderr.log').open('wb') as err:
        process = subprocess.Popen(command, env=snapshot_env, stdout=out, stderr=err)
        try:
            record['exit'] = process.wait(timeout=maximum_seconds)
            record['timed_out'] = False
        except subprocess.TimeoutExpired:
            process.kill()
            record['exit'] = process.wait(timeout=2)
            record['timed_out'] = True
    record['reaped'] = True
    for stream in ('stdout','stderr'):
        size=(output/('wait-snapshot.'+stream+'.log')).stat().st_size
        record[stream+'_bytes']=size
        if size>8*1024**2:
            raise ValueError('Wait snapshot output quota exceeded')
    return record
