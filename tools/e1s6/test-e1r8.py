#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""WO-19: E1R8 Python codec test (R8 step 2, part 2).

unittest for e1r8.py. It mirrors the reviewed C codec test
src/programs/wexelevate/test_e1r8_codec_main.c (WO-18): for every message
type a round trip at minimum and maximum length, then every section 11 codec
vector (one byte short and long, wrong magic/version/type/reserved, unknown
enum values, each bad string kind, bad S1 fields, refused RESULT with a
nonzero field, the UTF-8 list, 4096 accepted and 4097 refused, empty
password payload refused). It also reads the WO-18 "golden" lines from the
file named by E1R8_GOLDEN (skipped when unset) and asserts that e1r8.py
encodes byte-identical messages from the same fixed input and decodes each
line back to that input.
"""
import importlib.util
import os
import struct
import sys
import unittest
from pathlib import Path

sys.dont_write_bytecode = True

spec = importlib.util.spec_from_file_location("e1r8", Path(__file__).with_name("e1r8.py"))
e = importlib.util.module_from_spec(spec)
spec.loader.exec_module(e)


def golden_tuple():
    """The fixed golden input (WO-18 item 2)."""
    return e.E1R8Tuple(
        request_id=0x0102030405060708, attempt=1,
        nonce=bytes(range(0x10, 0x30)), channel_id=bytes(range(0x30, 0x40)),
        binding_digest=bytes(range(0x40, 0x60)), request_digest=bytes(range(0x60, 0x80)),
        deadline=0x1122334455667788, root_gen=1, server_gen=2, broker_gen=3,
        verifier_gen=4, map_gen=5, policy_gen=6, caller_session=7,
        desktop_route=bytes(range(0x80, 0x90)), desktop_mode=1,
        dialog_id=bytes(range(0x90, 0xa0)))


def acct63():
    return b"a" + b"1234567890" * 6 + b"12"


class E1R8Tests(unittest.TestCase):
    def deny(self, call, *args):
        with self.assertRaises(e.E1R8Error):
            call(*args)

    def test_round_trips(self):
        t = golden_tuple()
        key = bytes(range(0xa0, 0xb0))
        ev = bytes(range(0xb0, 0xc0))
        zero32 = bytes(32)
        zero16 = bytes(16)

        # Type 1: INIT (256..2067). Minimum: empty display strings, ":1.1".
        vo = b":1.1"
        data = e.encode_init(t, vo, b"", b"", b"")
        self.assertEqual(len(data), 256)
        in_ = e.decode_init(data, t)
        self.assertEqual(in_.verifier_owner, vo)
        self.assertEqual(in_.requester, b"")
        self.assertEqual(in_.image, b"")
        self.assertEqual(in_.publisher, b"")
        self.assertEqual(in_.tuple, t)
        # Maximum: 255-byte owner, 520-byte display strings.
        vo = b":" + b"1" * 252 + b"." + b"1"
        req = b"a\x00" * 260
        img = b"b\x00" * 260
        pub = b"c\x00" * 260
        total = 76 + 160 + (4 + 255) + (4 + 520) + (4 + 520) + (4 + 520)
        self.assertEqual(total, 2067)
        data = e.encode_init(t, vo, req, img, pub)
        in_ = e.decode_init(data, t)
        self.assertEqual(in_.verifier_owner, vo)
        self.assertEqual(in_.requester, req)
        self.assertEqual(in_.image, img)
        self.assertEqual(in_.publisher, pub)

        # Type 2: ACK (156).
        data = e.encode_ack(t)
        self.assertEqual(len(data), 156)
        a = e.decode_ack(data, t)
        self.assertEqual(a.binding_digest, t.binding_digest)
        self.assertEqual(a.request_digest, t.request_digest)
        self.assertEqual(a.dialog_id, t.dialog_id)

        # Type 3: SELECT_OK (76).
        data = e.encode_select_ok(t)
        self.assertEqual(len(data), 76)
        self.assertIsNone(e.decode_select_ok(data, t))

        # Type 4: SELECT (81..143).
        data = e.encode_select(t, b"a")
        self.assertEqual(len(data), 81)
        s = e.decode_select(data, t)
        self.assertEqual(s.account, b"a")
        data = e.encode_select(t, acct63())
        self.assertEqual(len(data), 143)
        s = e.decode_select(data, t)
        self.assertEqual(len(s.account), 63)

        # Type 5: READY (76).
        data = e.encode_ready(t)
        self.assertEqual(len(data), 76)
        self.assertIsNone(e.decode_ready(data, t))

        # Type 6: RESULT (116 refused, 117..179 authenticated).
        data = e.encode_result(t, 2, b"", zero32)
        self.assertEqual(len(data), 116)
        r = e.decode_result(data, t)
        self.assertEqual(r.status, 2)
        self.assertEqual(r.account, b"")
        data = e.encode_result(t, 1, acct63(), t.binding_digest)
        self.assertEqual(len(data), 179)
        r = e.decode_result(data, t)
        self.assertEqual(r.status, 1)
        self.assertEqual(len(r.account), 63)
        self.assertEqual(r.challenge, t.binding_digest)

        # Type 7: ANSWER (264). Confirm and Cancel.
        data = e.encode_answer(t, t.binding_digest, 2, 2, 0, 0x3ff)
        self.assertEqual(len(data), 264)
        a = e.decode_answer(data, t, 0x3ff)
        self.assertEqual(a.answer_kind, 2)
        self.assertEqual(a.round, 2)
        self.assertEqual(a.message_index, 0)
        self.assertEqual(a.challenge, t.binding_digest)
        data = e.encode_answer(t, zero32, 3, 0, 0, 0x3ff)
        a = e.decode_answer(data, t, 0x3ff)
        self.assertEqual(a.answer_kind, 3)
        self.assertEqual(a.round, 0)

        # Type 8: DONE (80).
        data = e.encode_done(t, 1)
        self.assertEqual(len(data), 80)
        d = e.decode_done(data, t)
        self.assertEqual(d.status, 1)

        # Type 9: ATTACH_OK (76).
        data = e.encode_attach_ok(t)
        self.assertEqual(len(data), 76)
        self.assertIsNone(e.decode_attach_ok(data, t))

        # Type 16: ENDORSED (244).
        data = e.encode_endorsed(t, 4242, 0)
        self.assertEqual(len(data), 244)
        en = e.decode_endorsed(data, t)
        self.assertEqual(en.pid, 4242)
        self.assertEqual(en.uid, 0)
        self.assertEqual(en.tuple, t)

        # Type 17: START (97..159).
        data = e.encode_start(t, b"a", key)
        self.assertEqual(len(data), 97)
        s = e.decode_start(data, t)
        self.assertEqual(s.account, b"a")
        self.assertEqual(s.account_key, key)
        data = e.encode_start(t, acct63(), key)
        self.assertEqual(len(data), 159)
        s = e.decode_start(data, t)
        self.assertEqual(len(s.account), 63)

        # Type 18: WORKER_READY (76).
        data = e.encode_worker_ready(t)
        self.assertEqual(len(data), 76)
        self.assertIsNone(e.decode_worker_ready(data, t))

        # Type 19: DELEGATED (76).
        data = e.encode_delegated(t)
        self.assertEqual(len(data), 76)
        self.assertIsNone(e.decode_delegated(data, t))

        # Type 20: RESULT_V (116 refused, 117..179 authenticated).
        data = e.encode_result_v(t, 2, zero16, b"", zero16)
        self.assertEqual(len(data), 116)
        r = e.decode_result_v(data, t)
        self.assertEqual(r.status, 2)
        self.assertEqual(r.account, b"")
        data = e.encode_result_v(t, 1, key, acct63(), ev)
        self.assertEqual(len(data), 179)
        r = e.decode_result_v(data, t)
        self.assertEqual(r.status, 1)
        self.assertEqual(len(r.account), 63)
        self.assertEqual(r.account_key, key)
        self.assertEqual(r.evidence_ref, ev)

        # Type 21: CANCEL (80).
        data = e.encode_cancel(t, 1)
        self.assertEqual(len(data), 80)
        c = e.decode_cancel(data, t)
        self.assertEqual(c.reason, 1)

        # Type 22: W_ACK (76).
        data = e.encode_w_ack(t)
        self.assertEqual(len(data), 76)
        self.assertIsNone(e.decode_w_ack(data, t))

        # Type 23: ATTACH_ACK (76).
        data = e.encode_attach_ack(t)
        self.assertEqual(len(data), 76)
        self.assertIsNone(e.decode_attach_ack(data, t))

        # Type 24: PROMPT_RELEASE (76).
        data = e.encode_prompt_release(t)
        self.assertEqual(len(data), 76)
        self.assertIsNone(e.decode_prompt_release(data, t))

        # Type 25: RELEASE (80).
        data = e.encode_release(t, 1)
        self.assertEqual(len(data), 80)
        r = e.decode_release(data, t)
        self.assertEqual(r.outcome, 1)

        # Type 26: RELEASED (76).
        data = e.encode_released(t)
        self.assertEqual(len(data), 76)
        self.assertIsNone(e.decode_released(data, t))

        # Type 27: W_SETUP (244).
        data = e.encode_w_setup(t, 4242, 0)
        self.assertEqual(len(data), 244)
        w = e.decode_w_setup(data, t)
        self.assertEqual(w.pid, 4242)
        self.assertEqual(w.uid, 0)
        self.assertEqual(w.tuple, t)

        # S7 version 2 PROMPT (178).
        data = e.encode_s7_prompt(t)
        self.assertEqual(len(data), 178)
        self.assertIsNone(e.decode_s7_prompt(data, t))

        # Secret password answer (157..4252).
        pw = "pässword".encode("utf-8")
        data = e.encode_secret_answer(t, 0x3ff, pw)
        self.assertEqual(len(data), 156 + 9)
        a = e.decode_secret_answer(data, t, 0x3ff)
        self.assertEqual(a.answer_kind, 1)
        self.assertEqual(a.round, 1)
        self.assertEqual(a.message_index, 0)
        self.assertEqual(a.payload_len, 9)
        self.assertEqual(a.payload, pw)

    def test_length_errors(self):
        t = golden_tuple()
        key = bytes(range(0xa0, 0xb0))
        pw = "pässword".encode("utf-8")

        # ACK: fixed 156.
        data = e.encode_ack(t)
        self.deny(e.decode_ack, data[:-1], t)
        self.deny(e.decode_ack, data + b"\x00", t)

        # SELECT: 81..143.
        data = e.encode_select(t, b"a")
        self.deny(e.decode_select, data[:-1], t)
        self.deny(e.decode_select, data + b"\x00", t)

        # START: 97..159.
        data = e.encode_start(t, b"a", key)
        self.deny(e.decode_start, data[:-1], t)
        self.deny(e.decode_start, data + b"\x00", t)

        # INIT: 256..2067.
        data = e.encode_init(t, b":1.1", b"", b"", b"")
        self.deny(e.decode_init, data[:-1], t)
        self.deny(e.decode_init, data + b"\x00", t)

        # ANSWER: fixed 264.
        data = e.encode_answer(t, t.binding_digest, 2, 2, 0, 0x3ff)
        self.deny(e.decode_answer, data[:-1], t, 0x3ff)
        self.deny(e.decode_answer, data + b"\x00", t, 0x3ff)

        # ENDORSED: fixed 244.
        data = e.encode_endorsed(t, 4242, 0)
        self.deny(e.decode_endorsed, data[:-1], t)
        self.deny(e.decode_endorsed, data + b"\x00", t)

        # Secret password: 157..4252.
        data = e.encode_secret_answer(t, 0x3ff, pw)
        self.deny(e.decode_secret_answer, data[:-1], t, 0x3ff)
        self.deny(e.decode_secret_answer, data + b"\x00", t, 0x3ff)

    def test_header_errors(self):
        t = golden_tuple()
        data = bytearray(e.encode_ack(t))

        # wrong magic
        data[0] = ord("X")
        self.deny(e.decode_ack, bytes(data), t)
        data[0] = ord("E")
        # wrong version
        data[4] = 2
        self.deny(e.decode_ack, bytes(data), t)
        data[4] = 1
        # wrong type
        data[6] = 3
        self.deny(e.decode_ack, bytes(data), t)
        data[6] = 2
        # nonzero reserved
        data[24] = 1
        self.deny(e.decode_ack, bytes(data), t)
        data[24] = 0
        # wrong request_id
        data[12] = 0xff
        self.deny(e.decode_ack, bytes(data), t)
        data[12] = 0x08
        # wrong nonce
        data[28] = 0xff
        self.deny(e.decode_ack, bytes(data), t)
        data[28] = 0x10
        # wrong channel_id
        data[60] = 0xff
        self.deny(e.decode_ack, bytes(data), t)
        data[60] = 0x30
        # valid again
        a = e.decode_ack(bytes(data), t)
        self.assertEqual(a.binding_digest, t.binding_digest)

    def test_enum_errors(self):
        t = golden_tuple()
        zero32 = bytes(32)

        # DONE: status must be 1 or 2.
        data = bytearray(e.encode_done(t, 1))
        data[76] = 3
        self.deny(e.decode_done, bytes(data), t)

        # CANCEL: reason must be 1.
        data = bytearray(e.encode_cancel(t, 1))
        data[76] = 2
        self.deny(e.decode_cancel, bytes(data), t)

        # RELEASE: outcome must be 1 or 2.
        data = bytearray(e.encode_release(t, 1))
        data[76] = 3
        self.deny(e.decode_release, bytes(data), t)

        # RESULT: status must be 1 or 2.
        data = bytearray(e.encode_result(t, 2, b"", zero32))
        data[76] = 3
        self.deny(e.decode_result, bytes(data), t)

        # desktop_mode in the tuple tail must be 1 or 2.
        data = bytearray(e.encode_endorsed(t, 4242, 0))
        data[76 + 140] = 3
        self.deny(e.decode_endorsed, bytes(data), t)

    def test_string_errors(self):
        t = golden_tuple()
        req = "Caller".encode("utf-16-le")
        vo = b":1.1"

        # SELECT: non-ASCII account name.
        self.deny(e.encode_select, t, b"a\x80b")
        # SELECT: leading dash.
        self.deny(e.encode_select, t, b"-a")
        # SELECT: 64-byte account (overlength).
        self.deny(e.encode_select, t, b"a" + b"123456789012345678901234567890123456789012345678901234567890123")

        # INIT: bad verifier owner (no dot).
        self.deny(e.encode_init, t, b":11", req, b"", b"")
        # INIT: bad verifier owner (non-digit).
        self.deny(e.encode_init, t, b":1.a", req, b"", b"")
        # INIT: display string with NUL (0x0000 code unit).
        self.deny(e.encode_init, t, vo, b"\x00\x00\x00\x00", b"", b"")
        # INIT: display string with control character.
        self.deny(e.encode_init, t, vo, b"\x01\x00", b"", b"")
        # INIT: display string with unpaired surrogate.
        self.deny(e.encode_init, t, vo, b"\x00\xd8", b"", b"")
        # INIT: display string with odd length.
        self.deny(e.encode_init, t, vo, b"a", b"", b"")

        # valid INIT again
        data = e.encode_init(t, vo, req, b"", b"")
        in_ = e.decode_init(data, t)
        self.assertEqual(in_.requester, req)

        # SELECT decode: bad account in a crafted buffer.
        data = bytearray(e.encode_select(t, b"a"))
        data[80] = 0x80
        self.deny(e.decode_select, bytes(data), t)

    def test_s1_errors(self):
        t = golden_tuple()
        data = bytearray(e.encode_answer(t, t.binding_digest, 2, 2, 0, 0x3ff))

        # wrong kind
        data[108] = 1
        self.deny(e.decode_answer, bytes(data), t, 0x3ff)
        data[108] = 2
        # wrong version
        data[110] = 2
        self.deny(e.decode_answer, bytes(data), t, 0x3ff)
        data[110] = 1
        # wrong length
        data[112] = 157
        self.deny(e.decode_answer, bytes(data), t, 0x3ff)
        data[112] = 156
        # caps outside the mask: required_caps is at 116..124; 0x3ff allows
        # bits 0..9, so set bit 10 (0x04 in byte 117).
        data[117] = 0x07
        self.deny(e.decode_answer, bytes(data), t, 0x3ff)
        data[117] = 0x03
        # wrong request_id
        data[124] = 0xff
        self.deny(e.decode_answer, bytes(data), t, 0x3ff)
        data[124] = 0x08
        # wrong nonce
        data[148] = 0xff
        self.deny(e.decode_answer, bytes(data), t, 0x3ff)
        data[148] = 0x10
        # answer_kind 1 refused on the broker pipe
        data[144] = 1
        self.deny(e.decode_answer, bytes(data), t, 0x3ff)
        data[144] = 2
        # Confirm with wrong round
        data[136] = 1
        self.deny(e.decode_answer, bytes(data), t, 0x3ff)
        data[136] = 2
        # Confirm with nonzero payload
        data[260] = 1
        self.deny(e.decode_answer, bytes(data), t, 0x3ff)
        data[260] = 0
        # valid again
        a = e.decode_answer(bytes(data), t, 0x3ff)
        self.assertEqual(a.answer_kind, 2)

    def test_refused_result_errors(self):
        t = golden_tuple()
        zero32 = bytes(32)
        zero16 = bytes(16)

        # RESULT (type 6) refused: nonzero challenge. The challenge occupies
        # bytes 84..116 (status 76..80, empty account length 80..84).
        data = bytearray(e.encode_result(t, 2, b"", zero32))
        data[115] = 1
        self.deny(e.decode_result, bytes(data), t)

        # RESULT_V (type 20) refused: nonzero account_key.
        data = bytearray(e.encode_result_v(t, 2, zero16, b"", zero16))
        data[80] = 1
        self.deny(e.decode_result_v, bytes(data), t)

        # RESULT_V (type 20) refused: nonzero evidence_ref.
        data = bytearray(e.encode_result_v(t, 2, zero16, b"", zero16))
        data[115] = 1
        self.deny(e.decode_result_v, bytes(data), t)

    def test_utf8_errors(self):
        t = golden_tuple()

        # truncated continuation
        self.deny(e.encode_secret_answer, t, 0x3ff, b"\xc3")
        # missing continuation (stray continuation byte)
        self.deny(e.encode_secret_answer, t, 0x3ff, b"\x80")
        # overlong C0 80
        self.deny(e.encode_secret_answer, t, 0x3ff, b"\xc0\x80")
        # overlong E0 80 80
        self.deny(e.encode_secret_answer, t, 0x3ff, b"\xe0\x80\x80")
        # overlong F0 80 80 80
        self.deny(e.encode_secret_answer, t, 0x3ff, b"\xf0\x80\x80\x80")
        # encoded surrogate ED A0 80
        self.deny(e.encode_secret_answer, t, 0x3ff, b"\xed\xa0\x80")
        # encoded surrogate ED BF BF
        self.deny(e.encode_secret_answer, t, 0x3ff, b"\xed\xbf\xbf")
        # scalar above U+10FFFF F4 90 80 80
        self.deny(e.encode_secret_answer, t, 0x3ff, b"\xf4\x90\x80\x80")
        # bytes F5..FF
        self.deny(e.encode_secret_answer, t, 0x3ff, b"\xf5")
        self.deny(e.encode_secret_answer, t, 0x3ff, b"\xff")
        # embedded NUL
        self.deny(e.encode_secret_answer, t, 0x3ff, b"\x70\x00\x73")
        # valid 4096-byte payload accepted
        pw = bytes((ord("a") + (i % 26)) for i in range(4096))
        data = e.encode_secret_answer(t, 0x3ff, pw)
        self.assertEqual(len(data), 156 + 4096)
        a = e.decode_secret_answer(data, t, 0x3ff)
        self.assertEqual(a.payload_len, 4096)
        self.assertEqual(a.payload, pw)
        # 4097-byte payload refused
        pw = bytes((ord("a") + (i % 26)) for i in range(4097))
        self.deny(e.encode_secret_answer, t, 0x3ff, pw)
        # empty password payload refused
        self.deny(e.encode_secret_answer, t, 0x3ff, b"")

    def test_tuples(self):
        t = golden_tuple()

        # Pre-endorsement tuple (204 bytes): section 4 offsets.
        data = e.encode_pre_endorsement_tuple(t)
        self.assertEqual(len(data), 204)
        self.assertEqual(struct.unpack_from("<Q", data, 0)[0], t.request_id)
        self.assertEqual(struct.unpack_from("<I", data, 8)[0], t.attempt)
        self.assertEqual(data[12:44], t.nonce)
        self.assertEqual(data[44:60], t.channel_id)
        self.assertEqual(data[60:92], t.binding_digest)
        self.assertEqual(data[92:124], t.request_digest)
        self.assertEqual(struct.unpack_from("<Q", data, 124)[0], t.deadline)
        self.assertEqual(struct.unpack_from("<Q", data, 132)[0], t.root_gen)
        self.assertEqual(struct.unpack_from("<Q", data, 140)[0], t.server_gen)
        self.assertEqual(struct.unpack_from("<Q", data, 148)[0], t.broker_gen)
        self.assertEqual(struct.unpack_from("<Q", data, 156)[0], t.verifier_gen)
        self.assertEqual(struct.unpack_from("<Q", data, 164)[0], t.map_gen)
        self.assertEqual(struct.unpack_from("<Q", data, 172)[0], t.policy_gen)
        self.assertEqual(struct.unpack_from("<I", data, 180)[0], t.caller_session)
        self.assertEqual(data[184:200], t.desktop_route)
        self.assertEqual(struct.unpack_from("<I", data, 200)[0], t.desktop_mode)
        d = e.decode_pre_endorsement_tuple(data)
        self.assertEqual(d.request_id, t.request_id)
        self.assertEqual(d.attempt, t.attempt)
        self.assertEqual(d.nonce, t.nonce)
        self.assertEqual(d.channel_id, t.channel_id)
        self.assertEqual(d.binding_digest, t.binding_digest)
        self.assertEqual(d.request_digest, t.request_digest)
        self.assertEqual(d.deadline, t.deadline)
        self.assertEqual(d.root_gen, t.root_gen)
        self.assertEqual(d.server_gen, t.server_gen)
        self.assertEqual(d.broker_gen, t.broker_gen)
        self.assertEqual(d.verifier_gen, t.verifier_gen)
        self.assertEqual(d.map_gen, t.map_gen)
        self.assertEqual(d.policy_gen, t.policy_gen)
        self.assertEqual(d.caller_session, t.caller_session)
        self.assertEqual(d.desktop_route, t.desktop_route)
        self.assertEqual(d.desktop_mode, t.desktop_mode)
        self.deny(e.decode_pre_endorsement_tuple, data[:-1])
        self.deny(e.decode_pre_endorsement_tuple, data + b"\x00")

        # Sealed tuple (220 bytes): section 4 offsets, dialog_id at 204.
        data = e.encode_sealed_tuple(t)
        self.assertEqual(len(data), 220)
        self.assertEqual(data[204:220], t.dialog_id)
        d = e.decode_sealed_tuple(data)
        self.assertEqual(d, t)
        self.deny(e.decode_sealed_tuple, data[:-1])
        self.deny(e.decode_sealed_tuple, data + b"\x00")

    def test_learn_and_huge_length(self):
        t = golden_tuple()
        vo = b":1.1"

        data = e.encode_init(t, vo, b"", b"", b"")
        in_ = e.decode_init_learn(data, t.request_id, 1, t.channel_id)
        self.assertEqual(in_.tuple, t)
        self.deny(e.decode_init_learn, data, t.request_id + 1, 1, t.channel_id)
        self.deny(e.decode_init_learn, data, t.request_id, 2, t.channel_id)
        other_channel = bytearray(t.channel_id)
        other_channel[0] ^= 1
        self.deny(e.decode_init_learn, data, t.request_id, 1, bytes(other_channel))
        # bad desktop_mode
        bad = bytearray(data)
        bad[76 + 140] = 3
        self.deny(e.decode_init_learn, bytes(bad), t.request_id, 1, t.channel_id)
        # bad magic still refused
        bad = bytearray(data)
        bad[0] = ord("X")
        self.deny(e.decode_init_learn, bytes(bad), t.request_id, 1, t.channel_id)
        # verifier owner length 0xfffffffc: must not wrap past the buffer
        bad = bytearray(data)
        bad[236:240] = (0xfffffffc).to_bytes(4, "little")
        self.deny(e.decode_init, bytes(bad), t)
        bad[236:240] = (0xffffffff).to_bytes(4, "little")
        self.deny(e.decode_init, bytes(bad), t)

        data = e.encode_w_setup(t, 4242, 0)
        w = e.decode_w_setup_learn(data, t.request_id, 1, t.channel_id)
        self.assertEqual(w.pid, 4242)
        self.assertEqual(w.uid, 0)
        self.assertEqual(w.tuple, t)
        self.deny(e.decode_w_setup_learn, data[:-1], t.request_id, 1, t.channel_id)

    def test_r9_round_trips(self):
        t = golden_tuple()
        t2 = golden_tuple()
        t2.attempt = 2
        key = bytes(range(0xa0, 0xb0))
        ev = bytes(range(0xb0, 0xc0))

        # Type 32: GRANT (108).
        data = e.encode_grant(t, key, ev)
        self.assertEqual(len(data), 108)
        g = e.decode_grant(data, t)
        self.assertEqual(g.account_key, key)
        self.assertEqual(g.evidence_ref, ev)

        # Type 33: GRANTED (76).
        data = e.encode_granted(t)
        self.assertEqual(len(data), 76)
        self.assertIsNone(e.decode_granted(data, t))

        # Type 40: REQUEST (232..1792).
        # Minimum: empty display strings.
        data = e.encode_request(t, 1, 1, b"", b"", b"")
        self.assertEqual(len(data), 232)
        r = e.decode_request(data, t)
        self.assertEqual(r.kind, 1)
        self.assertEqual(r.secure_desktop, 1)
        self.assertEqual(r.deadline, t.deadline)
        self.assertEqual(r.binding_digest, t.binding_digest)
        self.assertEqual(r.request_digest, t.request_digest)
        self.assertEqual((r.root_gen, r.server_gen, r.broker_gen,
                          r.verifier_gen, r.map_gen, r.policy_gen),
                         (t.root_gen, t.server_gen, t.broker_gen,
                          t.verifier_gen, t.map_gen, t.policy_gen))
        self.assertEqual(r.desktop_route, t.desktop_route)
        self.assertEqual(r.requester, b"")
        self.assertEqual(r.image, b"")
        self.assertEqual(r.publisher, b"")
        # Maximum: 520-byte display strings.
        req = b"a\x00" * 260
        img = b"b\x00" * 260
        pub = b"c\x00" * 260
        data = e.encode_request(t, 2, 0, req, img, pub)
        self.assertEqual(len(data), 1792)
        r = e.decode_request(data, t)
        self.assertEqual(r.kind, 2)
        self.assertEqual(r.secure_desktop, 0)
        self.assertEqual(len(r.requester), 520)
        self.assertEqual(len(r.image), 520)
        self.assertEqual(len(r.publisher), 520)

        # Type 41: DECISION (84 deny, 284 prompt).
        data = e.encode_decision(t, 2, 1)
        self.assertEqual(len(data), 84)
        d = e.decode_decision(data, t)
        self.assertEqual(d.status, 2)
        self.assertEqual(d.reason, 1)
        data = e.encode_decision(t, 1, 0)
        self.assertEqual(len(data), 284)
        d = e.decode_decision(data, t)
        self.assertEqual(d.status, 1)
        self.assertEqual(d.tuple, t)

        # Type 42: ENDORSED_LOCAL (96).
        data = e.encode_endorsed_local(t, t.dialog_id, 1)
        self.assertEqual(len(data), 96)
        el = e.decode_endorsed_local(data, t)
        self.assertEqual(el.status, 1)
        self.assertEqual(el.dialog_id, t.dialog_id)

        # Type 43: TO_D (152..2143).
        # Minimum: embedded SELECT_OK (76).
        emb = e.encode_select_ok(t)
        data = e.encode_to_d(t, emb)
        self.assertEqual(len(data), 152)
        td = e.decode_to_d(data, t)
        self.assertEqual(td.type, e.TYPE_SELECT_OK)
        self.assertEqual(td.frame_len, 76)
        # Maximum: embedded INIT max (2067).
        vo = b":" + b"1" * 252 + b"." + b"1"
        req = b"a\x00" * 260
        img = b"b\x00" * 260
        pub = b"c\x00" * 260
        emb = e.encode_init(t, vo, req, img, pub)
        self.assertEqual(len(emb), 2067)
        data = e.encode_to_d(t, emb)
        self.assertEqual(len(data), 2143)
        td = e.decode_to_d(data, t)
        self.assertEqual(td.type, e.TYPE_INIT)
        self.assertEqual(td.frame_len, 2067)

        # Type 44: FROM_D (157..340).
        # Minimum: embedded SELECT "a" (81).
        emb = e.encode_select(t, b"a")
        data = e.encode_from_d(t, emb)
        self.assertEqual(len(data), 157)
        fd = e.decode_from_d(data, t)
        self.assertEqual(fd.type, e.TYPE_SELECT)
        self.assertEqual(fd.frame_len, 81)
        # Maximum: embedded ANSWER (264).
        emb = e.encode_answer(t, t.binding_digest, 2, 2, 0, 0x3ff)
        data = e.encode_from_d(t, emb)
        self.assertEqual(len(data), 340)
        fd = e.decode_from_d(data, t)
        self.assertEqual(fd.type, e.TYPE_ANSWER)
        self.assertEqual(fd.frame_len, 264)

        # Type 45: RECHECK (80).
        data = e.encode_recheck(t, 1)
        self.assertEqual(len(data), 80)
        rc = e.decode_recheck(data, t)
        self.assertEqual(rc.step, 1)

        # Type 46: RECHECKED (84).
        data = e.encode_rechecked(t, 1, 1)
        self.assertEqual(len(data), 84)
        rc = e.decode_rechecked(data, t)
        self.assertEqual(rc.step, 1)
        self.assertEqual(rc.status, 1)

        # Type 47: LAUNCH (97..159).
        data = e.encode_launch(t, b"a", ev)
        self.assertEqual(len(data), 97)
        l = e.decode_launch(data, t)
        self.assertEqual(l.account, b"a")
        self.assertEqual(l.evidence_ref, ev)
        data = e.encode_launch(t, acct63(), ev)
        self.assertEqual(len(data), 159)
        l = e.decode_launch(data, t)
        self.assertEqual(len(l.account), 63)

        # Type 48: LAUNCHED (84).
        data = e.encode_launched(t, 1, 0)
        self.assertEqual(len(data), 84)
        l = e.decode_launched(data, t)
        self.assertEqual(l.status, 1)
        self.assertEqual(l.win32, 0)

        # Type 49: CANCEL_R9 (80).
        data = e.encode_cancel_r9(t, 1)
        self.assertEqual(len(data), 80)
        c = e.decode_cancel_r9(data, t)
        self.assertEqual(c.reason, 1)

        # Type 50: END (80).
        data = e.encode_end(t, 1)
        self.assertEqual(len(data), 80)
        en = e.decode_end(data, t)
        self.assertEqual(en.outcome, 1)

        # Type 51: NEXT_ATTEMPT (280, attempt 2).
        data = e.encode_next_attempt(t2)
        self.assertEqual(len(data), 280)
        na = e.decode_next_attempt(data, t2)
        self.assertEqual(na.tuple, t2)

    def test_r9_errors(self):
        t = golden_tuple()
        t2 = golden_tuple()
        t2.attempt = 2
        key = bytes(range(0xa0, 0xb0))
        ev = bytes(range(0xb0, 0xc0))
        zero16 = bytes(16)

        # --- One byte short and one byte long ---
        data = e.encode_grant(t, key, ev)
        self.deny(e.decode_grant, data[:-1], t)
        self.deny(e.decode_grant, data + b"\x00", t)
        data = e.encode_request(t, 1, 1, b"", b"", b"")
        self.deny(e.decode_request, data[:-1], t)
        self.deny(e.decode_request, data + b"\x00", t)
        data = e.encode_decision(t, 2, 1)
        self.deny(e.decode_decision, data[:-1], t)
        self.deny(e.decode_decision, data + b"\x00", t)
        emb = e.encode_select_ok(t)
        data = e.encode_to_d(t, emb)
        self.deny(e.decode_to_d, data[:-1], t)
        self.deny(e.decode_to_d, data + b"\x00", t)
        emb = e.encode_select(t, b"a")
        data = e.encode_from_d(t, emb)
        self.deny(e.decode_from_d, data[:-1], t)
        self.deny(e.decode_from_d, data + b"\x00", t)
        data = e.encode_launch(t, b"a", ev)
        self.deny(e.decode_launch, data[:-1], t)
        self.deny(e.decode_launch, data + b"\x00", t)
        data = e.encode_next_attempt(t2)
        self.deny(e.decode_next_attempt, data[:-1], t2)
        self.deny(e.decode_next_attempt, data + b"\x00", t2)

        # --- Wrong type and nonzero reserved ---
        data = bytearray(e.encode_recheck(t, 1))
        struct.pack_into("<H", data, 6, e.TYPE_RECHECKED)  # wrong type
        self.deny(e.decode_recheck, bytes(data), t)
        struct.pack_into("<H", data, 6, e.TYPE_RECHECK)
        data[24] = 1  # nonzero reserved
        self.deny(e.decode_recheck, bytes(data), t)
        data[24] = 0

        # --- Unknown enum values (0 and max+1) ---
        data = bytearray(e.encode_request(t, 1, 1, b"", b"", b""))
        data[76] = 0  # kind 0
        self.deny(e.decode_request, bytes(data), t)
        data[76] = 3  # kind 3 (max+1)
        self.deny(e.decode_request, bytes(data), t)
        data[76] = 1
        data[216] = 2  # secure_desktop 2 (max+1)
        self.deny(e.decode_request, bytes(data), t)
        data[216] = 1

        data = bytearray(e.encode_decision(t, 2, 1))
        data[76] = 0  # status 0
        self.deny(e.decode_decision, bytes(data), t)
        data[76] = 3  # status 3 (max+1)
        self.deny(e.decode_decision, bytes(data), t)
        data[76] = 2
        data[80] = 0  # reason 0
        self.deny(e.decode_decision, bytes(data), t)
        data[80] = 4  # reason 4 (max+1)
        self.deny(e.decode_decision, bytes(data), t)
        data[80] = 1

        data = bytearray(e.encode_endorsed_local(t, t.dialog_id, 1))
        data[92] = 0  # status 0
        self.deny(e.decode_endorsed_local, bytes(data), t)
        data[92] = 3  # status 3 (max+1)
        self.deny(e.decode_endorsed_local, bytes(data), t)
        data[92] = 1

        data = bytearray(e.encode_recheck(t, 1))
        data[76] = 0  # step 0
        self.deny(e.decode_recheck, bytes(data), t)
        data[76] = 5  # step 5 (max+1)
        self.deny(e.decode_recheck, bytes(data), t)
        data[76] = 1

        data = bytearray(e.encode_rechecked(t, 1, 1))
        data[76] = 0  # step 0
        self.deny(e.decode_rechecked, bytes(data), t)
        data[76] = 5  # step 5 (max+1)
        self.deny(e.decode_rechecked, bytes(data), t)
        data[76] = 1
        data[80] = 0  # status 0
        self.deny(e.decode_rechecked, bytes(data), t)
        data[80] = 3  # status 3 (max+1)
        self.deny(e.decode_rechecked, bytes(data), t)
        data[80] = 1

        data = bytearray(e.encode_launched(t, 1, 0))
        data[76] = 0  # status 0
        self.deny(e.decode_launched, bytes(data), t)
        data[76] = 4  # status 4 (max+1)
        self.deny(e.decode_launched, bytes(data), t)
        data[76] = 1

        data = bytearray(e.encode_cancel_r9(t, 1))
        data[76] = 0  # reason 0
        self.deny(e.decode_cancel_r9, bytes(data), t)
        data[76] = 7  # reason 7 (max+1)
        self.deny(e.decode_cancel_r9, bytes(data), t)
        data[76] = 1

        data = bytearray(e.encode_end(t, 1))
        data[76] = 0  # outcome 0
        self.deny(e.decode_end, bytes(data), t)
        data[76] = 4  # outcome 4 (max+1)
        self.deny(e.decode_end, bytes(data), t)
        data[76] = 1

        # --- Invalid strings ---
        # REQUEST: display string with odd length.
        self.deny(e.encode_request, t, 1, 1, b"a", b"", b"")
        # REQUEST: display string overlength (522).
        big = b"a\x00" * 261
        self.deny(e.encode_request, t, 1, 1, big, b"", b"")
        # REQUEST: display string with NUL.
        self.deny(e.encode_request, t, 1, 1, b"\x00\x00\x00\x00", b"", b"")
        # REQUEST: display string with control character.
        self.deny(e.encode_request, t, 1, 1, b"\x01\x00", b"", b"")
        # REQUEST: display string with unpaired surrogate.
        self.deny(e.encode_request, t, 1, 1, b"\x00\xd8", b"", b"")
        # LAUNCH: non-ASCII account name.
        self.deny(e.encode_launch, t, b"a\x80b", ev)
        # LAUNCH: 64-byte account (overlength).
        self.deny(e.encode_launch, t,
                  b"a" + b"123456789012345678901234567890123456789012345678901234567890123", ev)

        # --- TO_D / FROM_D embedded frame errors ---
        # TO_D: embedded frame of a wrong type (ACK is not a B->D type).
        emb = e.encode_ack(t)
        self.deny(e.encode_to_d, t, emb)
        # TO_D: embedded length disagrees.
        emb = bytearray(e.encode_select_ok(t))
        struct.pack_into("<I", emb, 8, 77)
        self.deny(e.encode_to_d, t, bytes(emb))
        # TO_D: embedded frame below its type's bounds (INIT 255).
        emb = bytearray(e.encode_init(t, b":1.1", b"", b"", b""))
        struct.pack_into("<I", emb, 8, 255)
        self.deny(e.encode_to_d, t, bytes(emb[:255]))
        # TO_D: embedded frame above its type's bounds (INIT 2068).
        vo = b":" + b"1" * 252 + b"." + b"1"
        req = b"a\x00" * 260
        img = b"b\x00" * 260
        pub = b"c\x00" * 260
        emb = bytearray(e.encode_init(t, vo, req, img, pub))
        struct.pack_into("<I", emb, 8, 2068)
        emb.append(0)
        self.deny(e.encode_to_d, t, bytes(emb))
        # FROM_D: embedded frame of a wrong type (INIT is not a D->B type).
        emb = e.encode_init(t, b":1.1", b"", b"", b"")
        self.deny(e.encode_from_d, t, emb)
        # FROM_D: embedded length disagrees.
        emb = bytearray(e.encode_select(t, b"a"))
        struct.pack_into("<I", emb, 8, 82)
        self.deny(e.encode_from_d, t, bytes(emb))
        # FROM_D: embedded frame below its type's bounds (SELECT 80).
        emb = bytearray(e.encode_select(t, b"a"))
        struct.pack_into("<I", emb, 8, 80)
        self.deny(e.encode_from_d, t, bytes(emb[:80]))
        # FROM_D: embedded frame above its type's bounds (SELECT 144).
        emb = bytearray(e.encode_select(t, b"a"))
        struct.pack_into("<I", emb, 8, 144)
        emb.extend(bytes(144 - 81))
        self.deny(e.encode_from_d, t, bytes(emb))
        # TO_D decode: embedded frame of a wrong type.
        emb = e.encode_select_ok(t)
        data = bytearray(e.encode_to_d(t, emb))
        struct.pack_into("<H", data, 76 + 6, e.TYPE_ACK)
        self.deny(e.decode_to_d, bytes(data), t)
        # TO_D decode: embedded length disagrees.
        emb = e.encode_select_ok(t)
        data = bytearray(e.encode_to_d(t, emb))
        struct.pack_into("<I", data, 76 + 8, 77)
        self.deny(e.decode_to_d, bytes(data), t)

        # --- DECISION / NEXT_ATTEMPT header ids differ from the tuple's ---
        data = bytearray(e.encode_decision(t, 1, 0))
        data[28] = 0xff  # header nonce differs from the tuple's
        self.deny(e.decode_decision, bytes(data), t)
        data[28] = 0x10
        data = bytearray(e.encode_next_attempt(t2))
        data[28] = 0xff  # header nonce differs from the tuple's
        self.deny(e.decode_next_attempt, bytes(data), t2)
        data[28] = 0x10

        # --- NEXT_ATTEMPT with attempt 1 ---
        self.deny(e.encode_next_attempt, t)

        # --- GRANT with a zero key or zero reference ---
        self.deny(e.encode_grant, t, zero16, ev)
        self.deny(e.encode_grant, t, key, zero16)
        data = bytearray(e.encode_grant(t, key, ev))
        data[76:92] = zero16  # zero the account_key
        self.deny(e.decode_grant, bytes(data), t)
        data[76:92] = key
        data[92:108] = zero16  # zero the evidence_ref
        self.deny(e.decode_grant, bytes(data), t)

    def test_golden(self):
        golden_path = os.environ.get("E1R8_GOLDEN")
        if not golden_path:
            self.skipTest("E1R8_GOLDEN not set")
        t = golden_tuple()
        key = bytes(range(0xa0, 0xb0))
        ev = bytes(range(0xb0, 0xc0))
        chal = bytes(range(0xc0, 0xe0))
        pw = "pässword".encode("utf-8")
        req = "Caller".encode("utf-16-le")
        img = "x.exe".encode("utf-16-le")
        t2 = golden_tuple()
        t2.attempt = 2
        disp_req = b"a\x00" * 260
        disp_img = b"b\x00" * 260
        disp_pub = b"c\x00" * 260
        vo_max = b":" + b"1" * 252 + b"." + b"1"

        def check_init(data):
            in_ = e.decode_init(data, t)
            self.assertEqual(in_.verifier_owner, b":1.42")
            self.assertEqual(in_.requester, req)
            self.assertEqual(in_.image, img)
            self.assertEqual(in_.publisher, b"")
            self.assertEqual(in_.tuple, t)

        def check_ack(data):
            a = e.decode_ack(data, t)
            self.assertEqual(a.binding_digest, t.binding_digest)
            self.assertEqual(a.request_digest, t.request_digest)
            self.assertEqual(a.dialog_id, t.dialog_id)

        def check_select(data):
            s = e.decode_select(data, t)
            self.assertEqual(s.account, b"admin")

        def check_result(data):
            r = e.decode_result(data, t)
            self.assertEqual(r.status, 1)
            self.assertEqual(r.account, b"admin")
            self.assertEqual(r.challenge, chal)

        def check_answer(data):
            a = e.decode_answer(data, t, 0x3ff)
            self.assertEqual(a.answer_kind, 2)
            self.assertEqual(a.round, 2)
            self.assertEqual(a.message_index, 0)
            self.assertEqual(a.challenge, chal)

        def check_endorsed(data):
            en = e.decode_endorsed(data, t)
            self.assertEqual(en.pid, 4242)
            self.assertEqual(en.uid, 0)
            self.assertEqual(en.tuple, t)

        def check_start(data):
            s = e.decode_start(data, t)
            self.assertEqual(s.account, b"admin")
            self.assertEqual(s.account_key, key)

        def check_result_v(data):
            r = e.decode_result_v(data, t)
            self.assertEqual(r.status, 1)
            self.assertEqual(r.account, b"admin")
            self.assertEqual(r.account_key, key)
            self.assertEqual(r.evidence_ref, ev)

        def check_w_setup(data):
            w = e.decode_w_setup(data, t)
            self.assertEqual(w.pid, 4242)
            self.assertEqual(w.uid, 0)
            self.assertEqual(w.tuple, t)

        def check_secret(data):
            a = e.decode_secret_answer(data, t, 0x3ff)
            self.assertEqual(a.answer_kind, 1)
            self.assertEqual(a.round, 1)
            self.assertEqual(a.message_index, 0)
            self.assertEqual(a.payload, pw)

        def check_grant(data):
            g = e.decode_grant(data, t)
            self.assertEqual(g.account_key, key)
            self.assertEqual(g.evidence_ref, ev)

        def check_request_min(data):
            r = e.decode_request(data, t)
            self.assertEqual(r.kind, 1)
            self.assertEqual(r.secure_desktop, 1)
            self.assertEqual(r.deadline, t.deadline)
            self.assertEqual(r.binding_digest, t.binding_digest)
            self.assertEqual(r.request_digest, t.request_digest)
            self.assertEqual((r.root_gen, r.server_gen, r.broker_gen,
                              r.verifier_gen, r.map_gen, r.policy_gen),
                             (t.root_gen, t.server_gen, t.broker_gen,
                              t.verifier_gen, t.map_gen, t.policy_gen))
            self.assertEqual(r.desktop_route, t.desktop_route)
            self.assertEqual(r.requester, b"")
            self.assertEqual(r.image, b"")
            self.assertEqual(r.publisher, b"")

        def check_request_max(data):
            r = e.decode_request(data, t)
            self.assertEqual(r.kind, 2)
            self.assertEqual(r.secure_desktop, 0)
            self.assertEqual(len(r.requester), 520)
            self.assertEqual(len(r.image), 520)
            self.assertEqual(len(r.publisher), 520)

        def check_decision_deny(data):
            d = e.decode_decision(data, t)
            self.assertEqual(d.status, 2)
            self.assertEqual(d.reason, 1)

        def check_decision_prompt(data):
            d = e.decode_decision(data, t)
            self.assertEqual(d.status, 1)
            self.assertEqual(d.tuple, t)

        def check_endorsed_local(data):
            el = e.decode_endorsed_local(data, t)
            self.assertEqual(el.status, 1)
            self.assertEqual(el.dialog_id, t.dialog_id)

        def check_to_d_min(data):
            td = e.decode_to_d(data, t)
            self.assertEqual(td.type, e.TYPE_SELECT_OK)
            self.assertEqual(td.frame_len, 76)

        def check_to_d_max(data):
            td = e.decode_to_d(data, t)
            self.assertEqual(td.type, e.TYPE_INIT)
            self.assertEqual(td.frame_len, 2067)

        def check_from_d_min(data):
            fd = e.decode_from_d(data, t)
            self.assertEqual(fd.type, e.TYPE_SELECT)
            self.assertEqual(fd.frame_len, 81)

        def check_from_d_max(data):
            fd = e.decode_from_d(data, t)
            self.assertEqual(fd.type, e.TYPE_ANSWER)
            self.assertEqual(fd.frame_len, 264)

        def check_launch_min(data):
            l = e.decode_launch(data, t)
            self.assertEqual(l.account, b"a")
            self.assertEqual(l.evidence_ref, ev)

        def check_launch_max(data):
            l = e.decode_launch(data, t)
            self.assertEqual(len(l.account), 63)

        def check_next_attempt(data):
            na = e.decode_next_attempt(data, t2)
            self.assertEqual(na.tuple, t2)

        cases = [
            ("1", lambda: e.encode_init(t, b":1.42", req, img, b""), check_init),
            ("2", lambda: e.encode_ack(t), check_ack),
            ("3", lambda: e.encode_select_ok(t), lambda d: e.decode_select_ok(d, t)),
            ("4", lambda: e.encode_select(t, b"admin"), check_select),
            ("5", lambda: e.encode_ready(t), lambda d: e.decode_ready(d, t)),
            ("6", lambda: e.encode_result(t, 1, b"admin", chal), check_result),
            ("7", lambda: e.encode_answer(t, chal, 2, 2, 0, 0x3ff), check_answer),
            ("8", lambda: e.encode_done(t, 1), lambda d: self.assertEqual(e.decode_done(d, t).status, 1)),
            ("9", lambda: e.encode_attach_ok(t), lambda d: e.decode_attach_ok(d, t)),
            ("16", lambda: e.encode_endorsed(t, 4242, 0), check_endorsed),
            ("17", lambda: e.encode_start(t, b"admin", key), check_start),
            ("18", lambda: e.encode_worker_ready(t), lambda d: e.decode_worker_ready(d, t)),
            ("19", lambda: e.encode_delegated(t), lambda d: e.decode_delegated(d, t)),
            ("20", lambda: e.encode_result_v(t, 1, key, b"admin", ev), check_result_v),
            ("21", lambda: e.encode_cancel(t, 1), lambda d: self.assertEqual(e.decode_cancel(d, t).reason, 1)),
            ("22", lambda: e.encode_w_ack(t), lambda d: e.decode_w_ack(d, t)),
            ("23", lambda: e.encode_attach_ack(t), lambda d: e.decode_attach_ack(d, t)),
            ("24", lambda: e.encode_prompt_release(t), lambda d: e.decode_prompt_release(d, t)),
            ("25", lambda: e.encode_release(t, 1), lambda d: self.assertEqual(e.decode_release(d, t).outcome, 1)),
            ("26", lambda: e.encode_released(t), lambda d: e.decode_released(d, t)),
            ("27", lambda: e.encode_w_setup(t, 4242, 0), check_w_setup),
            ("s7", lambda: e.encode_s7_prompt(t), lambda d: e.decode_s7_prompt(d, t)),
            ("secret", lambda: e.encode_secret_answer(t, 0x3ff, pw), check_secret),
            ("32", lambda: e.encode_grant(t, key, ev), check_grant),
            ("33", lambda: e.encode_granted(t), lambda d: e.decode_granted(d, t)),
            ("40", lambda: e.encode_request(t, 1, 1, b"", b"", b""),
             check_request_min),
            ("40", lambda: e.encode_request(t, 2, 0, disp_req, disp_img,
                                            disp_pub), check_request_max),
            ("41", lambda: e.encode_decision(t, 2, 1), check_decision_deny),
            ("41", lambda: e.encode_decision(t, 1, 0), check_decision_prompt),
            ("42", lambda: e.encode_endorsed_local(t, t.dialog_id, 1),
             check_endorsed_local),
            ("43", lambda: e.encode_to_d(t, e.encode_select_ok(t)), check_to_d_min),
            ("43", lambda: e.encode_to_d(t, e.encode_init(t, vo_max, disp_req,
                                                          disp_img, disp_pub)),
             check_to_d_max),
            ("44", lambda: e.encode_from_d(t, e.encode_select(t, b"a")),
             check_from_d_min),
            ("44", lambda: e.encode_from_d(t, e.encode_answer(t, t.binding_digest,
                                                              2, 2, 0, 0x3ff)),
             check_from_d_max),
            ("45", lambda: e.encode_recheck(t, 1),
             lambda d: self.assertEqual(e.decode_recheck(d, t).step, 1)),
            ("46", lambda: e.encode_rechecked(t, 1, 1),
             lambda d: self.assertEqual(e.decode_rechecked(d, t).step, 1)),
            ("47", lambda: e.encode_launch(t, b"a", ev), check_launch_min),
            ("47", lambda: e.encode_launch(t, acct63(), ev), check_launch_max),
            ("48", lambda: e.encode_launched(t, 1, 0),
             lambda d: self.assertEqual(e.decode_launched(d, t).status, 1)),
            ("49", lambda: e.encode_cancel_r9(t, 1),
             lambda d: self.assertEqual(e.decode_cancel_r9(d, t).reason, 1)),
            ("50", lambda: e.encode_end(t, 1),
             lambda d: self.assertEqual(e.decode_end(d, t).outcome, 1)),
            ("51", lambda: e.encode_next_attempt(t2), check_next_attempt),
        ]

        with open(golden_path, "r") as f:
            lines = f.read().splitlines()
        self.assertEqual(len(lines), len(cases))
        for line, (typ, enc, dec) in zip(lines, cases):
            got_typ, hexs = line.split(" ", 1)
            self.assertEqual(got_typ, typ)
            want = bytes.fromhex(hexs)
            got = enc()
            self.assertEqual(got, want, "encode mismatch for type %s" % typ)
            dec(want)


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(E1R8Tests)
    result = unittest.TestResult()
    suite.run(result)
    print("E1R8_CODEC cases=%d failures=%d errors=%d skipped=%d" %
          (result.testsRun, len(result.failures), len(result.errors),
           len(result.skipped)))
    for test, _ in result.failures + result.errors:
        print("FAIL " + test.id())
    raise SystemExit(0 if result.wasSuccessful() else 1)
