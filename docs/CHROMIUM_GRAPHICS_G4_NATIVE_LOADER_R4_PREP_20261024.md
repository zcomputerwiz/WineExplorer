<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Native loader R4: offline implementation, PREPARED_NOT_RUN

Implemented NATIVE_LOADER_R4_PLAN_20261024 only. No image creation/boot/SSH,
Windows/Wine/native PE/browser/debugger execution, rebuild, runtime extraction,
installer or production Wine/compositor change. R2/R3 failed layers and evidence
remain preserved. Creation and any later run need separate parent authority.

`linux_identity` defaults to approved_loader=None: generic browser matching is
unchanged. Native collector alone supplies fixed BUILD/loader/wine. New acceptance
is exactly `[approved_loader] + expected_argv[1:]` after existing trailing-NUL
normalization; no new flat/basename/inferred-path rule. Whole tail, PID/start,
prefix, bounds/deadlines/child rejection and rejected-byte capture remain.
The native guard requires that canonical loader's full SHA8f3ff89a... before any
Popen. Original19 environment pins and2 PE pins remain; loader is an explicitly
additional pin, including updated pins JSON. Future host pre-stage batch verifies
it separately, retaining19 environment records; malformed pin is refused before
SSH. Native binding/full status/natural helper0/reap requirements are unchanged.

Host reference `native-loader-host-reference-20261024.json` matches17 core pins;
24,336-byte internal loader hash matches. Frontend canonical host path resolves to
tools/wine/wine, not a shell script. Guest loader hash remains NOT_OBSERVED.
Prelaunch on-disk hash/argv policy is not loaded-file/security/API attestation.

Fresh immutable D:lab preparation:
`native-exit-calibration-r4-20261024.preflight/`.
Preflight SHA256
`ccfcb715be04a6a8f1df7d1b02cb4b4bb5f696850adea545e95d8b8093dfcb6b`;
16-member source TAR SHA256
`9fdbc9a2dc367866139e51c939c390ee929e20d4fb5711b04797cc5b12fa3970`.
Original366c/6087 PEs, fixed GUEST R1 stage/output and five build sources/receipt/
eight logs unchanged. Current proposed reader/collector pins are explicit (not
obsolete R1/R3 approval); per-file hashes pin WIP before this report commit.
HOST R4 run is absent. Target `system-native-identity-r4-20261024.qcow2` remains
canonical/ABSENT; launcher selects only it, no fallback. Exact future create
argv (inherit80GiB, no size/force), chain inspection and Windows launch/run/80s
stop commands are recorded, NOT executed. Fixed guest paths still require LIVE
canonical/lexists absence before writes; historical absence is not proof.

Read-only normal chains/owners/reserve/identities passed. Frozen parent full
c2acebb9... matched in10.496688s; failed R2 da698bec... in0.138072s and failed R3
27348d89... in0.123967s, each<120s. New layer would back only the frozen capacity
parent; both failed siblings stay outside its chain and must remain unchanged.
Lower full hashes remain historical with unchanged IDs; no65GB rescan. Durable
credential/seed match and all8 helpers/current tests/seeds/ISO/19 expected values
plus additional loader reference are inventoried. No guest hashes claimed.
Existing674/650/435,900/90/80,285/300/45/55,42-30-5-5, complete-empty ownership,
privateLO/unique sockets/actual VirGL and strict QMP/QEMU0/Windows0 gates remain.

Validation through WSL Debian from C: checkout scripts:

```text
python3 -B -m unittest test_g4_native_loader_r4 test_g4_native_identity_isolation test_g4_rejected_identity test_g4_native_calibration_refresh test_g4_native_exit_calibration test_g4_native_exit test_g4_prefix_ownership_integration test_g4_prefix_ownership test_g4_ready_boot_integration test_g4_guest_readiness test_g4_qmp_shutdown test_g4_virgl_launcher test_g4_virgl_budget test_g4_remote_failure test_g4_idle_diagnostic test_g4_edge_owned_threads -q
Ran 170 tests in 28.652s: OK, no skips.
```

Includes prior164 controls and6 new reader/guard/R4 controls: default rejection,
exact opt-in and full-tail negatives, child/prefix/start/hash refusal, no launch
on bad canonical/hash pin, actual R4 fake branch/metadata/preparation and failed
R3 identity/content drift. Legacy positive R3 fixtures now explicitly synthesize
historical approval/launcher; real old-source refusal remains independently
tested. No production approval relaxed. Shared C/owned Linux tests are not Wine
API/native calibration proof; no PE builds or unchanged Node tests rerun.

Private before/after/patch/log/audit directory:
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/native-exit-calibration-r4-20261024.change/`.
Launcher SHA39055731...; procedure1e045b1c... . Audit59 file hashes/all16 regular
TAR members pass; `audit.json` SHA256
`4a059cbfbc60ce176127e1ee4586c978ccc89f624bb8372abccd9498df330b87`.
C: branch only; existing M wine/main D: master/other workers untouched. STOPPED
for source/test/preparation review; no native or rendering acceptance claimed.
