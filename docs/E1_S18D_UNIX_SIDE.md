<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# E1-S18d: Broker Stage 2 Unix-Side Interfaces Design

Technical design, wire protocol, trust boundaries, failure handling, and test plan for Work Package E1-S18d ("Broker Stage 2 Evidence Redemption Unix-Side Interfaces").
Following [E1 decisions](E1_DECISIONS.md), [S1 contracts](E1_S1_CONTRACTS.md), [S4 accounts map](E1_S4_ACCOUNTS_MAP.md), [S6 transport](E1_S6_TRANSPORT.md), [S7 PAM worker](E1_S7_PAM_WORKER.md), [S8 evidence](E1_S8_EVIDENCE.md), [S12 profile storage](E1_S12_PROFILE_STORAGE.md), [S13 HKU load](E1_S13_HKU_LOAD.md), [S14 leases](E1_S14_LEASES.md), [S17 credential dialog](E1_S17_CREDENTIAL_DIALOG.md), and [S18 broker integration](E1_S18_BROKER_INTEGRATION.md).

---

## 1. Executive Summary & Missing Gaps

In E1-S18c, the standard-user elevation branch of `wexelevate` (broker) wired Stages 1..13 against NTDLL export boundaries and boundary tables. However, default live Stage 2 (`e1s18_transport_redeem_live`) was forced to fail closed with `ERROR_NOT_SUPPORTED` (50) due to four missing Unix-side interfaces:

1. **Missing Interface 1 (S8 REDEEM Transport Opcode in `wexhost`)**:
   `WexE1s6Transport` in `src/dlls/wexhost/e1s6.c` and `src/include/e1s6_protocol.h` only implemented operations 1..4 (`E1S6_OPEN`, `E1S6_CHILD_START`, `E1S6_EXCHANGE`, `E1S6_CLOSE`) plus test injections 5..8. There was no transport opcode to submit the 252-byte S8 REDEEM packet or receive the 240-byte redemption response.
2. **Missing Interface 2 (S8 Service Endpoint in `tools/e1s6/e1s6-lab.py`)**:
   `e1s6-lab.py` previously contained a stub comment declaring no evidence redemption or token creation lived there. It did not instantiate `evidence.py`'s `Service` or per-connection `Connection`, nor wire packets to `connection.serve()`.
3. **Missing Interface 3 (S7 PAM Worker Integration into S8 Evidence Service)**:
   `tools/e1s7/pam-worker.c` existed with its supervisor, but `e1s6-lab.py` did not invoke the worker to perform real PAM checks under fixed PAM service `wex-e1s-test` (lab) / `wineexplorer-elevate` (production), nor feed its private RESULT into `Service.collect()`.
4. **Missing Interface 4 (S12 `storage_generation` Query Interface)**:
   S1 Kind 3 evidence records (220 bytes) carry `map_gen` (offset 108), but do *not* carry `storage_generation`. The broker needs `storage_generation` to supply to S13 `NtLoadWexProfileEx(WEX_HKU_LOAD)` and S15 launch prep.

E1-S18d implements all four missing interfaces following the exact S1/S7/S8 specifications, connects `e1s18_transport_redeem_live` in the broker, and extends the native test suites to verify the complete chain in the lab.

---

## 2. Architecture of the Four Missing Interfaces

```mermaid
flowchart TD
    subgraph Windows / Broker
        B1["Broker Stage 1: consent.exe"] --> B2["Broker Stage 2: e1s18_transport_redeem_live"]
        B2 --> W1["wexhost!WexE1s6Transport(E1S6_REDEEM / E1S6_STORAGE_QUERY)"]
    end

    subgraph Wine Server
        WS["Server Endorsement: get_pipe_caller_binding(op=4)"]
    end

    subgraph Unix Lab Service (e1s6-lab.py)
        EP["S6 Transport Endpoint (e1s6-lab.py)"]
        ES["evidence.py Service (Root-Owned)"]
        EC["evidence.py Connection"]
        PW["S7 PAM Worker (tools/e1s7/pam-worker)"]
        S4["S4 Account Map (/etc/wineexplorer/accounts.map)"]
        S12["S12 Profile Storage (users/.e1s/<sid>/<leaf>/.complete)"]
    end

    W1 -->|"D-Bus / Unix Data FD"| EP
    W1 -->|"Endorse op=4"| WS
    WS -.->|"Control FD verify"| EP
    EP --> EC
    EC --> ES
    EP -->|"Spawn with private channel"| PW
    PW -->|"RESULT"| ES
    ES -->|"Collect: S1 Kind 3 (220B) + Ref (16B)"| EC
    EC -->|"Serve: REDEEM op=4 (252B)"| ES
    ES -->|"Burn & Return 240B reply"| EC
    EC -->|"240B Reply"| W1
    W1 -->|"E1S6_STORAGE_QUERY (32B)"| EP
    EP -->|"Descriptor-anchored read"| S12
    EP -->|"16B Reply (storage_gen)"| W1
    W1 -->|"Populated Stage 2 fields"| B2
```

