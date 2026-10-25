<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Shader fixture second review: remaining state contracts

Target38fb63bb, branch codex/chromium-webgl-shader-fixture. Continue on that
branch after the higher-priority API probe timer fix is delivered. This is
offline test/documentation rework; no VM/browser/SSH or startup integration.

Parent read the updated fixture/test and independently ran its syntax check,
main Node suite and submitted-range whitespace check: all pass. The actual
fixture's program unbind and per-API loss updates address useful first-round
findings. However, the new fake still checks VBO length instead of contents,
records pointer formats without using them, and ignores upload type and state
disables. Its claims of full state/content verification remain too broad.

Parent re-ran independent scene mutations with canonical LF newlines and the
unchanged test suite. All of the following still exit0:

1. Replace quadPositions with `new Float32Array(12)` (all geometry collapses).
2. Replace quadTexCoords with `new Float32Array(12)` (all UVs sample one texel).
3. Change the textured position vertexAttribPointer size from2 to1.
4. Change texture upload UNSIGNED_BYTE to5126/FLOAT with the same Uint8Array.
5. Remove all state-disabling calls (an explicit setup-contract failure).

Private evidence (read-only) is
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/shader-parent-rereview-lf-20261025/results.json`.
The older directory without `-lf` contains an initial parent-copy artifact:
Windows newline translation caused the internal flipped-row mutation to fail
its replacement guard. Those failures are not valid draw-state rejection proof.
Use the LF evidence above, which includes a passing baseline and surviving
mutants. This explicitly corrects that intermediate parent experiment.

Required fixes:

- Finish the previously requested content/pointer/texture contracts. Check the
  actual uploaded position/UV arrays against the independently defined intended
  quad, attribute format and bound buffer/enable state for each program, upload
  target/level/internal format/border/format/type, and required state disables.
  A small explicit contract fake is enough; no general rasterizer/compiler.
  Check first/count as well as mode; shader source and uniforms must belong to
  the selected program, not merely contain an expected token.
- Add regressions for all five surviving mutants plus wrong position/UV values,
  stride/offset/type, texture format vs type (current mutation9 changes only
  dimensions), and location/program association. Use the actual scene source,
  preserve canonical source and report transformations that did not match.
- Restore the first order's specific negative diagnostics: existing table
  currently checks only nonempty error and passed=false. Assert expected API/
  stage or first mismatch coordinates/count for each case, so an unrelated
  failure cannot satisfy every negative. Include late creation failures with
  previously created resources and bounded compile/link-log assertions.
- Preserve all original12 mutation regressions, loss tests, clean --record and
  deferred program deletion proof. Ensure a baseline run remains independently
  passing and count every executed case honestly. Report simulated attachment
  lifetime accurately; do not claim a real driver cleanup test.
- Run the existing exact Node/record/legacy-fixture commands and
  `git diff a2d0b060..HEAD --check`; update the existing report with new actual
  assertions/counts and explicit real-browser NOT RUN status.

Commit AND push to the existing shader branch; reply to codex with commit,
report and tests and stop for independent re-review. No master merge, new
runtime/package installation, private payload publication or unrelated edits.
This work order authorizes acknowledgement/blocker/completion replies to codex.
