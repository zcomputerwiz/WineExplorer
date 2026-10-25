<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# R4 compositor prefix scope — offline preparation

**PREPARED_NOT_RUN**, following COMPOSITOR_SCOPE_R4_PLAN_20261025. R3's
remaining PID937/start3373 was its owned labwc; preserve that failure and consumed
paths. No VM/SSH/mount/Wine/browser/API-probe execution in this step.

`OwnedWexcomp(..., strip_wineprefix=True)` removes WINEPREFIX from its copied
native compositor Popen environment only. Default is false. Caller Wine prefix
and existing shared Wayland endpoint updates remain. The record adds
`wineprefix_stripped`; resource sampling and genuine Wine/server idle assertions
are unchanged. Only fixed `--userenv-r4` opts in. R1/R2/R3 defaults remain.

Real Linux child regression observes the marked legacy child and a separate
marked owner in the actual /proc sampler, excludes the unmarked R4 child, then
proves that the real marked owner still rejects intermediate prefix cleanup.
All children terminate/reap. Endpoint/protocol callbacks are synthetic; this is
environment/ownership proof, not compositor or rendering proof. Runner tests
exercise each mode's constructor options, R4 CLI/view dispatch, API failure
before HTTP/browser, and successful probe/cleanup/browser sequencing.

New fixed HOST/output/profile `chromium-userenv-r4-20261025`, stage with `-stage`.
Private adapter `build/g4-virgl-20261022/chromium-userenv-r4-trial.py` is a small
copy of the preserved R3 adapter, selecting the explicit R4 view flag and helper.
Same readonly two-DLL view, API probe45, intermediate cleanup, ordinary eight
browser arguments, HTTP fixture, privateLO/backend and native observation.
285 total/55 cleanup/82 native observer/300 SSH/45 archive and900/90/80 outer
bounds remain. Diagnostic exit1 never means rendering acceptance.

Only the original parent output macro was crosscompiled into a fresh AMD64 PE;
original C++/contract source and old PEs remain unchanged. Strict compiler
`-Wall -Wextra -Werror`, exit0, empty stderr:
SHA `e15cf23578a9fa8304f95bc61a9f0ae714f73b650aa17bf9b1cede9af301cf95`,
306,537 bytes. No PE/Wine execution. Reused probe SHA `fb0dee8dcc3f5356bfec6bdbe24977fd2c101cd08f66ad7b041d645604c1eda3`,
SID DLLs89e93bd0…/3452a717…, existing runtime and existing R4 disk. No SID rebuild,
new layer, calibration or runtime extraction. Historical hashes warn; current
transfer integrity, concrete paths/owners/no-overwrite/space/budget checks remain.

Private artifacts under `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/`:

- `chromium-userenv-r4-20261025.preflight/`: immutable manifest, source TAR,
  PREPARED_NOT_RUN marker. Manifest SHA
  `0da5d96ae891d06d5b3d7f2781f4349185230b0fb9bbb15632ba9ee61793637b`;
  TAR SHA `d318b5231ac5bc34996675ca780214a6045d1960867764c6c527fbf5924f24b0`.
  Exact commands, 21 current transfer members, 258 runtime observations,
  module/probe/build/helper/source/test/seed inventories retained. Host run absent;
  guest path absence remains a future live guard, not observed here.
- `userenv-browser-r4-20261025.build/`: build script/source snapshots/output
  override/receipt/PE/compiler logs and focused test logs. Reused API probe and
  its host reference stay in the original R3 build directory.
- `chromium-userenv-r4-20261025.change/`: R3 adapter snapshot, R4 snapshot/diff,
  tracked source diff/current snapshots, audit and seal. Adapter SHA
  `1d8864c276a28cb0009b51395637aee668c6fc7f657f0c243cc4c14dda30a0e0`.
  Audit verified55 current inputs and all21 TAR members; final35-file seal.

Focused Linux command from the C-owned checkout's scripts directory:

```text
python3 -B -m unittest test_g4_compositor_scope test_g4_wexcomp_harness
 test_graphics_resources test_g4_userenv_view test_g4_chromium_userenv_r4_adapter
 test_g4_chromium_userenv_adapter test_g4_chromium_module_adapter
 test_g4_chromium_native_adapter test_g4_chromium_native_exit test_g4_native_exit
 test_g4_native_exit_calibration test_g4_edge_http_fixture -v
```

**91 PASS,37.785s,no skips**, including actual shared native C harness for R4
output contract. Final run used D: TMPDIR; all new child/build/evidence artifacts
are D:-resident. An initial child test failed its endpoint startup check;
the final test explicitly mocks endpoint/protocol while observing real child
environments and /proc. Earlier logs retained. `git diff --check` passes.

STOP for parent review before any live R4 dispatch. No Chromium outcome or G4
claim. C graphics branch only; M wine, D master, old preparations/runs, production
build files and other workers preserved.
