# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline tests of the actual ignored procedure; never dispatch SSH."""
import base64
import copy
import importlib.util
import json
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import g4_virgl_budget
import stop_g4_virgl_launcher

PRIVATE = Path(__file__).resolve().parents[1]/'build/g4-virgl-20261022/presentation-timing-trial.py'
LAB = Path('/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022')
REAL_RUN = subprocess.run


@unittest.skipUnless(PRIVATE.is_file() and LAB.is_dir(), 'owned private procedure/D: storage unavailable')
class RemoteFailureTests(unittest.TestCase):
    def procedure(self):
        spec = importlib.util.spec_from_file_location('remote_failure_private', PRIVATE)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_missing_and_empty_are_distinct(self):
        p = self.procedure()
        for value, present, kind in ((None, False, 'none'), (b'', True, 'bytes'), ('', True, 'text')):
            record = p.remote_failure(subprocess.CalledProcessError(1, ['actual', 'argv'], value, value))
            self.assertEqual(record['argv'], ['actual', 'argv'])
            self.assertEqual(record['returncode'], 1)
            for key in ('stdout', 'stderr'):
                item = record[key]
                self.assertEqual((item['present'], item['input_type']), (present, kind))
                self.assertEqual(item['base64'], '' if present else None)
                self.assertEqual(item['original_byte_length'], 0 if present else None)
                self.assertFalse(item['truncated'])

    def test_non_utf8_and_text_roundtrip(self):
        p = self.procedure()
        for value in (b'\x00\xff\xfe\r\n', 'unicode \u2603\ud800'):
            item = p.remote_failure(subprocess.CalledProcessError(-9, ['x'], value))['stdout']
            expected = value if isinstance(value, bytes) else value.encode('utf-8', 'surrogatepass')
            self.assertEqual(base64.b64decode(item['base64']), expected)
            self.assertEqual(item['original_byte_length'], len(expected))
            if isinstance(value, str):
                self.assertIn('unavailable', item['byte_origin'])
                self.assertEqual(expected.decode('utf-8', 'surrogatepass'), value)

    def test_deterministic_byte_caps(self):
        p = self.procedure()
        for value in (b'x'*9000, '\u2603'*4000):
            record = p.remote_failure(subprocess.CalledProcessError(1, ['x'], value, value))
            raw = value if isinstance(value, bytes) else value.encode('utf-8')
            for key, cap in (('stdout', 4096), ('stderr', 8192)):
                item = record[key]
                self.assertEqual(base64.b64decode(item['base64']), raw[:cap])
                self.assertEqual(item['original_byte_length'], len(raw))
                self.assertEqual(item['retained_byte_length'], cap)
                self.assertTrue(item['truncated'])

    def exercise(self, *, real=False, write_failure=None, malformed=False):
        p = self.procedure()
        with tempfile.TemporaryDirectory(prefix='remote-failure-test-', dir=LAB) as temporary:
            root = Path(temporary)
            run, prep = root/'run', root/'prep'
            run.mkdir(); prep.mkdir()
            p.RUN = p.BOOT = run
            p.PREP, p.LAB, p.BOOT_NAME = prep, root, 'owned-run'
            p.CONTROL = 'native-exit-calibration'
            (prep/'source-stage.tar').write_bytes(b'offline')
            ssh = [sys.executable, '-c',
                   'import sys;print("original stdout");print("original stderr",file=sys.stderr);sys.exit(7)'] if real else ['never-ssh']
            record = dict(ssh_argv=ssh, guest_test_argv=['consumer'], evidence_name='evidence',
                          host_helpers_sha256={}, stage_archive_sha256=p.sha(prep/'source-stage.tar'),
                          current_overlay='/owned/new.qcow2', minimum_guest_free_bytes=1024**3,
                          known_binary_sha256={'/bin/wine':'a'*64})
            (prep/'preflight.json').write_text(json.dumps(record))
            (run/'launcher-identity.json').write_text(json.dumps(dict(
                qemu_pid=2, qemu_start_ticks=22, xvfb_pid=3, xvfb_start_ticks=33)))
            (root/'owned-run.wsl-session.json').write_text('{}')
            calls, snapshots, failures = [], [], []
            original_save = p.save
            def save(path, value):
                if path.name == 'execution.json':
                    snapshots.append(copy.deepcopy(value))
                    if value.get('remote_failures') and write_failure == 'serialization' and 'archive_skipped' not in value:
                        original_save(path, {'unserializable':object()})
                    if value.get('remote_failures') and (write_failure == 'always' or
                            write_failure == 'early' and 'archive_skipped' not in value):
                        raise OSError('secondary diagnostic write failure')
                original_save(path, value)
            def dispatch(argv, **kwargs):
                command = shlex.split(argv[-1]); calls.append(command)
                if real:
                    kwargs['timeout'] = min(kwargs['timeout'], 5)
                    try:
                        return REAL_RUN(argv, **kwargs)
                    except subprocess.CalledProcessError as error:
                        failures.append(error)
                        raise
                if command[0] == 'df':
                    return subprocess.CompletedProcess(argv, 0, 'offline df')
                if command[0] == 'python3':
                    return subprocess.CompletedProcess(argv, 0, str(2*1024**3))
                if command[0] == 'sha256sum':
                    return subprocess.CompletedProcess(argv, 0, 'a'*64+'  /bin/wine\n')
                self.assertEqual(command[:3], ['sudo', '-n', 'python3'])
                error = subprocess.CalledProcessError(1, argv,
                    object() if malformed else b'\xffstdout', b'\xfestderr')
                failures.append(error)
                raise error
            with mock.patch.object(p, 'save', side_effect=save), \
                 mock.patch.object(p.subprocess, 'run', side_effect=dispatch), \
                 mock.patch.object(g4_virgl_budget, 'check_budget', return_value={'passed':True}), \
                 mock.patch.object(stop_g4_virgl_launcher, 'validate_identity', return_value={'pid':1}), \
                 mock.patch.object(stop_g4_virgl_launcher, 'proc_identity', side_effect=lambda pid: {
                     'start_ticks':22 if pid == 2 else 33,
                     'cmdline':['qemu', 'file=/owned/new.qcow2,format=qcow2,if=virtio']}), \
                 mock.patch('builtins.print') as printed:
                with self.assertRaises(subprocess.CalledProcessError) as caught:
                    p.run()
            self.assertIs(caught.exception, failures[0])
            self.assertEqual(len(failures), 1)
            final = json.loads(printed.call_args.args[0])
            self.assertEqual(final['error'], repr(failures[0]))
            self.assertIn('archive_skipped', final)
            self.assertFalse(any(c[0] in ('mkdir', 'test', 'tar', 'consumer') for c in calls))
            self.assertEqual([c[0] for c in calls], ['df'] if real else ['df','python3','sha256sum','sudo'])
            return final, snapshots, failures[0]

    def test_actual_procedure_saves_before_finally_and_never_stages(self):
        final, snapshots, error = self.exercise()
        early = next(s for s in snapshots if s.get('remote_failures'))
        self.assertNotIn('archive_skipped', early)
        self.assertEqual(final['remote_failures'][0]['argv'], error.cmd)
        self.assertEqual(base64.b64decode(final['remote_failures'][0]['stderr']['base64']), b'\xfestderr')

    def test_one_real_bounded_local_python_failure(self):
        final, _, error = self.exercise(real=True)
        record = final['remote_failures'][0]
        self.assertEqual(record['returncode'], 7)
        self.assertEqual(record['argv'], error.cmd)
        self.assertEqual(record['stdout']['input_type'], 'text')
        self.assertEqual(base64.b64decode(record['stdout']['base64']), b'original stdout\n')
        self.assertEqual(base64.b64decode(record['stderr']['base64']), b'original stderr\n')

    def test_early_write_failure_retains_primary_and_secondary(self):
        final, _, _ = self.exercise(write_failure='early')
        self.assertIn('secondary diagnostic write failure', final['diagnostic_write_errors'][0])
        self.assertEqual(len(final['remote_failures']), 1)

    def test_all_writes_fail_primary_survives_in_final_print(self):
        final, _, _ = self.exercise(write_failure='always')
        self.assertEqual(len(final['diagnostic_write_errors']), 2)
        self.assertEqual(len(final['remote_failures']), 1)

    def test_payload_serialization_failure_does_not_mask_primary(self):
        final, _, _ = self.exercise(malformed=True)
        self.assertIn('Unsupported captured output type', final['diagnostic_write_errors'][0])
        self.assertFalse(final.get('remote_failures'))

    def test_actual_json_serialization_write_failure_preserves_primary(self):
        final, _, _ = self.exercise(write_failure='serialization')
        self.assertIn('not JSON serializable', final['diagnostic_write_errors'][0])
        self.assertEqual(len(final['remote_failures']), 1)


if __name__ == '__main__':
    unittest.main()
