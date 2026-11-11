# E1-S18e: remediation of the standard-user chain review

Input: the adversarial review in `docs/E1_S18_REVIEW.md` (findings R1-R15, verdict "do not
enable"). This file records what has been fixed, how it was checked, and what is still open.
The standard-user chain stays disabled (`WEXELEVATE_E1S18`, lab define, off by default).

## Status

| Finding | Status | Files | Checked by |
| :--- | :--- | :--- | :--- |
| R1 stored fixture password | Fixed (fails closed) | `tools/e1s6/e1s6-lab.py` | `test_lab_adapter_supplies_no_credentials` in `tools/e1s6/test-evidence.py` |
| R2 prompt queue races | Fixed in the queue; arbitration with click consent open | `cred_dialog.c/.h`, `e1s18_broker.c` | Test 40 (12-thread stress), `test_prompt_queue_cancel_and_timeout`, both under ASan/UBSan |
| R3 storage generation default | Fixed (broker, evidence service, wineserver query) | `e1s18_broker.c`, `tools/e1s6/evidence.py`, Wine op 12 | Tests 18-22 and 39, evidence storage-query tests, S13 harness op 12 cases |
| R6 image protection | Broker side: lease handed to `start_guard` until exit | `e1s18_broker.c` | Test 41 (was 43) |
| R9 evidence adapter | Partly: init order, connection after endorsement, S1 digest offsets | `tools/e1s6/e1s6-lab.py` | same static test, S6 transport test |
| R12 broker worker exhaustion (production path) | Fixed | `main.c`, `wexelevate_request.h` | S6 transport test (filtered-admin path) |
| R13 caller-as-parent | Standard-user branch fixed; filtered-admin unchanged (see below) | `e1s18_broker.c` | Not testable natively; needs a live designated-parent test |
| R15 exact decoding | Fixed for answer, evidence, policy value and S10 SID (WO-1); dialog dump/copy controls open | `cred_dialog.c`, `e1s18_broker.c` | broker tests 42-44, `test_answer_record_exact_length` |
| R10 part 1 NEGOTIATE output | Fixed (WO-2); authenticated consumed-record install open | `e1s18_broker.c` | broker test 45 (fake enforces ntdll's NULL check) |
| Broker uses op 12 | Done (WO-3); S6 storage query retired (WO-4) | `e1s18_broker.c/.h`, wexhost, evidence service | broker test 46, evidence test, S6 302 PASS |
| R4 part: random nonce/channel, clock after transport | Done (WO-5); canonical digests, identities, generations open | `e1s18_broker.c` | broker tests 47-49 |
| R11 part: account lookup, UTF-16 env block, exact cwd | Done (WO-6); native-authority prefix/profile mapping and retained cwd open | `launch_prep.c/.h`, `e1s18_broker.c` | `test_launch_prep.sh` |
| R8 part: dialog inherits only its pipe | Done (WO-7, two fixes by review); duplex channel open | `e1s18_broker.c` | two-ABI compile only; no live run |
| R8 step 1: frozen attempt tuple, messages, states | Done (spec v5.1, Codex cleared steps 2 and 4) | `docs/E1_S18E_R8_SPEC.md` | spec review only |
| R8 step 2: E1R8 codecs | Done (WO-18 C, WO-19 Python; review fixed a 32-bit length wrap, added tuple learning) | `e1r8_codec.h`, `tools/e1s6/e1r8.py` | native + ASan/UBSan vectors, both MinGW ABIs compile, C/Python golden byte cross-check |
| R8 step 4: two explicit-list pipes, INIT/ACK | Done in code (WO-20; review fixed CancelSynchronousIo target, worker-join use-after-free, lost wait for the dialog after ACK, display-string stand-ins). Win32 pipe tests (partial INIT, blocked writer, broken pipe, cancellation, handle closure) still open: they need the spawn path driven under Wine | `e1s18_broker.c`, `consent.c` (lab only, under `WEXELEVATE_E1S18`) | native portable tests (test 56); both ABIs compile; shipping `consent.c` objects byte-identical; no Wine run |
| R8 step 3a-3c: protected dialog, loader pin, dialog image | Done (Wine 94f8b2fe; tests WO-21) | `server/process.c`, `handle.c`, `request.c`, `sysrun.c`, `named_pipe.c`, `thread.c` | normal suite 325 PASS (open denied / control opens both ABIs, unnarrowed refused); trace lab build 329 PASS (pin pid equals the child's Unix pid both ABIs, consent.exe recognised, probe not) |
| R8 step 3e (caller-binding authority) and 3d (endorsement request) | Open: design decided by Claude (docs/E1_S18E_R8_3E_DESIGN.md section 9), build by Antigravity/Qwen work orders | `docs/E1_S18E_R8_STEP3_PLAN.md` | |
| R8 steps 5-8 and a first elevated launch | Done in the lab (WO-29, 2026-11-10): password over the private socket, PAM result, Confirm, redemption, token, profile, program; with the lab stand-ins listed in the WO-29 result (chiefly: the broker installs its own consumed record, R10) | `consent.c`, wexhost `e1s6.c`, `e1s18_broker.c`, `tools/e1s6/e1s6-lab.py`, `tools/e1s7/` | lab S6 suite 380 PASS (`r8_e2e_elevated_64/32`; wrong password, unknown account, foreign attach and killed dialog refused; reviewed in WO-30) |
| R10 consumed-record producer | Done in the lab (WO-31, 2026-11-10): the verifier's receipt on the retained control socket; the server builds and installs the record; the broker's self-install is gone. Open from the R10 design: the revocable transaction owner (the verifier retires the attempt before issue; validation rests on its live pin and generations) and NEGOTIATE before redemption | `wine/server/named_pipe.c`, `tools/e1s6/e1s6-lab.py`, `e1s18_broker.c` | lab S6 suite 381 PASS (`r8_e2e_elevated_64/32` through the receipt, `r8_no_receipt_no_token`), normal suite 325, broker test |
| First-use profile provisioning | Done in the lab (WO-32, 2026-11-11): the verifier starts `wex-profile-provision` after a successful authentication; prefix by descriptor from the endorsed server, account from the protected map, prefix I/O as the standard user. Open: the real seed projection from `/etc/skel/.wine/user.reg` | `tools/wex-profile/wex-profile-provision.c`, `anchor.h`, `tools/e1s6/e1s6-lab.py`, `wine/server/named_pipe.c` | lab S6 suite 382 PASS (first elevation creates, second reuses; `r8_provision_refuses_bad_input`) |
| R14 second logon on a loaded profile | Done in the lab (WO-33, 2026-11-11): a further elevation of the same account takes its own lease on the loaded hive; a failed later setup returns only its lease. Open: per-lease user counting (users are counted per SID), a join after the accounts map changed is refused until the hive unloads | `wine/server/registry.c`, `tools/e1s6/e1s6-lab.py` | lab S6 suite 383 PASS (`r8_second_elevation_while_first_runs`) |
| Further attempts after a wrong password | Done in the lab (WO-34, 2026-11-11): up to three attempts per request, each with a fresh endorsed dialog, on the one deadline. Open: throttling across requests | `e1s18_broker.c`, `consent.c`, `wine/server/named_pipe.c`, `tools/e1s6/e1s6-lab.py` | lab S6 suite 384 PASS (`r8_retry_after_wrong_password`, three refusals in `r8_wrong_password_refused`) |
| The real password window | Checked in the lab (WO-35, 2026-11-11): typed by the host keyboard on the secure desktop, wrong then right password; fixed the program name shown (now the bound image from INIT) and the unpresented message box (error shown in the dialog). Open: Cancel and close in the window, account name carried into the fresh dialog | `consent.c`, `scripts/test_sysrun_server.sh` | lab S6 suite 385 PASS (`r8_typed_by_keyboard`), screenshots |
| Password check timing | Done (WO-36, 2026-11-11): a success is released at once, every refusal after a fixed 2 s | `tools/e1s7/pam-worker.c`, `protocol.h` | PAM worker test (success 0.02 s, refusals 1.9-2.25 s within 250 ms of each other) |
| Attempt throttling across requests | Done in the lab (WO-37, 2026-11-11): 3 unsuccessful attempts per account and 6 per calling user in 120 s, charged before the worker starts. Open: telling the person, counters across a verifier restart | `tools/e1s6/e1s6-lab.py` | lab S6 suite 386 PASS (`r8_throttled_after_three_refusals`) |
| R4 rest, R5, R7, R8 steps 9-10 (lifecycle races, regression matrix) | Open | | |

## What changed

**R12 (production filtered-admin path, `main.c`).** Every pipe read and write already went through
`pipe_io`, which is bounded by `READ_TIMEOUT` and aborted by `stop_event`. The only unbounded call
was `FlushFileBuffers` after the reply, which waits until the peer reads. It is replaced by a bounded
`pipe_io` read that returns when the client closes its end (or after `READ_TIMEOUT`), so the reply is
still delivered before `DisconnectNamedPipe`. Each client process may hold at most four workers
(`MAX_PER_CALLER`, counted per pipe client PID at accept); the existing global cap of
`2 * MAX_REQUESTS` workers stays. A first agent draft added a 15 second deadline over the whole
request, which also covered the consent prompt and broke the consent-timeout case of the S6 test
(error 233 instead of 1460), plus a flush thread that could outlive its pipe handle; both were
dropped. The production-object baseline in `scripts/test_broker_e1s18.sh` was regenerated from the
reviewed `main.c`; the byte-identical check stays.

**R2 (queue).** The queue is protected by one lock; one entry is active per (session, desktop)
route; waiters block on a per-entry event or condition until promoted, cancelled or timed out. Only
the owning request thread frees its entry (`wex_prompt_queue_release`): cancel and the deadline
sweep only mark, unlink, promote and signal. The agent draft freed entries in cancel and in the
deadline sweep while their request thread still held them; that was the use-after-free the review
described and is fixed. Still open: click consent does not take part in the same arbitration, and
admission is counted per caller process, not per logon.

**R3.** The broker zeroes the storage generation, closes the connection and denies on every query
failure or zero value; no default is guessed. The evidence service had denied with an audit reason
missing from `Audit.REASONS`, which marked the whole service failed; `storage` is now a permitted
reason, and a test proves a denied query leaves the service usable. The generation itself now comes
from the wineserver (next section).

**Storage generation query (Wine, `WEX_HKU_STORAGE_QUERY=12`).** Implemented as the review
recommended: broker only, after S10 issuance and before LOAD, on request 325. It takes the LOAD
record with storage generation 0, checks the original S10 context and issued token without burning
the one-shot LOAD attempt, and discovers the generation from the server's own `config_dir_fd` with
the same no-create validator LOAD uses (no SID hive lock, no `.hive.lock` creation, read-only hive
open; missing, unprovisioned, recovery or substituted profiles deny). LOAD keeps its exact
comparison. Listed in `docs/STOCK_CHANGES.md`. The broker does not call it yet (it still expects the
generation from Stage 2 redemption); moving the broker to op 12 belongs to batch 2.

**R1 and R9 (lab adapter).** The adapter no longer reads `/root/e1s-fixtures-passwords` or answers
PAM prompts. Until a private channel to the registered dialog exists (R8), the worker's dialog end
is closed at once, so every PAM conversation fails and no evidence issues. The generations are
assigned before the evidence service captures them, the evidence connection is created only after
endorsement, and the binding takes the request digest from S1 answer offset 104 (the old code used
the nonce at 40).

**R13.** The standard-user branch no longer designates the caller as parent, so the child does not
inherit the caller's debug object or job. The filtered-admin path still designates the caller as
parent (needed for console and window placement), so a caller under its own debugger or job passes
that to the elevated process. Fixing it needs a server-side flag that suppresses debugger and job
inheritance for broker-authorized creation; that is a separate decision.

**Other fixes found while checking.** A test fake wrote a 68-byte SID array into the 28-byte SID
field of the 136-byte S10 reply (found by ASan). Successful launches leaked the Unix environment
from launch preparation in the broker (30 allocations per test run); it is now freed on both exits.
`scripts/test_broker_e1s18.sh` now also runs the lab tests under ASan/UBSan. Two agent "regression
tests" for R12 and R13 checked only their own local variables and were removed.

## Open items (next batches)

* Batch 2 (broker/dialog): R4 real nonces, digests and identities; R5 final authorization at commit
  and resume; R7 cleanup ownership; R8 live dialog wiring and a private answer channel; R11 launch
  preparation inputs; R15 hardening; broker calls op 12 instead of taking the generation from Stage 2.
* Batch 3 (Unix and server): R9 explicit adapter phases, authority-backed validator and original
  deadline; R10 authenticated consumed-record installation in S10; R14 per-request leases for an
  already loaded profile.
* Live: R13 designated-parent behaviour, R12 under real Windows service stop, everything in VM B.
