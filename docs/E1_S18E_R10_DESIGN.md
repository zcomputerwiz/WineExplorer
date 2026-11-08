# E1-S18e R10: authenticated consumed-record installation (design, not implemented)

Finding R10 in `docs/E1_S18_REVIEW.md`: the S10 token context can only be filled by the lab
fixture (`WEX_TOKEN_TEST_INSTALL`, always-true validator). The internal handoff
`token_install_e1s_consumed` (`wine/server/token.c`, contract in `wine/server/e1s_token.h`) has
no production caller. ISSUE therefore always denies in the real chain. This note proposes the
producer. Nothing here is built yet.

## Threat model this design assumes

* Trusted: the wineserver, the root verifier service (`tools/e1s6/e1s6-lab.py` with the S8
  evidence service and S7 PAM worker), the kernel.
* The broker (`wexelevate`, SYSTEM role) is trusted at the Windows level, but the review asks
  that the server not take authentication results as bytes from it (defence in depth against
  broker bugs or a substituted broker image).
* Out of scope, as decided before: native code running as the caller's host UID (it can trace
  the wineserver itself). The relevant attacker is the caller's Windows processes.

## What exists today

* `sysrun_e1s6_endorse` (`wine/server/sysrun.c`): on each broker transport step the server
  dups the broker's control socket, data socket and pin from the broker's handle table, checks
  that the peer of both sockets is the same live root process (SO_PEERCRED uid 0 and pidfd
  pins), and sends a 96-byte endorsement with server and broker generations over the control
  socket with SCM_CREDENTIALS. It is one-way and keeps no descriptor afterwards.
* The verifier atomically consumes evidence in `Service.redeem` and returns the 220-byte
  record to the broker over the data socket.
* The S10 context (`struct e1s_token_context`) is created by NEGOTIATE in the broker thread,
  holds the broker primary/generation, and `e1s_install_consumed` already validates kind 4,
  expiry, caller objects and the validator before setting state 1. ISSUE burns before checking.

## Proposal

1. **Order.** NEGOTIATE moves before Stage 2 redemption, so the context exists when the
   evidence is consumed. ISSUE stays after redemption.
2. **Bind the redemption to the context.** The broker's REDEEM transport call names its S10
   context handle. In the endorsement for REDEEM (operation 4), the server looks the context up
   in the broker's own handle table (`e1s_context_current`: same process, thread, primary and
   generation), creates a fresh 16-byte receipt id, stores it in the context with the
   expected request id and evidence reference, and includes the receipt id in the endorsement.
   The server also keeps the dup of the control socket and both verifier pidfd pins in the
   context (released at context destruction).
3. **Receipt from the verifier to the server.** After `Service.redeem` consumes the evidence
   atomically, the verifier sends a receipt on its control socket (the peer the server just
   endorsed through): receipt id, request id, evidence reference, the exact 220-byte record,
   the canonical account name resolved through S4 from the PAM result, the original absolute
   deadline, and the current root/verifier/map/policy generations. The broker still gets its
   copy of the record on the data socket as today.
4. **Server receives without blocking.** The context's control descriptor is registered with
   the server's fd poll loop. On readable, the server reads one packet nonblocking, requires
   SCM_CREDENTIALS uid 0 and the pinned verifier pid, both pins still live, and an exact match
   of receipt id, request id, reference, broker and server generations. It then builds the
   kind 4 input from the receipt (not from broker bytes) and calls `e1s_install_consumed` with
   `validate` = "verifier pins live, generations unchanged, deadline not passed, caller
   current", and `release` = close the pins and descriptor. Any mismatch burns the context
   (state 2), as today.
5. **ISSUE unchanged.** The broker's ISSUE input must equal the installed input byte for byte;
   ISSUE keeps burn-before-validation and the fresh S4 lookup.
6. **Fixture stays lab-only.** `WEX_TOKEN_TEST_INSTALL` and its always-true validator stay under
   the S10 test define; the new path is the only production producer.

## Consequences

* The canonical account name comes from the verifier's S4 resolution, not from the dialog
  (also closes the name part of R4).
* Cancellation: closing the verifier connection or a verifier restart kills the pins, so
  `validate` fails and ISSUE denies; no numeric grant survives (contract in `e1s_token.h`).
* Ordering race: ISSUE can arrive before the receipt. ISSUE must not wait; the broker waits on
  a short bounded server-side "installed" query or retries once within the original deadline.
  A second ISSUE is impossible (state 2 after the first), so the broker must only call ISSUE
  after the query reports state 1.

## Decisions needed before building

1. Moving NEGOTIATE before redemption (changes stage order and the S18 tests).
2. The verifier speaking back to the server on the control socket (today one-way).
3. A small server query "context installed" for the ordering race, or a blocking-free retry.

Files that would change: `wine/server/sysrun.c`, `wine/server/token.c`, `wine/server/e1s_token.h`,
`src/dlls/wexhost/e1s6.c` (pass the context handle), `tools/e1s6/e1s6-lab.py` and
`tools/e1s6/evidence.py` (receipt), `src/programs/wexelevate/e1s18_broker.c` (order, query),
plus the S10 harness (`scripts/test_e1s10_token.sh`, `scripts/e1s10_token_probe.c`).
