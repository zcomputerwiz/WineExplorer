# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline R6 preparation contracts.

Self-contained (no dependency on private VM-adapter artifacts, so it never skips):
verifies the fresh r6 mode/paths are exact and do not collide with any historical
mode's stage/output/profile, that the full in-repo source inventory for the r6
readonly view + metadata probe is present, and that the native PE's pass-gate
check count stays consistent with the Python decoder. Live VM orchestration is
deferred to the post-Luna preparation (OFFLINE_CODE_READY_ARTIFACTS_PENDING).
"""
from pathlib import Path
import json
import unittest
from unittest import mock

import g4_userenv_view as view
import g4_userenv_r6_probe as probe6
import test_g4_chromium_native_exit as native

REPO=Path(__file__).resolve().parents[1]
SCRIPTS=REPO/'scripts'
HOST='chromium-registration-r6-20261026'

class R6PreparationTests(unittest.TestCase):
    def test_r6_mode_selects_exact_fresh_paths_with_no_historical_collision(self):
        r=native.r
        try:
            r.configure('userenv-r6')
            self.assertEqual(r.MODE,'userenv-r6')
            self.assertEqual(r.HOST,HOST)
            stage=r.LAB/(HOST+'-stage');output=r.LAB/HOST;profile=output/'profile'
            self.assertEqual((r.STAGE,r.OUTPUT,r.PROFILE),(stage,output,profile))
            # Profile is nested under the fresh OUTPUT, aligning with the native helper's WEX_EXIT_OUTPUT+"\profile".
            self.assertEqual(r.PROFILE,r.OUTPUT/'profile')
            prior=set()
            for mode in ('r1','module-warn-r2','userenv-r3','userenv-r4'):
                r.configure(mode);prior|={r.STAGE,r.OUTPUT,r.PROFILE}
            # No historical-path overwrite: r6 stage/output/profile are all fresh.
            self.assertFalse({stage,output,profile}&prior)
        finally:r.configure('r1')

    def test_r6_in_repo_source_inventory_is_complete(self):
        for name in ('check-g4-chromium-native-exit.py','g4_userenv_view.py',
                     'g4_userenv_probe.py','g4_userenv_probe.cpp','g4_userenv_r6_probe.py',
                     'g4_userenv_r6_probe.cpp','g4_native_exit.cpp','g4_native_exit_contract.h'):
            self.assertTrue((SCRIPTS/name).is_file(),name)

    def test_r6_view_stages_userenv_and_kernelbase_for_both_arches(self):
        with tempfile.TemporaryDirectory(prefix='r6-adapter-view-',dir=native.LAB) as folder:
            root=Path(folder);build=root/'build';stage=root/'stage';build.mkdir()
            with mock.patch.object(view,'BUILD',build),mock.patch.object(view,'STAGE',stage),mock.patch.object(view,'MODE','userenv-r6'):
                sel=view.selected()
                self.assertEqual(len(sel),4)
                self.assertEqual(sorted(Path(t).name for _,t,_ in sel),['kernelbase.dll','kernelbase.dll','userenv.dll','userenv.dll'])
                self.assertEqual(sorted(m for _,_,m in sel),[0x14c,0x14c,0x8664,0x8664])
            # Existing modes keep staging userenv only (defaults unchanged).
            with mock.patch.object(view,'BUILD',build),mock.patch.object(view,'STAGE',stage),mock.patch.object(view,'MODE','userenv-r4'):
                self.assertEqual(len(view.selected()),2)

    def test_r6_native_pe_checkcount_matches_python_decoder(self):
        cpp=(SCRIPTS/'g4_userenv_r6_probe.cpp').read_text()
        self.assertIn('checks==21',cpp) # native PE passes iff exactly 21 checks, 0 failures
        self.assertEqual(probe6.CHECKS,21)
        hdr=(SCRIPTS/'g4_native_exit_contract.h').read_text()
        self.assertIn('#ifndef WEX_EXIT_OUTPUT',hdr) # output-only override hook reused by the R4/R5 recipe

    def test_r6_probe_lifecycle_sequence_and_ownership_invariants(self):
        nonce='0'*32
        def _hr(c):
            v=(0x80070000|c)&0xFFFFFFFF
            return v-0x100000000 if v>=0x80000000 else v
        ABSENT=_hr(0x2);EXISTS=_hr(0xb7)
        def base():  # canonical success record (v=2), matching the native PE's output contract
            return dict(v=2,nonce=nonce,pid=44,userenv_path=r'C:\WINDOWS\system32\userenv.dll',
                        kernelbase_path=r'C:\WINDOWS\system32\kernelbase.dll',paths_normal=True,exports_found=True,
                        checks=21,failures=0,derive_hr=0,initial_lookup_hr=ABSENT,register_attempted=True,
                        register_hr=0,owned=True,post_register_lookup_hr=0,duplicate_register_hr=EXISTS,
                        verify_lookup_hr=0,unregister_attempted=True,unregister_hr=0,final_lookup_hr=ABSENT,
                        cleanup_attempted=False,cleanup_succeeded=False,passed=True)
        # Canonical lifecycle -> accepted (proves the decoder is not over-rejecting).
        self.assertTrue(probe6.decode(json.dumps(base()).encode(),nonce,0)['passed'])

        # Sequence scenarios: each models a genuine failure record; we assert its own ownership/mutation
        # facts and that the decoder gates it (rejects), so no browser launch follows.
        col=base();col.update(initial_lookup_hr=0,register_attempted=False,owned=False,unregister_attempted=False,cleanup_attempted=False,failures=1,checks=14,passed=False)
        self.assertFalse(col['register_attempted']);self.assertFalse(col['unregister_attempted'])  # no mutation, no unregister
        with self.assertRaises(ValueError):probe6.decode(json.dumps(col).encode(),nonce,0)

        fr=base();fr.update(register_hr=EXISTS,owned=False,unregister_attempted=False,cleanup_attempted=False,failures=1,checks=15,passed=False)
        self.assertFalse(fr['owned']);self.assertFalse(fr['unregister_attempted'])  # failed fresh registration -> nothing owned -> no unregister
        with self.assertRaises(ValueError):probe6.decode(json.dumps(fr).encode(),nonce,0)

        pl=base();pl.update(post_register_lookup_hr=EXISTS,unregister_attempted=False,cleanup_attempted=True,cleanup_succeeded=True,failures=1,checks=16,passed=False)
        self.assertTrue(pl['owned']);self.assertTrue(pl['cleanup_attempted'])  # post-register failure -> owned cleanup runs
        with self.assertRaises(ValueError):probe6.decode(json.dumps(pl).encode(),nonce,0)

        cf=base();cf.update(unregister_hr=EXISTS,cleanup_attempted=True,cleanup_succeeded=False,failures=1,checks=20,passed=False)
        self.assertTrue(cf['cleanup_attempted']);self.assertFalse(cf['cleanup_succeeded'])  # cleanup failure -> fail (nonzero exit upstream)
        with self.assertRaises(ValueError):probe6.decode(json.dumps(cf).encode(),nonce,0)

        # Invariant negatives: each keeps passed=True so it clears the pass-gate and is rejected by a
        # specific safety invariant, isolating that invariant's enforcement in the decoder.
        def reject(**mut):
            v=base();v.update(mut)
            with self.assertRaises(ValueError):probe6.decode(json.dumps(v).encode(),nonce,0)
        reject(owned=True,register_hr=EXISTS)                                    # owned without a fresh S_OK registration
        reject(unregister_attempted=True,owned=False,register_attempted=False)   # released without owning
        reject(cleanup_attempted=True,owned=False,unregister_attempted=False)    # finally-cleanup without ownership
        reject(initial_lookup_hr=0,register_attempted=True)                      # mutated despite a non-absent identity

import tempfile

if __name__=='__main__':unittest.main()
