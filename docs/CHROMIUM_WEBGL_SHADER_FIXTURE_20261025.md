<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium WebGL1/WebGL2 Deterministic Shader and Texture Fixture

**Date:** 2026-10-25  
**Branch:** `codex/chromium-webgl-shader-fixture`  
**Base Commit:** `a2d0b060b15aa2196b868fe9f9c4a3863fe38046`  
**Repository:** `https://github.com/zcomputerwiz/WineExplorer`  

---

## 1. Overview & Purpose

This document specifies the deterministic WebGL1 and WebGL2 shader and texture test fixture designed to validate GL context acquisition, shader compilation, program linking, VBO vertex attribute binding, texture sampling, and pixel-level rasterization in Chromium.

The fixture provides:
- **Dual API Coverage**: Independent, parallel evaluation of `webgl` (WebGL1, GLSL ES 1.00) and `webgl2` (WebGL2, `#version 300 es`).
- **Deterministic Workload**: Fixed 8×8 viewport/canvas (64 pixels total), explicit sentinel clears, single-color VBO draw, and 2×2 RGBA texture sampling.
- **Strict Verification**: Exact 64-pixel byte comparisons for both draws, requiring 100% pixel match with zero tolerance for color shifts or rounding errors.
- **Fail-Closed Safety**: Invalidation on context loss, GL errors, missing uniforms/attributes, compiler errors, or bad nonces.
- **Resource Hygiene**: Strict `finally` deletion of all created programs, shaders, VBO buffers, and textures while preserving rendered pixels in the default framebuffer.

---

## 2. Protocol Specification

### 2.1 Title Protocol & Wire Format

Upon completion (or failure), the fixture updates `document.title` with the distinct protocol prefix `WEXSHADER ` followed by a compact JSON record:

```text
WEXSHADER {"v":1,"nonce":"<32-hex>","kind":"ready","passed":<bool>,"apis":{...},"frozenAtMs":<number>}
```

* **Prefix:** `WEXSHADER ` (strictly distinct from `WEXEDGE`).
* **Protocol Version:** `v: 1`
* **Status:** `kind: 'ready'`
* **Passed Flag:** `passed: true` only if both WebGL1 and WebGL2 succeed across all shader, linking, draw, and 64-pixel comparisons.
* **Nonce:** Exactly 32 lowercase hex characters passed in the URL fragment (`#nonce=<32-hex>`). Invalid or missing nonces immediately fail (`passed: false`).

### 2.2 Representative Result Schema

```json
{
  "v": 1,
  "nonce": "e4d909c290d0fb1ca068ffaddf22cbd0",
  "kind": "ready",
  "passed": true,
  "apis": {
    "webgl": {
      "compileVs": true,
      "compileFsSolid": true,
      "compileFsTex": true,
      "linkSolid": true,
      "linkTex": true,
      "solidMatches": 64,
      "textureMatches": 64,
      "corners": {
        "ll": [255, 0, 0, 255],
        "lr": [0, 255, 0, 255],
        "ul": [0, 0, 255, 255],
        "ur": [255, 255, 0, 255]
      },
      "firstMismatch": null,
      "error": 0,
      "lost": false,
      "renderer": "ANGLE (VirGL Renderer)"
    },
    "webgl2": {
      "compileVs": true,
      "compileFsSolid": true,
      "compileFsTex": true,
      "linkSolid": true,
      "linkTex": true,
      "solidMatches": 64,
      "textureMatches": 64,
      "corners": {
        "ll": [255, 0, 0, 255],
        "lr": [0, 255, 0, 255],
        "ul": [0, 0, 255, 255],
        "ur": [255, 255, 0, 255]
      },
      "firstMismatch": null,
      "error": 0,
      "lost": false,
      "renderer": "ANGLE (VirGL Renderer)"
    }
  },
  "frozenAtMs": 100
}
```

If an error or mismatch occurs, `passed` is `false`, `firstMismatch` contains `{ draw, x, y, expected, actual }`, and top-level `error` contains the descriptive failure message.

---

## 3. Shader Dialects & Pipeline Construction

### 3.1 WebGL1 (GLSL ES 1.00)

* **Vertex Shader (`vsSource1`):**
  ```glsl
  attribute vec2 a_position;
  attribute vec2 a_texCoord;
  varying vec2 v_texCoord;
  void main() {
    gl_Position = vec4(a_position, 0.0, 1.0);
    v_texCoord = a_texCoord;
  }
  ```
