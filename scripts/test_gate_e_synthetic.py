#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Gate E: Responsiveness, truthful failure, and safe recovery verification."""
import hashlib
import json
import os
import socket
import struct
import sys
import tempfile
import threading
import time
from pathlib import Path
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent
sys.path.insert(0, str(HERE))

# Load wex-components
import importlib.util
spec = importlib.util.spec_from_file_location("components", HERE / "wex-components.py")
components = importlib.util.module_from_spec(spec)
spec.loader.exec_module(components)

def log(msg):
    print(f"[Gate E] {msg}", flush=True)

class GateETests:
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="wex-gate-e-")
        self.root = Path(self.tmp.name)
        self.stage_file = self.root / "stage"
        self.stage_file.write_text("oobe\n")
        self.sock_path = self.root / "coordinator.sock"
        self.ops_dir = self.root / "component-operations"
        self.ops_dir.mkdir(mode=0o700, parents=True)
        self.receipts_dir = self.root / "component-receipts"
        self.receipts_dir.mkdir(parents=True)
        self.machine_dir = self.root / "machine"
        self.machine_dir.mkdir(parents=True)
        
        self.env_patch = patch.dict(os.environ, {
            'WEX_OOBE_STAGE_FILE': str(self.stage_file),
            'WEX_OOBE_SOCKET': str(self.sock_path),
            'WEX_COMPONENTS_DIR': str(self.receipts_dir),
            'WEX_MACHINE_PREFIX': str(self.machine_dir)
        })
        self.env_patch.start()
        
        self.ops_patch = patch.object(components, 'OPERATION_STATE', self.ops_dir)
        self.ops_patch.start()
        
        class FakePwd:
            pw_uid = 999
        self.pwd_patch = patch.object(components.pwd, 'getpwnam', return_value=FakePwd())
        self.pwd_patch.start()
        
    def tearDown(self):
        self.pwd_patch.stop()
        self.ops_patch.stop()
        self.env_patch.stop()
        self.tmp.cleanup()

    def start_coordinator(self):
        self.coord_thread = threading.Thread(target=components.oobe_coordinator, daemon=True)
        self.coord_thread.start()
        for _ in range(50):
            if self.sock_path.exists():
                break
            time.sleep(0.05)
        if not self.sock_path.exists():
            raise RuntimeError("Coordinator did not create socket")

    def send(self, line):
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
            s.settimeout(5.0)
            s.connect(str(self.sock_path))
            s.sendall(line.encode("utf-8") + b"\n")
            return s.recv(4096).decode("utf-8")

    def test_cancellation_deferred_during_running_msi(self):
        log("--- Test 1: Cancellation deferred during running installer ---")
        evt = threading.Event()
        def mock_install(v, s):
            log("Mock install started (simulating running MSI)...")
            evt.wait(timeout=5)
            log("Mock install finished safely.")

        fake_sha = components.COMPONENTS_METADATA['vcrun2022']['terms_text_sha256']
        with patch.object(components, 'license_text', return_value=('terms', fake_sha)), \
             patch.object(components, 'install', side_effect=mock_install):
            self.start_coordinator()
            
            # Start install
            resp = self.send(f"START_INSTALL vcrun2022 {fake_sha}")
            log(f"START_INSTALL -> {resp.strip()}")
            assert resp.startswith("OK op_vcrun2022_"), f"Expected OK op, got {resp}"
            op_id = resp.strip().split()[1]
            
            # Request cancellation while installer is running
            log("Sending CANCEL_INSTALL while installer is running...")
            cancel_resp = self.send(f"CANCEL_INSTALL {op_id}")
            log(f"CANCEL_INSTALL -> {cancel_resp.strip()}")
            assert "OK DEFERRED" in cancel_resp, f"Expected DEFERRED, got {cancel_resp}"
            assert "waiting for safe completion" in cancel_resp
            
            # Check query status: must remain RUNNING (not falsely rolled back or aborted early)
            q_resp = self.send(f"QUERY_INSTALL {op_id}")
            log(f"QUERY_INSTALL during deferred cancel -> {q_resp.strip()}")
            assert q_resp.strip() == "STATUS RUNNING", f"Expected STATUS RUNNING, got {q_resp}"
            
            # Duplicate install attempt must be rejected/deduplicated to existing op
            dup_resp = self.send(f"START_INSTALL vcrun2022 {fake_sha}")
            log(f"Duplicate START_INSTALL -> {dup_resp.strip()}")
            assert op_id in dup_resp
            
            # Let installer finish safely
            evt.set()
            time.sleep(0.2)
            
            # Final status
            final_q = self.send(f"QUERY_INSTALL {op_id}")
            log(f"Final QUERY_INSTALL after safe completion -> {final_q.strip()}")
            assert final_q.strip() == "STATUS SUCCESS"
            log("PASS: Cancellation safely deferred without premature rollback or corruption.")

    def test_interrupted_running_journal_becomes_unknown_and_requires_reconciliation(self):
        log("--- Test 2: Interrupted RUNNING journal becomes UNKNOWN on recovery ---")
        journal = self.ops_dir / "operations.json"
        stale_op_id = "op_vcrun2022_interrupted123"
        fake_sha = components.COMPONENTS_METADATA['vcrun2022']['terms_text_sha256']
        
        # Simulate ungraceful crash during RUNNING state
        journal.write_text(json.dumps({
            stale_op_id: {
                'verb': 'vcrun2022',
                'sha': fake_sha,
                'status': 'RUNNING',
                'error': '',
                'cancelled': False
            }
        }))
        os.chmod(journal, 0o600)
        log("Simulated ungraceful crash with RUNNING journal.")
        
        self.start_coordinator()
        
        # Query the interrupted op: coordinator must report UNKNOWN rather than guessing SUCCESS or FAILED
        q_resp = self.send(f"QUERY_INSTALL {stale_op_id}")
        log(f"QUERY_INSTALL interrupted op -> {q_resp.strip()}")
        assert "STATUS UNKNOWN" in q_resp, f"Expected STATUS UNKNOWN, got {q_resp}"
        assert "administrator reconciliation required" in q_resp
        
        # Retry must be blocked because an interrupted operation requires administrator reconciliation
        with patch.object(components, 'license_text', return_value=('terms', fake_sha)):
            retry_resp = self.send(f"START_INSTALL vcrun2022 {fake_sha}")
            log(f"START_INSTALL during UNKNOWN state -> {retry_resp.strip()}")
            assert "ERR" in retry_resp and "reconciliation" in retry_resp, f"Expected reconciliation error, got {retry_resp}"
        log("PASS: Interrupted RUNNING journal safely transitions to UNKNOWN and blocks overlapping retry.")

    def test_admin_reconciliation_workflow(self):
        log("--- Test 3: Administrator reconciliation workflow ---")
        journal = self.ops_dir / "operations.json"
        op_id = "op_vcrun2022_reconcile_test"
        fake_sha = components.COMPONENTS_METADATA['vcrun2022']['terms_text_sha256']
        
        # 1. State requires reconciliation
        journal.write_text(json.dumps({
            op_id: {
                'verb': 'vcrun2022',
                'sha': fake_sha,
                'status': 'UNKNOWN',
                'error': 'Power loss during installation',
                'cancelled': False
            }
        }))
        os.chmod(journal, 0o600)
        self.start_coordinator()
        
        # Blocked
        with patch.object(components, 'license_text', return_value=('terms', fake_sha)):
            assert "reconciliation" in self.send(f"START_INSTALL vcrun2022 {fake_sha}")
        
        # 2. Administrator reconciles:
        # Step A: Establish idleness (no wineserver or worker running)
        # Step B: Inspect evidence and reconcile journal
        data = json.loads(journal.read_text())
        data[op_id]['status'] = 'FAILED'
        data[op_id]['error'] = 'Administrator reconciled: aborted after inspection'
        journal.write_text(json.dumps(data))
        os.chmod(journal, 0o600)
        log("Administrator inspected system and reconciled operation record to FAILED.")
        
        # Stop and restart coordinator after reconciliation
        self.stage_file.write_text("done\n")
        time.sleep(0.2)
        self.stage_file.write_text("oobe\n")
        self.start_coordinator()
        
        # 3. Retry now permitted!
        with patch.object(components, 'license_text', return_value=('terms', fake_sha)), \
             patch.object(components, 'install'):
            new_resp = self.send(f"START_INSTALL vcrun2022 {fake_sha}")
            log(f"START_INSTALL after reconciliation -> {new_resp.strip()}")
            assert new_resp.startswith("OK op_vcrun2022_"), f"Expected retry success, got {new_resp}"
        log("PASS: Administrator reconciliation workflow verified.")

def run():
    suite = GateETests()
    for name in ("test_cancellation_deferred_during_running_msi",
                 "test_interrupted_running_journal_becomes_unknown_and_requires_reconciliation",
                 "test_admin_reconciliation_workflow"):
        suite.setUp()
        try:
            getattr(suite, name)()
        finally:
            suite.tearDown()
    print("\nALL GATE E SYNTHETIC RECOVERY CHECKS PASSED!", flush=True)

if __name__ == "__main__":
    run()
