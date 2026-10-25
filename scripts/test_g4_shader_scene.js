// SPDX-License-Identifier: GPL-2.0-or-later
// Dependency-free Node tests for the WebGL1/WebGL2 shader and texture fixture.
'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const { spawnSync } = require('node:child_process');

const scenePath = path.join(__dirname, 'g4-shader-probe/scene.js');
const source = fs.readFileSync(scenePath, 'utf8');
const validNonce = 'e'.repeat(32);

async function runFixture(options = {}) {
  let title = '';
  const frames = [];
  const timers = [];
  const clearedTimers = [];
  const events = {};
  const apiCalls = { webgl: [], webgl2: [] };
  const trackedResources = {
    webgl: { shaders: new Set(), programs: new Set(), buffers: new Set(), textures: new Set() },
    webgl2: { shaders: new Set(), programs: new Set(), buffers: new Set(), textures: new Set() }
  };

  const canvases = {};

  for (const api of ['webgl', 'webgl2']) {
    let currentClearColor = [0, 0, 0, 255];
    let framebufferPixels = new Uint8Array(8 * 8 * 4); // 64 pixels * 4
    let drawCount = 0;

    // R1 & R2: Active GL state tracking
    let currentProgram = null;
    let currentArrayBuffer = null;
    const vertexAttribs = {}; // loc -> { enabled, buffer, size, type, norm, stride, offset }
    const uniforms = new Map(); // program -> Map(name -> value)
    let activeTextureUnit = 33984; // gl.TEXTURE0
    let boundTexture = null;
    let currentViewport = null;
    let lastClear = null;
    const markedForDeletion = new Set();

    const gl = {
      VERTEX_SHADER: 35633,
      FRAGMENT_SHADER: 35632,
      COMPILE_STATUS: 35713,
      LINK_STATUS: 35714,
      ARRAY_BUFFER: 34962,
      STATIC_DRAW: 35044,
      FLOAT: 5126,
      TEXTURE_2D: 3553,
      TEXTURE0: 33984,
      TEXTURE_MIN_FILTER: 10241,
      TEXTURE_MAG_FILTER: 10240,
      TEXTURE_WRAP_S: 10242,
      TEXTURE_WRAP_T: 10243,
      NEAREST: 9728,
      CLAMP_TO_EDGE: 33071,
      RGBA: 6408,
      UNSIGNED_BYTE: 5121,
      BLEND: 3042,
      DEPTH_TEST: 2929,
      DITHER: 3024,
      SCISSOR_TEST: 3089,
      CULL_FACE: 2884,
      COLOR_BUFFER_BIT: 16384,
      TRIANGLES: 4,
      NO_ERROR: 0,
      RENDERER: 7937,

      createShader(type) {
        apiCalls[api].push({ method: 'createShader', type });
        if (options.failCreateShader && (options.targetApi === undefined || options.targetApi === api)) return null;
        const s = { id: Symbol('shader'), type, source: null, compiled: false };
        trackedResources[api].shaders.add(s);
        return s;
      },
      shaderSource(shader, src) {
        apiCalls[api].push({ method: 'shaderSource', shader, src });
        shader.source = src;
        if (api === 'webgl' && src.includes('#version 300 es')) {
          throw new Error('WebGL1 must not contain #version 300 es');
        }
        if (api === 'webgl2' && !src.startsWith('#version 300 es')) {
          throw new Error('WebGL2 must start with #version 300 es');
        }
      },
      compileShader(shader) {
        apiCalls[api].push({ method: 'compileShader', shader });
        const isVs = shader.type === gl.VERTEX_SHADER;
        const isFs = shader.type === gl.FRAGMENT_SHADER;
        if (isVs && !shader.source.includes('gl_Position')) {
          shader.compiled = false;
          shader.infoLog = 'Contract check: vertex shader missing gl_Position';
        } else if (options.failCompileVs && isVs && (options.targetApi === undefined || options.targetApi === api)) {
          shader.compiled = false;
          shader.infoLog = 'Simulated vertex compile error';
        } else if (options.failCompileFsSolid && isFs && shader.source.includes('u_color') && (options.targetApi === undefined || options.targetApi === api)) {
          shader.compiled = false;
          shader.infoLog = 'Simulated fragment solid compile error';
        } else if (options.failCompileFsTex && isFs && shader.source.includes('u_sampler') && (options.targetApi === undefined || options.targetApi === api)) {
          shader.compiled = false;
          shader.infoLog = 'Simulated fragment tex compile error';
        } else {
          shader.compiled = true;
          shader.infoLog = '';
        }
      },
      getShaderParameter(shader, pname) {
        if (pname === gl.COMPILE_STATUS) return shader.compiled;
        return null;
      },
      getShaderInfoLog(shader) {
        return shader.infoLog || '';
      },
      deleteShader(shader) {
        apiCalls[api].push({ method: 'deleteShader', shader });
        trackedResources[api].shaders.delete(shader);
      },

      createProgram() {
        apiCalls[api].push({ method: 'createProgram' });
        if (options.failCreateProgram && (options.targetApi === undefined || options.targetApi === api)) return null;
        const p = { id: Symbol('program'), vs: null, fs: null, linked: false };
        trackedResources[api].programs.add(p);
        return p;
      },
      attachShader(prog, shader) {
        apiCalls[api].push({ method: 'attachShader', prog, shader });
        if (shader.type === gl.VERTEX_SHADER) prog.vs = shader;
        if (shader.type === gl.FRAGMENT_SHADER) prog.fs = shader;
      },
      linkProgram(prog) {
        apiCalls[api].push({ method: 'linkProgram', prog });
        const isSolid = prog.fs && prog.fs.source && prog.fs.source.includes('u_color');
        const isTex = prog.fs && prog.fs.source && prog.fs.source.includes('u_sampler');
        if (options.failLinkSolid && isSolid && (options.targetApi === undefined || options.targetApi === api)) {
          prog.linked = false;
          prog.infoLog = 'Simulated solid link error';
        } else if (options.failLinkTex && isTex && (options.targetApi === undefined || options.targetApi === api)) {
          prog.linked = false;
          prog.infoLog = 'Simulated tex link error';
        } else if (options.failLink && (options.targetApi === undefined || options.targetApi === api)) {
          prog.linked = false;
          prog.infoLog = 'Simulated general link error';
        } else {
          prog.linked = true;
          prog.infoLog = '';
        }
      },
      getProgramParameter(prog, pname) {
        if (pname === gl.LINK_STATUS) return prog.linked;
        return null;
      },
      getProgramInfoLog(prog) {
        return prog.infoLog || '';
      },
      deleteProgram(prog) {
        apiCalls[api].push({ method: 'deleteProgram', prog });
        // R2: Model deferred program deletion: if program is currently bound, defer cleanup
        if (prog === currentProgram) {
          markedForDeletion.add(prog);
        } else {
          trackedResources[api].programs.delete(prog);
        }
      },

      getAttribLocation(prog, name) {
        apiCalls[api].push({ method: 'getAttribLocation', name });
        if (options.missingAttrib === name) return -1;
        if (name === 'a_position') return 0;
        if (name === 'a_texCoord') return 1;
        return -1;
      },
      getUniformLocation(prog, name) {
        apiCalls[api].push({ method: 'getUniformLocation', name });
        if (options.missingUniform === name) return null;
        return { name };
      },
      enableVertexAttribArray(loc) {
        apiCalls[api].push({ method: 'enableVertexAttribArray', loc });
        if (!vertexAttribs[loc]) vertexAttribs[loc] = {};
        vertexAttribs[loc].enabled = true;
      },
      vertexAttribPointer(loc, size, type, norm, stride, offset) {
        apiCalls[api].push({ method: 'vertexAttribPointer', loc, size, type, norm, stride, offset });
        if (!vertexAttribs[loc]) vertexAttribs[loc] = {};
        vertexAttribs[loc].buffer = currentArrayBuffer;
        vertexAttribs[loc].size = size;
        vertexAttribs[loc].type = type;
        vertexAttribs[loc].norm = norm;
        vertexAttribs[loc].stride = stride;
        vertexAttribs[loc].offset = offset;
      },
      uniform4f(loc, r, g, b, a) {
        apiCalls[api].push({ method: 'uniform4f', loc, val: [r, g, b, a] });
        if (currentProgram && loc && loc.name) {
          if (!uniforms.has(currentProgram)) uniforms.set(currentProgram, new Map());
          uniforms.get(currentProgram).set(loc.name, [r, g, b, a]);
        }
      },
      uniform1i(loc, v) {
        apiCalls[api].push({ method: 'uniform1i', loc, val: v });
        if (currentProgram && loc && loc.name) {
          if (!uniforms.has(currentProgram)) uniforms.set(currentProgram, new Map());
          uniforms.get(currentProgram).set(loc.name, v);
        }
      },

      createBuffer() {
        apiCalls[api].push({ method: 'createBuffer' });
        if (options.failCreateBuffer && (options.targetApi === undefined || options.targetApi === api)) return null;
        const b = { id: Symbol('buffer'), data: null };
        trackedResources[api].buffers.add(b);
        return b;
      },
      bindBuffer(target, buf) {
        apiCalls[api].push({ method: 'bindBuffer', target, buf });
        if (target === gl.ARRAY_BUFFER) {
          currentArrayBuffer = buf;
        }
      },
      bufferData(target, data, usage) {
        apiCalls[api].push({ method: 'bufferData', target, length: data ? data.length : 0, usage });
        if (target === gl.ARRAY_BUFFER && currentArrayBuffer) {
          currentArrayBuffer.data = data;
        }
      },
      deleteBuffer(buf) {
        apiCalls[api].push({ method: 'deleteBuffer', buf });
        trackedResources[api].buffers.delete(buf);
      },

      createTexture() {
        apiCalls[api].push({ method: 'createTexture' });
        if (options.failCreateTexture && (options.targetApi === undefined || options.targetApi === api)) return null;
        const t = { id: Symbol('texture'), params: {}, image: null, width: 0, height: 0, format: 0 };
        trackedResources[api].textures.add(t);
        return t;
      },
      activeTexture(unit) {
        apiCalls[api].push({ method: 'activeTexture', unit });
        activeTextureUnit = unit;
      },
      bindTexture(target, tex) {
        apiCalls[api].push({ method: 'bindTexture', target, tex });
        if (target === gl.TEXTURE_2D) {
          boundTexture = tex;
        }
      },
      texParameteri(target, pname, param) {
        apiCalls[api].push({ method: 'texParameteri', target, pname, param });
        if (target === gl.TEXTURE_2D && boundTexture) {
          if (!boundTexture.params) boundTexture.params = {};
          boundTexture.params[pname] = param;
        }
      },
      texImage2D(target, level, ifmt, w, h, border, fmt, type, pixels) {
        apiCalls[api].push({ method: 'texImage2D', w, h, length: pixels ? pixels.length : 0 });
        if (target === gl.TEXTURE_2D && boundTexture) {
          boundTexture.image = pixels ? new Uint8Array(pixels) : null;
          boundTexture.width = w;
          boundTexture.height = h;
          boundTexture.format = fmt;
        }
      },
      deleteTexture(tex) {
        apiCalls[api].push({ method: 'deleteTexture', tex });
        trackedResources[api].textures.delete(tex);
      },

      disable(cap) {
        apiCalls[api].push({ method: 'disable', cap });
      },
      viewport(x, y, w, h) {
        apiCalls[api].push({ method: 'viewport', x, y, w, h });
        currentViewport = { x, y, w, h };
      },
      clearColor(r, g, b, a) {
        currentClearColor = [Math.round(r * 255), Math.round(g * 255), Math.round(b * 255), Math.round(a * 255)];
        apiCalls[api].push({ method: 'clearColor', color: currentClearColor });
      },
      clear(mask) {
        apiCalls[api].push({ method: 'clear', mask, color: currentClearColor });
        lastClear = [...currentClearColor];
        for (let i = 0; i < 64; i++) {
          framebufferPixels[i * 4 + 0] = currentClearColor[0];
          framebufferPixels[i * 4 + 1] = currentClearColor[1];
          framebufferPixels[i * 4 + 2] = currentClearColor[2];
          framebufferPixels[i * 4 + 3] = currentClearColor[3];
        }
      },
      useProgram(prog) {
        apiCalls[api].push({ method: 'useProgram', prog });
        currentProgram = prog;
        // R2: If currentProgram set to null, flush all deferred programs marked for deletion
        if (prog === null) {
          for (const p of markedForDeletion) {
            trackedResources[api].programs.delete(p);
          }
          markedForDeletion.clear();
        }
      },
      drawArrays(mode, first, count) {
        drawCount++;
        apiCalls[api].push({ method: 'drawArrays', mode, first, count, drawCount });
        assert.equal(mode, gl.TRIANGLES);

        if (drawCount === 1) {
          // --- Draw 1: Solid Color Draw ---
          let valid = true;
          if (count !== 6) valid = false;
          if (!currentProgram || !currentProgram.fs || !currentProgram.fs.source || !currentProgram.fs.source.includes('u_color')) valid = false;
          const posAttrib = vertexAttribs[0];
          if (!posAttrib || !posAttrib.enabled || !posAttrib.buffer || !posAttrib.buffer.data || posAttrib.buffer.data.length < 12) valid = false;
          const progUniforms = uniforms.get(currentProgram);
          const col = progUniforms ? progUniforms.get('u_color') : null;
          if (!col || col[0] !== 1 || col[1] !== 0 || col[2] !== 1 || col[3] !== 1) valid = false;
          if (!currentViewport || currentViewport.x !== 0 || currentViewport.y !== 0 || currentViewport.w !== 8 || currentViewport.h !== 8) valid = false;
          if (!lastClear || lastClear[0] !== 64 || lastClear[1] !== 64 || lastClear[2] !== 64 || lastClear[3] !== 255) valid = false;

          if (valid && !options.solidLeftAsSentinel) {
            for (let i = 0; i < 64; i++) {
              framebufferPixels[i * 4 + 0] = 255;
              framebufferPixels[i * 4 + 1] = 0;
              framebufferPixels[i * 4 + 2] = 255;
              framebufferPixels[i * 4 + 3] = 255;
            }
          }
        } else if (drawCount === 2) {
          // --- Draw 2: Textured Quad Draw ---
          let valid = true;
          if (count !== 6) valid = false;
          if (!currentProgram || !currentProgram.fs || !currentProgram.fs.source || !currentProgram.fs.source.includes('u_sampler')) valid = false;
          const posAttrib = vertexAttribs[0];
          const texAttrib = vertexAttribs[1];
          if (!posAttrib || !posAttrib.enabled || !posAttrib.buffer || !posAttrib.buffer.data || posAttrib.buffer.data.length < 12) valid = false;
          if (!texAttrib || !texAttrib.enabled || !texAttrib.buffer || !texAttrib.buffer.data || texAttrib.buffer.data.length < 12) valid = false;
          const progUniforms = uniforms.get(currentProgram);
          const sampler = progUniforms ? progUniforms.get('u_sampler') : null;
          if (sampler !== 0) valid = false;
          if (activeTextureUnit !== gl.TEXTURE0) valid = false;
          if (!boundTexture || !boundTexture.params) valid = false;
          else {
            if (boundTexture.params[gl.TEXTURE_MIN_FILTER] !== gl.NEAREST) valid = false;
            if (boundTexture.params[gl.TEXTURE_MAG_FILTER] !== gl.NEAREST) valid = false;
            if (boundTexture.params[gl.TEXTURE_WRAP_S] !== gl.CLAMP_TO_EDGE) valid = false;
            if (boundTexture.params[gl.TEXTURE_WRAP_T] !== gl.CLAMP_TO_EDGE) valid = false;
          }
          if (!boundTexture || !boundTexture.image || boundTexture.image.length !== 16 || boundTexture.width !== 2 || boundTexture.height !== 2 || boundTexture.format !== gl.RGBA) valid = false;
          if (!currentViewport || currentViewport.x !== 0 || currentViewport.y !== 0 || currentViewport.w !== 8 || currentViewport.h !== 8) valid = false;
          if (!lastClear || lastClear[0] !== 0 || lastClear[1] !== 255 || lastClear[2] !== 255 || lastClear[3] !== 255) valid = false;

          if (valid && !options.textureLeftAsSentinel) {
            for (let y = 0; y < 8; y++) {
              for (let x = 0; x < 8; x++) {
                let tx = (x < 4) ? 0 : 1;
                let ty = (y < 4) ? 0 : 1;
                if (options.swappedQuadrants) {
                  tx = (tx === 0) ? 1 : 0;
                }
                const texIdx = (ty * 2 + tx) * 4;
                let r = boundTexture.image[texIdx + 0];
                let g = boundTexture.image[texIdx + 1];
                let b = boundTexture.image[texIdx + 2];
                let a = boundTexture.image[texIdx + 3];

                if (options.wrongInteriorPixel && x === 2 && y === 2) {
                  r = 0; g = 0; b = 0; a = 255;
                }
                if (options.wrongAlpha && x === 0 && y === 0) {
                  a = 128;
                }

                const pixIdx = (y * 8 + x) * 4;
                framebufferPixels[pixIdx + 0] = r;
                framebufferPixels[pixIdx + 1] = g;
                framebufferPixels[pixIdx + 2] = b;
                framebufferPixels[pixIdx + 3] = a;
              }
            }
          }
        }
      },
      readPixels(x, y, w, h, fmt, type, out) {
        apiCalls[api].push({ method: 'readPixels', x, y, w, h });
        out.set(framebufferPixels);
      },
      getError() {
        return options.glError ? 1280 : gl.NO_ERROR;
      },
      isContextLost() {
        return !!options.contextLostAlready;
      },
      getExtension(name) {
        if (name === 'WEBGL_debug_renderer_info') return { UNMASKED_RENDERER_WEBGL: 37446 };
        return null;
      },
      getParameter(pname) {
        if (options.renderer !== undefined) return options.renderer;
        return 'Mock ANGLE (VirGL test fixture)';
      }
    };

    canvases[api] = {
      addEventListener(name, callback) {
        events[api + ':' + name] = callback;
      },
      getContext(name, attrs) {
        assert.equal(name, api);
        assert.equal(attrs.alpha, false);
        assert.equal(attrs.antialias, false);
        assert.equal(attrs.preserveDrawingBuffer, true);
        if (options.missing === api) return null;
        return gl;
      }
    };
  }

  const document = {
    getElementById(id) {
      return canvases[id] || null;
    },
    set title(val) {
      title = val;
    },
    get title() {
      return title;
    }
  };

  const hashNonce = options.badNonce ? '#nonce=bad' :
                    options.missingNonce ? '' :
                    '#nonce=' + validNonce;

  const context = {
    document,
    location: { hash: hashNonce },
    URLSearchParams,
    Uint8Array,
    Float32Array,
    performance: { now: () => 100 },
    requestAnimationFrame(cb) {
      frames.push(cb);
    },
    setTimeout(cb, ms) {
      timers.push(cb);
      return timers.length;
    },
    clearTimeout(id) {
      clearedTimers.push(id);
    }
  };

  const effectiveSource = options.sourceOverride || source;

  if (options.lostBeforeReady) {
    const targetApi = options.targetApi || 'webgl';
    const evtKey = targetApi + ':webglcontextlost';
    if (events[evtKey]) {
      events[evtKey]({ preventDefault() {} });
    }
  }

  const promise = vm.runInNewContext(effectiveSource, context);

  if (options.lostBeforeReady) {
    const targetApi = options.targetApi || 'webgl';
    const evtKey = targetApi + ':webglcontextlost';
    if (events[evtKey]) {
      events[evtKey]({ preventDefault() {} });
    }
  }

  if (frames.length) {
    if (!options.lostBeforeReady) {
      assert.equal(title, '', 'Must not set ready title before rAF presentation opportunity');
    }
    if (options.timeout) {
      timers[0]();
    } else {
      frames.shift()(16);
      if (!options.lostBeforeReady) {
        assert.equal(title, '', 'Must not set ready title after only one frame');
      }
      frames.shift()(32);
    }
  }

  await promise;

  let record = null;
  if (title.startsWith('WEXSHADER ')) {
    record = JSON.parse(title.slice(10));
  }

  return {
    document,
    get title() { return title; },
    record,
    events,
    apiCalls,
    trackedResources,
    clearedTimers
  };
}

