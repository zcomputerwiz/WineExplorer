#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""One approved Edge MSI in a fresh owned prefix; never a browser/graphics test."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import threading
import time

import graphics_resources
from graphics_wexcomp import OwnedWexcomp, verify_native_gl_driver

LAB = Path('/home/debian/wex-g4')
BUILD = Path('/var/tmp/wex-g3-repair-20261018/clean-source/build/wow64')
VERSION = '156.0.4314.42'
PACKAGE = 'MicrosoftEdgeEnterpriseX64-'+VERSION+'.msi'
SHA256 = '324d226c5ade08c2776645cfc2a7b12af9c08214abf18c6499410cd8ed92d1b3'
BYTES = 215257088
MIN_FREE = 5*1024**3
TOTAL_SECONDS, CLEANUP_SECONDS = 285, 40  # 15s headroom inside the SSH 300s bound
SIGNER = '4028CAD637509D4744B17EC5B42AED8D7A31E6AF'


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def validate(package, provenance, output):
    if (not output.is_absolute() or output.parent.resolve() != LAB.resolve()
            or not re.fullmatch(r'edge-install-[A-Za-z0-9._-]+', output.name)
            or output.exists() or output.is_symlink()):
        raise ValueError('Output must be a fresh direct owned-lab child named edge-install-*')
    vendor = LAB/(output.name+'-stage')/'vendor'
    if (package.name != PACKAGE or provenance.name != 'acquisition.json'
            or package.resolve(strict=True).parent != vendor.resolve(strict=True)
            or provenance.resolve(strict=True).parent != vendor.resolve(strict=True)
            or not vendor.resolve().is_relative_to(LAB.resolve())):
        raise ValueError('Package/provenance must be the exact approved staged files')
    record = json.loads(provenance.read_text())
    if (record.get('status') != 'ACQUIRED_HASH_AND_MICROSOFT_SIGNATURE_VERIFIED_NOT_EXECUTED'
            or record.get('version') != VERSION or record.get('payload') != PACKAGE
            or record.get('hashPassed') is not True or record.get('expectedSha256') != SHA256
            or record.get('actualSha256') != SHA256 or record.get('payloadBytes') != BYTES
            or record.get('consent', {}).get('statement') != 'Yes, accept and test privately'
            or record.get('consent', {}).get('termsUrl') != 'https://www.microsoft.com/en-us/edge/server/download'
            or record.get('authenticode', {}).get('status') != 'Valid'
            or record.get('authenticode', {}).get('signer', {}).get('thumbprintSha1') != SIGNER
            or record.get('payloadHttp', {}).get('finalHost') != 'msedge.sf.dl.delivery.mp.microsoft.com'):
        raise ValueError('Approved consent/signature/provenance mismatch')
    if package.stat().st_size != BYTES or digest(package) != SHA256:
        raise ValueError('Approved MSI size/SHA256 mismatch')
    if shutil.disk_usage(LAB).free < MIN_FREE:
        raise ValueError('At least 5 GiB free is mandatory; do not delete evidence')
    return {'package': str(package.resolve()), 'sha256': SHA256, 'bytes': BYTES,
            'provenance_sha256': digest(provenance), 'consent_and_host_signature_provenance_verified': True}


