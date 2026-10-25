<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Narrow remaining shader test rework: current-program uniform validation

Antigravity/Gemini: continue only on your existing shader branch/worktree at
862e72f4. R5 preparation9b49ce70 passed parent review and does not need rework.
No new VM/SSH/browser, preparation, runtime or production changes in this task.

Parent reviewed the second shader rework and independently passed its syntax,
33-negative/24-mutation suite, legacy Edge scene and presentation-timing suites,
and full submitted-range whitespace check. The five previously surviving draw
mutants now have explicit checks. Keep those improvements and existing limits.

One specific promised program-state contract is still incorrect:
`uniform4f` and `uniform1i` in test_g4_shader_scene.js write to `loc.prog` without
checking `currentProgram === loc.prog`. Real WebGL uniform calls require a
current program and a location belonging to it. This fake can certify success
for uniform calls which would produce INVALID_OPERATION and no update.

Parent temporary LF scene reproduction:

1. Remove the existing `gl.useProgram(progSolid);` before setup.
2. Insert it immediately AFTER `gl.uniform4f(uColor, 1.0, 0.0, 1.0, 1.0);`.
3. Run the unchanged test script's --record mode against that actual mutated
   scene. It exits0 and emits passed:true, as does the baseline.

The baseline and mutant result summaries/copies are retained read-only under
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/shader-parent-final-review-20261025/`.
This is a demonstrated synthetic-test false pass, not a claim that the canonical
fixture makes this incorrect call. The canonical draw workload remains useful.

Required bounded fix:

- Both fake setters must require a current linked program matching the uniform
  location's owning program before changing uniform state. Invalid operations
  must not mutate another program; reflect a clear error/GL error in the fake.
  Keep simulated pixel output and do not build a general GL implementation.
- Add independent mutation/contract cases for a uniform call with no current
  program and with the wrong program current, for both uniform types. Ensure
  the baseline succeeds and these cases are rejected for the intended reason,
  not a zero-match transform or unrelated mutant assertion. Exercise WebGL1
  and WebGL2. Preserve previous33 negatives/24 mutations, loss, cleanup and
  clean single-line successful --record behavior.
- Run the existing exact Node commands and `git diff a2d0b060..HEAD --check`;
  update the report counts and accurately describe uniform state validation.
  Do not broaden scope into shader/fixture changes unless required by the fix.
- Commit AND push the shader branch, send codex the commit/report/results and
  stop for independent re-review. Acknowledgement/blocker/completion replies
  to codex are authorized. No merge/PR/VM work.

Reference: [Khronos WebGL uniform API requirements](https://registry.khronos.org/webgl/specs/latest/1.0/).
