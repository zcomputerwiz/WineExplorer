<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# R4 parent review: ready for one startup diagnostic

Reviewed Luna implementation `5cb366f9af26cc77e7dc80e996cfab95de0a8111`,
the private R4 adapter/preparation, and independent review `05a31621`.
**No blocking finding or required rework. READY_FOR_ONE_R4_TRIAL**, subject to
the existing live prelaunch guards. No trial was launched during this review.

The child-only environment correction is small and correctly scoped:
`strip_wineprefix=False` preserves existing behavior; explicit userenv-r4 opts
in; only the copied compositor environment loses WINEPREFIX. Caller prefix,
shared Wayland endpoint setup, real prefix/process sampling and intermediate
Wine cleanup remain intact. R4 dispatch/view/helper use the same fixed fresh
output and existing runtime. The R3 failure is preserved and not relabelled.

Parent independently ran, in Debian WSL from the C: graphics checkout's scripts
directory, with Python `-B` and D:-resident TMPDIR:

```text
python3 -B -m unittest test_g4_compositor_scope test_g4_wexcomp_harness
 test_graphics_resources test_g4_userenv_view test_g4_chromium_userenv_r4_adapter
 test_g4_chromium_userenv_adapter test_g4_chromium_module_adapter
 test_g4_chromium_native_adapter test_g4_chromium_native_exit test_g4_native_exit
 test_g4_native_exit_calibration test_g4_edge_http_fixture -q

python3 -B -m unittest test_g4_virgl_launcher test_g4_virgl_budget
 test_g4_qmp_shutdown test_g4_guest_readiness -q
```

Results: **91 tests in40.886s and52 tests in4.148s, all pass, no skips**.
Each displayed module list is one command. The first group includes actual
native child environments and /proc sampling with mocked compositor endpoint/
protocol; the second includes synthetic ownership/budget/shutdown faults and
real socketpair/readiness subprocess controls. These are not real guest boot,
compositor rendering, browser startup or actual QMP guest-shutdown evidence.

Parent also independently checked:

- All35 retained sealed preparation/build/change files match their recorded
  contents; the source TAR matches its manifest and has exactly21 safe regular
  members. All21 current transfer inputs match that TAR; all10 observed host
  launch/adapter/stop/readiness/budget helper contents remain unchanged.
- Compiled native helper is AMD64 and contains the intended fixed R4 output and
  existing Chromium runtime strings. No PE was executed by these checks.
- Existing selected R4 qcow2 has the intended four-image backing chain, all on
  D:, ending at the original Debian source. Read-only qemu-img info succeeds;
  no image creation, mounting, repair or mutation was performed.
- No QEMU/Xvfb processes were present in Debian. Fresh host R4 run directory is
  absent. Host D: has about1.69TiB free; Debian root has about637GiB available.
  This is **not** the test guest's ext4 free-space measurement.
- Read launch/adapter/namespace/root-view/stop code: unique filesystem X socket
  directories, loopback-only consumer namespace, fixed read-only two-DLL view,
  pre-consumer API45/intermediate idle checks, explicit inner/outer VirGL checks,
  bounded native observer and clean shutdown evidence remain in the path.
  Existing bounds:900s launcher,90s shutdown reserve,80s stopper; raw UTC
  headroom674/preflight650/preconsumer435; runner285/cleanup55, helper82,
  consumer SSH300/archive45. Cleanup must show owned child reaping and actual
  graceful guest shutdown; fallback termination is retained failure evidence.

Private parent audit summary:
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/chromium-userenv-r4-parent-review-20261025.json`.
Historical metadata/hash/version changes remain diagnostic warnings. The checks
above concern current selected files/transfer, not a return to historical
filesystem identity gating or loaded-binary attestation.

## Remaining live checks and interpretation

The VM is off. Guest free space, absence of the fixed fresh guest stage/output,
current prefix idleness, guest readiness and effective backend cannot be
established by this offline review. The existing adapter checks these after
boot and before staging/profile/browser creation; insufficient space or occupied
paths must stop without overwriting retained evidence.

Next is one normal R4 diagnostic using the exact prepared launch/run/stop
commands in the existing immutable preflight. Preserve its native exit/journal,
HTTP requests, actual front if captured, API and cleanup evidence. Do not repeat
it automatically or speculate about another missing API before inspecting its
outcome. A held-handle native observation, API probe, diagnostic exit1 or
orchestration success alone is not rendering/G4/security acceptance.

The shader fixture and Gemini rework are separate, remain unimported and do not
block this trial. Main D: master, M wine, guest Wine/compositor core, OOBE/token
and other agents' work remain untouched. No master merge or browser success
claim follows from this review. No rework assignment was necessary.
