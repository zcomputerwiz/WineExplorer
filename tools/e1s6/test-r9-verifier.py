#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Unit tests for E1-S18e R9 lab verifier (WO R9-2).

Tests attempt state logic, channel-kind classification and gating, binding,
and GRANT/REDEEM/CANCEL transitions for both R9-enabled and R9-off paths.
Plain python3, no Wine or D-Bus daemon required.
"""
import importlib.util
import os
from pathlib import Path
import struct
import sys
import time
import unittest

sys.dont_write_bytecode = True

_HERE = Path(__file__).resolve().parent
if str(_HERE) not in sys.path:
    sys.path.insert(0, str(_HERE))

import e1r8

_spec = importlib.util.spec_from_file_location("e1s6_lab", _HERE / "e1s6-lab.py")
lab = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(lab)


def make_attempt(request_id=0x12345678, attempt=1, state="RESULT_RETAINED",
                 account=b"admin", account_key=b"K" * 16, evidence_ref=b"E" * 16,
                 evidence_record=b"R" * 248, channel_id=b"C" * 16,
                 deadline_offset_ns=10_000_000_000):
    now = time.monotonic_ns()
    t = e1r8.E1R8Tuple(
        request_id=request_id,
        attempt=attempt,
        nonce=b"N" * 32,
        channel_id=channel_id,
        binding_digest=b"B" * 32,
        request_digest=b"D" * 32,
        deadline=now + deadline_offset_ns,
        root_gen=101,
        server_gen=102,
        broker_gen=103,
        verifier_gen=104,
        map_gen=105,
        policy_gen=106,
        caller_session=1,
        desktop_route=b"R" * 16,
        desktop_mode=e1r8.DESKTOP_MODE_SECURE,
        dialog_id=b"D" * 16,
    )
    return {
        "tuple": t,
        "state": state,
        "account": account,
        "account_key": account_key,
        "evidence_ref": evidence_ref,
        "evidence": evidence_record,
        "pid": 4321,
        "uid": 1000,
    }


class TestR9Verifier(unittest.TestCase):

    def test_config_s0_user_resolution(self):
        """Configuration: WEX_E1S6_LAB_S0_USER resolved to uid; missing is error."""
        # Missing / empty user name
        with self.assertRaises(ValueError):
            lab.r9_resolve_s0_user("")
        with self.assertRaises(ValueError):
            lab.r9_resolve_s0_user(None)

        # Numeric string resolves to that uid
        self.assertEqual(lab.r9_resolve_s0_user("0"), 0)
        self.assertEqual(lab.r9_resolve_s0_user("1000"), 1000)

        # Root user always exists on unix systems
        root_uid = lab.r9_resolve_s0_user("root")
        self.assertEqual(root_uid, 0)

        # Non-existent user name raises error
        with self.assertRaises(ValueError):
            lab.r9_resolve_s0_user("nonexistent_user_xyz_12345")

    def test_channel_classification(self):
        """Endorser kinds: S0 uid gives B0 channel; session user gives A channel."""
        system_gid = 100
        s0_uid = 105
        client_uid = 1000

        # R9 on: S0 endorses -> B0 channel
        kind = lab.r9_classify_channel(True, s0_uid, client_uid, s0_uid, system_gid, system_gid)
        self.assertEqual(kind, lab.CHANNEL_KIND_B0)

        # R9 on: session user endorses -> A channel
        kind = lab.r9_classify_channel(True, s0_uid, client_uid, client_uid, system_gid, system_gid)
        self.assertEqual(kind, lab.CHANNEL_KIND_A)

        # R9 on: foreign uid -> refused
        with self.assertRaises(ValueError):
            lab.r9_classify_channel(True, s0_uid, client_uid, 9999, system_gid, system_gid)

        # R9 on: wrong gid -> refused
        with self.assertRaises(ValueError):
            lab.r9_classify_channel(True, s0_uid, client_uid, s0_uid, 999, system_gid)

        # R9 off: session user -> A channel
        kind = lab.r9_classify_channel(False, s0_uid, client_uid, client_uid, system_gid, system_gid)
        self.assertEqual(kind, lab.CHANNEL_KIND_A)

        # R9 off: S0 endorser is refused when client is different
        with self.assertRaises(ValueError):
            lab.r9_classify_channel(False, s0_uid, client_uid, s0_uid, system_gid, system_gid)

    def test_message_gating(self):
        """Message gating: START, CANCEL, RELEASE, GRANT on B0; REDEEM on A."""
        # R9 on, B0 channel
        for allowed in (17, 21, 25, 32):  # START, CANCEL, RELEASE, GRANT
            self.assertTrue(lab.r9_gate_message(True, lab.CHANNEL_KIND_B0, allowed))
        for refused in (28, 1, 2, 40):  # REDEEM, INIT, ACK, REQUEST
            self.assertFalse(lab.r9_gate_message(True, lab.CHANNEL_KIND_B0, refused))

        # R9 on, A channel
        self.assertTrue(lab.r9_gate_message(True, lab.CHANNEL_KIND_A, 28))  # REDEEM
        for refused in (17, 21, 25, 32, 1, 2):  # START, CANCEL, RELEASE, GRANT
            self.assertFalse(lab.r9_gate_message(True, lab.CHANNEL_KIND_A, refused))

        # R9 off: legacy broker channel accepts START, RELEASE, REDEEM
        for allowed in (17, 25, 28):
            self.assertTrue(lab.r9_gate_message(False, lab.CHANNEL_KIND_A, allowed))
        # GRANT (32) and CANCEL (21) not accepted in legacy
        self.assertFalse(lab.r9_gate_message(False, lab.CHANNEL_KIND_A, 32))
        self.assertFalse(lab.r9_gate_message(False, lab.CHANNEL_KIND_A, 21))

    def test_grant_in_each_state_other_than_result_retained_refused(self):
        """1. GRANT in each state other than RESULT_RETAINED (refused)."""
        other_states = [
            "ENDORSED",
            "STARTED",
            "WORKER_READY",
            "ATTACH_RESERVED",
            "DELEGATED",
            "PAM",
            "GRANTED",
            "REDEEMED",
            "CANCELLED",
            "RETIRED",
            "REFUSED",
        ]
        grant_msg = e1r8.Grant(account_key=b"K" * 16, evidence_ref=b"E" * 16)
        for st in other_states:
            with self.subTest(state=st):
                rec = make_attempt(state=st, account_key=b"K" * 16, evidence_ref=b"E" * 16)
                ok, new_state, reply, err = lab.r9_handle_grant(rec, lab.CHANNEL_KIND_B0, grant_msg, r9_enabled=True)
                self.assertFalse(ok)
                self.assertEqual(new_state, st)
                self.assertEqual(rec["state"], st)
                self.assertIsNone(reply)

    def test_grant_with_different_account_key(self):
        """2. GRANT with a different account key (refused, state unchanged)."""
        rec = make_attempt(state="RESULT_RETAINED", account_key=b"K" * 16, evidence_ref=b"E" * 16)
        grant_msg = e1r8.Grant(account_key=b"X" * 16, evidence_ref=b"E" * 16)
        ok, new_state, reply, err = lab.r9_handle_grant(rec, lab.CHANNEL_KIND_B0, grant_msg, r9_enabled=True)
        self.assertFalse(ok)
        self.assertEqual(new_state, "RESULT_RETAINED")
        self.assertEqual(rec["state"], "RESULT_RETAINED")
        self.assertIsNone(reply)

    def test_grant_with_different_evidence_reference(self):
        """3. GRANT with a different evidence reference (refused, state unchanged)."""
        rec = make_attempt(state="RESULT_RETAINED", account_key=b"K" * 16, evidence_ref=b"E" * 16)
        grant_msg = e1r8.Grant(account_key=b"K" * 16, evidence_ref=b"X" * 16)
        ok, new_state, reply, err = lab.r9_handle_grant(rec, lab.CHANNEL_KIND_B0, grant_msg, r9_enabled=True)
        self.assertFalse(ok)
        self.assertEqual(new_state, "RESULT_RETAINED")
        self.assertEqual(rec["state"], "RESULT_RETAINED")
        self.assertIsNone(reply)

    def test_grant_twice(self):
        """4. GRANT twice: first succeeds, second refused, state stays GRANTED."""
        rec = make_attempt(state="RESULT_RETAINED", account_key=b"K" * 16, evidence_ref=b"E" * 16)
        grant_msg = e1r8.Grant(account_key=b"K" * 16, evidence_ref=b"E" * 16)

        # First GRANT succeeds
        ok, new_state, reply, err = lab.r9_handle_grant(rec, lab.CHANNEL_KIND_B0, grant_msg, r9_enabled=True)
        self.assertTrue(ok)
        self.assertEqual(new_state, "GRANTED")
        self.assertEqual(rec["state"], "GRANTED")
        self.assertEqual(reply, e1r8.encode_granted(rec["tuple"]))

        # Second GRANT refused
        ok2, new_state2, reply2, err2 = lab.r9_handle_grant(rec, lab.CHANNEL_KIND_B0, grant_msg, r9_enabled=True)
        self.assertFalse(ok2)
        self.assertEqual(new_state2, "GRANTED")
        self.assertEqual(rec["state"], "GRANTED")
        self.assertIsNone(reply2)

    def test_grant_on_a_channel(self):
        """5. GRANT on an A channel (refused, state unchanged)."""
        rec = make_attempt(state="RESULT_RETAINED", account_key=b"K" * 16, evidence_ref=b"E" * 16)
        grant_msg = e1r8.Grant(account_key=b"K" * 16, evidence_ref=b"E" * 16)
        ok, new_state, reply, err = lab.r9_handle_grant(rec, lab.CHANNEL_KIND_A, grant_msg, r9_enabled=True)
        self.assertFalse(ok)
        self.assertEqual(new_state, "RESULT_RETAINED")
        self.assertEqual(rec["state"], "RESULT_RETAINED")
        self.assertIsNone(reply)

    def test_redeem_without_grant(self):
        """6. REDEEM without GRANT (in RESULT_RETAINED with R9 on, refused)."""
        rec = make_attempt(state="RESULT_RETAINED", evidence_ref=b"E" * 16)
        now = time.monotonic_ns()
        ok, new_state, reply, err = lab.r9_handle_redeem(
            rec, lab.CHANNEL_KIND_A, b"E" * 16, now, r9_enabled=True)
        self.assertFalse(ok)
        self.assertIsNone(reply)

    def test_redeem_twice(self):
        """7. REDEEM twice: first succeeds, second refused."""
        rec = make_attempt(state="GRANTED", evidence_ref=b"E" * 16, evidence_record=b"X" * 248)
        now = time.monotonic_ns()

        # First REDEEM succeeds
        ok, new_state, reply, err = lab.r9_handle_redeem(
            rec, lab.CHANNEL_KIND_A, b"E" * 16, now, r9_enabled=True)
        self.assertTrue(ok)
        self.assertEqual(new_state, "REDEEMED")
        self.assertEqual(rec["state"], "REDEEMED")
        self.assertIsNotNone(reply)

        # Second REDEEM refused
        ok2, new_state2, reply2, err2 = lab.r9_handle_redeem(
            rec, lab.CHANNEL_KIND_A, b"E" * 16, now, r9_enabled=True)
        self.assertFalse(ok2)
        self.assertIsNone(reply2)

    def test_redeem_on_b0_channel(self):
        """8. REDEEM on the B0 channel (refused)."""
        rec = make_attempt(state="GRANTED", evidence_ref=b"E" * 16)
        now = time.monotonic_ns()
        ok, new_state, reply, err = lab.r9_handle_redeem(
            rec, lab.CHANNEL_KIND_B0, b"E" * 16, now, r9_enabled=True)
        self.assertFalse(ok)
        self.assertIsNone(reply)

    def test_redeem_on_another_a_channel(self):
        """9. REDEEM on another A channel (attempt not in channel records, refused)."""
        now = time.monotonic_ns()
        # Another A channel looking up an attempt it does not own gets None
        ok, new_state, reply, err = lab.r9_handle_redeem(
            None, lab.CHANNEL_KIND_A, b"E" * 16, now, r9_enabled=True)
        self.assertFalse(ok)
        self.assertIsNone(reply)

    def test_cancel_in_granted_then_redeem(self):
        """10. CANCEL in GRANTED then REDEEM (refused, no receipt)."""
        rec = make_attempt(state="GRANTED", evidence_ref=b"E" * 16, evidence_record=b"X" * 248)
        cancel_msg = e1r8.Cancel(reason=e1r8.CANCEL_REASON)

        # CANCEL moves GRANTED -> CANCELLED, wipes evidence
        ok, new_state, err = lab.r9_handle_cancel(rec, lab.CHANNEL_KIND_B0, cancel_msg, r9_enabled=True)
        self.assertTrue(ok)
        self.assertEqual(new_state, "CANCELLED")
        self.assertEqual(rec["state"], "CANCELLED")
        self.assertIsNone(rec.get("evidence"))
        self.assertIsNone(rec.get("evidence_ref"))

        # Subsequent REDEEM is refused
        now = time.monotonic_ns()
        ok2, new_state2, reply2, err2 = lab.r9_handle_redeem(
            rec, lab.CHANNEL_KIND_A, b"E" * 16, now, r9_enabled=True)
        self.assertFalse(ok2)
        self.assertIsNone(reply2)

    def test_cancel_after_redeemed(self):
        """11. CANCEL after REDEEMED (state stays REDEEMED)."""
        rec = make_attempt(state="REDEEMED")
        cancel_msg = e1r8.Cancel(reason=e1r8.CANCEL_REASON)
        ok, new_state, err = lab.r9_handle_cancel(rec, lab.CHANNEL_KIND_B0, cancel_msg, r9_enabled=True)
        self.assertTrue(ok)
        self.assertEqual(new_state, "REDEEMED")
        self.assertEqual(rec["state"], "REDEEMED")
        self.assertTrue(rec.get("cancelled"))

    def test_start_on_a_channel(self):
        """12. START on an A channel (refused)."""
        rec = make_attempt(state="ENDORSED", channel_id=b"A" * 16)
        ok, new_state, err = lab.r9_handle_start(
            rec, lab.CHANNEL_KIND_A, b"A" * 16, True, lab.CHANNEL_KIND_A, True, b"A" * 16, r9_enabled=True)
        self.assertFalse(ok)
        self.assertEqual(new_state, "ENDORSED")

    def test_start_naming_channel_not_live_a_channel(self):
        """13. START naming a channel id that is not a live A channel (refused)."""
        rec = make_attempt(state="ENDORSED", channel_id=b"A" * 16)

        # Case A: channel does not exist
        ok, new_state, err = lab.r9_handle_start(
            rec, lab.CHANNEL_KIND_B0, b"A" * 16, False, lab.CHANNEL_KIND_A, True, b"A" * 16, r9_enabled=True)
        self.assertFalse(ok)

        # Case B: channel is a B0 channel, not an A channel
        ok, new_state, err = lab.r9_handle_start(
            rec, lab.CHANNEL_KIND_B0, b"A" * 16, True, lab.CHANNEL_KIND_B0, True, b"A" * 16, r9_enabled=True)
        self.assertFalse(ok)

        # Case C: channel not endorsed by session server
        ok, new_state, err = lab.r9_handle_start(
            rec, lab.CHANNEL_KIND_B0, b"A" * 16, True, lab.CHANNEL_KIND_A, False, b"A" * 16, r9_enabled=True)
        self.assertFalse(ok)

        # Case D: channel id mismatch
        ok, new_state, err = lab.r9_handle_start(
            rec, lab.CHANNEL_KIND_B0, b"X" * 16, True, lab.CHANNEL_KIND_A, True, b"A" * 16, r9_enabled=True)
        self.assertFalse(ok)

        # Valid binding succeeds
        ok, new_state, err = lab.r9_handle_start(
            rec, lab.CHANNEL_KIND_B0, b"A" * 16, True, lab.CHANNEL_KIND_A, True, b"A" * 16, r9_enabled=True)
        self.assertTrue(ok)
        self.assertEqual(new_state, "STARTED")

    def test_r9_off_redeem_in_result_retained_and_grant_refused(self):
        """14. R9 off: REDEEM in RESULT_RETAINED still works and GRANT is refused as unknown."""
        # REDEEM in RESULT_RETAINED works with R9 off
        rec = make_attempt(state="RESULT_RETAINED", evidence_ref=b"E" * 16, evidence_record=b"X" * 248)
        now = time.monotonic_ns()
        ok, new_state, reply, err = lab.r9_handle_redeem(
            rec, lab.CHANNEL_KIND_A, b"E" * 16, now, r9_enabled=False)
        self.assertTrue(ok)
        self.assertEqual(new_state, "REDEEMED")
        self.assertIsNotNone(reply)

        # GRANT refused as unknown type with R9 off
        rec2 = make_attempt(state="RESULT_RETAINED", account_key=b"K" * 16, evidence_ref=b"E" * 16)
        grant_msg = e1r8.Grant(account_key=b"K" * 16, evidence_ref=b"E" * 16)
        ok2, new_state2, reply2, err2 = lab.r9_handle_grant(
            rec2, lab.CHANNEL_KIND_B0, grant_msg, r9_enabled=False)
        self.assertFalse(ok2)
        self.assertEqual(new_state2, "RESULT_RETAINED")
        self.assertEqual(err2, "grant-r9-disabled-unknown-type")
        self.assertIsNone(reply2)

    def test_r9_full_happy_path(self):
        """Complete R9 flow: ENDORSED -> STARTED -> RESULT_RETAINED -> GRANTED -> REDEEMED."""
        rec = make_attempt(state="ENDORSED", account_key=b"K" * 16, evidence_ref=b"E" * 16,
                           evidence_record=b"X" * 248, channel_id=b"A" * 16)
        # 1. START on B0
        ok, state, _ = lab.r9_handle_start(
            rec, lab.CHANNEL_KIND_B0, b"A" * 16, True, lab.CHANNEL_KIND_A, True, b"A" * 16, r9_enabled=True)
        self.assertTrue(ok)
        self.assertEqual(state, "STARTED")

        # Worker finishes PAM -> RESULT_RETAINED
        rec["state"] = "RESULT_RETAINED"

        # 2. GRANT on B0
        grant_msg = e1r8.Grant(account_key=b"K" * 16, evidence_ref=b"E" * 16)
        ok, state, reply, _ = lab.r9_handle_grant(rec, lab.CHANNEL_KIND_B0, grant_msg, r9_enabled=True)
        self.assertTrue(ok)
        self.assertEqual(state, "GRANTED")
        self.assertEqual(reply, e1r8.encode_granted(rec["tuple"]))

        # 3. REDEEM on A
        now = time.monotonic_ns()
        ok, state, reply, _ = lab.r9_handle_redeem(
            rec, lab.CHANNEL_KIND_A, b"E" * 16, now, r9_enabled=True)
        self.assertTrue(ok)
        self.assertEqual(state, "REDEEMED")
        self.assertIsNotNone(reply)


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(TestR9Verifier)
    runner = unittest.TextTestRunner(stream=sys.stdout, verbosity=2)
    result = runner.run(suite)
    print("R9_VERIFIER cases=%d failures=%d errors=%d" %
          (result.testsRun, len(result.failures), len(result.errors)), flush=True)
    if not result.wasSuccessful():
        sys.exit(1)
    print("R9-2-OK", flush=True)
