<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Bounded readiness repair before any further guest diagnostic

Read STARTUP_CHECKPOINT_20261023/source mapping. Window reset:0%five-hour,
15%weekly. Parent plans/reviews; reuse Luna Dirac. No VM boot or disk creation
authorized by this OFFLINE handoff. Old read-only/calibration permissions consumed.

Evidence: last procedure began about20s after controller start, failed SSH banner
exchange before df. Serial still early systemd/cloud-init, kernel timestamps
~48s at remount. Forwarded TCP listener is not guest readiness. Subsequent ACPI
powerdown45s timed out, SIGTERM fallback stopped QEMU, NOT graceful filesystem
proof. Root cause of the earlier idle command exit1 remains unknown.

## Luna write scope: readiness helper and tests ONLY

Add scripts/g4_guest_readiness.py and scripts/test_g4_guest_readiness.py, plus
one concise report. Do not edit private launcher/procedure/other files or prepare
another run yet. Parent separately reviews storage and shutdown code/evidence.

- Function/library only: never start/stop VM, modify guest, install packages or
  launch Wine/browser. Caller supplies the existing pinned SSH prefix (same
  key/known_hosts/StrictHostKeyChecking=yes/BatchMode/localhost2235/debian).
  No TCP-connect-only pass or unauthenticated banner-only pass.
- Bounded authenticated read-only Python command must echo a fresh32hex nonce,
  report actual uid/username/boot UUID, cloud-init JSON status and systemd boot
  state. Ready only cloud-init done without error and systemd running, expected
  nonroot debian UID (1000), exact nonce, valid stable boot UUID. Do not start or
  repair services or accept degraded state silently. Record power-handler unit
  states if easily available, but do not assume a particular unit is installed
  or claim this proves ACPI shutdown works.
- Require two consecutive valid authenticated responses with SAME boot UUID;
  changed boot/nonce/identity fails closed, never adopted. Capture each attempt's
  bounded stdout/stderr/argv/exit/timing, including errors. Whole gate <=180s,
  individual attempt<=10s including kill/reap reserve, modest sleep between
  attempts, no spin. Authentication/host-key errors never become readiness.
  Original failure preserved on timeout/cleanup errors. No unbounded children.
- Use a small fixed stdlib guest probe, no deployment or file writes; bound its
  local subprocess queries/outputs. Enforce strict duplicate-free JSON/schema/
  types/size. The helper returns readiness evidence only, not rendering/security
  acceptance. Later caller must still enforce650/435 rawUTC guards; gate cannot
  extend900s launcher or consume90s shutdown reserve.
- Execute host tests (D:temp/output) for TCP/banner-only failures, auth failure,
  stale/wrong nonce/UID/name/boot/malformed/oversized/partial JSON, cloud-init or
  systemd starting/degraded/error, boot change, two valid replies, elapsed budget,
  per-attempt timeout/kill/reap and primary-error retention. Test actual helper
  logic, not source strings; at least one real bounded original host subprocess
  control for timeout/reap if using a new process runner. NO SSH/VM/Wine in tests.

Keep this small; no orchestration redesign. Commit AND push exactly owned files,
STOP parent review before integration, preparation or live run. Existing M wine
gitlink and D:master/other workers unchanged. All private VM/evidence/runtime D:.
95%five-hour/99%weekly stopping rules; never consume credits. No merge/bypass.
