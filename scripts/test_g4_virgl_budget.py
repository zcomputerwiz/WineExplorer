# SPDX-License-Identifier: GPL-2.0-or-later
from datetime import datetime, timedelta, timezone
import importlib.util
import io
import json
from pathlib import Path
import shlex
import subprocess
import tarfile
import tempfile
import unittest
from unittest import mock

import g4_virgl_budget
from g4_virgl_budget import check_budget
import stop_g4_virgl_launcher


class BudgetTests(unittest.TestCase):
    start = '2026-10-22T19:38:50.6690000Z'
    now = '2026-10-22T19:39:42.747Z'

    def assess(self, required=650, now=None, **changes):
        session = dict(runName='owned-run', state='running', windowsPid=123,
                       windowsStartTimeUtc=self.start)
        session.update(changes)
        return check_budget(json.dumps(session), 'owned-run', required,
                            now=self.now if now is None else now)

    def test_actual_raw_windows_timestamp_not_powershell_local_string(self):
        result = self.assess()
        self.assertTrue(result['passed'])
        self.assertEqual(result['elapsed_seconds'], 52.078)
        self.assertEqual(result['remaining_seconds'], 847.922)
        self.assertNotEqual(result['remaining_seconds'], 18847.922)
        self.assertEqual(result['controller_start_utc'], '2026-10-22T19:38:50.669000+00:00')
        self.assertEqual(result['required_seconds'], 650)
        self.assertFalse(result['ownership_proven'])

    def test_timezone_equivalent_aware_inputs(self):
        for start in ('2026-10-22T14:38:50.669-05:00', '2026-10-23T01:08:50.669+05:30'):
            for now in ('2026-10-22T14:39:42.747-05:00', datetime(2026,10,22,19,39,42,747000,tzinfo=timezone.utc)):
                with self.subTest(start=start, now=now):
                    result = self.assess(now=now, windowsStartTimeUtc=start)
                    self.assertTrue(result['passed'])
                    self.assertEqual(result['remaining_seconds'], 847.922)

    def test_both_guard_boundaries_and_zero_elapsed(self):
        start = datetime.fromisoformat(self.start.replace('Z', '+00:00'))
        for required in (650, 435):
            at = start + timedelta(seconds=900-required)
            self.assertTrue(self.assess(required, at)['passed'])
            self.assertFalse(self.assess(required, at+timedelta(microseconds=1))['passed'])
        self.assertTrue(self.assess(900, start)['passed'])
        self.assertFalse(self.assess(now=start+timedelta(seconds=901))['passed'])

    def test_malformed_missing_naive_future_and_wrong_session(self):
        for stamp in (None, 5, '', '2026-10-22T19:38:50', '10/22/2026 14:38:50',
                      '2026-02-30T19:38:50Z', '2026-10-22T19:39:43Z'):
            with self.subTest(stamp=stamp):
                self.assertFalse(self.assess(windowsStartTimeUtc=stamp)['passed'])
        for change in ({'runName':'other'}, {'state':'exited'}, {'state':None},
                       {'windowsPid':True}, {'windowsPid':0}):
            self.assertFalse(self.assess(**change)['passed'])
        for now in ('2026-10-22T19:39:42', datetime(2026,10,22), 'bad'):
            self.assertFalse(self.assess(now=now)['passed'])

    def test_invalid_budgets_and_raw_shapes_fail_closed(self):
        for required in (None, True, '650', 0, -1, 901, float('nan'), float('inf')):
            with self.subTest(required=required):
                self.assertFalse(self.assess(required)['passed'])
        for raw in ('{', '[]', 'null', '{}', {'runName':'owned-run'}, b'\xff',
                    '{"runName":"owned-run","runName":"other"}', '{"value":NaN}'):
            self.assertFalse(check_budget(raw, 'owned-run', 650, now=self.now)['passed'])
        self.assertFalse(check_budget('{}', '../wrong', 650, now=self.now)['passed'])

    def test_raw_utf8_bom_is_supported(self):
        raw = json.dumps(dict(runName='owned-run', state='running', windowsPid=123,
                              windowsStartTimeUtc=self.start))
        self.assertTrue(check_budget(b'\xef\xbb\xbf'+raw.encode(), 'owned-run', 650, now=self.now)['passed'])


PRIVATE = Path(__file__).resolve().parents[1]/'build/g4-virgl-20261022/presentation-timing-trial.py'


