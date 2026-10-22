<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Fix the host budget guard before remaining controls

Post-recovery x64 report 9cb47a12 has passing graphics but a real procedure
failure: converting an already-parsed PowerShell JSON DateTime back through a
string lost UTC, reporting 18847.922 seconds remaining instead of 847.922.
The separately corrected raw-UTC calculation and actual 146-second clean run
show no timeout extension, but do not certify that the original guard worked.
i686 and strict WebView2 were not run. Preserve this failure; do not repeat x64
merely to erase it. Fix this repeated clock-conversion trap once before continuing.

Luna implementation-only scope, no VM/SSH/consumer action:

- Add a small original pure Python host-budget helper and executable unit tests,
  preferably scripts/g4_virgl_budget.py plus test_g4_virgl_budget.py. Parse the
  RAW session JSON timestamp, never a PowerShell-reformatted date. Require an
  explicit UTC/aware timestamp and matching expected run name, running session,
  finite nonnegative elapsed time and a valid required budget. Fail closed on
  missing/malformed/naive/future timestamps, stale run/session or insufficient
  time. Return elapsed/remaining/required and UTC origins for evidence. No new
  orchestration framework or graphics behavior change.
- Compute conservatively from the Windows controller start, which precedes the
  Linux launcher's 900-second lifetime. Do not raise that lifetime. Distinguish
  this UTC budget measurement from proof of live Linux launcher/child ownership;
  existing exact-identity validation remains mandatory.
- Use the helper inside the existing private procedure before its guest preflight
  operations (require 650 seconds), and again immediately before the consumer
  (300 consumer + 45 archive + 90 cleanup = 435 seconds). Log each correct guard
  result. Never proceed if a guard fails. Avoid optional/ad-hoc PowerShell budget
  expressions; the executed procedure itself must enforce the checks.
- Unit tests must reproduce the actual raw timestamp case: controller
  2026-10-22T19:38:50.6690000Z and now 19:39:42.747Z imply 847.922 seconds,
  NOT 18847.922. Cover timezone-equivalent aware inputs, boundaries, expired,
  malformed/future/naive times, wrong run, nonrunning state and invalid budgets.
  Inject clocks/session data; no live VM or sleeps. Run existing launcher and
  consumer controls affected by the helper; no need rerun unchanged pixel suites.
- Preserve old preparations. Prepare revised i686/WebView2 manifests with unique
  budgetfix names, hashing the new host helper/private procedure and complete
  unchanged guest inventory. No guest boot. Existing x64 evidence stays intact.

Commit AND push implementation/tests plus concise note with exact validation.
Parent independently reviews helper, tests and revised manifests before dispatching
the two remaining controls. No source/build/runtime changes, GPU faults, retries,
downloads, cleanup or master merge. Standing usage checkpoints still apply.