### Interface 1: S8 REDEEM Transport Opcode in `wexhost`
- **New Constants in `src/include/e1s6_protocol.h`**:
  - `E1S6_REDEEM_REQ_SIZE = 252`: Size of the wire REDEEM request.
  - `E1S6_REDEEM_RESP_SIZE = 240`: Size of the wire REDEEM response.
  - `E1S6_STORAGE_QUERY_REQ_SIZE = 32`: Size of the storage generation query request.
  - `E1S6_STORAGE_QUERY_RESP_SIZE = 16`: Size of the storage generation query response.
  - `E1S6_REDEEM = 9`: WexE1s6Transport dispatch opcode for evidence redemption.
  - `E1S6_STORAGE_QUERY = 10`: WexE1s6Transport dispatch opcode for storage generation query.
- **Handler in `src/dlls/wexhost/e1s6.c`**:
  - `E1S6_REDEEM`:
    1. Validates connection liveness, channel ownership, and caller pipe binding.
    2. Calls `endorse(4, 0)`: passes operation 4 (`REDEEM`) to server via `get_pipe_caller_binding`.
    3. Sends the 252-byte REDEEM packet over `data_fd` with `MSG_NOSIGNAL`.
    4. Receives the 240-byte reply via `receive_packet()`.
    5. Checks `status == 0` (OK). If non-zero, sets `params->err = ERROR_ACCESS_DENIED`.
  - `E1S6_STORAGE_QUERY`:
    1. Validates connection liveness and channel ownership.
    2. Sends the 32-byte storage query packet (`E1S8CTL\0`, version=1, op=6, length=32, account_key[16]).
    3. Receives the 16-byte reply (`status:u32, reserved:u32, storage_generation:u64`).
    4. Validates `status == 0`.
- **PE Client Export**:
  - `WexE1s6Transport` in `src/dlls/wexhost/wexhost.c` already forwards `(pipe, operation, buffer, size, channel)` to `unixlib.c`. No export signature changes needed; opcodes 9 and 10 are transparently dispatched.

### Interface 2: S8 Evidence Endpoint in `tools/e1s6/e1s6-lab.py`
- Instantiates a shared `evidence.Service` at service startup when `--pam-worker` and `--pam-audit` are supplied.
- Uses freshly generated 64-bit `root_gen` and `verifier_gen`.
- Constructs an `evidence.Audit` descriptor anchored on the provided audit file (mode 0600, root-owned, append-only, no ACLs).
- Implements `validate(connection, binding)`: verifies connection liveness, checks that `(root_gen, verifier_gen, server_gen, broker_gen)` match current authoritative generations, and checks that no stale/cancelled flags are set.
- On each S6 transaction (`Transaction`), creates an `evidence.Connection(service, tx, receive)`.
- Routes incoming seqpacket data on `tx.data` during evidence redemption to `connection.serve()`.
- Runs `service.tick()` on the GLib main loop timeout (every 50 ms).

### Interface 3: S7 PAM Worker Integration
- Spawns `tools/e1s7/pam-worker` via `supervisor.Worker` for each authentication attempt under the service lock.
- Fixed service name: `wex-e1s-test` for lab tests (configurable via test flags in lab mode).
- Uses private socketpair for the worker's `--control-fd` (for START/RESULT) and `--dialog-fd` (for PROMPT/ANSWER).
- Feeds typed account and credential over the worker's private channel (`--dialog-fd`):
  - Listens for `S7_PROMPT` (type 2) from `pam-worker`.
  - Feeds `S7_ANSWER` (type 3) with UTF-8 password payload.
  - Never logs passwords, passes passwords in argv, or exposes them in process environment.
  - Securely zeroes credential buffers immediately after sending.
- Collects worker `RESULT` (type 5) on control socket readiness:
  - Calls `supervisor.Worker.collect()`: verifies exit status and takes random 32-byte seed.
  - Calls `Service.collect(connection, request_id)`: validates worker exit, generates 16-byte random `evidence_ref`, builds 220-byte S1 Kind 3 evidence record.
  - Sends the 240-byte ready reply (`OK:u32 + ref:16 + record:220`) over `tx.data`.

### Interface 4: S12 `storage_generation` Query Interface
- **Decision on Source**:
  Per `docs/E1_S12_PROFILE_STORAGE.md` and `wine/server/registry.c`:
  `NtLoadWexProfileEx(WEX_HKU_LOAD)` requires `storage_generation` as an *input* in the 48-byte S12/S13 record (`wex_token_u64(record + 32)` must be non-zero). It checks that the existing profile's `.complete` file contains an exact matching `generation=`.
  Therefore, the broker cannot obtain `storage_generation` from the S13 load reply; it *must* obtain it before calling Stage 4.