@unittest.skipUnless(PRIVATE.is_file(), 'Private lab procedure unavailable; pure budget tests remain portable')
class ProcedureTests(unittest.TestCase):
    """Execute the actual private procedure with all guest/process calls mocked."""
    def exercise(self, seconds, *, state='running', missing=False, identity_failure=False):
        spec = importlib.util.spec_from_file_location('budget_private_procedure', PRIVATE)
        procedure = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(procedure)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            run, prep = root/'owned-run', root/'prep'
            run.mkdir(); prep.mkdir()
            procedure.LAB, procedure.RUN, procedure.BOOT, procedure.PREP = root, run, run, prep
            procedure.BOOT_NAME = 'owned-run'
            procedure.STAGE, procedure.OUTPUT = '/guest/stage', '/guest/output'
            record = dict(ssh_argv=['ssh'], guest_test_argv=['consumer'], evidence_name='evidence',
                          known_binary_sha256={'/bin/wine':'a'*64}, source_manifest={'script.py':{'sha256':'b'*64}})
            (prep/'preflight.json').write_text(json.dumps(record))
            (prep/'source-stage.tar').write_bytes(b'offline staged archive')
            identity = {'qemu_pid':2,'qemu_start_ticks':22,'xvfb_pid':3,'xvfb_start_ticks':33}
            (run/'launcher-identity.json').write_text(json.dumps(identity))
            if not missing:
                (root/'owned-run.wsl-session.json').write_text(json.dumps(dict(
                    runName='owned-run', state=state, windowsPid=123,
                    windowsStartTimeUtc=BudgetTests.start)))
            start = datetime.fromisoformat(BudgetTests.start.replace('Z', '+00:00'))
            clocks, checks, calls = iter(seconds), [], []
            actual_check = g4_virgl_budget.check_budget
            def check(raw, expected, required):
                checks.append(required)
                return actual_check(raw, expected, required, now=start+timedelta(seconds=next(clocks)))
            def remote(argv, **kwargs):
                self.assertEqual(argv[0], 'ssh')
                command = shlex.split(argv[1]); calls.append(command)
                if command[0] == 'sha256sum':
                    hashes = {'/bin/wine':'a'*64, '/guest/stage/script.py':'b'*64}
                    return subprocess.CompletedProcess(argv, 0, ''.join(hashes[p]+'  '+p+'\n' for p in command[2:]))
                if command[:2] == ['tar','-C']:
                    with tarfile.open(fileobj=kwargs['stdout'], mode='w:gz') as archive:
                        data=b'{}'; item=tarfile.TarInfo('results.json'); item.size=len(data)
                        archive.addfile(item, io.BytesIO(data))
                return subprocess.CompletedProcess(argv, 0, 'offline disk space')
            caught = None
            with mock.patch.object(g4_virgl_budget, 'check_budget', side_effect=check), \
                 mock.patch.object(stop_g4_virgl_launcher, 'validate_identity',
                                   side_effect=RuntimeError('identity failure') if identity_failure else None,
                                   return_value={'pid':1}) as ownership, \
                 mock.patch.object(stop_g4_virgl_launcher, 'proc_identity',
                                   side_effect=lambda pid: {'start_ticks':22 if pid==2 else 33}), \
                 mock.patch.object(procedure.subprocess, 'run', side_effect=remote), mock.patch('builtins.print'):
                try:
                    procedure.run()
                except RuntimeError as error:
                    caught = str(error)
            info = json.loads((run/'execution.json').read_text())
            logs = {p.name:json.loads(p.read_text()) for p in run.glob('budget-*.json')}
            self.assertTrue(ownership.called, 'UTC budget cannot replace Linux ownership validation')
            return calls, checks, info, logs, caught

    def test_first_guard_failure_blocks_all_guest_and_archive_calls(self):
        calls, checks, info, logs, error = self.exercise([251])
        self.assertEqual(calls, [])
        self.assertEqual(checks, [650])
        self.assertIn('before-guest-preflight', error)
        self.assertFalse(logs['budget-before-guest-preflight.json']['passed'])
        self.assertIn('archive_skipped', info)

    def test_second_guard_failure_blocks_consumer_and_archive(self):
        calls, checks, info, logs, error = self.exercise([52.078, 466])
        self.assertEqual(checks, [650,435])
        self.assertEqual([c[0] for c in calls], ['df','sha256sum','test','mkdir','tar','sha256sum'])
        self.assertIn('before-consumer', error)
        self.assertFalse(logs['budget-before-consumer.json']['passed'])
        self.assertIn('archive_skipped', info)

    def test_success_uses_both_guards_then_consumer_then_archive(self):
        calls, checks, info, logs, error = self.exercise([52.078, 465])
        self.assertIsNone(error)
        self.assertEqual(checks, [650,435])
        self.assertEqual(calls[-2][0], 'consumer')
        self.assertEqual(calls[-1][:2], ['tar','-C'])
        self.assertEqual(logs['budget-before-guest-preflight.json']['remaining_seconds'], 847.922)
        self.assertEqual(logs['budget-before-consumer.json']['remaining_seconds'], 435)
        self.assertEqual(info['consumer_ssh_exit'], 0)
        self.assertEqual(info['archive_exit'], 0)

    def test_missing_or_stale_session_and_bad_identity_never_reach_guest(self):
        for kwargs in ({'missing':True}, {'state':'exited'}, {'identity_failure':True}):
            with self.subTest(kwargs=kwargs):
                calls, _, info, _, error = self.exercise([52.078], **kwargs)
                self.assertEqual(calls, [])
                self.assertIsNotNone(error)
                self.assertIn('archive_skipped', info)


if __name__ == '__main__':
    unittest.main()