const negativeCases = [
  { label: 'missing webgl1', options: { missing: 'webgl' } },
  { label: 'missing webgl2', options: { missing: 'webgl2' } },
  { label: 'bad nonce', options: { badNonce: true } },
  { label: 'missing nonce', options: { missingNonce: true } },

  // Shader compilation & linking
  { label: 'fail compile vertex webgl1', options: { failCompileVs: true, targetApi: 'webgl' } },
  { label: 'fail compile vertex webgl2', options: { failCompileVs: true, targetApi: 'webgl2' } },
  { label: 'fail compile fs solid webgl1', options: { failCompileFsSolid: true, targetApi: 'webgl' } },
  { label: 'fail compile fs solid webgl2', options: { failCompileFsSolid: true, targetApi: 'webgl2' } },
  { label: 'fail compile fs tex webgl1', options: { failCompileFsTex: true, targetApi: 'webgl' } },
  { label: 'fail compile fs tex webgl2', options: { failCompileFsTex: true, targetApi: 'webgl2' } },
  { label: 'fail link solid webgl1', options: { failLinkSolid: true, targetApi: 'webgl' } },
  { label: 'fail link solid webgl2', options: { failLinkSolid: true, targetApi: 'webgl2' } },
  { label: 'fail link tex webgl1', options: { failLinkTex: true, targetApi: 'webgl' } },
  { label: 'fail link tex webgl2', options: { failLinkTex: true, targetApi: 'webgl2' } },

  // Resource creation & layout
  { label: 'fail create buffer', options: { failCreateBuffer: true } },
  { label: 'fail create texture', options: { failCreateTexture: true } },
  { label: 'fail create program', options: { failCreateProgram: true } },
  { label: 'fail create shader', options: { failCreateShader: true } },
  { label: 'missing attrib a_position', options: { missingAttrib: 'a_position' } },
  { label: 'missing attrib a_texCoord', options: { missingAttrib: 'a_texCoord' } },
  { label: 'missing uniform u_color', options: { missingUniform: 'u_color' } },
  { label: 'missing uniform u_sampler', options: { missingUniform: 'u_sampler' } },

  // Pixel comparisons & sentinels
  { label: 'solid draw left as sentinel', options: { solidLeftAsSentinel: true } },
  { label: 'texture draw left as sentinel', options: { textureLeftAsSentinel: true } },
  { label: 'wrong interior pixel', options: { wrongInteriorPixel: true } },
  { label: 'wrong alpha', options: { wrongAlpha: true } },
  { label: 'swapped texture quadrants', options: { swappedQuadrants: true } },

  // GL errors & context states
  { label: 'gl error present', options: { glError: true } },
  { label: 'context already lost', options: { contextLostAlready: true } },
  { label: 'missing renderer string', options: { renderer: '' } },

  // Timing
  { label: 'rAF timer timeout', options: { timeout: true } }
];

