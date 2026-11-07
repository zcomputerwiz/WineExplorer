<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# E1-S6 acceptance failure (2026-11-07), input for the fix task

### 3d S6 transport — exact failure output

All c2 (registry/elevation) and c3 (system role) cases PASS. The elevation-broker c4 section passes through `c4_e1s5_mutated-request_32`, then:

```
ok recognized E1-S5 case
ok created the bounded probe watchdog
FAIL the broker completed the requested record fixture (not an early denial) (error 13)
FAIL the accept/mismatch result is correct (error 13)
ok the state fixture returned no process handle
ok completed E1-S5 broker exchange
ok the record fixture launched no child
e1s=FAIL
FAIL: c4_e1s6_accept_64
E1S6_TRANSPORT FAIL
S6_RC=1
```

The probe (`scripts/wexelevate_probe.c:1273-1277`) expects `reply.error == ERROR_SUCCESS` for the accept mode but the broker returned **13 (ERROR_ACCESS_DENIED)** — i.e. the broker **denied** the S6 transport accept instead of completing the record fixture.

Keep-run fixture state (WEX_C2_KEEP set, for diagnosis):
- `e1s6-service.log`: `E1S6 READY` (the S6 lab service **did** start and become ready).
- After the test + cleanup: `e1s6-lab.py` child removed, `org.wineexplorer.E1S6Lab.conf` policy removed, no `e1s6-lab.py` process, D-Bus name owner `boolean false`.
- wineserver in use (lab tree): `6216934707bd08c0316f03d91386498a` (the S6 lab build).

So the S6 lab service started and reported READY, but the broker's S6 transport accept path denied the request (error 13). This is a **functional failure in the S6 transport accept path** (or its interaction with the S10 token scaffolding), not a fixture/setup failure. It blocks the S10 probe matrix (3f) and the S6 acceptance gate required by the repair doc.

## Static interpretation correction

The output above is preserved verbatim. Its error-name interpretation is wrong:
`wine/include/winerror.h:227,235` defines ERROR_ACCESS_DENIED as 5 and
ERROR_INVALID_DATA as 13. The S6 broker fixture retains 13 when Open, child
startup, exchange or final binding verification fails; the S5 record constructor
can also return it before S6 starts. E1S6 READY precedes all of those operations.
Thus this log establishes a failed fixture, but does not distinguish a security
denial from setup/kernel/procfs/child failure, or implicate S10. See
[E1_S6_ACCEPT_REVIEW.md](E1_S6_ACCEPT_REVIEW.md) for the full static trace and
test-only diagnostic sites. The original transport failure remains unlocated
until matching denial-site logs are available; no runtime repair is claimed.
