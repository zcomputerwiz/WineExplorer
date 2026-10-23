<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Stack calibration building blocks — NOT_RUN_NOT_INTEGRATED

Implementation `381ef400`, limited to the CURRENT offline milestone in
EDGE_STACK_CALIBRATION_PLAN. No runner/calibration orchestration, live preparation,
VM/SSH/Wine/debugger/Edge execution, download or production/security changes.
Tracked work only in the explicit owned C: worktree; D: master/other workers and
submodules untouched. Existing unused M wine remains unstaged. Return for review;
this is NOT PREPARED_FOR_LIVE and does not authorize attachment.

Three small files: original g4_stack_waiter.cpp, pure g4_stack_commands.py and
executable test_g4_stack_commands.py. No existing consumers modified/importing them.

Waiter accepts ONLY --wait NONCE32 / --release NONCE32 (32 ASCII hex, either case).
Unique nonce-scoped manual-reset event creation rejects collisions without
signalling/reusing the old event. Optional main/worker thread names, worker-ready
handshake and checked API results precede one flushed JSON record with EXACT
`{pid,main_tid,worker_tid,creation_time,nonce}`. Normal waits capped29s, with the
remaining1s inside30s reserved for cooperative cancellation/join. Success requires
release and successful worker join/exit; failures/timeouts are nonzero. Shared
worker error is LONG with InterlockedExchange/InterlockedCompareExchange, including
the delayed-scheduling race highlighted by parent. No external target lookup,
payload read, debugger API or privileges. Native runtime behavior NOT PROVEN.

Python validates exact schema/positive nonboolean uint32 IDs, distinct TIDs,
positive uint64 creation time and nonce, rejecting duplicate JSON fields, unknown/
missing keys, overflow, strings/floats/bools, nonfinite values, malformed/trailing
content and injection. Returns ONLY five decimal-token command lines:
attach PID; bt MAIN_TID; bt WORKER_TID; detach; quit (each newline terminated).
No subprocess, file write, attachment or ownership assertion. Text/schema tests
do NOT prove WineDbg grammar/runtime behavior or attach/detach safety.

## Actual offline validation

From `C:/Users/Owner/.codex/worktrees/chromium-graphics-plan/WineExplorer`:

```powershell
wsl -d Debian --exec unshare --net sh -lc 'ip link set dev lo up && cd /mnt/c/Users/Owner/.codex/worktrees/chromium-graphics-plan/WineExplorer/scripts && python3 -B -m unittest test_graphics_private_loopback test_g4_edge_headful test_g4_edge_http_fixture test_g4_edge_owned_threads test_g4_edge_install test_g4_virgl_budget test_g4_virgl_launcher test_g4_consumer_harness test_g4_stack_commands'
node scripts/test_g4_edge_scene.js
pwsh -NoProfile -File scripts/test_g4_virgl_session.ps1
git diff --check
```

176 tests PASS, no skips (170 baseline+6 new); final176 rerun after atomic fix.
Actual Node fixture/controller checks PASS (unchanged). Expected missing-Electron-
helper argparse stderr is a passing existing negative. Native API tests were NOT
run/mocked as execution proof. Actual MinGW cross-compile after atomic fix:
timeout25 x86_64-w64-mingw32-g++ -O2 -static -Wall -Wextra -o private D: final PE,
from scripts/g4_stack_waiter.cpp. Exit0, warning/error log0 bytes, machine0x8664,
250995 bytes. PE NOT executed. No statement about real event/timeout/detach behavior.

Private artifacts:
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/edge-stack-building-blocks-offline-20261023-r1/`
contains initial/final PE, compiler stdout/stderr/exit logs and metadata.json with
all3 source hashes/schema/non-ownership labels. No vendor payload/art in Git.

- Final native source SHA256: `f81de554e3e8ea59cba8e856c2741b2086c52768930c22bd68906d9e8c2660b9`
- Final original PE SHA256: `2e5f008c54f2186cd01b89558c9a4a9be7e5c7bdff276b713116c3bbeed1bcd2`

Usage last checked90% five-hour/44% weekly, reset1792751063, credits0. Stop this
bounded step now; no integration or live work this window. Parent reviews these
blocks before a separately scoped calibration runner and any later execution.
