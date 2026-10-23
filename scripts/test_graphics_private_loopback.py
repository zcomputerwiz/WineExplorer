# SPDX-License-Identifier: GPL-2.0-or-later
"""Only owned private namespaces: no VM, browser, parent lo or real NIC changes."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

spec=importlib.util.spec_from_file_location('isolated',Path(__file__).with_name('run-isolated-graphics.py'))
iso=importlib.util.module_from_spec(spec);spec.loader.exec_module(iso)


def state(up=False):
    return {'namespace':{'inode':2},'addresses':[{'ifname':'lo','link_type':'loopback',
            'flags':['LOOPBACK']+(['UP'] if up else []),
            'addr_info':[{'local':'127.0.0.1','prefixlen':8}] if up else []}],
            'routes4':[{'dev':'lo','dst':'127.0.0.0/8'}] if up else [],'routes6':[]}


class LoopbackTests(unittest.TestCase):
    def test_refuse_original_namespace_before_any_command(self):
        with patch.object(iso,'net_identity',return_value={'inode':1}),patch.object(iso.subprocess,'run') as run:
            with self.assertRaises(RuntimeError):iso.enable_private_loopback(3)
            run.assert_not_called()

    def test_only_loopback_command_after_private_proof(self):
        with patch.object(iso,'net_identity',side_effect=[{'inode':1},{'inode':2}]), \
             patch.object(iso,'network_state',side_effect=[state(),state(True)]),patch.object(iso.subprocess,'run') as run:
            record=iso.enable_private_loopback(3)
            self.assertEqual(record['parent_namespace'],{'inode':1})
            run.assert_called_once_with(['ip','link','set','dev','lo','up'],check=True,timeout=1)

    def test_foreign_interfaces_addresses_and_routes_fail(self):
        for field,value in [('addresses',[{'ifname':'eth0','link_type':'ether'}]),
                            ('routes4',[{'dev':'eth0','dst':'default','gateway':'10.0.0.1'}]),
                            ('routes6',[{'dev':'lo','dst':'::/0'}])]:
            bad=state(True);bad[field]=value
            with self.subTest(field=field),self.assertRaises((RuntimeError,ValueError)):iso.require_loopback_only(bad,True)
        bad=state(True);bad['addresses'][0]['addr_info'].append({'local':'10.0.0.1','prefixlen':24})
        with self.assertRaises(RuntimeError):iso.require_loopback_only(bad,True)

    def test_dirty_initial_namespace_never_modifies_it(self):
        with patch.object(iso,'net_identity',side_effect=[{'inode':1},{'inode':2}]), \
             patch.object(iso,'network_state',return_value=state(True)),patch.object(iso.subprocess,'run') as run:
            with self.assertRaises(RuntimeError):iso.enable_private_loopback(3)
            run.assert_not_called()

    def test_missing_or_same_namespace_proof_refuses_runner(self):
        with patch.dict(iso.os.environ,{},clear=True):
            with self.assertRaises(RuntimeError):iso.verify_private_loopback()
        with patch.dict(iso.os.environ,{'WEX_G4_LOOPBACK_PARENT_FD':'3'}),patch.object(iso,'net_identity',return_value={'inode':1}):
            with self.assertRaises(RuntimeError):iso.verify_private_loopback()

    def test_ip_queries_bounded_readonly(self):
        with patch.object(iso,'net_identity',return_value={'inode':2}), \
             patch.object(iso.subprocess,'run',return_value=Mock(stdout='[]')) as run:
            iso.network_state()
            self.assertEqual(run.call_count,3)
            self.assertTrue(all(c.kwargs['timeout']==1 for c in run.call_args_list))
            self.assertTrue(all('set' not in c.args[0] for c in run.call_args_list))

    def test_ip_setup_timeout_is_failure(self):
        with patch.object(iso,'net_identity',side_effect=[{'inode':1},{'inode':2}]), \
             patch.object(iso,'network_state',return_value=state()), \
             patch.object(iso.subprocess,'run',side_effect=subprocess.TimeoutExpired('ip',1)):
            with self.assertRaises(subprocess.TimeoutExpired):iso.enable_private_loopback(3)

    def wrapper(self,enabled):
        with tempfile.TemporaryDirectory() as directory:
            argv=['run-isolated-graphics.py','--socket-root',directory]+(['--private-loopback'] if enabled else [])+['--','true']
            child=Mock();child.wait.return_value=0
            with patch.object(sys,'argv',argv),patch.object(iso.subprocess,'Popen',return_value=child) as popen:
                self.assertEqual(iso.main(),0)
                return popen.call_args

    def test_normal_wrapper_unchanged_no_descriptor_or_loopback(self):
        call=self.wrapper(False)
        self.assertNotIn('pass_fds',call.kwargs);self.assertIn('--child',call.args[0]);self.assertNotIn('--child-loopback',call.args[0])

    def test_optin_passes_one_original_namespace_descriptor(self):
        call=self.wrapper(True)
        self.assertEqual(len(call.kwargs['pass_fds']),1);self.assertIn('--child-loopback',call.args[0])
        self.assertEqual(call.args[0][:5],['unshare','--mount','--net','--propagation','private'])

    def test_actual_bounded_private_loopback_and_default_down(self):
        capability=subprocess.run(['unshare','--net','true'],capture_output=True,text=True,timeout=5)
        if capability.returncode:
            self.skipTest('Actual unshare unavailable: '+capability.stderr.strip())
        parent=iso.net_identity()
        module=str(Path(iso.__file__).resolve())
        prefix=('import importlib.util,json,socket; s=importlib.util.spec_from_file_location("i",'+repr(module)+'); '
                'i=importlib.util.module_from_spec(s);s.loader.exec_module(i); ')
        with tempfile.TemporaryDirectory() as directory:
            for up in (False,True):
                code=prefix+('v=i.verify_private_loopback(); '
                    'sock=socket.socket();sock.settimeout(1);sock.bind(("127.0.0.1",0));sock.listen(1);'
                    'client=socket.create_connection(sock.getsockname(),timeout=1);peer,_=sock.accept();'
                    'peer.sendall(b"private");assert client.recv(7)==b"private";'
                    'peer.close();client.close();sock.close();print(json.dumps(v))'
                    if up else 'v=i.network_state();i.require_loopback_only(v,False);print(json.dumps(v))')
                command=[sys.executable,'-B',str(Path(iso.__file__)),'--socket-root',directory]
                if up:command+=['--private-loopback']
                command+=['--']
                if up and os.getuid()==0:command+=['runuser','-u','nobody','--']
                command+=[sys.executable,'-B','-c',code]
                result=subprocess.run(command,capture_output=True,text=True,timeout=15)
                self.assertEqual(result.returncode,0,result.stderr)
                observed=json.loads(result.stdout)
                current=observed['observed']['namespace'] if up else observed['namespace']
                self.assertNotEqual(current,parent)
                if up:self.assertEqual(observed['parent_namespace'],parent)
        self.assertEqual(iso.net_identity(),parent)


if __name__=='__main__':unittest.main()
