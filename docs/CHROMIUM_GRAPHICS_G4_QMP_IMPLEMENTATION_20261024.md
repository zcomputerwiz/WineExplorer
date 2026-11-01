<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Fixed QMP shutdown observer — OFFLINE, NOT INTEGRATED

Implements only the last handoff in
[BOOT_SAFETY_REVIEW](CHROMIUM_GRAPHICS_G4_BOOT_SAFETY_REVIEW_20261024.md).
Owned changes: `scripts/g4_qmp_shutdown.py`, its test module and this report.
No private helper/launcher/procedure edits, VM/QEMU/SSH execution, preparation,
image creation or guest access. M wine and D:master remain untouched.

Library `observe_shutdown()` uses one <=50s deadline from before socket creation
through connection, greeting, capabilities negotiation, powerdown ACK and guest
event observation. Only `qmp_capabilities` and `system_powerdown` can be sent,
with distinct per-call random IDs. Newline-buffered parsing retains fragmented/
coalesced frames, duplicate-free JSON and strict bounded schemas. Limits:
8KiB/frame,64KiB combined transcript bytes,256 transcript records,128 messages.
Raw rx/tx bytes are base64 with monotonic observations; tx completion, errors,
asynchronous events and close errors are retained separately.

Matching ACKs and post-request `SHUTDOWN` guest=true/reason=guest-shutdown are
both required for `protocol_complete`. Events before the own request, including
coalesced/partial buffered input, are drained/labelled and never attributed.
Event-after-request/before-ACK is retained until its matching ACK arrives.
POWERDOWN, EOF without required evidence, wrong/duplicate IDs, explicit errors,
RESET, host-signal/quit or guest-reset shutdown cannot complete the observation.
Already-buffered tail messages are validated, not ignored after apparent success.

This is **protocol evidence only**, no independent graceful/passed boolean and
no QEMU wait/signal/quit/reset/fallback. Future caller must separately prove the
owned endpoint/process, actual QEMU exit0 and no fallback within90s reserve.
Event attribution means observed stream ordering after the own send, not a
causal proof or cross-clock mapping of QEMU timestamps. No filesystem or ACPI
functional success follows from unit tests. Existing failed boot remains failed.

Protocol details checked against primary
[QMP framing/IDs/events specification](https://www.qemu.org/docs/master/interop/qmp-spec.html)
and [SHUTDOWN/POWERDOWN reference](https://www.qemu.org/docs/master/interop/qemu-qmp-ref.html#event-SHUTDOWN).

## Actual focused validation

WSL Debian, owned C: worktree `scripts` directory:

```text
python3 -B -m unittest test_g4_qmp_shutdown -q
Ran 13 tests in 0.028s — OK, exit0, no skips
```

Actual socketpairs (no QEMU) exercise fragmented greeting/frames, coalesced
messages, ACK-first/event-first and pre-request-event nonattribution. Original
local peer threads have bounded socket I/O and joins; all finished. Temporary
logs use private D: paths. Fake socket/clock controls cover explicit errors,
missing/wrong/duplicate IDs, EOF/partial/malformed/duplicate-key/nonfinite data,
greeting/event types, non-guest reasons, RESET, each quota, a shared deadline
consumed by negotiation, connect/send/close failures and primary error retention.
No unrelated test suites rerun. Live QMP/schema compatibility and actual guest
powerdown remain untested; STOP for parent review before integration/preparation.

## Independent parent acceptance

Parent read committed9b729ff7 observer, framing/state machine and all tests.
Independently49 tests PASS/no skips in2.798s:13 QMP +16 readiness +3 launcher +17
budget. Real socketpair controls passed. ACK/event ordering, primary errors,
limits and protocol-only classification satisfy this bounded building block.
Raw stream ordering is not a causal or guest-filesystem proof; strict caller
QEMU0/no-fallback and exact owned endpoint/process checks remain required.
Both helpers accepted for OFFLINE integration under READY_BOOT_INTEGRATION_PLAN;
no image creation, VM boot or SSH authority follows this review.
