<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Authenticated readiness helper — OFFLINE, NOT INTEGRATED

Implements only [READINESS_PLAN](CHROMIUM_GRAPHICS_G4_READINESS_PLAN_20261024.md).
Changed files: `scripts/g4_guest_readiness.py`, its focused test module, this
report. No private launcher/procedure edits, preparation, VM/SSH/Wine/native PE
execution, disk creation or guest access. Existing M wine is untouched.

Library `wait_ready()` accepts the existing pinned SSH prefix (explicit key,
known_hosts, StrictHostKeyChecking=yes, BatchMode=yes, localhost2235/debian),
generates a32hex nonce and issues only one fixed stdlib Python read-only probe.
It requires actual UID1000/name debian, exact nonce, canonical nonzero boot UUID,
cloud-init done without errors/recoverable errors and systemd running, twice
consecutively on the same boot. Starting states reset the streak; identity/boot
changes, malformed replies, degraded/error state and auth/host-key errors fail
closed. A failed transport/banner attempt never contributes to readiness.

All attempts retain argv, monotonic times/duration, exit, bounded base64 output,
quota/error and kill/reap evidence. Whole gate max180s, each attempt max10s
including2s cleanup reserve, default2s inter-attempt sleep. Pipe quotas are
enforced during reads (16KiB stdout/8KiB stderr); oversized evidence cannot pass.
The first error remains alongside any cleanup error, never a fake successful
reap. No launcher lifetime extension or borrowed shutdown budget is implemented.

Embedded probe bounds each cloud-init/systemctl query to1.5s plus0.5s reap and
4KiB per output stream. Strict duplicate-free outer/query schemas and types,
UTF-8/JSON/base64/size checks apply. Cloud-init's upstream JSON may contain other
version-specific fields; required status/errors/recoverable_errors fields are
strictly checked and its bounded original bytes remain in query evidence.
Read-only systemd-logind LoadState/ActiveState/SubState are recorded, including
missing-unit results; they are NOT an ACPI functional test or readiness shortcut.

## Actual tests

WSL Debian, owned C: checkout `scripts` directory:

```text
python3 -B -m unittest test_g4_guest_readiness -q
Ran 16 tests in 2.617s — OK, no skips, exit0
```

Actual library tests cover banner/transport/auth failures, wrong nonce/UID/name/
boot, boolean UID, malformed/duplicate/partial/oversized/nonfinite JSON, cloud-init
errors/starting and systemd degraded/starting, boot change, consecutive replies,
whole deadline and pinned prefix/budget negatives. Embedded PROBE itself executes
with original local Python query children in place of guest utilities, including
timeout and quota kill/reap. Separate real host children verify timeout, bounded
output, exit/reap and PID absence; fake failures exercise spawn/reap errors and
primary-error preservation. All new temporary paths are private D: directories;
no actual SSH, cloud-init/systemctl query, Wine or guest program was executed.

## Limits / next boundary

No live authentication, guest cloud-init schema compatibility, boot or shutdown
was tested. OS process creation/scheduling and kill/reap failures cannot be made
infallible: bounds reserve cleanup time and fail explicitly rather than claim
readiness on failure. Caller must persist returned evidence, apply existing
650/435 rawUTC guards and preserve90s shutdown reserve inside900s. This is no
browser/graphics/security/G4 or clean-filesystem claim. Earlier idle exit1 remains
unexplained. Parent reviews before integration/preparation or ANY live authority.
STOP; other workers, D:master and existing private manifests remain unchanged.
