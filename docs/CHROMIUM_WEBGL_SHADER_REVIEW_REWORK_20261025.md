<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Parent review and required rework: shader fixture round 1

Review target: `48db5177f27cdba48b961d720163bdb2579d39f6`,
`codex/chromium-webgl-shader-fixture`. Verdict: **REWORK_REQUIRED**.
Keep the useful separate fixture; do not merge it or integrate it into R4 yet.

Parent independently read all four deliverables and ran the four requested Node
commands. Syntax and the three Node suites pass. However, the new harness
accepts broken draw setup and misrepresents resource lifetime. `git diff
48db5177^ 48db5177 --check` fails on four trailing-whitespace lines in the report.
No browser, VM, SSH, driver shader compiler or real rasterization was run.

## R1 — P1: the fake accepts missing or incorrect draw inputs

Location: `scripts/test_g4_shader_scene.js`, especially bufferData around185,
drawArrays around244, and the final success assertions around410 onwards.

The harness records many calls without checking their values or order.
`drawArrays` manufactures the correct image solely from drawCount. Parent ran
independent temporary copies of the actual test/scene and observed exit0 for
each of these mutations, while leaving the worker checkout unchanged:

| Scene mutation | Current test exit |
| --- | --- |
| Remove `gl.uniform1i(uSampler, 0)` | 0 |
| Remove all `gl.vertexAttribPointer` calls | 0 |
| Supply `new Uint8Array(16)` instead of textureBytes | 0 |
| Change MIN_FILTER from NEAREST to LINEAR (undefined in this fake) | 0 |
| Remove all `gl.bufferData` calls | 0 |
| Remove all `gl.useProgram` calls | 0 |
| Replace gl_Position expression with an invalid shader identifier | 0 |

The sampler omission alone can work on a real context because its default unit
is zero; it violates the explicit setup contract rather than independently
proving wrong pixels. Missing buffers/pointers/program and zero texture data
are substantive draw failures that the suite currently cannot distinguish.
The shader mutation shows a limit of this fake, not a real driver compilation
result. Do not claim Node compiles or fully validates GLSL syntax.

Required fix:

- Give the fake enough state/explicit assertions to validate current linked
  program, uploaded VBO contents, enabled attribute-to-buffer associations and
  pointer formats, uniform association/values, active texture unit/binding,
  upload format/type/bytes, min/mag/wrap parameters, viewport and state disables.
  Check sentinel clear -> draw -> readPixels order for each API and draw.
- It is fine to keep synthetic pixel output. Do not implement a GLSL compiler
  or rasterizer. It must not emit the expected image while required state is
  missing or wrong. Validate shader dialect and the intended source contract
  honestly; distinguish those assertions from actual shader compilation.
- Add automated mutation/contract regressions which prove the first six
  mutations above are rejected. Also reject an incorrect sampler unit, incorrect
  VBO/UV data, wrong texture format, flipped upload rows, and wrong vertex count.
  Mutants should run against in-memory scene variants or unique temporary
  copies, never overwrite the canonical source. Mutation tests must fail if
  their chosen transformation matches zero source locations.
- Failures must be distinguishable: do not allow every negative fixture to pass
  its test just because an unrelated fake assertion fails first. Assert each
  injected failure's expected API/stage/diagnostic or pixel coordinates/counts.
  Exercise both APIs and late resource failures where resources already exist.
- Test bounded compile/link logs and texture vertical flips as well as swaps;
  these original work-order requirements were not demonstrated by current tests.

## R2 — P2: current-program deletion is deferred

Location: `scripts/g4-shader-probe/scene.js` finally block around332; fake
`deleteProgram` and `useProgram` in the Node suite.

The last texture program is still current when `deleteProgram` is called.
GL marks a current program for deletion and keeps it until it stops being
current; attached shaders can consequently stay alive too. The fake immediately
removes every object from a Set and incorrectly certifies complete cleanup.
Parent's narrow fake variant that retains the current deleted program fails the
existing success test with `All programs must be deleted in webgl: 1 !== 0`.

