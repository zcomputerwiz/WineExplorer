# SPDX-License-Identifier: GPL-2.0-or-later
"""Executable text/schema tests, not native API, ownership or debugger proof."""
import json
import unittest
from g4_stack_commands import commands,identity


class StackCommandsTests(unittest.TestCase):
    def setUp(self):
        self.record=dict(pid=408,main_tid=412,worker_tid=576,creation_time=134372105672917660,nonce='a'*32)

    def test_exact_five_numeric_commands_newlines_and_schema_copy(self):
        expected='attach 408\nbt 412\nbt 576\ndetach\nquit\n'
        self.assertEqual(commands(self.record),expected)
        self.assertEqual(commands(json.dumps(self.record)+'\n'),expected)
        self.assertEqual(identity(self.record),self.record);self.assertIsNot(identity(self.record),self.record)

    def test_unsigned_boundaries_and_uppercase_hex_nonce(self):
        r=dict(self.record,pid=2**32-1,main_tid=1,worker_tid=2**32-1,creation_time=2**64-1,nonce='ABCDEF0123456789'*2)
        self.assertEqual(commands(r),'attach 4294967295\nbt 1\nbt 4294967295\ndetach\nquit\n')

    def test_missing_unknown_fields_and_duplicate_tids(self):
        for key in self.record:
            r=dict(self.record);r.pop(key)
            with self.subTest(missing=key),self.assertRaises(ValueError):commands(r)
        for key,value in [('command','bt all'),('threads',[412,576]),('pid_extra',99)]:
            with self.assertRaises(ValueError):commands(dict(self.record,**{key:value}))
        with self.assertRaises(ValueError):commands(dict(self.record,worker_tid=self.record['main_tid']))

    def test_boolean_negative_zero_overflow_strings_and_floats_rejected(self):
        for key in ('pid','main_tid','worker_tid','creation_time'):
            maximum=2**64-1 if key=='creation_time' else 2**32-1
            for value in (True,False,0,-1,maximum+1,'408','0x198','408\nbt all',1.0,None,[],{}):
                with self.subTest(key=key,value=value),self.assertRaises(ValueError):commands(dict(self.record,**{key:value}))

    def test_nonce_rejects_injection_nonhex_whitespace_wrong_length_and_type(self):
        for nonce in ('','a'*31,'a'*33,'g'*32,'a'*31+'\n',' '*32,'a'*32+'\nquit',None,32,[]):
            with self.subTest(nonce=nonce),self.assertRaises(ValueError):commands(dict(self.record,nonce=nonce))

    def test_duplicate_json_keys_trailing_commands_malformed_nonfinite_and_oversize(self):
        text=json.dumps(self.record)
        for raw in (text[:-1]+',"pid":999}',text+'\nquit',text+' {}',text+';bt all',
                    '{','[]','null','true','NaN',text.replace('408','Infinity'),text+' '*4097):
            with self.subTest(raw=raw[:80]),self.assertRaises(ValueError):commands(raw)
        for value in (None,[],1,b'{}'):
            with self.assertRaises(ValueError):commands(value)


if __name__=='__main__':unittest.main()
