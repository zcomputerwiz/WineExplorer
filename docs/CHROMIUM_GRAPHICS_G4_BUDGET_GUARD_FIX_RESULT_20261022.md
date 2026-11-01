<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Host budget guard fix — implementation/offline preparation only

Added `scripts/g4_virgl_budget.py` and executable
`scripts/test_g4_virgl_budget.py`. No VM/SSH/consumer action, graphics behavior,
Wine/build/runtime/pin change, download or cleanup. The original x64 procedure
failure remains in 9cb47a12; x64 was not repeated.

The pure helper parses RAW session JSON and explicit aware timestamps, normalizes
to UTC, checks expected run/running state/valid PID and finite nonnegative elapsed
time against the unchanged 900-second lifetime. Bad/missing/duplicate JSON,
naive/future/malformed times, wrong/stale session and invalid/insufficient budget
fail closed. It returns elapsed/remaining/required and UTC origins. Actual raw
Windows start `2026-10-22T19:38:50.6690000Z` with now `19:39:42.747Z` yields
**847.922**, not 18847.922. UTC budget explicitly is NOT live ownership proof.

Existing private `presentation-timing-trial.py` now enforces/logs **650 seconds
before guest preflight**, then **435 immediately before consumer dispatch**.
It still validates exact Linux launcher/child identity first. Failed guards save
per-guard JSON and execution error metadata, skip consumer and finally archive
SSH. Consumer-attempt tracking preserves the normal archive path only after
dispatch. If archiving raises, execution/guard/consumer state is saved; an earlier
primary exception is retained, with the archive error recorded separately.
Shutdown still uses the separate exact-identity SIGINT/QMP stopper/controller.

## Verification

Debian WSL, repository root, all PASS (exit 0):

```text
python3 -m unittest discover -s scripts -p 'test_g4_virgl*.py'
python3 -m unittest discover -s scripts -p 'test_g4_consumer_harness.py'
git diff --check
```

**14 + 19 = 33 tests, no skips.** New tests include six pure-budget cases and
six actual-private-procedure cases with ALL guest/process callbacks mocked:
first/second guard failures, missing/stale session/identity, success ordering,
and primary/archive exception preservation. Private-procedure tests skip only
on machines without that private lab file; it was present and executed here.
No sleeps/live checks, unchanged pixel/Node suites not rerun.

## Revised preparations — PREPARED_NOT_RUN

Under `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/`:

- `post-recovery-i686-budgetfix-v2-20261022.preflight/`
- `post-recovery-webview2-budgetfix-v2-20261022.preflight/`

Both preserve exact commands, new guard limits, archives and **five** host-helper
hashes/copies. All archive members/current sources/helpers and unchanged binary
expectations verified offline. Guest inventories remain 8/6 files and binaries
16/19; their archive hashes equal the accepted original preparations. No run
directory exists. Original preparations and provisional non-v2 budgetfix records
are preserved; provisional records marked superseded after the finally fix.
Only v2 is submitted for parent review. Full dependency/budget audit copied in.

```text
g4_virgl_budget.py SHA256:
7a816cb4ff39f5ec2ef0349f61bd0629eced1d4eea692a93530eacafefca0a09
private procedure SHA256:
519fa6c5a1a9a76dddb85aa6a3a20ee6cd333e22a82a6cb8f68469318f9f3533
```

No ad-hoc PowerShell date expression is needed or trusted by the executed path.
The fix is offline validated, not live validated. Return for parent review before
either remaining control; no new orchestration framework or G4 claim.
