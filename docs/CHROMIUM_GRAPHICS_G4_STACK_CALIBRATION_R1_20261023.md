<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# One original-child calibration — executed once, cleanly stopped

**TOOL CALIBRATION PASS ONLY.** Exact reviewed integration381bb46a and immutable
`stack-calibration-r1-20261023.preflight` used once. No Edge, retry, fault,
Wineboot/update, install, rebuild, profile creation or other consumer. No source,
production, D: master/other-checkout edits; existing unused `M wine` unstaged.
This does not diagnose Edge startup or establish Chromium/G4 acceptance.

## Actual result

Runner passed=true, elapsed3.579123s. Native readiness/before/after all identical:
WinPID32 (hex0020), mainTID36 (0024), workerTID224 (00e0), creation134372266229754620,
nonce `7d823f37b707419cb2bed6fcc3eb7851`. Independent Linux original PopenPID888,
start_ticks3701, exact retained prefix/PE/--wait/nonce argv before==after; no numeric
Win32/Linux mapping. Original PE hashes before wait/each verify/release/after all
`17d1ecdc15542c6b9099f63b4b4c923d36ae768c86ca2ab41c77d364f7da12b0`.

Actual generated command file:

```text
attach 32
bt 36
bt 224
detach
quit
```

WineDbg exit0 in0.732636s, below10s inclusive bound. Actual stdout binds both
selected HEX TIDs separately to exact full original Z: image and process0020;
8 main frames/6 worker frames, each including ntdll, kernelbase wait functions
and stack-waiter module offsets. Zero printed frame pointers occurred legitimately.
Some kernelbase argument values print `<internal error>`; argument decoding is
limited, not credited. Module/PC stacks are useful calibration evidence, not a
source-line/argument correctness attestation. No selected-TID/attach/parser error.
Detach is silent: native before/after verification0, same live Linux identity,
exact nonce release0 and original child0 establish functional resume/release;
DebugActiveProcessStop API return remains NOT independently proven.

All operation bounds true; post-ready budget before attach23.978891s. Raw child
stdout121/stderr27404 bytes; debugger stdout1949/stderr3093, below quotas. No
timeout or forced debugger/child kill. Prefix cleanup kill0/wait0, no remaining
owned servers/processes. Pinned installed payload/registration unchanged before/
after (version_source=registration, not independent PE file version). Exact
private-lo proof unchanged, inner/outer VirGL verified:
`virgl (D3D12 (Intel(R) UHD Graphics 750))`. Compositor log retains a headless mode
test-failed message; no rendering acceptance is claimed from this tool exercise.

## Preconditions, execution, shutdown

Live preboot usage35% five-hour/50% weekly, credits0. Rechecked approved preflight
SHA5cc02499..., archive709df46...,12 members/current sources/PE/pins,5 C: helpers,
2 seeds and installed reference; no prior launcher/QEMU/run. Authorized existing
key mode0600/public-seed match, strict known_hosts. Actual guest free1,308,176,384
bytes checked BEFORE transfer/stage creation;19 actual known binary hashes and
12 deployed member hashes match. Mandatory650/435 rawUTC guards passed with
856.160950/853.761231s remaining. No manual timestamp conversion.

Exact launch/run/stop commands are unchanged in preflight.json: C: PowerShell
session controller `-RunName stack-calibration-r1-20261023`; C: private procedure
`WEX_G4_CONTROL=stack-calibration ... run`; isolated socket/private-lo runuser
dispatch to staged `check-g4-stack-calibration.py --output-dir` the run-named LAB
path; separate C: identity stopper with this run's launcher-identity.json and
`--timeout60`. No Ctrl-C. Host procedure0, consumer SSH0, evidence archive0.

Windows WSL23000 UTC10:56:14.3769408..10:57:24.1634089, exited0; controller session
9078 returned0. Exact Linux launcher31193/start2169055, QEMU31208/start2169092,
Xvfb31195/start2169067 validated. Separate stopper0: stop_reason=signal,
QMP graceful_powerdown=true, QEMU0, qemu_stopped/xvfb_stopped=true. Read-only
post-stop /proc checks find all three absent; Windows23000 absent. Explicit final
metadata, not disappearance alone, establishes graceful owned shutdown well
within900s and90s cleanup reserve. No additional guest actions after collection.

## Preserved evidence / handoff

`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/stack-calibration-r1-20261023/`
contains complete20-file consumer archive/extracted evidence, native raw readiness/
before/after logs, debugger stdout/stderr, selected commands, Linux records,
release/child/cleanup statuses, compositor/EGL logs, all hashes/guards/commands,
launcher/stopper/execution and live-status.json. No screenshots requested for this
tool-only calibration. Adjacent host recheck and Windows session/logs retained.
Original PREPARED_NOT_RUN history is untouched; current live-status explicitly
says EXECUTED_ONCE_CALIBRATION_PASSED_GUEST_STOPPED.

Second-read audit: all20 archive files match extracted bytes and original hashes;
42 run files hashed in independent-evidence-audit.json, zero hash errors.
Consumer results SHA256 `2efa4cb70dfaad61c8fcb6ae6ab768c2f37d7fc9e3e159a31cf186a9b77f411c`;
archive `a9e7c1fd343c41304f479358b7b291e9a25c8d0f78c940e13d2061cc6e5637b4`.
Source/compile/tool provenance limits remain in the integration/discovery reports:
on-disk19 hashes + builtin dbghelp policy, NOT runtime-loaded-file or reproducible
build attestation. No unchanged unit tests rerun/no code change in this live step.
Stop for parent actual-stack/log review; no Edge attach or next trial authorized.
