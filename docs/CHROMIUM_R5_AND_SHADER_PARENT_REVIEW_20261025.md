<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# R5 diagnostic and shader uniform re-review

Parent independently reviewed R5 worker report5823aed6 and shader0d96719a.

R5 executed once. All53 retained seal entries and22 archive file contents match
independent reads (archive paths resolved inside chromium-native-evidence, not
against the unrelated host results.json). Normal builtin SID probe45/45 passed.
The native held-handle journal binds WinPID220 and reports0x80000003 after about
2seconds; Linux parent naturally exits0, reaped before prefix cleanup. HTTP has
zero requests. Parent viewed terminal-front.png: console/error text, no browser
fixture. No Chromium rendering, sandbox or G4 acceptance.

Parent read QMP transcript: matching capabilities/powerdown ACKs and subsequent
SHUTDOWN guest=true/reason guest-shutdown, QEMU0, no fallback, stopped QEMU/Xvfb;
controller0 and owned absence are additionally recorded in worker audit. Both
actual VirGL renderer records and normal-route API probe pass; no graphics-cause
inference follows. All fixed R5 paths are consumed and must be preserved.

The new candidate startup failure is unresolved kernelbase!AppContainerRegisterSid
at36.051 on00dc:01f0 followed by breakpoint at36.060 on that same PID/TID.
The exact runtime source revision7d9824099030f5c28c939ba08654562faaad7c09,
sandbox/win/src/app_container_base.cc, lines43-86 binds this and three related
exports with CHECK(fn). CreateProfile calls RegisterSid at189, handles
ERROR_ALREADY_EXISTS at190 and otherwise requires real registration before
profile-directory creation. This is strong source/log-consistent evidence for
the next investigation; no symbolized PC/callstack proves the exact failing
CHECK yet. Do not silently convert it to a graphics fault or use success stubs.

Reference: https://chromium.googlesource.com/chromium/src/+/7d9824099030f5c28c939ba08654562faaad7c09/sandbox/win/src/app_container_base.cc

Shader re-review: actual two-file diff0d96719a reviewed. Independently ran
node --check scripts/g4-shader-probe/scene.js; node scripts/test_g4_shader_scene.js;
node scripts/test_g4_shader_scene.js --record; node scripts/test_g4_edge_scene.js;
node scripts/test_g4_presentation_timing_fixture.js; git diff a2d0b060..HEAD --check.
All pass:35negative/28mutation cases; clean successful single-line ready record.
Current-program uniform rejection now generates INVALID_OPERATION without
updating state; canonical workload unchanged. ACCEPTED for scoped offline
fixture use. This is synthetic validation, not actual shader/driver/presentation
proof. Null uniform-location no-op/general WebGL conformance remain outside this
fixture's nonnull checked-location path; do not claim a complete GL emulator.
Shader files remain on their dedicated pushed branch, unimported into R5.

Next: original Windows oracle for AppContainer registration family, then a
reviewed real Wine implementation and one fresh runtime test. No R5 retry,
master merge, unrelated security/OOBE import or vendor payload publication.
