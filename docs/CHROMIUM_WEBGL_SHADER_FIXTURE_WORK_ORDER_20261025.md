<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Assignment: deterministic WebGL shader and texture workload

Implementer: Antigravity worker, mailbox `antigravity`. Parent retains review
and later real-browser execution. This assignment is independent of the prepared
R4 startup trial. Do not change that trial, its fixture, sources or preparations.

## Objective and scope

The existing `scripts/g4-edge-probe/scene.js` exercises clears, FBO creation and
readback, but never compiles shaders or draws textured geometry. Add a separate
original offline fixture which tests shader compilation, linking, vertex input,
uniform setup, actual drawing and texture sampling in **both WebGL1 and WebGL2**.
It will be used once full Chromium startup works; offline tests alone cannot
prove ANGLE/VirGL, shader correctness on a driver, acceleration or presentation.

Base your dedicated branch `codex/chromium-webgl-shader-fixture` on
`fcd34809513530fa96800446823fb64034fa50df` (or the subsequent work-order-only
commit on `origin/codex/g4-r1-completion`). Use your own D:-resident worktree;
do not switch or reset your completed review branch. Leave parent C: graphics
checkout, main D: master, M wine, all other workers and existing VM state alone.
No submodule initialization or gitlink staging is needed.

## Deliverable files

1. `scripts/g4-shader-probe/index.html`: original self-contained layout with
   distinct WebGL1/WebGL2 canvases and visible expected quadrant reference.
2. `scripts/g4-shader-probe/scene.js`: the workload and machine-readable result.
3. `scripts/test_g4_shader_scene.js`: dependency-free Node tests exercising the
   actual fixture source in an instrumented fake browser/GL environment.
4. `docs/CHROMIUM_WEBGL_SHADER_FIXTURE_20261025.md`: protocol, expected pixels,
   exact test results, implementation limits and later browser integration notes.

Keep changes to these new files unless an essential small shared change is
demonstrated and explained. Do not modify old scene.js/index.html, HTTP route
allowlists, native parent PEs, runner modes, R4 prep or existing result parsers.
No package downloads, browser launches, network listeners or VM/SSH authority.

## Required workload

- Parse the URL fragment nonce using the existing 32 lowercase hex convention.
  Accept no external resources or query-controlled code/paths. Invalid nonce
  must not yield a successful ready record. No fetch, CDN, frameworks, CSS/canvas
  fallback or vendor assets. Use plain JS/WebGL and original shaders.
- Acquire independent `webgl` and `webgl2` contexts with alpha=false,
  antialias=false and preserveDrawingBuffer=true. Require both; missing WebGL2
  is a recorded unsupported/failure outcome, not a pass or fallback to WebGL1.
- Use GLSL ES 1.00 syntax for WebGL1 and valid `#version 300 es` shaders for
  WebGL2 (in/out and explicit fragment output). Check vertex and fragment
  COMPILE_STATUS independently, then LINK_STATUS, retaining bounded useful logs
  and the failing API/stage. Validate required attributes/uniform locations and
  resource creation results. Use a VBO with explicit vertex attributes and draw
  geometry; a clear or manufactured JS result cannot substitute for drawing.
- Use a small fixed 8x8 viewport/canvas and two discriminating draws per API:
  first an opaque solid-color shader draw, then an opaque textured quad/triangles
  with interpolated texture coordinates and an explicit sampler uniform bound
  to texture unit 0. Read all 64 RGBA pixels after each draw and compare every
  byte with independently specified expected values; report the first mismatch.
- Texture draw: upload a 2x2 RGBA/UNSIGNED_BYTE texture from explicit byte data,
  with NEAREST min/mag, CLAMP_TO_EDGE S/T, no mipmap requirement. Define source
  row/origin and expected framebuffer coordinates explicitly. Required quadrants
  in GL lower-left coordinates: lower-left red, lower-right green, upper-left
  blue, upper-right yellow; each opaque, only 0 or 255 channels. Disable blend,
  dither, depth, scissor and other inherited state relevant to the deterministic
  checks. This is a default-framebuffer shader workload; an added FBO is not
  required. Do not use sRGB/filtering/AA tolerance to hide wrong pixels.