- **Smallest Correct Interface**:
  A read-only query operation on the privileged service (`STORAGE_QUERY` = operation 6 in the `E1S8CTL` framing, opcode 10 in `WexE1s6Transport`).
  - Request (32 bytes):
    - `Header`: 16 bytes (`E1S8CTL\0`, version=1, operation=6, length=32).
    - `account_key`: 16 bytes.
  - Server behavior:
    1. Looks up `account_key` in S4 account map (`/etc/wineexplorer/accounts.map`).
    2. Identifies mapped Windows SID (`S-1-5-21-...`).
    3. Descriptor-anchored open of `.e1s/<sid>/<leaf>/.complete` in the active prefix.
    4. Parses `generation=<u64>` from `.complete`.
    5. Returns `status: 0, reserved: 0, storage_generation: <u64>`.
  - Response (16 bytes):
    - `status`: u32 (0 = OK, 1 = DENIED).
    - `reserved`: u32 (0).
    - `storage_generation`: u64.

---

## 3. Record Layouts & Wire Protocol Details

All wire records use unsigned little-endian integers with exact packed alignment. No padding or trailing bytes.

### 3.1 S8 Control Header (`E1S8CTL`) - 16 Bytes
```
+00: Magic [8B]: "E1S8CTL\0"
+08: Version [u16]: 1
+10: Operation [u16]:
     4 = REDEEM
     5 = CANCEL
     6 = STORAGE_QUERY
+12: Total Length [u32]: exact packet length in bytes
```

### 3.2 S8 REDEEM Request - 252 Bytes
```
+000: Control Header [16B] (magic="E1S8CTL\0", version=1, op=4, len=252)
+016: Evidence Reference [16B] (opaque 128-bit connection-bound reference)
+032: S1 Kind 3 Evidence Record [220B] (exact record to be redeemed)
```

### 3.3 S8 REDEEM / READY Response - 240 Bytes
```
+000: Status [u32] (0 = OK, 1 = DENIED)
+004: Evidence Reference [16B] (burned reference on success, zero on failure)
+020: S1 Kind 3 Evidence Record [220B] (evidence record on success, zero on failure)
```

### 3.4 S1 Kind 3 Evidence Record Layout - 220 Bytes
```
+000: kind [u16] = 3
+002: version [u16] = 1
+004: length [u32] = 220
+008: required_caps [u64] = 0x3ff (or 0 on lab link)
+016: request_id [u64]
+024: request_digest [32B]
+056: binding_digest [32B]
+088: nonce [32B]
+120: attempt [u32] (1..3)
+124: account_key [16B]
+140: host_uid [u64] (must equal requester's host UID)
+148: completed [u64] (monotonic timestamp ns)
+156: expiry [u64] (monotonic deadline ns)
+164: root_gen [u64]
+172: server_gen [u64]
+180: broker_gen [u64]
+188: verifier_gen [u64]
+196: map_gen [u64]
+204: policy_gen [u64]
+212: method [u32] = 1 (PAM auth + acct_mgmt)
+216: service [u32] = 1 (wineexplorer-elevate)
```

### 3.5 S12 Storage Generation Query Request - 32 Bytes
```
+00: Control Header [16B] (magic="E1S8CTL\0", version=1, op=6, len=32)
+16: account_key [16B]
```

### 3.6 S12 Storage Generation Query Response - 16 Bytes
```
+00: status [u32] (0 = OK, 1 = DENIED)
+04: reserved [u32] = 0
+08: storage_generation [u64]
```

---

## 4. Trust Boundaries & Security Decisions

1. **Root-Owned Verifier Helper**:
   `pam-worker` runs as root to execute PAM authentication and account management. The broker and Wine processes run as unprivileged users. The worker drops all supplementary groups, sets `PR_SET_PDEATHSIG` to SIGKILL, marks itself non-dumpable, and closes all non-essential descriptors via `close_range`.
2. **Secret Boundary**:
   Passwords travel *only* over the private socketpair between the dialog/feeder and `pam-worker`. Passwords never appear in:
   - Command-line arguments (`argv`)
   - Environment variables (`envp`)
   - Broker IPC records or pipe buffers
   - D-Bus calls or bus monitoring
   - Service logs or system journals
   All memory buffers holding passwords are wiped with `explicit_bzero` / `wex_secure_wipe` immediately after transmission.
3. **Descriptor-Anchored Filesystem Traversal**:
   Opening the S12 `.complete` metadata file requires `O_NOFOLLOW | O_CLOEXEC` traversal from the anchored prefix root. Symlinks, hardlinks > 1, foreign ownership, group/other write permissions, and POSIX ACLs deny immediately.
