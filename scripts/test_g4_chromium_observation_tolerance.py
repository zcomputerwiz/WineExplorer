# SPDX-License-Identifier: GPL-2.0-or-later
"""Focused offline tests for Chromium periodic identity observation tolerance in
check-g4-native-exit-calibration.collect(). Drives the real collect() with a tolerant
Chromium contract and a mocked child/linux_identity; no Wine/VM/browser execution.

Proves: (1) a transient observation timeout / read-unavailability AFTER the owned Popen
identity + native held-handle binding are established is recorded as partial evidence and
lets the native journal complete instead of aborting; (2) concrete PID reuse after binding
still raises and prevents any unsafe signal/adoption; (3) the real whole-operation deadline
still ends and cleans the owned run; (4) the default (non-opt-in) contract stays strict.
Run from scripts/: python3 -B -m unittest test_g4_chromium_observation_tolerance."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock,patch

spec=importlib.util.spec_from_file_location('exit_cal',Path(__file__).with_name('check-g4-native-exit-calibration.py'))
cal=importlib.util.module_from_spec(spec);spec.loader.exec_module(cal)


class ToleranceTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='chromium-obs-tol-')
        self.addCleanup(self.temp.cleanup)
        self.output=Path(self.temp.name)/'out';self.output.mkdir()
        (self.output/'native-exit.jsonl').write_bytes(b'{"v":1,"event":"bound"}\n') # binding present from first poll
        self.clock=0.0;self.raised=None
        self.child=Mock(pid=1234);self.popen=Mock(return_value=self.child)

    def _contract(self,tolerant):
        contract=dict(operation_seconds=82,nonce='a'*32,image='Z:\\chrome.exe',
            command=lambda out,n:['wine','native-exit.exe','--nonce',n],binding=lambda v,n,i:v,parse=lambda raw,n,i:{'outcome':'native_exit'})
        if tolerant:contract['tolerant_periodic_identity']=True
        return contract

    def _drive(self,tolerant,identity,poll,stat_ticks=(99,99),wait_result=0,sleep_step=1.0):
        # identity: list of per-call results/exceptions, or a single value returned every call.
        if isinstance(identity,list):identity_side=list(identity)
        else:identity_side=lambda *a,**k:(identity(*a,**k) if callable(identity) else identity)
        if poll=='never':self.child.poll.side_effect=None;self.child.poll.return_value=None
        else:self.child.poll.side_effect=list(poll);self.child.poll.return_value=None
        self.child.wait.return_value=wait_result
        result={}
        with patch.object(cal,'stat_record',side_effect=[{'start_ticks':t} for t in stat_ticks]), \
             patch.object(cal,'read_owned_stat',return_value=b'synthetic'), \
             patch.object(cal,'linux_identity',side_effect=identity_side):
            def cleanup(reason):result.setdefault('cleanup_started',{'reason':reason})
            try:
                cal.collect(self.output,{},result,1000,cleanup,lambda:None,now=lambda:self.clock,
                    sleep=lambda s:setattr(self,'clock',self.clock+sleep_step),popen=self.popen,contract=self._contract(tolerant))
                self.raised=None
            except Exception as error:self.raised=error
        return result

    def test_transient_observation_deadline_after_binding_is_tolerated_and_journal_completes(self):
        record={'linux_pid':1234,'start_ticks':99}
        transient=cal.LinuxIdentityError('Linux identity observation deadline',{'rejection_reason':'Linux identity observation deadline'})
        result=self._drive(True,[record,transient],[None,None,0])
        self.assertIsNone(self.raised)
        self.assertTrue(result['observation_complete'])
        self.assertEqual(result['native']['outcome'],'native_exit')
        self.assertNotIn('error',result);self.assertNotIn('linux_helper_rejected_identity',result)
        warn=result['linux_helper_observation_warnings']
        self.assertEqual(warn['count'],1);self.assertIn('Linux identity observation deadline',warn['reasons'])
        self.assertTrue(result['helper_reaped']);self.child.kill.assert_not_called()

    def test_transient_read_query_failure_after_binding_is_tolerated(self):
        record={'linux_pid':1234,'start_ticks':99}
        transient=cal.LinuxIdentityError('Linux identity read/query failed: OSError',{'rejection_reason':'Linux identity read/query failed: OSError'})
        result=self._drive(True,[record,transient],[None,None,0])
        self.assertIsNone(self.raised)
        self.assertTrue(result['observation_complete'])
        self.assertEqual(result['linux_helper_observation_warnings']['reasons'].get('Linux identity read/query failed: OSError'),1)
        self.assertNotIn('error',result)

    def test_concrete_pid_reuse_after_binding_still_aborts_and_never_signals(self):
        record={'linux_pid':1234,'start_ticks':99}
        reuse=cal.LinuxIdentityError('Linux PID reused',{'rejection_reason':'Linux PID reused'})
        result=self._drive(True,[record,reuse],[None,None,None],stat_ticks=(99,100))
        self.assertIsInstance(self.raised,cal.LinuxIdentityError)
        self.assertNotIn('linux_helper_observation_warnings',result)
        self.assertIn('linux_helper_rejected_identity',result)
        self.assertIn('no signal',str(result.get('cleanup_errors')))
        self.child.kill.assert_not_called()

    def test_whole_operation_deadline_still_ends_and_cleans_owned_run(self):
        record={'linux_pid':1234,'start_ticks':99}
        result=self._drive(True,[record]*30,'never',stat_ticks=(99,99),wait_result=-9,sleep_step=10.0)
        self.assertIsInstance(self.raised,TimeoutError)
        self.assertIn('operation timeout',str(self.raised))
        self.assertTrue(result['observer_killed']);self.assertTrue(result['helper_reaped'])
        self.assertEqual(result['cleanup_started']['reason'],'owned_helper_abort')
        self.child.kill.assert_called_once()

    def test_strict_contract_still_aborts_on_transient_after_binding(self):
        record={'linux_pid':1234,'start_ticks':99}
        transient=cal.LinuxIdentityError('Linux identity observation deadline',{'rejection_reason':'Linux identity observation deadline'})
        result=self._drive(False,[record,transient],[None,None,None],stat_ticks=(99,99))
        self.assertIsInstance(self.raised,cal.LinuxIdentityError)
        self.assertNotIn('linux_helper_observation_warnings',result)
        self.assertIn('linux_helper_rejected_identity',result)

    def test_transient_classifier_whitelist_is_strict_on_unknown_reasons(self):
        self.assertTrue(cal.transient_identity_reason(ValueError('Linux identity observation deadline')))
        self.assertTrue(cal.transient_identity_reason(ValueError('Linux identity read/query failed: OSError')))
        for reason in ('Linux PID reused','Linux PID reused after observation','Linux prefix mismatch',
            'Linux image/app URL/profile/argv mismatch','Oversized or EOF-unproven proc field: /proc/1/cmdline',
            'Linux identity FD cleanup failed'):
            self.assertFalse(cal.transient_identity_reason(ValueError(reason)),reason)


if __name__=='__main__':unittest.main()