- Before each draw clear to a different sentinel color so a skipped/no-op draw
  fails exact comparison. The solid draw color must differ from the sentinel
  and every texture quadrant. Keep the final visible canvas at the texture draw.
- Require NO_ERROR and no context loss. Register context-loss handlers before
  rendering; loss during startup or after ready must invalidate the result.
  Release shader/program/buffer/texture resources in a finally path on success
  and failure. Preserve final framebuffer pixels after resource deletion.
- Publish a distinct title prefix `WEXSHADER ` with one JSON result, protocol
  `v:1`, nonce, `kind:'ready'`, passed boolean and separate per-API results.
  Include compile/link outcomes, solid/texture pixel-match counts (out of 64),
  compact corner sample arrays, GL error, context-loss status and renderer string
  when available. Record failing stage/message. Do not embed full image buffers
  or unbounded compiler logs in the title. Never claim backend acceleration from
  a renderer string. Do not reuse `WEXEDGE` or pretend existing parsers accept it.
- If using rAF to wait for rendering opportunities, use a bounded timer and test
  it. rAF/API readback success is not proof of a front-window screenshot. Do not
  add perpetual animation or polling.

## Required offline tests

Use Node's built-in assert/vm/fs facilities, as in `test_g4_edge_scene.js`.
Execute the actual new scene source, not a separately implemented copy. The fake
GL may return fixture pixel buffers, but must record and check the actual API
calls, shader dialect/source, buffer/attribute binding, texture upload/parameters,
sampler unit, viewport, sentinel clear and draw ordering. Label this synthetic
coverage; it does not compile GLSL or rasterize. Do not make the fake silently
accept arbitrary shader/program/texture calls or derive expected pixels from the
production comparison function.

Prove all of these produce passed=false or explicit invalid-input failure:

- Missing WebGL1, missing WebGL2; bad/missing nonce.
- Vertex compilation failure, fragment compilation failure, link failure for
  each API; useful bounded diagnostic stage/log retained.
- Required resource creation failure, missing attribute and missing uniform.
- Solid draw left as sentinel; textured draw left as sentinel; a wrong interior
  pixel (not only a corner), wrong alpha, and swapped/flipped texture quadrants.
- GL error, context already lost, loss before readiness and loss after readiness.
- If asynchronous readiness exists: timer expiry; no success before completion.

Assert cleanup on successful and failed paths (only created resources need
deletion), both APIs were attempted on the successful path, and the record's
nonce/version/schema/sample counts match. Prefer table-driven failure cases.
Run and record exact exit codes/output for:

```text
node --check scripts/g4-shader-probe/scene.js
node scripts/test_g4_shader_scene.js
node scripts/test_g4_edge_scene.js
node scripts/test_g4_presentation_timing_fixture.js
git diff --check
```

No broad unrelated test suite is needed. Use existing Node installation; if a
runtime/input is unavailable, report the exact blocker rather than installing
components or substituting an unreported fake pass.

## Completion and handoff

The document must distinguish implemented checks, synthetic Node results and
**real-browser validation NOT RUN**. Include exact expected solid color/texture
matrix/origin, representative compact result schema, commands, counts and any
open issues. Describe future local HTTP route/runner integration as a TODO;
do not implement it into the current startup diagnostic. Include no proprietary
payloads/art, raw profiles or private VM logs in Git.

Commit AND push your branch to origin. Send a bridge reply to mailbox `codex`
with actual worker/model, branch, commit, report filename, test summary and
remaining limitations, then stop for parent review. This work order authorizes
acknowledgement/progress/blocker/completion replies to `codex`. No master merge,
PR, sandbox bypass, feature spoof or further tasks without parent direction.