4. **Single-Use Burn Tracking**:
   Evidence redemption atomically burns the 16-byte evidence reference from the service table *before* comparing fields. Replay attempts are rejected with `ERROR_ACCESS_DENIED`. The broker maintains secondary burn tracking (`e1s18_burn_evidence`) as defense in depth.
5. **Quota Enforcement**:
   - Maximum 3 attempts per elevation request.
   - 120-second hard monotonic deadline covering prompt queue, dialog, PAM, and redemption.
   - At most 1 active authentication per requester host UID.
   - Service-wide throttling: 3 failures per account, 6 failures per requester UID in a 120-second rolling window.
6. **Strict Fail-Closed Behavior**:
   Any protocol error, timeout, crash, mismatch of generations, or integrity break aborts launch, wipes secrets, cleans up child processes, unloads hives, and returns documented error codes.

---

## 5. Denial Table

| Denial Point | Trigger Condition | Status Code | Required Actions |
|---|---|---|---|
| **Invalid Opcode** | Opcode not in `{E1S6_OPEN, CHILD_START, EXCHANGE, CLOSE, REDEEM, STORAGE_QUERY}`. | `ERROR_NOT_SUPPORTED` (50) | Close channel, deny immediately. |
| **Endorsement Mismatch** | Server rejects `get_pipe_caller_binding(op=4)` or sequence mismatch. | `ERROR_ACCESS_DENIED` (5) | Log denial site, close channel. |
| **S7 Auth Failure** | PAM returns `PAM_AUTH_ERR`, wrong password, or account locked. | `ERROR_ACCESS_DENIED` (5) | Increment failure throttle, record audit, return DENIED. |
| **Unknown Account** | Typed account not found in S4 account map. | `ERROR_ACCESS_DENIED` (5) | Run decoy timing check (4s delay), record audit, deny. |
| **Expired Evidence** | Monotonic time `now >= expiry`. | `ERROR_ACCESS_DENIED` (5) | Burn reference, emit expired audit, deny. |
| **Replayed Evidence** | Reference already burned in previous redemption. | `ERROR_ACCESS_DENIED` (5) | Reject replay, emit audit, deny. |
| **Quota Exceeded** | Attempt > 3 or caller failure count > 6 in window. | `ERROR_ACCESS_DENIED` (5) | Deny request, reject spawning worker. |
| **Worker Crash** | `pam-worker` terminates abnormally or killed by signal. | `ERROR_ACCESS_DENIED` (5) | Reap process group, emit audit, fail closed. |
| **Service Restart** | Native service restarted, invalidating `verifier_gen`. | `ERROR_ACCESS_DENIED` (5) | Generations mismatch, all pending refs destroyed. |
| **Secret in Log Leak** | Audit or journal contains canary password. | Test Failure (`FAIL`) | Test harness asserts zero canary strings in logs. |
| **Storage Query Miss** | S12 profile `.complete` file absent or corrupt. | `ERROR_FILE_NOT_FOUND` (2) | Return 0 generation, fail closed in Stage 4. |

---

## 6. Implementation & Test Plan

1. **`src/include/e1s6_protocol.h`**:
   Add `E1S6_REDEEM_REQ_SIZE` (252), `E1S6_REDEEM_RESP_SIZE` (240), `E1S6_STORAGE_QUERY_REQ_SIZE` (32), `E1S6_STORAGE_QUERY_RESP_SIZE` (16), `E1S6_REDEEM` (9), `E1S6_STORAGE_QUERY` (10).
2. **`src/dlls/wexhost/e1s6.c`**:
   Add handlers for `E1S6_REDEEM` and `E1S6_STORAGE_QUERY` in `e1s6_transport()`.
3. **`tools/e1s6/e1s6-lab.py`**:
   Wire `evidence.Service` and `evidence.Connection`, implement worker spawn and private credential feeding, connect `Connection.serve()` for `REDEEM` and `STORAGE_QUERY`.
4. **`src/programs/wexelevate/e1s18_broker.c`**:
   Update `e1s18_transport_redeem_live` to invoke the transport for real redemption and storage generation query.
5. **`src/programs/wexelevate/test_broker_e1s18_main.c`**:
   Keep all 36 existing tests passing. Add tests for `e1s18_transport_redeem_live` error paths.
6. **Unix Test Scripts**:
   Extend `scripts/test_evidence.sh` to exercise end-to-end redemption, wrong password, unknown account, expired, replayed, quota exceeded, concurrent attempts, worker crash, service restart, and secret omission from logs.
7. **Harness Validations**:
   Run `test_e1s6_transport.sh` (must pass 302 checks), `test_evidence.sh`, `test_pam_worker.sh`, and `test_broker_e1s18.sh`.