* **Solid Fragment Shader (`fsSolidSource1`):**
  ```glsl
  precision mediump float;
  uniform vec4 u_color;
  void main() {
    gl_FragColor = u_color;
  }
  ```
* **Textured Fragment Shader (`fsTexSource1`):**
  ```glsl
  precision mediump float;
  varying vec2 v_texCoord;
  uniform sampler2D u_sampler;
  void main() {
    gl_FragColor = texture2D(u_sampler, v_texCoord);
  }
  ```

### 3.2 WebGL2 (`#version 300 es`)

* **Vertex Shader (`vsSource2`):**
  ```glsl
  #version 300 es
  in vec2 a_position;
  in vec2 a_texCoord;
  out vec2 v_texCoord;
  void main() {
    gl_Position = vec4(a_position, 0.0, 1.0);
    v_texCoord = a_texCoord;
  }
  ```
* **Solid Fragment Shader (`fsSolidSource2`):**
  ```glsl
  #version 300 es
  precision mediump float;
  uniform vec4 u_color;
  out vec4 fragColor;
  void main() {
    fragColor = u_color;
  }
  ```
* **Textured Fragment Shader (`fsTexSource2`):**
  ```glsl
  #version 300 es
  precision mediump float;
  in vec2 v_texCoord;
  uniform sampler2D u_sampler;
  out vec4 fragColor;
  void main() {
    fragColor = texture(u_sampler, v_texCoord);
  }
  ```

---

## 4. Expected Pixels, Quadrants & Geometry

### 4.1 Fixed Viewport & Coordinates

* **Canvas Dimensions:** 8 × 8 pixels (64 pixels total).
* **Coordinate System:** OpenGL lower-left origin:
  * `(x=0, y=0)` is lower-left.
  * `(x=7, y=7)` is upper-right.

### 4.2 Draw 1: Solid Color Draw

1. **Clear Sentinel 1:** Framebuffer cleared to Dark Gray `[64, 64, 64, 255]`.
2. **Draw:** Full-screen quad rendered with `progSolid` and uniform `u_color = [1.0, 0.0, 1.0, 1.0]` (Magenta).
3. **Expected Result:** All 64 pixels must equal `[255, 0, 255, 255]`.

### 4.3 Draw 2: Textured Quad Draw

1. **Clear Sentinel 2:** Framebuffer cleared to Cyan `[0, 255, 255, 255]`.
2. **Texture Data:** 2×2 RGBA texture uploaded via `texImage2D`:
   * Row 0 `(t=0)`: Texel `(0, 0)` = Red `[255, 0, 0, 255]`, Texel `(1, 0)` = Green `[0, 255, 0, 255]`.
   * Row 1 `(t=1)`: Texel `(0, 1)` = Blue `[0, 0, 255, 255]`, Texel `(1, 1)` = Yellow `[255, 255, 0, 255]`.
   * Filtering: `NEAREST` min/mag, `CLAMP_TO_EDGE` S/T wrap.
3. **Draw:** Full-screen quad rendered with `progTex` sampling texture unit 0.
4. **Expected Result:** Four distinct 4×4 quadrants in OpenGL lower-left orientation:

| Quadrant | X Coordinates | Y Coordinates | Expected RGBA Color |
| :--- | :--- | :--- | :--- |
| **Lower-Left (`ll`)** | `x ∈ [0..3]` | `y ∈ [0..3]` | `[255, 0, 0, 255]` (Red) |
| **Lower-Right (`lr`)** | `x ∈ [4..7]` | `y ∈ [0..3]` | `[0, 255, 0, 255]` (Green) |
| **Upper-Left (`ul`)** | `x ∈ [0..3]` | `y ∈ [4..7]` | `[0, 0, 255, 255]` (Blue) |
| **Upper-Right (`ur`)** | `x ∈ [4..7]` | `y ∈ [4..7]` | `[255, 255, 0, 255]` (Yellow) |

### 4.4 Compact Corner Samples

| Sample ID | Coordinates `(x, y)` | Expected RGBA |
| :--- | :--- | :--- |
| `ll` | `(0, 0)` | `[255, 0, 0, 255]` |
| `lr` | `(7, 0)` | `[0, 255, 0, 255]` |
| `ul` | `(0, 7)` | `[0, 0, 255, 255]` |
| `ur` | `(7, 7)` | `[255, 255, 0, 255]` |

---

## 5. Offline Test Results & Proof

The fixture was independently validated using the exact command suite specified in the work order.

### Exact Commands Executed

