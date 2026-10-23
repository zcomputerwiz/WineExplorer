<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Native exit calibration runner checkpoint — NOT_PREPARED

OFFLINE source/tests only under CHROMIUM_EXIT_PLAN narrowed handoff and latest
parent instruction. No VM/SSH/Wine/native-PE/browser/calibration execution.
No private-procedure edits, immutable preparation, vendor staging or Chromium
integration. Accepted helper/child sources/build directory/PE bytes unchanged.

Added check-g4-native-exit-calibration.py and focused tests. Fixed future run,
ordinary helper --calibration only, two original PEs pinned366c6928.../6087e3ef...;
no debugger/stack invocation. Guard design reuses19 environment pins, retained
idle prefix/drive mappings,1GiB free, original PE architecture/hash, privateLO,
owned compositor and explicit inner/outerVirGL. Future helper operation42s
including initial binding30s/native wait5s/reap5s; runner285s/cleanup55s.

Strict bounded duplicate-free exactly-bound+exit parser, same nonce/child PID/
creation/full image/command/flags/environment/monotonic time, unsigned3221225599
and0xc000007f. Separate Linux helper Popen PID/start/full argv/prefix, observed
code0 before cleanup, explicit cleanup-start. Exact owned observer kill/reap
before prefix teardown; unknown proc identity is not adopted or signaled; failed
reap skips prefix teardown and remains failed. Raw journal/error logs retained.

## Requested schema applied, but provenance conflict remains

Per latest parent direction, parser/fixtures now require NO mode field and the
exact8fc037ca464bc22952c9f9d63447cf0feb7dcab07e20489cb2d3a4ef3e1a8ba7 marker,
manifest_verified_by_native=false. Marker explicitly labelled UNVERIFIED, not
calibration-runtime attestation. No helper change/rebuild was made to achieve it.

Separately, actual read-only data inspection of the retained366c... PE found its
bound format DOES contain `"mode":"%s"` and `"manifest_sha256":%s`. Its source
SHA2568eb2bcf8394d33d00689c0a8f4d17bdd0881fc5e9dc471b300dd5d2953b64fa6 matches
original build-results.json; that source selects calibration/null for this mode.
Exact raw format/source/hash evidence is retained at
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/native-exit-calibration-schema-readonly-20261023.json`.
This was data inspection, NOT PE/Wine execution. The requested parser and that
static source/PE evidence conflict; do NOT claim a runnable matching preparation
or calibrate until parent resolves it. Original evidence was not overwritten.

## Actual tests / preserved state

`python3 -B -m unittest test_g4_native_exit_calibration test_g4_native_exit -v`
under explicit C: worktree WSL scripts path:19 tests PASS/no skips,5.151s
(18 actual parser/fake-runner methods +1 actual shared-C host compile/run).
Parser failures cover duplicate/missing/partial/extra/error/timeout events,
all binding/exit identity/schema/time/status/command mutations; signaled259
rejected as WRONG calibration value, not asserted live. Runner controls cover
space/idle/hash/PE guard-before-any-process, Popen/procstat/identity failure,
early/nonzero helper, timeout/quota, namespace drift, kill/reap failure and
cleanup order/primary preservation. Synthetic tests are not native API proof.
Final rerun also covers immediate helper_started assignment inside Popen scope.

Rehashed actual unchanged PEs:
native-exit.exe366c69286a7eb6469e4bfd9921643202b746fc987de46e9f9ded2bf6713b8d60;
exit-child.exe6087e3ef18afca2381d7d675e3c7b3ef19141be34d5427c8293684fdfab6c5f9.
All test output/temp D:. No unrelated tests repeated. Stop for parent review:
NOT_PREPARED, no live authority. Pre-existing `M wine`, D: master/submodules and
other workers unchanged. No general orchestration framework added.
