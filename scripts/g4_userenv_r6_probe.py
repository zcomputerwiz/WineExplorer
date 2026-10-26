# SPDX-License-Identifier: GPL-2.0-or-later
"""Bounded R6 registration metadata sanity probe phase.

Reuses the reviewed 45s API-probe/reap/intermediate-cleanup timing from
g4_userenv_probe.phase verbatim (via its decoder hook) so the timer arithmetic
fix is preserved unchanged; only the decoded evidence schema differs, because
the R6 native PE resolves four kernelbase registration exports plus userenv SID
derivation and drives a full register/lookup/duplicate/unregister lifecycle.
"""
import json
import subprocess
import time

import g4_userenv_probe as _base

CHECKS=21
USERENV=r'c:\windows\system32\userenv.dll'
KERNELBASE=r'c:\windows\system32\kernelbase.dll'

def _hr(code):  # HRESULT_FROM_WIN32 as a signed int32, matching the native PE's %ld fields
    v=(0x80070000|code)&0xFFFFFFFF
    return v-0x100000000 if v>=0x80000000 else v

ABSENT=_hr(0x2)   # ERROR_FILE_NOT_FOUND: the identity is not registered
EXISTS=_hr(0xb7)  # ERROR_ALREADY_EXISTS: a duplicate registration was rejected

_HR_FIELDS=('derive_hr','initial_lookup_hr','register_hr','post_register_lookup_hr',
            'duplicate_register_hr','verify_lookup_hr','unregister_hr','final_lookup_hr')
_FLAG_FIELDS=('register_attempted','owned','unregister_attempted','cleanup_attempted','cleanup_succeeded')

def decode(raw,nonce,code):
    def pairs(items):
        value={}
        for k,v in items:
            if k in value:raise ValueError('Duplicate R6 probe key')
            value[k]=v
        return value
    if type(raw) is not bytes or len(raw)>4096:raise ValueError('R6 probe stdout quota/type')
    v=json.loads(raw,object_pairs_hook=pairs,parse_constant=lambda _:(_ for _ in ()).throw(ValueError('Nonfinite R6 probe value')))
    keys={'v','nonce','pid','userenv_path','kernelbase_path','paths_normal','exports_found','checks','failures',
          'derive_hr','initial_lookup_hr','register_attempted','register_hr','owned','post_register_lookup_hr',
          'duplicate_register_hr','verify_lookup_hr','unregister_attempted','unregister_hr','final_lookup_hr',
          'cleanup_attempted','cleanup_succeeded','passed'}
    if (type(v) is not dict or set(v)!=keys or type(code) is not int or code!=0 or type(v['v']) is not int or v['v']!=2
        or v['nonce']!=nonce or type(v['pid']) is not int or not 0<v['pid']<2**32
        or type(v['userenv_path']) is not str or v['userenv_path'].casefold()!=USERENV
        or type(v['kernelbase_path']) is not str or v['kernelbase_path'].casefold()!=KERNELBASE
        or v['paths_normal'] is not True or v['exports_found'] is not True or v['passed'] is not True
        or type(v['checks']) is not int or v['checks']!=CHECKS or type(v['failures']) is not int or v['failures']!=0):
        raise ValueError('Failed/malformed R6 registration metadata probe')
    for f in _HR_FIELDS:
        if type(v[f]) is not int:raise ValueError('R6 probe HRESULT field type')  # bool is a distinct type, so flags are excluded here
    for f in _FLAG_FIELDS:
        if type(v[f]) is not bool:raise ValueError('R6 probe flag field type')
    # Safety invariants that must hold for ANY record, pass or fail.
    if v['owned'] and v['register_hr']!=0:raise ValueError('R6 probe ownership without a fresh S_OK registration')
    if v['unregister_attempted'] and not v['owned']:raise ValueError('R6 probe release without ownership')
    if v['cleanup_attempted'] and not v['owned']:raise ValueError('R6 probe finally-cleanup without ownership')
    if v['initial_lookup_hr']!=ABSENT and v['register_attempted']:raise ValueError('R6 probe mutation of a non-absent identity')
    # Canonical success: the exact genuine per-step HRESULT sequence, with no emergency cleanup needed.
    if (v['derive_hr']!=0 or v['initial_lookup_hr']!=ABSENT
        or v['register_attempted'] is not True or v['register_hr']!=0 or v['owned'] is not True
        or v['post_register_lookup_hr']!=0 or v['duplicate_register_hr']!=EXISTS or v['verify_lookup_hr']!=0
        or v['unregister_attempted'] is not True or v['unregister_hr']!=0 or v['final_lookup_hr']!=ABSENT
        or v['cleanup_attempted'] is not False):
        raise ValueError('Failed R6 registration metadata probe lifecycle')
    return v

def phase(output,env,result,probe,build,prefix,helpers,monitor,sample,deadline,*,popen=subprocess.Popen,now=time.monotonic,sleep=time.sleep):
    _base.phase(output,env,result,probe,build,prefix,helpers,monitor,sample,deadline,popen=popen,now=now,sleep=sleep,decoder=decode)
