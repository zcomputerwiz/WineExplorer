# SPDX-License-Identifier: GPL-2.0-or-later
"""Calibration command TEXT only; validated schema is NOT PID ownership proof.

No subprocess/attach/file write. Never use this as live execution authority.
"""
import json
import re


def identity(record):
    if isinstance(record,str):
        if len(record.encode('utf-8'))>4096: raise ValueError('Identity JSON size limit')
        def pairs(items):
            result={}
            for key,value in items:
                if key in result: raise ValueError('Duplicate identity field')
                result[key]=value
            return result
        record=json.loads(record,object_pairs_hook=pairs,
            parse_constant=lambda _: (_ for _ in ()).throw(ValueError('Nonfinite identity')))
    if type(record) is not dict or set(record)!= {'pid','main_tid','worker_tid','creation_time','nonce'}:
        raise ValueError('Exact calibration identity schema required')
    for key in ('pid','main_tid','worker_tid','creation_time'):
        maximum=2**64-1 if key=='creation_time' else 2**32-1
        if type(record[key]) is not int or not 0<record[key]<=maximum:
            raise ValueError('Invalid positive unsigned identity: '+key)
    if record['main_tid']==record['worker_tid']: raise ValueError('Distinct selected TIDs required')
    if not isinstance(record['nonce'],str) or not re.fullmatch('[0-9a-fA-F]{32}',record['nonce']):
        raise ValueError('Exact32hex nonce required')
    return dict(record)


def commands(record):
    record=identity(record)
    return (f"attach {record['pid']}\nbt {record['main_tid']}\nbt {record['worker_tid']}\n"
            'detach\nquit\n')
