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

def decode(raw,nonce,code):
    def pairs(items):
        value={}
        for k,v in items:
            if k in value:raise ValueError('Duplicate R6 probe key')
            value[k]=v
        return value
    if type(raw) is not bytes or len(raw)>4096:raise ValueError('R6 probe stdout quota/type')
    v=json.loads(raw,object_pairs_hook=pairs,parse_constant=lambda _:(_ for _ in ()).throw(ValueError('Nonfinite R6 probe value')))
    keys={'v','nonce','pid','userenv_path','kernelbase_path','paths_normal','exports_found','checks','failures','passed'}
    if (type(v) is not dict or set(v)!=keys or type(code) is not int or code!=0 or type(v['v']) is not int or v['v']!=1
        or v['nonce']!=nonce or type(v['pid']) is not int or not 0<v['pid']<2**32
        or type(v['userenv_path']) is not str or v['userenv_path'].casefold()!=USERENV
        or type(v['kernelbase_path']) is not str or v['kernelbase_path'].casefold()!=KERNELBASE
        or v['paths_normal'] is not True or v['exports_found'] is not True or v['passed'] is not True
        or type(v['checks']) is not int or v['checks']!=CHECKS or type(v['failures']) is not int or v['failures']!=0):
        raise ValueError('Failed/malformed R6 registration metadata probe')
    return v

def phase(output,env,result,probe,build,prefix,helpers,monitor,sample,deadline,*,popen=subprocess.Popen,now=time.monotonic,sleep=time.sleep):
    _base.phase(output,env,result,probe,build,prefix,helpers,monitor,sample,deadline,popen=popen,now=now,sleep=sleep,decoder=decode)
