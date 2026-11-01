# SPDX-License-Identifier: GPL-2.0-or-later
"""File observations, not historical approval gates or loaded-file attestation."""
import hashlib
import math
import os
from pathlib import Path
import stat
import time


def require_regular_canonical(path):
    p=Path(path)
    if p.is_symlink() or p.resolve(strict=True)!=p.absolute() or not p.is_file():
        raise ValueError('Required regular canonical path: '+str(p))
    return p


def compare_actual(actual,previous=None):
    """Already-observed values (e.g. remote hashes); never invent missing data."""
    warnings=[]
    for key,value in (previous or {}).items():
        if actual is not None and key in actual and actual[key] is not None and value is not None and actual[key]!=value:
            warnings.append(dict(code='historical_difference',field=key,previous=value,actual=actual[key]))
    old_hash=(previous or {}).get('sha256')
    current_hash=(actual or {}).get('sha256')
    return dict(actual=actual,previous=previous,warnings=warnings,
        unknown_fields=[key for key,value in (actual or {}).items() if value is None] if actual is not None else ['actual'],
        previous_version_tested=(current_hash==old_hash if current_hash is not None and old_hash is not None else None))


def observe_file(path,previous=None,*,maximum_seconds=120,hash_file=True,deadline=None,now=time.monotonic):
    if type(maximum_seconds) not in (int,float) or not math.isfinite(maximum_seconds) or not 0<maximum_seconds<=120:
        raise ValueError('Inventory observation deadline must be0..120s')
    p=Path(path);begun=now();actual=None;errors=[];stage='metadata';stable=None
    end=begun+maximum_seconds if deadline is None else min(begun+maximum_seconds,deadline)
    def metadata(s,resolved):
        return dict(path=str(resolved),device=s.st_dev,inode=s.st_ino,bytes=s.st_size,
                    mtime_ns=s.st_mtime_ns,ctime_ns=s.st_ctime_ns,sha256=None)
    def budget():
        if now()>=end:raise TimeoutError('Diagnostic inventory deadline')
    try:
        budget();resolved=p.resolve(strict=True);s=resolved.stat();actual=metadata(s,resolved)
        if not stat.S_ISREG(s.st_mode):raise ValueError('Not a regular file')
        if hash_file:
            stage='read';digest=hashlib.sha256();count=0
            with resolved.open('rb') as stream:
                opened=metadata(os.fstat(stream.fileno()),resolved);actual=opened
                while True:
                    budget();data=stream.read(1024**2)
                    if not data:break
                    count+=len(data);digest.update(data)
                budget();after=metadata(os.fstat(stream.fileno()),resolved)
            stage='post_metadata';post=metadata(resolved.stat(),resolved)
            stable=opened==after==post
            actual.update(sha256=digest.hexdigest(),bytes_hashed=count)
            if not stable:errors.append(dict(stage=stage,error_type='FileChangedDuringRead',errno=None))
    except Exception as error:
        errors.append(dict(stage=stage,error_type=type(error).__name__,errno=getattr(error,'errno',None)))
    record=compare_actual(actual,previous)
    if errors:record['previous_version_tested']=None
    record.update(requested_path=str(p),observation_errors=errors,observation_complete=not errors,
        hash_requested=hash_file,
        stable_during_read=stable if actual is not None and hash_file else None,
        elapsed_seconds=now()-begun,maximum_seconds=maximum_seconds,
        limitation='Observed file bytes only; no assertion about loaded executable or effective security')
    return record


def observe_files(previous_by_path,**kwargs):
    return {str(p):observe_file(p,previous,**kwargs) for p,previous in previous_by_path.items()}
