<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# R2 module-warning diagnostic — offline preparation

**PREPARED_NOT_RUN**, under `MODULE_WARN_CHECKPOINT_20261024`; parent review
required before dispatch. No VM/SSH/Wine/Windows PE/browser execution, runtime
download/extraction, PE rebuild, new image/layer or calibration occurred.

New ignored `build/g4-virgl-20261022/chromium-module-trial.py` is the small
explicit R2 copy of the accepted native-trial pattern. The original R1 adapter
and historical preparations/runs are untouched. Only fixed R2 paths/helper and
`--module-warn-r2` are selected; no arbitrary mode/executable/flag passthrough.
Current header image and reviewed output-only build header are checked for the
concrete compiled paths. Build receipts/hashes are observations, not historical
approval gates. The three static missing-export candidates remain unproven.

Fresh HOST `chromium-native-module-r2-20261024`; GUEST source stage
`/home/debian/wex-g4/chromium-native-module-r2-20261024-stage`, output/profile/
logs/events under `/home/debian/wex-g4/chromium-native-module-r2-20261024`.
Existing compiled browser image/runtime and shared installed prefix remain
unchanged; prefix is not pristine. Original normal eight browser arguments,
HTTP page/JS, security/backend and native API sequence remain. Only logging adds
`warn+module,trace+seh`; stderr quota remains1MiB/fatal, not trace+module.

Only existing `system-native-identity-r4-20261024.qcow2` is selected. Metadata/
hash differences warn; concrete chain/owners/PID reuse/no-overwrite/transfer and
functional checks remain. Authenticated readiness,674/650/435 raw budgets,
COMPLETE-empty prefix and free-space checks precede staging. Both R2 guest paths
must be absent live; not asserted observed offline. Current source transfer
checksums remain required. Runtime availability is checked live: complete fixed
runtime is reused read-only; partial paths fail without repair. Existing bounded
absent-runtime arm remains, without new download authority.285/55 consumer,
82s helper,300 SSH/45 archive,900/90/80 outer limits are unchanged. Diagnostic1
is deliberate nonacceptance; archive/orchestration and native observation remain
separate. Exact owned stop still requires QMP event/ACKs, QEMU0, no fallback,
children absent and Windows controller0; stopper0 alone is insufficient.

Private artifacts under `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/`:

- `chromium-native-module-r2-20261024.preflight/`: immutable
  `PREPARED_NOT_RUN`, `preflight.json` and `source-stage.tar`.
  Preflight SHA`0e7ba49a8fb7eb85a543be1a085092570b26fb3b425411aebb821d862f9af0ad`;
  TAR SHA`cb27aa607fc2705377ced6d42c675f40b20e88ae3795004f95d1c606333a350e`.
-16 current TAR members including actual existing R2 PE
  `b34ed44a3a819ff2a0d3e39428d08f3419e1fc79f668e7135fe51c571cb11fdf`;
  no vendor runtime in the TAR.258 runtime-file observations, current helper/
  test/source/seed/ISO/native-build/Wine/storage inventories and durable-key
  public-seed match are recorded. Historical19 environment references are
  diagnostic context for actual guest observations, not hash refusal gates.
- Exact Windows controller launch, WSL adapter run,80s identity stopper and
  guest consumer argv reside in the manifest; none dispatched. Host run absent.
- `chromium-native-module-r2-20261024.change/`: R1 reference snapshot, R2
  snapshot/diff/absent-before marker, tests/preparation logs, audit and13-file
  seal. Audit matched42 current inputs/receipts and all16 TAR members.
  Adapter SHA`50e3098d845326bfa751137d72967d57fcf81578f4fc0057c47f02092af09a1f`.

Focused WSL command (scripts cwd): `python3 -B -m unittest
test_g4_chromium_module_adapter test_g4_chromium_native_adapter test_g4_native_exit
test_g4_chromium_native_exit test_g4_native_exit_calibration
test_g4_edge_http_fixture -v`: **58 PASS,22.234s,no skips**. Includes actual
R2 adapter fake-callback guard/timeout/no-stage/archive paths, fixed-contract
mismatch rejection, runner/config agreement, original R1 controls, shared host
C contract tests and real original HTTP socket tests. No fake guest callback is
native/browser acceptance. `git diff --check` passed. Only test/report tracked;
private helper retained for parent review. M wine/D: master/others preserved.
Stop before any live authority.
