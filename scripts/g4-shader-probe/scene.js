// SPDX-License-Identifier: GPL-2.0-or-later
// Original offline WebGL1 and WebGL2 shader and texture fixture.
// No external resources, network calls, CDN, or CSS canvas fallback.
'use strict';

(async function () {
  const result = { v: 1, nonce: null, kind: 'ready', passed: false, apis: {} };
  let lostEvent = false;

  try {
    const rawNonce = new URLSearchParams(location.hash.slice(1)).get('nonce');
    if (!/^[a-f0-9]{32}$/.test(rawNonce || '')) {
      throw new Error('Invalid nonce');
    }
    result.nonce = rawNonce;

    const vsSource1 = [
      'attribute vec2 a_position;',
      'attribute vec2 a_texCoord;',
      'varying vec2 v_texCoord;',
      'void main() {',
      '  gl_Position = vec4(a_position, 0.0, 1.0);',
      '  v_texCoord = a_texCoord;',
      '}'
    ].join('\n');

    const fsSolidSource1 = [
      'precision mediump float;',
      'uniform vec4 u_color;',
      'void main() {',
      '  gl_FragColor = u_color;',
      '}'
    ].join('\n');

    const fsTexSource1 = [
      'precision mediump float;',
      'varying vec2 v_texCoord;',
      'uniform sampler2D u_sampler;',
      'void main() {',
      '  gl_FragColor = texture2D(u_sampler, v_texCoord);',
      '}'
    ].join('\n');

    const vsSource2 = [
      '#version 300 es',
      'in vec2 a_position;',
      'in vec2 a_texCoord;',
      'out vec2 v_texCoord;',
      'void main() {',
      '  gl_Position = vec4(a_position, 0.0, 1.0);',
      '  v_texCoord = a_texCoord;',
      '}'
    ].join('\n');

    const fsSolidSource2 = [
      '#version 300 es',
      'precision mediump float;',
      'uniform vec4 u_color;',
      'out vec4 fragColor;',
      'void main() {',
      '  fragColor = u_color;',
      '}'
    ].join('\n');

    const fsTexSource2 = [
      '#version 300 es',
      'precision mediump float;',
      'in vec2 v_texCoord;',
      'uniform sampler2D u_sampler;',
      'out vec4 fragColor;',
      'void main() {',
      '  fragColor = texture(u_sampler, v_texCoord);',
      '}'
    ].join('\n');

    const quadPositions = new Float32Array([
      -1.0, -1.0,
       1.0, -1.0,
      -1.0,  1.0,
      -1.0,  1.0,
       1.0, -1.0,
       1.0,  1.0
    ]);

    const quadTexCoords = new Float32Array([
      0.0, 0.0,
      1.0, 0.0,
      0.0, 1.0,
      0.0, 1.0,
      1.0, 0.0,
      1.0, 1.0
    ]);

    const textureBytes = new Uint8Array([
      255,   0,   0, 255, // (s=0, t=0): lower-left: red
        0, 255,   0, 255, // (s=1, t=0): lower-right: green
        0,   0, 255, 255, // (s=0, t=1): upper-left: blue
      255, 255,   0, 255  // (s=1, t=1): upper-right: yellow
    ]);

    for (const api of ['webgl', 'webgl2']) {
      const canvas = document.getElementById(api);
      if (!canvas) throw new Error('Missing canvas element ' + api);

      const apiRecord = {
        compileVs: false,
        compileFsSolid: false,
        compileFsTex: false,
        linkSolid: false,
        linkTex: false,
        solidMatches: 0,
        textureMatches: 0,
        corners: null,
        firstMismatch: null,
        error: 0,
        lost: false,
        renderer: null
      };
      result.apis[api] = apiRecord;

      canvas.addEventListener('webglcontextlost', (e) => {
        lostEvent = true;
        result.passed = false;
        result.error = api + ' context lost';
        if (result.apis[api]) result.apis[api].lost = true;
        if (e && e.preventDefault) e.preventDefault();
        document.title = 'WEXSHADER ' + JSON.stringify(result);
      });

      const gl = canvas.getContext(api, { alpha: false, antialias: false, preserveDrawingBuffer: true });
      if (!gl) throw new Error('Missing ' + api);
      if (gl.isContextLost()) {
        apiRecord.lost = true;
        throw new Error(api + ' context already lost');
      }

      const createdShaders = [];
      const createdPrograms = [];
      const createdBuffers = [];
      const createdTextures = [];

      try {
        const vsSrc = api === 'webgl2' ? vsSource2 : vsSource1;
        const fsSolidSrc = api === 'webgl2' ? fsSolidSource2 : fsSolidSource1;
        const fsTexSrc = api === 'webgl2' ? fsTexSource2 : fsTexSource1;

        const compile = (type, src, stageName) => {
          const shader = gl.createShader(type);
          if (!shader) throw new Error(api + ' failed to create shader: ' + stageName);
          createdShaders.push(shader);
          gl.shaderSource(shader, src);
          gl.compileShader(shader);
          if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
            const log = String(gl.getShaderInfoLog(shader) || '').slice(0, 256);
            throw new Error(api + ' ' + stageName + ' compile failed: ' + log);
          }
          return shader;
        };

        const link = (vs, fs, progName) => {
          const prog = gl.createProgram();
          if (!prog) throw new Error(api + ' failed to create program: ' + progName);
          createdPrograms.push(prog);
          gl.attachShader(prog, vs);
          gl.attachShader(prog, fs);
          gl.linkProgram(prog);
          if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
            const log = String(gl.getProgramInfoLog(prog) || '').slice(0, 256);
            throw new Error(api + ' ' + progName + ' link failed: ' + log);
          }
          return prog;
        };

        const vsSolid = compile(gl.VERTEX_SHADER, vsSrc, 'vertex-solid');
        apiRecord.compileVs = true;

        const fsSolid = compile(gl.FRAGMENT_SHADER, fsSolidSrc, 'fragment-solid');
        apiRecord.compileFsSolid = true;

        const progSolid = link(vsSolid, fsSolid, 'solid-program');
        apiRecord.linkSolid = true;

        const vsTex = compile(gl.VERTEX_SHADER, vsSrc, 'vertex-tex');
        const fsTex = compile(gl.FRAGMENT_SHADER, fsTexSrc, 'fragment-tex');
        apiRecord.compileFsTex = true;

        const progTex = link(vsTex, fsTex, 'tex-program');
        apiRecord.linkTex = true;

        const aPosSolid = gl.getAttribLocation(progSolid, 'a_position');
        if (aPosSolid < 0) throw new Error(api + ' missing attribute a_position in solid');
        const uColor = gl.getUniformLocation(progSolid, 'u_color');
        if (!uColor) throw new Error(api + ' missing uniform u_color');

        const aPosTex = gl.getAttribLocation(progTex, 'a_position');
        if (aPosTex < 0) throw new Error(api + ' missing attribute a_position in tex');
        const aTexCoord = gl.getAttribLocation(progTex, 'a_texCoord');
        if (aTexCoord < 0) throw new Error(api + ' missing attribute a_texCoord in tex');
        const uSampler = gl.getUniformLocation(progTex, 'u_sampler');
        if (!uSampler) throw new Error(api + ' missing uniform u_sampler');

        const posBuffer = gl.createBuffer();
        if (!posBuffer) throw new Error(api + ' failed to create posBuffer');
        createdBuffers.push(posBuffer);
        gl.bindBuffer(gl.ARRAY_BUFFER, posBuffer);
        gl.bufferData(gl.ARRAY_BUFFER, quadPositions, gl.STATIC_DRAW);

        const texBuffer = gl.createBuffer();
        if (!texBuffer) throw new Error(api + ' failed to create texBuffer');
        createdBuffers.push(texBuffer);
        gl.bindBuffer(gl.ARRAY_BUFFER, texBuffer);
        gl.bufferData(gl.ARRAY_BUFFER, quadTexCoords, gl.STATIC_DRAW);

        const texture = gl.createTexture();
        if (!texture) throw new Error(api + ' failed to create texture');
        createdTextures.push(texture);
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, texture);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 2, 2, 0, gl.RGBA, gl.UNSIGNED_BYTE, textureBytes);

        gl.disable(gl.BLEND);
        gl.disable(gl.DEPTH_TEST);
        gl.disable(gl.DITHER);
        gl.disable(gl.SCISSOR_TEST);
        gl.disable(gl.CULL_FACE);
        gl.viewport(0, 0, 8, 8);

        // --- Draw 1: Solid Color Draw ---
        // Sentinel clear 1: [64, 64, 64, 255]
        gl.clearColor(64 / 255, 64 / 255, 64 / 255, 1.0);
        gl.clear(gl.COLOR_BUFFER_BIT);

        gl.useProgram(progSolid);
        gl.bindBuffer(gl.ARRAY_BUFFER, posBuffer);
        gl.enableVertexAttribArray(aPosSolid);
        gl.vertexAttribPointer(aPosSolid, 2, gl.FLOAT, false, 0, 0);
        gl.uniform4f(uColor, 1.0, 0.0, 1.0, 1.0); // Magenta [255, 0, 255, 255]
        gl.drawArrays(gl.TRIANGLES, 0, 6);

        const solidPixels = new Uint8Array(8 * 8 * 4);
        gl.readPixels(0, 0, 8, 8, gl.RGBA, gl.UNSIGNED_BYTE, solidPixels);

        let solidMatches = 0;
        let solidMismatch = null;
        const expectedSolid = [255, 0, 255, 255];
        for (let y = 0; y < 8; y++) {
          for (let x = 0; x < 8; x++) {
            const idx = (y * 8 + x) * 4;
            const px = [solidPixels[idx], solidPixels[idx + 1], solidPixels[idx + 2], solidPixels[idx + 3]];
            if (px[0] === expectedSolid[0] && px[1] === expectedSolid[1] && px[2] === expectedSolid[2] && px[3] === expectedSolid[3]) {
              solidMatches++;
            } else if (!solidMismatch) {
              solidMismatch = { draw: 'solid', x, y, expected: expectedSolid, actual: px };
            }
          }
        }
        apiRecord.solidMatches = solidMatches;
        if (solidMismatch) {
          apiRecord.firstMismatch = solidMismatch;
          throw new Error(api + ' solid draw mismatch at (' + solidMismatch.x + ',' + solidMismatch.y + ')');
        }

        // --- Draw 2: Textured Quad Draw ---
        // Sentinel clear 2: [0, 255, 255, 255] (Cyan)
        gl.clearColor(0.0, 1.0, 1.0, 1.0);
        gl.clear(gl.COLOR_BUFFER_BIT);

        gl.useProgram(progTex);
        gl.bindBuffer(gl.ARRAY_BUFFER, posBuffer);
        gl.enableVertexAttribArray(aPosTex);
        gl.vertexAttribPointer(aPosTex, 2, gl.FLOAT, false, 0, 0);

        gl.bindBuffer(gl.ARRAY_BUFFER, texBuffer);
        gl.enableVertexAttribArray(aTexCoord);
        gl.vertexAttribPointer(aTexCoord, 2, gl.FLOAT, false, 0, 0);

        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, texture);
        gl.uniform1i(uSampler, 0);
        gl.drawArrays(gl.TRIANGLES, 0, 6);

        const texPixels = new Uint8Array(8 * 8 * 4);
        gl.readPixels(0, 0, 8, 8, gl.RGBA, gl.UNSIGNED_BYTE, texPixels);

        let texMatches = 0;
        let texMismatch = null;
        for (let y = 0; y < 8; y++) {
          for (let x = 0; x < 8; x++) {
            const idx = (y * 8 + x) * 4;
            const px = [texPixels[idx], texPixels[idx + 1], texPixels[idx + 2], texPixels[idx + 3]];
            const expected = (x < 4 && y < 4) ? [255, 0, 0, 255] :
                             (x >= 4 && y < 4) ? [0, 255, 0, 255] :
                             (x < 4 && y >= 4) ? [0, 0, 255, 255] :
                                                 [255, 255, 0, 255];
            if (px[0] === expected[0] && px[1] === expected[1] && px[2] === expected[2] && px[3] === expected[3]) {
              texMatches++;
            } else if (!texMismatch) {
              texMismatch = { draw: 'texture', x, y, expected, actual: px };
            }
          }
        }
        apiRecord.textureMatches = texMatches;

        const getCorner = (x, y) => {
          const o = (y * 8 + x) * 4;
          return [texPixels[o], texPixels[o + 1], texPixels[o + 2], texPixels[o + 3]];
        };
        apiRecord.corners = {
          ll: getCorner(0, 0),
          lr: getCorner(7, 0),
          ul: getCorner(0, 7),
          ur: getCorner(7, 7)
        };

        if (texMismatch) {
          apiRecord.firstMismatch = texMismatch;
          throw new Error(api + ' texture draw mismatch at (' + texMismatch.x + ',' + texMismatch.y + ')');
        }

        const debug = gl.getExtension('WEBGL_debug_renderer_info');
        const renderer = String(gl.getParameter(debug ? debug.UNMASKED_RENDERER_WEBGL : gl.RENDERER));
        const glErr = gl.getError();
        const ctxLost = gl.isContextLost();

        apiRecord.renderer = renderer;
        apiRecord.error = glErr;
        apiRecord.lost = ctxLost;

        if (glErr !== gl.NO_ERROR) throw new Error(api + ' glError ' + glErr);
        if (ctxLost) throw new Error(api + ' context lost');
        if (!renderer || renderer === 'null' || renderer === 'undefined') throw new Error(api + ' missing renderer');
      } finally {
        if (gl.isContextLost()) {
          apiRecord.lost = true;
        }
        gl.useProgram(null);
        for (const p of createdPrograms) gl.deleteProgram(p);
        for (const s of createdShaders) gl.deleteShader(s);
        for (const b of createdBuffers) gl.deleteBuffer(b);
        for (const t of createdTextures) gl.deleteTexture(t);
      }
    }

    // Bounded rAF wait for presentation opportunity
    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('rAF timeout')), 1500);
      requestAnimationFrame(() => requestAnimationFrame(() => {
        clearTimeout(timer);
        resolve();
      }));
    });

    if (lostEvent) throw new Error('Context lost before ready');

    result.frozenAtMs = performance.now();
    result.passed = true;
  } catch (error) {
    result.passed = false;
    result.error = String(error && error.message ? error.message : error);
  }

  document.title = 'WEXSHADER ' + JSON.stringify(result);
})();
