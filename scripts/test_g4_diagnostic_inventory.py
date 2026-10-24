# SPDX-License-Identifier: GPL-2.0-or-later
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import g4_diagnostic_inventory as inventory
import g4_capacity
import test_g4_native_loader_r4 as r4
import test_g4_native_calibration_refresh as fixtures

LAB=fixtures.owners.LAB


@unittest.skipUnless(LAB.is_dir(),'private D: storage unavailable')
class DiagnosticInventoryTests(unittest.TestCase):
    def test_actual_hash_and_all_historical_differences_are_warnings(self):
        with tempfile.TemporaryDirectory(prefix='inventory-',dir=LAB) as temp:
            path=Path(temp)/'payload';path.write_bytes(b'actual changed bytes')
            old=dict(path='/previous/path',device=999,inode=999,bytes=0,mtime_ns=0,ctime_ns=0,sha256='0'*64)
            row=inventory.observe_file(path,old)
            self.assertTrue(row['observation_complete']);self.assertFalse(row['previous_version_tested'])
            self.assertEqual(row['actual']['sha256'],hashlib.sha256(b'actual changed bytes').hexdigest())
            self.assertEqual({w['field'] for w in row['warnings']},set(old))
            self.assertEqual(row['previous'],old)
            self.assertTrue(inventory.observe_file(path,row['actual'])['previous_version_tested'])
            inventory.require_regular_canonical(path)

    def test_missing_unreadable_and_deadline_never_invent_actual_hash(self):
        with tempfile.TemporaryDirectory(prefix='inventory-errors-',dir=LAB) as temp:
            path=Path(temp)/'payload';missing=inventory.observe_file(path,{'sha256':'0'*64})
            self.assertIsNone(missing['actual']);self.assertIsNone(missing['previous_version_tested'])
            path.write_bytes(b'original')
            with mock.patch.object(Path,'open',side_effect=PermissionError(13,'denied')):
                row=inventory.observe_file(path)
            self.assertIsNone(row['actual']['sha256']);self.assertEqual(row['observation_errors'][0]['error_type'],'PermissionError')
            with mock.patch.object(Path,'open',side_effect=OSError(5,'read error')):
                row=inventory.observe_file(path)
            self.assertEqual(row['observation_errors'][0]['errno'],5)
            clock=iter([0,0,2,2])
            row=inventory.observe_file(path,maximum_seconds=1,now=lambda:next(clock))
            self.assertFalse(row['observation_complete']);self.assertIsNone(row['actual']['sha256'])
            json.dumps(row,allow_nan=False)
            expired=inventory.observe_file(path,deadline=0,now=lambda:0)
            self.assertIsNone(expired['actual']);self.assertEqual(expired['observation_errors'][0]['error_type'],'TimeoutError')

    def test_symlink_operational_refusal_and_remote_comparison_no_old_version_claim(self):
        with tempfile.TemporaryDirectory(prefix='inventory-path-',dir=LAB) as temp:
            root=Path(temp);target=root/'target';target.write_bytes(b'actual');alias=root/'alias';alias.symlink_to(target)
            with self.assertRaises(ValueError):inventory.require_regular_canonical(alias)
        row=inventory.compare_actual({'sha256':'a'*64},{'sha256':'b'*64})
        self.assertFalse(row['previous_version_tested']);self.assertEqual(row['warnings'][0]['code'],'historical_difference')
        self.assertIsNone(inventory.compare_actual({'sha256':None},{'sha256':'b'*64})['previous_version_tested'])

    def test_actual_private_storage_dev_hash_warning_wrong_disk_backing_and_owner_fatal(self):
        p=r4.procedure()
        with tempfile.TemporaryDirectory(prefix='storage-inventory-',dir=LAB) as temp:
            root=Path(temp);p.LAB=root;system=root/'system';base=root/'base'
            paths=[root/'system-native-identity-r4-20261024.qcow2',root/'system-capacity-r1-20261023.qcow2',system,base]
            for path in paths:path.write_bytes(b'plain fixture bytes, not images')
            previous=[dict(g4_capacity.file_id(v),device=999,inode=999,mtime_ns=0) for v in paths[1:]]
            record=dict(current_overlay=str(paths[0]),storage=dict(future_expected_rows=[{'filename':str(v)} for v in paths],identities=previous,
                parent_full_hash={'path':str(paths[1]),'sha256':'0'*64}))
            rows=[dict(filename=str(v),format='qcow2',**{'full-backing-filename':str(paths[i+1]) if i<3 else None,'dirty-flag':False,
                'format-specific':{'data':{'corrupt':False}}}) for i,v in enumerate(paths)]
            with mock.patch.object(g4_capacity,'SYSTEM',system),mock.patch.object(g4_capacity,'BASE',base), \
                 mock.patch.object(g4_capacity,'owners',return_value={'idle':True}), \
                 mock.patch.object(p.subprocess,'run',return_value=mock.Mock(stdout=json.dumps(rows).encode())) as calls:
                observed=p.diagnostic_storage(record)
                self.assertTrue(observed['files'][str(paths[1])]['warnings']);self.assertFalse(observed['files'][str(paths[1])]['previous_version_tested'])
                record['current_overlay']='/wrong/disk'
                with self.assertRaisesRegex(RuntimeError,'Wrong selected writable disk'):p.diagnostic_storage(record)
                self.assertEqual(calls.call_count,1);record['current_overlay']=str(paths[0])
                rows[0]['full-backing-filename']='/wrong/backing';calls.return_value.stdout=json.dumps(rows).encode()
                with self.assertRaisesRegex(RuntimeError,'disk/backing'):p.diagnostic_storage(record)
                with mock.patch.object(g4_capacity,'owners',side_effect=ValueError('Active owner')):
                    with self.assertRaisesRegex(ValueError,'Active owner'):p.diagnostic_storage(record)

    def test_current_private_hash_differences_continue_but_archive_and_paths_do_not(self):
        with mock.patch.object(fixtures,'procedure',side_effect=r4.procedure):
            for problem in ('helper','binary','loaderhash','input','approval'):
                info,_,_,error,_,_=fixtures.RefreshTests.exercise(self,problem=problem)
                self.assertIsNone(error);self.assertEqual(info['consumer_ssh_exit'],0)
                self.assertTrue(info.get('diagnostic_inventory') or info.get('environment_inventory'))
            for problem in ('archive','path','loaderpin','overlay','identity','exists'):
                _,calls,_,error,_,_=fixtures.RefreshTests.exercise(self,problem=problem)
                self.assertIsNotNone(error);self.assertFalse(any(c[0] in ('mkdir','tar') for c in calls))
            info,_,_,error,_,_=fixtures.RefreshTests.exercise(self,problem='hash-unavailable')
            self.assertIsNone(error);self.assertEqual(info['consumer_ssh_exit'],0)
            self.assertEqual(info['remote_file_inventory_observation']['returncode'],1)
            self.assertTrue(all(v['actual']['sha256'] is None and v['previous_version_tested'] is None for v in info['environment_inventory'].values()))
            info,_,_,error,_,_=fixtures.RefreshTests.exercise(self,problem='transfer')
            self.assertIn('Deployed source hash mismatch',str(error));self.assertIn('archive_skipped',info)
            self.assertNotIn('consumer_ssh_exit',info)

    def test_retired_cli_cannot_select_consumed_historical_preparation(self):
        p=r4.procedure()
        with self.assertRaisesRegex(RuntimeError,'preparation dispatcher retired'):p.main('prepare')
        p.NATIVE_R4=False
        with self.assertRaisesRegex(RuntimeError,'legacy controls retired'):p.main('run')
        with tempfile.TemporaryDirectory(prefix='overwrite-inventory-',dir=LAB) as temp:
            root=Path(temp);p.PREP=root/'prep';p.PREP.mkdir();p.RUN=p.BOOT=root/'run';p.RUN.mkdir()
            (p.PREP/'preflight.json').write_text(json.dumps({'ssh_argv':[],'guest_test_argv':[]}))
            evidence=p.RUN/'preflight.json';evidence.write_bytes(b'preserve old evidence')
            with self.assertRaisesRegex(RuntimeError,'existing run evidence'):p.run()
            self.assertEqual(evidence.read_bytes(),b'preserve old evidence')


if __name__=='__main__':unittest.main()
