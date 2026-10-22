#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Run token regressions on an explicit build and owned display/prefixes.

This runner proves only its recorded functional cases, not sandbox containment.
No installed runtime, live prefix, inherited override, or global cleanup is used.
"""
import argparse
import hashlib
import re
import json
import os
from pathlib import Path
import select
import signal
import shutil
import subprocess
import tempfile


KNOWN_UPSTREAM_FAILURES = {('shell32', 'shlexec'): 2}


def run_logged(command, environment, output, label, timeout):
    """Keep diagnostics even when a regression hangs instead of exiting."""
    stdout = output / (label + '.stdout.log')
    stderr = output / (label + '.stderr.log')
    # Files, not pipes: a timed-out child's descendants cannot keep communicate()
    # waiting for EOF. The finally block terminates only our matching servers.
    with stdout.open('wb') as out, stderr.open('wb') as err:
        process = subprocess.Popen(command, env=environment, stdout=out, stderr=err,
                                   start_new_session=True)
        try:
            code = process.wait(timeout=timeout)
        except BaseException:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=5)
            raise
    return subprocess.CompletedProcess(command, code, stdout.read_text(errors='replace'),
                                       stderr.read_text(errors='replace'))


def matching_servers(server, prefix):
    """Read-only backstop after -w; never select servers merely by process name."""
    matches = []
    for process in Path('/proc').iterdir():
        if not process.name.isdigit():
            continue
        try:
            if process.stat().st_uid != os.getuid() or (process / 'exe').resolve() != server:
                continue
            if b'WINEPREFIX=' + os.fsencode(prefix) in (process / 'environ').read_bytes().split(bytes([0])):
                matches.append(int(process.name))
        except (FileNotFoundError, ProcessLookupError):
            continue
    return matches


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--wine-build', type=Path, default=os.environ.get('WINE_BUILD_DIR'))
    parser.add_argument('--output-dir', type=Path)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument('--checkpoint-a', action='store_true')
    modes.add_argument('--checkpoint-b', action='store_true')
    modes.add_argument('--all', action='store_true')
    parser.add_argument('--focused', action='store_true', help='Only new focused probes; NOT upstream-suite acceptance')
    parser.add_argument('--runner-fixture', choices=('success', 'failure', 'timeout', 'signal'),
                        help='Cleanup fixture only; NOT token/elevation acceptance')
    args = parser.parse_args()
    if not args.wine_build:
        parser.error('An explicit --wine-build or WINE_BUILD_DIR is required; installed Wine is never substituted')
    build = Path(args.wine_build).resolve(strict=True)
    wine, server = build / 'wine', build / 'server/wineserver'
    for executable in (wine, server):
        if not executable.is_file() or not os.access(executable, os.X_OK):
            parser.error('Missing matching build executable: ' + str(executable))
    suites = []
    if not args.focused and not args.runner_fixture:
        for arch in ('x86_64', 'i386'):
            if not args.checkpoint_b:
                suites.extend(((arch, 'advapi32', 'security'), (arch, 'ntdll', 'info')))
            if not args.checkpoint_a:
                suites.append((arch, 'shell32', 'shlexec'))
        missing = [str(build / 'dlls' / dll / 'tests' / (arch + '-windows') / (dll + '_test.exe'))
                   for arch, dll, case in suites
                   if not (build / 'dlls' / dll / 'tests' / (arch + '-windows') / (dll + '_test.exe')).is_file()]
        if missing:
            parser.error('Required test executables missing (no tests passed): ' + ', '.join(missing))
    if not args.checkpoint_a and not args.runner_fixture:
        for arch in ('x86_64', 'i386'):
            consent = build / 'programs/consent' / (arch + '-windows') / 'consent.exe'
            if not consent.is_file():
                parser.error('Missing actual shipping consent binary: ' + str(consent))
    if not shutil.which('Xvfb'):
        parser.error('Xvfb is required; no existing display will be reused')
    sockets = Path('/tmp/.X11-unix')
    if sockets.exists() and os.statvfs(sockets).f_flag & os.ST_RDONLY:
        parser.error('Read-only WSLg X sockets: use an owned directory bound in a private mount namespace; '
                     'never modify global sockets')
    host_netns = None
    if os.environ.get('WSL_DISTRO_NAME'):
        try:
            host_netns = os.readlink('/proc/1/ns/net')
        except PermissionError:
            # Public namespace identity captured by the caller BEFORE unshare;
            # this is a test-isolation guard, not an authorization credential.
            host_netns = os.environ.get('WEX_TOKEN_HOST_NETNS')
        if not host_netns or not re.fullmatch(r'net:\[\d+\]', host_netns):
            parser.error('Capture WEX_TOKEN_HOST_NETNS before --net when /proc/1/ns/net is not readable')
        if os.readlink('/proc/self/ns/net') == host_netns:
            parser.error('WSL tests require a private network namespace too (--net), to isolate abstract X sockets')
    output = args.output_dir.resolve() if args.output_dir else Path(tempfile.mkdtemp(prefix='wex-token-evidence-'))
    if args.output_dir:
        output.mkdir(parents=True, exist_ok=False)
    source = Path(__file__).resolve().parent
    result = {'wine_build': str(build), 'uid': os.getuid(), 'cases': {},
              'scope': ('runner fixture only' if args.runner_fixture else
                        'focused regressions only' if args.focused else 'project and upstream cases'),
              'containment_claim': False}
    if host_netns:
        result.update(host_network_namespace=host_netns, network_namespace=os.readlink('/proc/self/ns/net'))
    print('Evidence:', output, flush=True)
    scratch = tempfile.TemporaryDirectory(prefix='wex-token-check-', dir='/tmp', delete=False)
    root = Path(scratch.name)
    display = None
    prefixes = []
    environment = None
    read_fd, write_fd = os.pipe()

    def interrupted(signum, frame):
        raise InterruptedError('Runner received signal ' + str(signum))

    for signum in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(signum, interrupted)
    try:
        with (output / 'xvfb.log').open('wb') as xlog:
            display = subprocess.Popen(['Xvfb', '-displayfd', str(write_fd), '-screen', '0',
                                        '1024x768x24', '-nolisten', 'tcp'],
                                       pass_fds=(write_fd,), stdout=xlog, stderr=xlog,
                                       start_new_session=True)
        os.close(write_fd)
        write_fd = None
        if not select.select([read_fd], [], [], 10)[0]:
            raise TimeoutError('Owned Xvfb did not allocate a display')
        number = os.read(read_fd, 64).strip().decode('ascii')
        if not number.isdigit() or display.poll() is not None:
            raise ValueError('Owned Xvfb failed: inspect xvfb.log')
        environment = {key: value for key, value in os.environ.items()
                       if not key.startswith(('WINE', 'VK_', 'WEX_')) and
                       key not in ('LD_PRELOAD', 'LD_LIBRARY_PATH', 'DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY')}
        environment.update(DISPLAY=':' + number, WINE=str(wine), WINESERVER=str(server),
                           WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d', WINEDEBUG='-all',
                           LIBGL_ALWAYS_SOFTWARE='1', WEX_TOKEN_REGRESSION_TEST='1')
        home = root / 'home'
        home.mkdir()
        runtime = root / 'runtime'
        runtime.mkdir(mode=0o700)
        environment.update(HOME=str(home), XDG_CONFIG_HOME=str(home / 'config'),
                           XDG_CACHE_HOME=str(home / 'cache'), XDG_DATA_HOME=str(home / 'data'),
                           XDG_STATE_HOME=str(home / 'state'), XDG_RUNTIME_DIR=str(runtime))
        result['display'] = environment['DISPLAY']
        result['prefix_root'] = str(root)
        result['environment'] = {key: environment[key] for key in
                                 ('DISPLAY', 'WINE', 'WINESERVER', 'WINEDLLOVERRIDES', 'WINEDEBUG')}
        for arch, compiler in (('x86_64', 'x86_64-w64-mingw32-g++'), ('i386', 'i686-w64-mingw32-g++')):
            exe = output / ('token-regression-' + arch + '.exe')
            command = [compiler, '-O2', '-Wall', '-Wextra', '-Werror', '-static-libgcc', '-static-libstdc++',
                       str(source / 'test_token_security_regressions.cpp'), '-o', str(exe),
                       '-ladvapi32', '-lshell32', '-lole32']
            compiled = run_logged(command, environment, output, arch + '.compile', 90)
            if compiled.returncode:
                raise RuntimeError('Probe compilation failed: ' + arch)
            windres = ('x86_64' if arch == 'x86_64' else 'i686') + '-w64-mingw32-windres'
            child_object = output / ('token-manifest-child-' + arch + '.res.o')
            child_exe = output / ('token-manifest-child-' + arch + '.exe')
            for label, child_command in (
                    ('child-res', [windres, str(source / 'token_manifest_child.rc'), '-I', str(source), '-O', 'coff',
                                   '-o', str(child_object)]),
                    ('child-exe', [compiler, '-O2', '-Wall', '-Wextra', '-Werror', '-static-libgcc',
                                   '-static-libstdc++', str(source / 'token_manifest_child.cpp'),
                                   str(child_object), '-o', str(child_exe)])):
                built = run_logged(child_command, environment, output, arch + '.' + label, 90)
                if built.returncode:
                    raise RuntimeError('Manifest child build failed: ' + arch + ' ' + label)
            prefix = root / arch
            prefixes.append(prefix)
            env = dict(environment, WINEPREFIX=str(prefix))
            if args.runner_fixture:
                warmup = run_logged([str(wine), str(exe), '--helper-exit', '0'], env, output, 'warmup', 60)
                if warmup.returncode:
                    raise RuntimeError('Runner fixture warmup failed')
                (output / 'ready.json').write_text(json.dumps({'prefix': str(prefix), 'display_pid': display.pid,
                                                              'runner_pid': os.getpid()}))
                mode = args.runner_fixture
                command = [str(wine), str(exe)] + (['--helper-exit', '0' if mode == 'success' else '42']
                           if mode in ('success', 'failure') else ['--hold'])
                run = run_logged(command, env, output, 'fixture', 1 if mode == 'timeout' else 240)
                result['fixture_exit'] = run.returncode
                if run.returncode:
                    raise RuntimeError('Intentional runner fixture failure: ' + str(run.returncode))
                return
            command = [str(wine), str(exe)] + ([] if args.checkpoint_a else ['--shell-tests'])
            run = run_logged(command, env, output, arch, 120)
            markers = ['deny_only_acl_and_object_open=PASS']
            if not args.checkpoint_a:
                markers.append('elevation_helper_errors_and_child_handle=PASS')
                markers.append('elevation_helper_states_manifest_child=PASS')
                markers.append('linked_token_routes_closed=PASS')
            passed = run.returncode == 0 and all(marker in run.stdout for marker in markers)
            result['cases'][arch] = {'command': command, 'exit_code': run.returncode, 'passed': passed,
                                     'sha256': hashlib.sha256(exe.read_bytes()).hexdigest()}
            print(arch, 'PASS' if passed else 'FAIL', run.stdout, flush=True)
            if not passed:
                raise RuntimeError('Token regression failed: ' + arch)
            if not args.checkpoint_a:
                # The root token is filtered only if the helper is configured before the process connects to the
                # server, and a child inherits its parent's token. So configure it in a second, throw-away prefix
                # and start the probe as a new Unix-launched process there (see limited_token_shape()).
                shape_prefix = root / (arch + '-limited')
                prefixes.append(shape_prefix)
                shape_env = dict(environment, WINEPREFIX=str(shape_prefix))
                setup = run_logged([str(wine), str(exe), '--helper-set'], shape_env, output, arch + '.limited-setup', 120)
                if setup.returncode:
                    raise RuntimeError('Could not configure the helper for the limited-token probe: ' + arch)
                subprocess.run([str(server), '-k'], env=shape_env, timeout=15)
                subprocess.run([str(server), '-w'], env=shape_env, timeout=30)  # registry is saved on exit
                command = [str(wine), str(exe), '--limited-shape']
                run = run_logged(command, shape_env, output, arch + '.limited', 120)
                passed = run.returncode == 0 and 'limited_token_shape=PASS' in run.stdout
                result['cases'][arch + '.limited'] = {'command': command, 'exit_code': run.returncode, 'passed': passed}
                print(arch, 'limited PASS' if passed else 'limited FAIL', run.stdout, flush=True)
                if not passed:
                    raise RuntimeError('Limited token shape failed: ' + arch)
            for suite_arch, dll, case in suites:
                if suite_arch != arch:
                    continue
                test = build / 'dlls' / dll / 'tests' / (arch + '-windows') / (dll + '_test.exe')
                label = arch + '.' + dll + '.' + case
                command = [str(wine), str(test), case]
                run = run_logged(command, env, output, label, 180)
                counts = re.findall(r'(\d+) tests executed[^\n]*?\b(\d+) failures', run.stdout + run.stderr)
                # Test programs that spawn children print one summary per process (children often run 0 tests), so
                # require assertions to have run overall and no failures in any process.
                # shell32 shlexec has 2 failures on unmodified Wine master in this environment (the
                # SEE_MASK_CLASSNAME .exe cases); anything beyond that baseline fails the run.
                expected = KNOWN_UPSTREAM_FAILURES.get((dll, case), 0)
                passed = ((run.returncode == 0 or (expected and run.returncode != 0)) and bool(counts) and
                          any(int(n) > 0 for n, f in counts) and sum(int(f) for n, f in counts) == expected)
                result['cases'][label] = {'command': command, 'exit_code': run.returncode,
                                         'summary_counts': counts, 'passed': passed}
                if not passed:
                    raise RuntimeError('Upstream suite failed or did not execute assertions: ' + label)
            if not args.checkpoint_a:
                consent = build / 'programs/consent' / (arch + '-windows') / 'consent.exe'
                # Redirect Z only inside this fresh prefix. The untrusted fixture
                # is never written into the host's real /etc or another prefix.
                fixture = root / (arch + '-fixture')
                config = fixture / 'etc/wineexplorer/test-elevation.conf'
                config.parent.mkdir(parents=True)
                config.write_text('mode=approve\n')
                config.chmod(0o666)
                shutil.copy2(consent, prefix / 'drive_c/consent-under-test.exe')
                shutil.copy2(exe, prefix / 'drive_c/token-consent-probe.exe')
                z_drive = prefix / 'dosdevices/z:'
                if not z_drive.is_symlink():
                    raise RuntimeError('Unexpected Z mapping in owned prefix')
                z_drive.unlink()
                z_drive.symlink_to(fixture, target_is_directory=True)
                command = [str(wine), r'C:\token-consent-probe.exe', '--consent', r'C:\consent-under-test.exe']
                run = run_logged(command, env, output, arch + '.consent', 120)
                passed = run.returncode == 0 and 'shipping_consent_has_no_approval_bypass=PASS' in run.stdout
                result['cases'][arch + '.consent'] = {'command': command, 'exit_code': run.returncode,
                                                     'passed': passed, 'fixture_mode': '0666',
                                                     'shipping_binary_sha256': hashlib.sha256(consent.read_bytes()).hexdigest()}
                print(arch, 'consent PASS' if passed else 'consent FAIL', run.stdout, flush=True)
                if not passed:
                    raise RuntimeError('Shipping consent regression failed: ' + arch)
    except BaseException as error:
        result['failure'] = repr(error)
        raise
    finally:
        # Finish cleanup even if a second cancellation arrives.
        for signum in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
            signal.signal(signum, signal.SIG_IGN)
        os.close(read_fd)
        if write_fd is not None:
            os.close(write_fd)
        errors = []
        for prefix in prefixes:
            env = dict(environment or os.environ, WINEPREFIX=str(prefix))
            try:
                killed = subprocess.run([str(server), '-k'], env=env, timeout=15)
                if killed.returncode not in (0, 1):
                    raise RuntimeError('Unexpected wineserver -k exit: ' + str(killed.returncode))
                subprocess.run([str(server), '-w'], env=env, timeout=15, check=True)
                # Background processes (conhost, explorer, services) write to the probe's stderr log after
                # the probe exits, so look for unhandled exceptions only once the server is idle.
                log = output / (prefix.name + '.stderr.log')
                if log.exists() and 'Unhandled' in log.read_text(errors='replace'):
                    raise RuntimeError('Unhandled exception in a background process: ' + log.name)
                remaining = matching_servers(server, prefix)
                if remaining:
                    raise RuntimeError('Owned server still running: ' + str(remaining))
                result.setdefault('cleanup', []).append({'prefix': str(prefix), 'kill_exit': killed.returncode,
                                                         'wait_exit': 0, 'remaining_servers': []})
            except (OSError, subprocess.SubprocessError, RuntimeError) as error:
                errors.append(str(error))
        if display and display.poll() is None:
            os.killpg(display.pid, signal.SIGTERM)
            try:
                display.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(display.pid, signal.SIGKILL)
                display.wait(timeout=5)
        if errors:
            result['cleanup_errors'] = errors
            result['retained_prefix_root'] = str(root)
        else:
            scratch.cleanup()
        (output / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
        if errors:
            raise RuntimeError('Owned-prefix cleanup needs inspection; see results.json')


if __name__ == '__main__':
    main()
