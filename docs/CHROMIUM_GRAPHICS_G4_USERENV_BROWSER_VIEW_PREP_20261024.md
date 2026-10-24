<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# R3 private normal-module view — offline preparation

**PREPARED_NOT_RUN** under `USERENV_BROWSER_VIEW_PLAN_20261024`. No VM/SSH/
guest/Wine/browser/mount dispatch or new image. Only the explicitly authorized
original read-only API probe executed on host Windows; no profile/token changes.
Existing R4/runtime/R1/R2 evidence and sparse Wine e096ae4 remain unchanged.

New fixed `--userenv-r3` selects HOST/output/profile
`chromium-userenv-r3-20261024`, GUEST output
`/home/debian/wex-g4/chromium-userenv-r3-20261024`, and source stage with `-stage`
suffix. R1 default and R2 mode remain; flags mutually exclusive. Browser image,
normal eight arguments, HTTP fixture/security/backend and warn+module/trace+seh
logging remain. Native-parent C++ sequence unchanged, output-only override PE:
SHA`8f6b6c99910a754b5c4fbe9f5035587d2e15e3321a13443252ec2cff6642c8cc`,
AMD64,306,537 bytes, warning-clean crosscompile; original helpers not overwritten.

`g4_userenv_view.py` is a root-only fixed wrapper inside existing isolated-graphics
mount/net namespaces. Before mounts it rejects init/shared namespaces, failed
privateLO proof, noncanonical/missing paths and wrong source PE architectures.
It binds ONLY BUILD read-only and the two builtin DLL files read-only, then emits
bounded before/inside/context JSON to stdout BEFORE fixed runuser/debian exec.
No path/command passthrough. Root view cap12s/stdout32KiB; mount callbacks cap2s.
This step tested those commands with fakes, not actual mounts. Wrapper JSON also
survives a later runner guard failure in the host's captured consumer stdout.

Original `g4_userenv_probe.cpp` dynamically resolves default userenv, verifies
system32 path, exact independent SID vectors/layout/FreeSid, invalid-output
sentinels and1/63/64/65 boundaries, then emits nonce/PID/bounded JSON. Final
AMD64 probe SHA`fb0dee8dcc3f5356bfec6bdbe24977fd2c101cd08f66ad7b041d645604c1eda3`.
Actual host Windows final probe: exit0,45 checks/0 failures, normal
`C:\WINDOWS\SYSTEM32\userenv.dll`, PID27692. Initial warning-as-error compile
diagnostics and earlier successful host output before a SID-layout safety guard
are preserved. Final compile stderr empty. No Wine API unit repeat or calibration.

R3 runs probe AFTER guards/compositor and BEFORE HTTP/browser. Strict duplicate-
free JSON/nonce/module/check-count/exit validation, active stdout4KiB/stderr1MiB
quotas, owned Popen kill/reap and intermediate prefix cleanup fail closed. Probe
markers are separate from browser final cleanup; idle prefix is required before
browser. Failed/unreaped probe cannot launch browser or authorize prefix teardown
while its observer remains unreaped. Existing final cleanup55s remains available.
Probe allowance45s includes startup/run25, reap5, intermediate cleanup15. A slow-
start regression verifies reserves. Whole285s/82s helper/300SSH/45archive and
900/90/80 outer bounds remain; no extension. Diagnostic1 remains nonacceptance.

New ignored `build/g4-virgl-20261022/chromium-userenv-trial.py` reuses R2 pattern,
dispatching fixed root view after isolated namespace entry. It records bounded
outside physical module hashes before/after consumer, even on consumer failure,
and retains primary errors. Metadata differences warn; current transfer integrity,
concrete disk/backings/owners/paths/budgets/space/privateLO/backend checks remain.
No persistent old-build overwrite. Outside observation errors remain explicit.

Private D: artifacts under `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/`:

- `chromium-userenv-r3-20261024.preflight/`: final immutable marker/manifest/TAR.
  Manifest SHA`a091646d461b6ff2ef3c49bcc5379809845760c4cf2b285db1fcc09df1764c92`;
  TAR SHA`0fae8f2481c0b0e22c6750e75401d81da50b6f248710c3bd9e72db5c73e02a28`.
  21 members: current transitive sources, original HTML/JS, helper/probe,
  runtime-manifest and both reviewed builtin DLLs (89e93bd0…/3452a717…).
  No vendor runtime payload in source TAR;258 actual cached observations retained.
  Exact launch/run/stop/root-view/outside-observation argv and current inventories
  recorded. Host run absent; guest path absence remains unobserved until live.
- `userenv-browser-r3-20261024.build/`: C++/header override build receipt/source
  snapshots/PEs/compiler logs, probe receipt and original host results. Helper
  compile inputs are C++/header/override; current Python comes from final TAR.
- `chromium-userenv-r3-20261024.change/`: adapter/reference/diff, current source
  snapshots, audit/logs and49-file seal. Audit matched53 current inputs/receipts
  and all21 TAR members. Adapter SHA
  `0f04cc7a3648b24f69c8989d1eba5d46720219a317723645e3cb8c52093b0b56`.
  Earlier unreviewed draft retained as `.preflight-before-reserve-fix`; superseded
  after tightening the slow-start reap reserve, never dispatched or overwritten.

Focused WSL command: `python3 -B -m unittest test_g4_userenv_view
test_g4_chromium_userenv_adapter test_g4_chromium_module_adapter
test_g4_chromium_native_adapter test_g4_chromium_native_exit test_g4_native_exit
test_g4_native_exit_calibration test_g4_edge_http_fixture -v`:
**68 PASS,25.518s,no skips**. Exercises real validation/parser/phase/adapter code
with fake mount/process callbacks; actual shared host C harness runs default/R2/
R3 contracts, and original HTTP socket controls remain. `git diff --check` passes.
No fake callback is guest/native/browser proof. STOP parent review before any
live control; no source Wine/prod library or root gitlink change. M wine/mainD
master/other agents preserved. No graphics or effective sandbox acceptance claim.
