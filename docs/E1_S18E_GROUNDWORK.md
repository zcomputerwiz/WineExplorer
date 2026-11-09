# E1-S18e groundwork: bindings, clock, per-request transport, revocation (plan)

Second item of the agreed build order (docs/E1_DECISIONS.md, 2026-11-08 design decisions;
docs/E1_S18E_DESIGN_REVIEW.md). R13 is done. This plan covers what R4, R8 and R10 all build on.
Nothing here is implemented yet; each step becomes a work order or my own change, with tests.

## 1. One canonical S1 encoder (R4)

Today the standard-user branch hashes a few ad-hoc fields (`e1s18_compute_digests` in
`src/programs/wexelevate/e1s18_broker.c`), while the S5 code in `src/programs/wexelevate/e1s.h`
already encodes the S1 caller binding (kind 1) and the frozen request exactly as
docs/E1_S1_CONTRACTS.md specifies (`e1s_binding_digest`, `e1s_freeze`). S5 uses BCrypt and
`IsValidSid`, so it cannot be compiled into the native S18 unit tests.

* Move the pure byte encoding (`e1s_bytes`, `e1s_uint`, `e1s_put`, `e1s_blob`, `e1s_string`, the
  binding and request field order) into a portable header used by both, with the hash function and
  SID validation supplied by the caller (BCrypt and `IsValidSid` in the PE build, the existing
  `sha256_*` and a structural SID check in native tests).
* `e1s18_handle_standard_request` builds the binding and frozen request through it. The binding
  needs endorsed identities (prefix, host process, opener/writer ids, desktop route, channel) and
  all generations; none exist in production yet, so the request denies before any dialog. That is
  the correct state until the registration work below supplies them; the lab fixtures supply them.
* Tests: the existing S5 golden vectors must produce identical digests through the shared code;
  every missing identity or generation denies.

## 2. One clock and one deadline (R4, R9)

S1 requires one absolute deadline, set at admission and including queue time, on the verifier
host's monotonic clock. The broker uses `GetTickCount64`, the verifier `CLOCK_MONOTONIC`.

* The broker takes time only from the native transport (a wexhost Unix call returning
  `CLOCK_MONOTONIC` ns), sets the deadline once at admission, and passes it unchanged to the
  dialog, the verifier and the server. Clock failure denies.
* The verifier uses the deadline from the broker's request (checked against its own clock and an
  upper bound), not its own five-second setup deadline.
* Tests: expiry while queued, while prompting and during redemption; clock failure.

## 3. Per-request transport (R10, review item 1)

wexhost keeps one global active channel and pipe (`src/dlls/wexhost/e1s6.c`). A second OPEN would
replace the broker generation and the first request's authority.

* First version: serialize standard-user requests in the broker for their whole lifetime (one in
  flight; others wait in the prompt queue or are refused with ERROR_BUSY at admission), and keep
  the one S6 transport open from registration through redemption, installation and final checks.
  Per-request transport objects can replace this later.
* Tests: a second request while one is in flight waits or gets ERROR_BUSY and never opens a second
  transport; the first request's transport stays the same until it ends.

## 4. Revocation owner (R10, review item 2)

The verifier burns evidence before comparing it and `_stop` clears it; nothing survives
redemption to be revoked later.

* After a successful redemption the verifier creates a separate consumed-transaction record
  (request, account key, generations, deadline, connection) that lives until the broker reports
  commit or cancel, or the connection, bus name, policy/map generation or deadline goes.
  Revocation is reported on the control socket to the server (the R10 receipt channel) so the
  server's installed record becomes invalid before ISSUE, LOAD or commit.
* Tests: each revocation trigger after redemption makes the next server operation deny.

## Order

1 and 3 first (broker-side, testable natively), then 2 (wexhost and verifier), then 4 together with
the R10 receipt. Each step keeps the standard-user chain disabled and the filtered-admin path
unchanged (its production-object baseline check stays).