const mutationCases = [
  {
    name: 'Remove gl.uniform1i(uSampler, 0)',
    transform: (src) => src.replace('gl.uniform1i(uSampler, 0);', '// gl.uniform1i(uSampler, 0);')
  },
  {
    name: 'Remove all gl.vertexAttribPointer calls',
    transform: (src) => src.replaceAll('gl.vertexAttribPointer(', '// gl.vertexAttribPointer(')
  },
  {
    name: 'Supply new Uint8Array(16) instead of textureBytes in texImage2D',
    transform: (src) => src.replace('textureBytes);', 'new Uint8Array(16));')
  },
  {
    name: 'Change MIN_FILTER from NEAREST to LINEAR (9729)',
    transform: (src) => src.replace('gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);', 'gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, 9729);')
  },
  {
    name: 'Remove all gl.bufferData calls',
    transform: (src) => src.replaceAll('gl.bufferData(', '// gl.bufferData(')
  },
  {
    name: 'Remove all gl.useProgram calls',
    transform: (src) => src.replaceAll('gl.useProgram(', '// gl.useProgram(')
  },
  {
    name: 'Replace gl_Position with invalid shader identifier',
    transform: (src) => src.replaceAll('gl_Position', 'gl_InvalidPosition')
  },
  {
    name: 'Wrong sampler unit (1 instead of 0)',
    transform: (src) => src.replace('gl.uniform1i(uSampler, 0);', 'gl.uniform1i(uSampler, 1);')
  },
  {
    name: 'Wrong texture format / dimensions',
    transform: (src) => src.replace('gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 2, 2, 0, gl.RGBA,', 'gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 4, 1, 0, gl.RGBA,')
  },
  {
    name: 'Flipped texture rows in textureBytes',
    transform: (src) => {
      const target = 'const textureBytes = new Uint8Array([\n' +
        '      255,   0,   0, 255, // (s=0, t=0): lower-left: red\n' +
        '        0, 255,   0, 255, // (s=1, t=0): lower-right: green\n' +
        '        0,   0, 255, 255, // (s=0, t=1): upper-left: blue\n' +
        '      255, 255,   0, 255  // (s=1, t=1): upper-right: yellow\n' +
        '    ]);';
      const replacement = 'const textureBytes = new Uint8Array([\n' +
        '        0,   0, 255, 255, // (s=0, t=1): upper-left: blue\n' +
        '      255, 255,   0, 255, // (s=1, t=1): upper-right: yellow\n' +
        '      255,   0,   0, 255, // (s=0, t=0): lower-left: red\n' +
        '        0, 255,   0, 255  // (s=1, t=0): lower-right: green\n' +
        '    ]);';
      return src.replace(target, replacement);
    }
  },
  {
    name: 'Wrong vertex count in drawArrays (3 instead of 6)',
    transform: (src) => src.replace('gl.drawArrays(gl.TRIANGLES, 0, 6);', 'gl.drawArrays(gl.TRIANGLES, 0, 3);')
  },
  {
    name: 'Omit gl.useProgram(null) before program deletion',
    transform: (src) => src.replace('gl.useProgram(null);', '// gl.useProgram(null);')
  }
];