Required fix: release the current program, normally with `gl.useProgram(null)`,
in the cleanup path before deleting programs/shaders. Retain final pixels and
handle partial creation/failure without masking the original diagnostic. Model
deferred deletion/attachment references sufficiently in tests to catch omission
of this release, on both successful and failed paths. Do not substitute context
destruction or clearing the canvas. Distinguish API deletion requests from
actual simulated lifetime completion.

References: [Khronos glDeleteProgram contract](https://wikis.khronos.org/opengl/GLAPI/glDeleteProgram),
[WebGL specification](https://registry.khronos.org/webgl/specs/latest/1.0/).

## R3 — P2: context loss produces contradictory per-API status

Location: scene.js context-loss handler around105; apiRecord.lost assignment
around327; test's after-ready loss case around448.

After the registered loss event, the title says `passed:false` and error
`webgl context lost`, but `apis.webgl.lost` remains false from the previous
successful snapshot. Parent appended an assertion of `lost === true` to the
existing loss test and reproduced failure `false !== true`.

Required fix: initialize the per-API record early enough that already-lost and
event-lost contexts can be recorded; update that API's lost status when loss
occurs. Preserve prior successful pixel information as historical evidence,
but never present that API as currently not lost. Add before/after-ready loss
checks for WebGL1 and WebGL2; the unaffected API's status must remain accurate.
No loss event may be followed by a new passed=true title.

## R4 — P2: --record emits a failed record on a passing suite

Location: `scripts/test_g4_shader_scene.js` around515.

The after-ready loss test mutates the successful `good` fixture's title; record
mode later prints `good.title`. Independently running
`node scripts/test_g4_shader_scene.js --record` exits0 but outputs a record with
passed=false and `webgl context lost`. This cannot be used as a valid successful
fixture record by later parser/integration tests.

Required fix: capture the original successful title immutably, or use a separate
successful run for --record. Preserve the loss regression. Add an independent
check that record mode emits exactly one WEXSHADER line, valid v1/nonce/both-API
successful result, with no preceding diagnostic stdout. Test failure records
separately rather than silently labelling them successful control output.

## R5 — P3: correct counts, evidence claims and commit whitespace

The negative-case table has **32** entries, but console/report say28. Generate
the displayed count from the executed table; distinguish table negatives from
separate loss/mutation cases. The report says API sequences, VBO bindings and
texture parameterization are verified when they were merely recorded. Rewrite
it around the final real assertions and retain explicit synthetic-only scope.
An empty working-tree `git diff --check` does not check the submitted commit.
Remove the four trailing-whitespace lines and check the whole delivered range.

## Implementation ownership and required handoff

Continue on your existing shader branch/worktree only. Parent source and all
R4 private artifacts remain read-only and unchanged. Implement only the fixture,
its tests and its report; no VM/browser run, HTTP/runner integration, package
installation, submodule/gitlink changes or production Wine changes. Existing
offline compile/link return-value injection coverage remains useful; preserve it.

Run (record exact exit codes and counts):

```text
node --check scripts/g4-shader-probe/scene.js
node scripts/test_g4_shader_scene.js
node scripts/test_g4_shader_scene.js --record
node scripts/test_g4_edge_scene.js
node scripts/test_g4_presentation_timing_fixture.js
git diff a2d0b060..HEAD --check
```

Additionally run the new mutation, expected-diagnostic, loss-status and
deferred-lifetime regressions. Report which tests are integrated into the main
Node command and which are separate commands. If you add a new test file, list
its exact invocation. Update docs/CHROMIUM_WEBGL_SHADER_FIXTURE_20261025.md with
the findings fixed, truthful counts and remaining real-browser NOT RUN status.
The GLSL ES1 varying precision defaults were checked against the specification
and are not a requested fix; do not add an unrelated shader change on that basis.

Commit AND push fixes to `codex/chromium-webgl-shader-fixture`, then reply to
mailbox codex with commit, report, actual tests and any unresolved findings.
This work order authorizes acknowledgement/blocker/completion replies to codex.
Stop for parent re-review. No merge or further task delegation beyond this scope.

Parent's private reproducibility evidence is at
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/shader-parent-review-20261025/`
(mutation-results.json, additional-results.json and temporary test copies).
Read-only access is allowed; do not overwrite or publish that directory.
