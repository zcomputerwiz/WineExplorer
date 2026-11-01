<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Offline integration of the original-child stack calibration

Checkpoint28205469 and building blocks381ef400/d363fa33 supersede the stale wake
prompt. Parent accepted176 tests and cross-compilation, not native execution.
This plan authorizes OFFLINE implementation/tests/preparation only. No boot,
Wine/debugger/Edge execution until parent source/test/manifest review.

## One narrow calibration, not an Edge runner change

Reuse existing environment, compositor/namespace, logging, rawUTC, installed
payload and ownership helpers. Keep the new healthy calibration separate from
the unchanged headful Edge runner; no browser launch/imported security work.
Fresh direct LAB child stack-calibration-* output/stage; retained owned prefix,
no copy, Wineboot-u, installer, runtime download or new profile. Refuse symlinks,
existing output, <1GiB free or any existing owned-prefix consumer before creation.
Record pinned Edge payload/registration before/after without claiming live hive
processing. Whole runner<=285s inside SSH300/launcher900, cleanup reserve55s;
debugger startup/collection/timeout kill+reap total<=10s. No retry path.

Use original g4_stack_waiter.cpp plus g4_stack_commands.py. Add one expressly
read-only --verify NONCE PID CREATION MAIN_TID WORKER_TID mode to the original
fixture (previous exact two-mode contract extended only here). Strict numeric/
nonce parsing, exact original target image == verifier's own module file path,
matching process creation, STILL_ACTIVE and both distinct TIDs owned by that PID.
QUERY_LIMITED_INFORMATION rights only, no attach/suspend/context/token operations
in verifier. Return bounded strict identity JSON and checked failures. Native
records are verification evidence, not a numeric Win32/Linux ID mapping.

Cross-compile the ORIGINAL fixture on host before preparation, -Wall -Wextra;
include source AND exact generated original PE in fresh source-stage inventory.
All generated binaries/private evidence D:. Reuse compiled PE in guest, no rebuild
of loaded binaries. Hash it before wait, verify, release and afterward.

Start ONE --wait nonce child, <=30s bounded initial readiness read <=4096 bytes, exact
five-field JSON/nonce, no additional/trailing output. Independently validate
recorded Linux Popen PID/start/prefix/exact original-image argv, still alive;
run native --verify before attach and after debugger exit, compare full identity.
Keep before/after records and hashes. Missing/ambiguous/reused/foreign ownership
fails closed, no process scan/alternate tool/privilege fallback. The child gets
its own separate group; its native30s lifetime remains unchanged.
The initial30s allows ordinary retained-prefix/loader startup, not a forced
update or retry; after the flushed readiness record, budget the native waiter's
remaining lifetime separately and refuse attach if adequate time is unavailable.
This preparatory cap does not change the existing Edge45s readiness criterion.

Generate ONLY attach PID / bt MAIN_TID / bt WORKER_TID / detach / quit using the
existing strict generator. Parent inspected pinned Wine debug.l: digit tokens are
decimal; hex input is explicit0x. Never accept command text/path/PID from CLI.
Launch exact inventoried WineDbg PE with --file of the owned generated file.
Pin both tool hashes from STACK_DISCOVERY_R1: WineDbg71b6beda.../dbghelp5338dbd...
plus all17 core/installed hashes; record source4/host-build provenance limitation.
Empty/local-only _NT_SYMBOL_PATH/_NT_ALT_SYMBOL_PATH, sanitized env and inherited
private-lo namespace, no external symbol download/security flags/GUI debugger.

Retain bounded raw stdout/stderr, source/binary/argv/command hashes and times.
Require debugger exit0, exact attached PID, two selected-TID backtrace headers
with actual frame records (not headers alone), useful waiter-module/Windows-wait
frames, no unknown-TID/attach failure/parser/error output or other target PID.
Validate output using representative PINNED Wine source formats; tests must not
teach a parser invented success strings. Treat unavailable symbols/frames as
limited/failed calibration, not fabricated success. Normal WineDbg detach is
silent: do NOT invent an explicit success line. Source parser exit also detaches.
Prove functional detach/resume through native after-verification plus child
still alive, exact nonce --release exit0, then original child exit0 within2s.
That is not an independent trace of DebugActiveProcessStop's return value.

Any debugger timeout/log overflow may terminate the attached OWNED child because
kill-on-exit is not disabled. Record failed calibration, retain partial logs,
never call it harmless. Kill/reap only exact-owned debugger helper group; cleanup
release/kill only original child/owned prefix, keep primary/secondary errors.
No Edge launch on success OR failure; no deliberate timeout-fault trial now.
Result classification must say TOOL CALIBRATION ONLY, not Chromium/G4 acceptance.

## Offline tests and complete handoff

Tests: no output/child before path/space/hash/idle/namespace guards; malformed/
duplicate/trailing/oversized/timeout readiness; wrong nonce/PE/PID/start/prefix/
creation/TID/owner and before/after change; command whitelist/exact argv/no extra
flags; real representative attach/two-stack parsing vs wrong/missing/empty frames,
unknown TIDs/foreign PID; silent detach evidence vs mere process absence; child
dies before release/wrong release exit; debugger timeout/quota/kill-reap failure;
primary error and every cleanup failure retained. Unit mocks are NOT Wine proof.
Run176 prior Linux controls in fresh isolated netns plus new tests, Node fixture,
controller and native cross-compile. Never weaken existing assertions.

Extend only explicit private procedure mode/inventory, not a generic framework.
Fresh PREPARED_NOT_RUN with exact source/PE/tool19/core/host-helper/public-seed/
installed-reference hashes and C: launch/run/stop paths, D: VM storage; absent run.
650/435 rawUTC gates remain, explicit inner/outer VirGL and isolated socket/lo
proof using existing compositor for consistency (not graphics acceptance).
Parent independent review precedes ONE later healthy calibration; a later Edge
stack probe is a different milestone. Commit AND push source/tests/report.

Work C:/Users/Owner/.codex/worktrees/chromium-graphics-plan/WineExplorer,
codex/g4-r1-completion. Preserve M wine and other workers/D: master. Private
storage D:, only owned VirGL guest. No master merge, copied vendor/art, bypass,
capability spoof or reset credits. Live usage0% five-hour/45% weekly at wake;
check milestones, stop/checkpoint95% five-hour, weekly99%. Reuse Luna Dirac.