(async () => {
  // R4: Clean standalone run for --record
  if (process.argv.includes('--record')) {
    const good = await runFixture();
    process.stdout.write(good.title + '\n');
    return;
  }

  // --- 1. Successful execution path ---
  const good = await runFixture();
  assert(good.record !== null, 'Title must contain WEXSHADER JSON result');
  assert.equal(good.record.v, 1, 'Protocol version must be 1');
  assert.equal(good.record.nonce, validNonce, 'Nonce must match');
  assert.equal(good.record.kind, 'ready');
  assert.equal(good.record.passed, true, 'Good run must pass');
  assert.equal(good.clearedTimers.length, 1, 'rAF timeout timer must be cleared');

  // Validate WebGL1 and WebGL2 results
  for (const api of ['webgl', 'webgl2']) {
    const apiResult = good.record.apis[api];
    assert(apiResult, 'Must contain result for ' + api);
    assert.equal(apiResult.compileVs, true);
    assert.equal(apiResult.compileFsSolid, true);
    assert.equal(apiResult.compileFsTex, true);
    assert.equal(apiResult.linkSolid, true);
    assert.equal(apiResult.linkTex, true);
    assert.equal(apiResult.solidMatches, 64, 'Must match all 64 solid pixels');
    assert.equal(apiResult.textureMatches, 64, 'Must match all 64 texture pixels');
    assert.equal(apiResult.error, 0, 'GL error must be NO_ERROR');
    assert.equal(apiResult.lost, false, 'Context must not be lost');
    assert(apiResult.renderer && apiResult.renderer.length > 0, 'Renderer must be populated');

    // Check compact 4 corners
    assert.deepEqual(apiResult.corners.ll, [255, 0, 0, 255], 'Lower-left must be red');
    assert.deepEqual(apiResult.corners.lr, [0, 255, 0, 255], 'Lower-right must be green');
    assert.deepEqual(apiResult.corners.ul, [0, 0, 255, 255], 'Upper-left must be blue');
    assert.deepEqual(apiResult.corners.ur, [255, 255, 0, 255], 'Upper-right must be yellow');

    // Assert that all created resources were properly cleaned up
    assert.equal(good.trackedResources[api].shaders.size, 0, 'All shaders must be deleted in ' + api);
    assert.equal(good.trackedResources[api].programs.size, 0, 'All programs must be deleted in ' + api);
    assert.equal(good.trackedResources[api].buffers.size, 0, 'All buffers must be deleted in ' + api);
    assert.equal(good.trackedResources[api].textures.size, 0, 'All textures must be deleted in ' + api);
  }

  // --- 2. R3: Per-API Context loss after ready ---
  // WebGL1 loss after ready
  const goodLost1 = await runFixture();
  assert(goodLost1.events['webgl:webglcontextlost'], 'Context lost listener must be registered for webgl');
  goodLost1.events['webgl:webglcontextlost']({ preventDefault() {} });
  const afterLostRecord1 = JSON.parse(goodLost1.title.slice(10));
  assert.equal(afterLostRecord1.passed, false, 'Context loss after ready must invalidate result');
  assert.match(afterLostRecord1.error, /webgl context lost/);
  assert.equal(afterLostRecord1.apis.webgl.lost, true, 'WebGL1 lost flag must be true');
  assert.equal(afterLostRecord1.apis.webgl2.lost, false, 'WebGL2 lost flag must remain false');

  // WebGL2 loss after ready
  const goodLost2 = await runFixture();
  assert(goodLost2.events['webgl2:webglcontextlost'], 'Context lost listener must be registered for webgl2');
  goodLost2.events['webgl2:webglcontextlost']({ preventDefault() {} });
  const afterLostRecord2 = JSON.parse(goodLost2.title.slice(10));
  assert.equal(afterLostRecord2.passed, false, 'Context loss after ready must invalidate result');
  assert.match(afterLostRecord2.error, /webgl2 context lost/);
  assert.equal(afterLostRecord2.apis.webgl.lost, false, 'WebGL1 lost flag must remain false');
  assert.equal(afterLostRecord2.apis.webgl2.lost, true, 'WebGL2 lost flag must be true');

  // --- 3. R3: Lost before ready for both APIs ---
  const lostBefore1 = await runFixture({ lostBeforeReady: true, targetApi: 'webgl' });
  assert.equal(lostBefore1.record.passed, false, 'Lost before ready (webgl) must fail');
  assert.equal(lostBefore1.record.apis.webgl.lost, true, 'Lost before ready (webgl) must set lost=true');

  const lostBefore2 = await runFixture({ lostBeforeReady: true, targetApi: 'webgl2' });
  assert.equal(lostBefore2.record.passed, false, 'Lost before ready (webgl2) must fail');
  assert.equal(lostBefore2.record.apis.webgl2.lost, true, 'Lost before ready (webgl2) must set lost=true');

  // --- 4. Table-driven negative test cases ---
  for (const testCase of negativeCases) {
    const bad = await runFixture(testCase.options);
    assert(bad.record !== null, `Negative case '${testCase.label}' must produce record`);
    assert.equal(bad.record.passed, false, `Negative case '${testCase.label}' must yield passed=false`);
    assert(bad.record.error && bad.record.error.length > 0, `Negative case '${testCase.label}' must report error message`);

    // Verify cleanup: created resources must still be cleaned up on failure
    for (const api of ['webgl', 'webgl2']) {
      assert.equal(bad.trackedResources[api].shaders.size, 0, `Shaders leaked on failure in '${testCase.label}'`);
      assert.equal(bad.trackedResources[api].programs.size, 0, `Programs leaked on failure in '${testCase.label}'`);
      assert.equal(bad.trackedResources[api].buffers.size, 0, `Buffers leaked on failure in '${testCase.label}'`);
      assert.equal(bad.trackedResources[api].textures.size, 0, `Textures leaked on failure in '${testCase.label}'`);
    }
  }

  // --- 5. R1 & R2: Mutation regressions ---
  for (const mc of mutationCases) {
    const mutated = mc.transform(source);
    assert.notEqual(mutated, source, `Mutation '${mc.name}' must modify source`);
    const bad = await runFixture({ sourceOverride: mutated });
    assert(bad.record !== null, `Mutation '${mc.name}' must produce record`);
    const rejected = (bad.record.passed === false) ||
                     (bad.trackedResources.webgl.programs.size > 0) ||
                     (bad.trackedResources.webgl2.programs.size > 0);
    assert.equal(rejected, true, `Mutation '${mc.name}' must be rejected by contract or resource leak`);
  }

  // --- 6. R4: Automated sub-process test for --record ---
  const child = spawnSync(process.execPath, [__filename, '--record'], { encoding: 'utf8' });
  assert.equal(child.status, 0, '--record sub-process must exit with status 0');
  const recordLines = child.stdout.trim().split(/\r?\n/);
  assert.equal(recordLines.length, 1, '--record must emit exactly 1 line');
  assert(recordLines[0].startsWith('WEXSHADER '), '--record output must start with WEXSHADER');
  const parsedRecord = JSON.parse(recordLines[0].slice(10));
  assert.equal(parsedRecord.passed, true, '--record must report passed: true');

  console.log(`PASS WebGL shader & texture fixture: WebGL1/WebGL2, GLSL ES 1.00 & 300 es, solid/texture draws, 64-pixel match, sentinels, cleanup, ${negativeCases.length} negative tests, ${mutationCases.length} mutation regressions`);
})().catch(err => {
  console.error(err);
  process.exitCode = 1;
});