```powershell
node --check scripts/g4-shader-probe/scene.js
node scripts/test_g4_shader_scene.js
node scripts/test_g4_edge_scene.js
node scripts/test_g4_presentation_timing_fixture.js
git diff --check
```

### Execution Results

1. **`node --check scripts/g4-shader-probe/scene.js`**:
   - Exit code: `0` (clean syntax verification).
2. **`node scripts/test_g4_shader_scene.js`**:
   - Exit code: `0`
   - Output: `PASS WebGL shader & texture fixture: WebGL1/WebGL2, GLSL ES 1.00 & 300 es, solid/texture draws, 64-pixel match, sentinels, cleanup, 28 negative tests`
   - Validated:
     - Full successful execution across both WebGL1 and WebGL2.
     - 28 negative test cases verifying fail-closed handling for:
       - Missing WebGL1 / Missing WebGL2
       - Bad nonce / Missing nonce
       - Vertex, solid fragment, and textured fragment compile errors for both APIs
       - Program link errors for both APIs
       - Resource creation failures (`createBuffer`, `createTexture`, `createProgram`, `createShader`)
       - Missing vertex attributes (`a_position`, `a_texCoord`)
       - Missing uniforms (`u_color`, `u_sampler`)
       - Solid draw left as sentinel
       - Textured draw left as sentinel
       - Wrong interior pixel (e.g. at (2, 2))
       - Wrong alpha channel (e.g. alpha = 128)
       - Swapped texture quadrants
       - GL error presence (`getError() !== 0`)
       - Context already lost on acquisition
       - Context lost before readiness
       - Context lost after readiness (dynamic title invalidation)
       - Missing renderer string
       - rAF presentation opportunity timer expiration
     - Complete resource cleanup on both successful and failure paths (zero leaked shaders, programs, buffers, or textures).
3. **`node scripts/test_g4_edge_scene.js`**:
   - Exit code: `0`
   - Output: `PASS actual Edge fixture: GL/FBO/final colors, two frames, timeout/API/error/loss negatives`
4. **`node scripts/test_g4_presentation_timing_fixture.js`**:
   - Exit code: `0`
   - Output: `actual fixture timing/recovery, RGB observation, visible GL palettes, pre-fault gate, bad-front pairs and incidental-readback suppression: PASS`
5. **`git diff --check`**:
   - Exit code: `0` (no whitespace errors or conflict markers).

---

## 6. Implementation Limits & Scope Boundaries

* **Synthetic Coverage Limitation:**
  The Node test suite executes `scene.js` in an instrumented VM context with a mock GL implementation that verifies call sequences, shader source compliance, buffer bindings, texture parameterization, draw calls, and pixel expectations.
  *It does not compile GLSL against a real GPU driver or perform hardware rasterization.*
* **Real-Browser Validation Status:**
  **NOT RUN**. No VM boot, SSH session, live Chromium process, or browser execution was conducted as part of this offline fixture task.
* **Scope Discipline:**
  No modifications were made to existing test runners, HTTP fixtures, preflight scripts, or VM disk images.

---

## 7. Deliverable File Inventory

| File Path | Description |
| :--- | :--- |
| `scripts/g4-shader-probe/index.html` | Original offline page containing WebGL1/WebGL2 canvases (8×8) and an expected quadrant visual reference. |
| `scripts/g4-shader-probe/scene.js` | Main fixture implementation performing context setup, shader compile/link, VBO quad setup, texture upload, solid/textured draws, exact 64-pixel comparisons, and resource cleanup. |
| `scripts/test_g4_shader_scene.js` | Dependency-free Node test suite with comprehensive instrumented mock GL, testing full pass and 28 table-driven failure modes. |
| `docs/CHROMIUM_WEBGL_SHADER_FIXTURE_20261025.md` | This technical specification and test report. |

---

## 8. Future Integration Notes (TODO)

Once Chromium startup and window presentation are confirmed in live trials:
1. **HTTP Fixture Route:** Extend the Python test HTTP server (e.g. `g4_edge_http_fixture.py`) to expose `/g4-shader-probe/index.html` and `/g4-shader-probe/scene.js` under an allowlisted route.
2. **Runner Integration:** Add a dedicated test mode (e.g. `--shader-probe`) to the graphics runner to dispatch Chromium with the shader probe URL and parse the `WEXSHADER` title record.
3. **Driver Diagnostic Assertion:** Correlate reported `renderer` string and pixel matches with ANGLE and VirGL hardware acceleration telemetry.