def environment(output):
    env = {k:v for k,v in os.environ.items()
           if not k.startswith(('WINE','WEX_','VK_','MESA_','GALLIUM_','DXVK_','VKD3D_'))
           and k not in ('DISPLAY','WAYLAND_DISPLAY','WAYLAND_SOCKET','DBUS_SESSION_BUS_ADDRESS',
                         'LD_PRELOAD','LD_LIBRARY_PATH','LIBGL_ALWAYS_SOFTWARE','XDG_RUNTIME_DIR',
                         'XDG_CONFIG_DIRS','USERPROFILE','APPDATA','LOCALAPPDATA','TEMP','TMP','HOMEDRIVE','HOMEPATH')}
    env.update(WINEPREFIX=str(output/'prefix'), WINE=str(BUILD/'wine'), WINESERVER=str(BUILD/'server/wineserver'),
        WINEDEBUG='-all,+timestamp,+pid,+tid,+winediag,+msi,+process', WINE_D3D_CONFIG='renderer=gl',
        WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d;d3d11,dxgi,wined3d,dcomp=b;winex11.drv=d;winewayland.drv=b')
    env.update({key:str(output/folder) for key,folder in (
        ('HOME','home'),('XDG_CACHE_HOME','cache'),('XDG_CONFIG_HOME','config'),
        ('XDG_DATA_HOME','data'),('TMPDIR','tmp'))})
    return env


def install_command(package, output):
    win = lambda p: 'Z:'+str(p).replace('/', '\\')
    return [str(BUILD/'wine'), 'msiexec', '/i', win(package), '/qn', '/norestart', '/l*v', win(output/'install-msi.log')]


def pe_metadata(path):
    """Bounds-checked DOS/COFF architecture only; no file-version claim."""
    if path.stat().st_size > 64*1024**2:
        raise ValueError('Unexpectedly large msedge.exe')
    data = path.read_bytes()
    if len(data) < 64 or data[:2] != b'MZ':
        raise ValueError('Installed executable is not PE')
    off = struct.unpack_from('<I', data, 0x3c)[0]
    if off < 64 or off+24 > len(data) or data[off:off+4] != b'PE\0\0':
        raise ValueError('Truncated/invalid PE header')
    machine = struct.unpack_from('<H', data, off+4)[0]
    if machine != 0x8664:
        raise ValueError('Installed PE is not x64')
    return {'path': str(path.resolve()), 'sha256': digest(path), 'machine': machine}


def installed_evidence(prefix):
    paths = [prefix/'drive_c'/folder/'Microsoft/Edge/Application/msedge.exe'
             for folder in ('Program Files', 'Program Files (x86)')]
    found = [p for p in paths if p.is_file()]
    if len(found) != 1 or not found[0].resolve().is_relative_to(prefix.resolve()):
        raise ValueError('Expected exactly one installed msedge.exe inside fresh prefix')
    evidence = pe_metadata(found[0])
    registrations = []
    for hive in ('system.reg', 'user.reg'):
        path = prefix/hive
        if not path.is_file():
            continue
        for key, values in re.findall(r'(?ms)^\[([^\n]+)\][^\n]*\n(.*?)(?=^\[|\Z)', path.read_text(errors='replace')):
            normal = key.replace('\\\\','\\').lower()
            if (('\\uninstall\\' in normal or '\\edgeupdate\\clients\\' in normal)
                    and re.search(r'^"(?:DisplayName|name)"="Microsoft Edge"$', values, re.M)
                    and re.search(r'^"(?:DisplayVersion|pv)"="'+re.escape(VERSION)+r'"$', values, re.M)):
                registrations.append({'hive':hive, 'key':key, 'values':values})
    if not registrations:
        raise ValueError('Installed product registration/version evidence missing')
    return {'executable':evidence, 'registration':registrations,
            'version':VERSION, 'version_source':'registration', 'independent_pe_file_version_verified':False}


def collect_setup_logs(prefix, output, deadline, now):
    logs, total = [], 0
    for index, path in enumerate(prefix.rglob('*.log')):
        if index >= 128 or now() >= deadline:
            logs.append({'scan_stopped':'count/deadline cap; remaining logs retained in prefix'})
            break
        if not path.is_file() or not path.resolve().is_relative_to(prefix.resolve()):
            continue
        size = path.stat().st_size
        relative = str(path.relative_to(prefix))
        if size > 16*1024**2 or total+size > 32*1024**2:
            logs.append({'prefix_path':relative, 'retained_in_prefix_only':True})
            continue
        target = output/('setup-log-'+hashlib.sha256(relative.encode()).hexdigest()[:16]+'.log')
        shutil.copyfile(path, target); total += size
        logs.append({'prefix_path':relative, 'copy':target.name, 'sha256':digest(target)})
    return logs


def run(package, provenance, output, helpers, *, now=time.monotonic):
    start = now()  # include package hashing/guards in the complete run budget
    approved = validate(package, provenance, output)  # no prefix/process before guards
    output.mkdir(exist_ok=False)
    env = environment(output)
    for key in ('HOME','XDG_CACHE_HOME','XDG_CONFIG_HOME','XDG_DATA_HOME','TMPDIR'):
        Path(env[key]).mkdir()
    helpers.configure_gl_driver(env, 'virgl')
    comp = OwnedWexcomp(LAB/'labwc', LAB/'wayland', output, env, 'gles2')
    deadline = start+TOTAL_SECONDS
    result = {'classification':'Installation prerequisite ONLY; no browser/graphics acceptance',
              'passed':False, 'status':'RUNNING', 'approved':approved, 'prefix_retained':str(output/'prefix'),
              'command':install_command(package, output), 'maximum_seconds':TOTAL_SECONDS}
    stop = threading.Event()
    def sample():
        try:
            with (output/'installer-processes.jsonl').open('a') as log:
                while not stop.is_set():
                    snapshot = graphics_resources.sample(output/'prefix')
                    for pid, item in snapshot.items():
                        try:
                            item['cmdline'] = (Path('/proc')/pid/'cmdline').read_bytes().replace(b'\0',b' ').decode(errors='replace')
                        except OSError:
                            pass
                    log.write(json.dumps({'elapsed':now()-start,'processes':snapshot})+'\n'); log.flush()
                    stop.wait(.2)
        except Exception as error:
            result['process_monitor_error'] = repr(error)
    monitor = threading.Thread(target=sample, daemon=True)
    def bound(maximum, cleanup=False):
        remaining = deadline-now()-(0 if cleanup else CLEANUP_SECONDS)
        if remaining <= 0:
            raise TimeoutError('Complete installation deadline exhausted')
        return min(maximum, remaining)
    attempted = False
    try:
        monitor.start()
        result['wexcomp'] = comp.start()
        result['outer_gl_driver_verification'] = comp.verify_gpu_renderer('virgl')
        result['gl_driver_verification'] = verify_native_gl_driver(env, output, 'virgl')
        boot = helpers.run_logged([str(BUILD/'wine'),'wineboot','-u'], env, output, 'wineboot', bound(90))
        result['wineboot_exit'] = boot.returncode
        if boot.returncode:
            raise RuntimeError('Fresh prefix initialization failed')
        installer_bound = bound(120)
        attempted = True
        installed = helpers.run_logged(result['command'], env, output, 'installer', installer_bound)
        result['installer_exit'] = installed.returncode
        if installed.returncode in (3010,1641,194,105):
            result['status'] = 'REBOOT_REQUIRED_OR_TRUNCATED_REBOOT_CODE_REVIEW_REQUIRED'
        elif installed.returncode:
            result['status'] = 'INSTALLER_FAILED'
        else:
            result['status'] = 'INSTALLER_RETURNED_ZERO_PENDING_PAYLOAD_CHECK'
        comp.verify()
    except Exception as error:
        result['status'] = 'TIMEOUT' if isinstance(error, subprocess.TimeoutExpired) or isinstance(error, TimeoutError) else 'FAILED'
        result['error'] = repr(error)
    finally:
        result['installer_attempted'] = attempted
        for label, args in (('kill',['-k']), ('wait',['-w'])):
            try:
                code = helpers.run_logged([str(BUILD/'server/wineserver')]+args, env, output, 'cleanup-'+label, bound(10, True)).returncode
                result['cleanup_'+label+'_exit'] = code
            except Exception as error:
                result.setdefault('cleanup_errors', []).append(repr(error))
        stop.set()
        if monitor.ident is not None: monitor.join(timeout=1)
        result['process_monitor_stopped'] = not monitor.is_alive()
        try:
            result['remaining_servers'] = helpers.matching_servers(BUILD/'server/wineserver', output/'prefix')
        except Exception as error:
            result.setdefault('cleanup_errors', []).append(repr(error))
        try:
            comp.stop()
            result['remaining_prefix_processes'] = graphics_resources.sample(output/'prefix')
        except Exception as error:
            result.setdefault('cleanup_errors', []).append(repr(error))
        try:
            result['setup_logs'] = collect_setup_logs(output/'prefix', output, deadline, now)
            if result['status'] == 'INSTALLER_RETURNED_ZERO_PENDING_PAYLOAD_CHECK':
                result['installed'] = installed_evidence(output/'prefix')
                result['status'] = 'INSTALLED_PREREQUISITE_ONLY'
        except Exception as error:
            result['postprocess_error'] = repr(error)
            result.setdefault('error', repr(error))  # never mask the installer/timeout failure
            if result['status'] == 'INSTALLER_RETURNED_ZERO_PENDING_PAYLOAD_CHECK':
                result['status'] = 'PAYLOAD_OR_LOG_EVIDENCE_FAILED'
        finally:
            result['elapsed_seconds'] = now()-start
            result['passed'] = (result['status']=='INSTALLED_PREREQUISITE_ONLY' and not result.get('cleanup_errors')
                            and result.get('cleanup_kill_exit') in (0,1) and result.get('cleanup_wait_exit') == 0
                            and not result.get('remaining_servers', [1]) and not result.get('remaining_prefix_processes', {'missing':1})
                            and not result.get('process_monitor_error') and not result.get('postprocess_error') and result['process_monitor_stopped']
                            and result['elapsed_seconds'] <= TOTAL_SECONDS)
            (output/'results.json').write_text(json.dumps(result, indent=2)+'\n')
    return 0 if result['passed'] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--provenance', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('edge_install_helpers', Path(__file__).with_name('check-g3-correctness.py'))
    helpers = importlib.util.module_from_spec(spec); spec.loader.exec_module(helpers)
    return run(args.package, args.provenance, args.output_dir, helpers)


if __name__ == '__main__':
    raise SystemExit(main())
