// SPDX-License-Identifier: GPL-2.0-or-later
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const vm = require('node:vm');
const protocol = require('./g4-electron-probe/presentation-timing');
const frameWait = require('./g4-electron-probe/recovery-frame-wait');

async function scenario(conflict = false) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'wex-timing-fixture-'));
  const logPath = path.join(root, 'host.log'), runId = 'c'.repeat(32);
  const events = [], pending = new Map();
  let serial = 0, time = 0, done = false, pageCalls = 0, kills = 0, metricsCalls = 0, exits = [];
  const timers = {
    setTimeout(fn, ms) { const id = ++serial; pending.set(id, { at: time + ms, fn }); return id; },
    clearTimeout(id) { pending.delete(id); },
  };
  function interval(fn, ms) {
    const id = ++serial;
    const tick = () => { pending.set(id, { at: time + ms, fn: tick }); fn(); };
    pending.set(id, { at: time + ms, fn: tick }); return id;
  }
  const box = { style: {}, getBoundingClientRect: () => ({ left: 192, top: 177, width: 40, height: 40 }) };
  const gl = { default: true,
    isContextLost: () => false, clearColor() {}, clear() {}, getError: () => 0,
    readPixels(_x, _y, _w, _h, _format, _type, data) { data.set(this.default ? [255,0,0,255] : [0,255,0,255]); },
    createTexture() { return {}; }, bindTexture() {}, texImage2D() {}, createFramebuffer() { return {}; },
    bindFramebuffer(_kind, framebuffer) { this.default = framebuffer === null; }, framebufferTexture2D() {},
    checkFramebufferStatus: () => 36053, deleteFramebuffer() {}, deleteTexture() {},
    getExtension: () => ({ UNMASKED_RENDERER_WEBGL: 1 }), getParameter: () => 'ANGLE hardware test',
  };
  const renderer = { box, counter: { textContent: '' }, innerWidth: 732, innerHeight: 462,
    framesCount: 14, timerTicks: 1, performance: { now: () => time, getEntriesByType: () => [] },
    getComputedStyle: () => ({ backgroundColor: 'rgb(232,245,233)' }),
    document: { body: { style: {} }, visibilityState: 'visible', querySelector: () => ({ before() {} }),
      createElement: () => ({ style: {}, remove() {}, addEventListener() {}, getContext: () => gl }) },
    requestAnimationFrame: callback => timers.setTimeout(() => callback(time), 16),
    setTimeout: timers.setTimeout, clearTimeout: timers.clearTimeout,
  };
  renderer.window = renderer;
  vm.createContext(renderer);
  const app = { setPath() {}, enableSandbox() {}, on() {}, whenReady: () => Promise.resolve(),
    getGPUInfo: () => Promise.resolve({}), getGPUFeatureStatus: () => ({ gpu_compositing: 'enabled' }),
    getAppMetrics() { metricsCalls++; throw new Error('Timing dispatched GPU recovery metrics'); },
    quit() { done = true; }, exit(code) { exits.push(code); done = true; } };
  class BrowserWindow {
    constructor() {
      this.webContents = {
        executeJavaScript: source => Promise.resolve(vm.runInContext(source, renderer)),
        capturePage() {
          pageCalls++;
          const frontAck = path.join(root, `presentation-${runId}-fronts_complete.ack`);
          assert(fs.existsSync(frontAck), 'actual fixture cannot read page before four-front ACK');
          return Promise.resolve({ getSize: () => ({ width: 732, height: 462 }),
            isEmpty: () => false, toPNG: () => Buffer.alloc(100) });
        },
      };
    }
    loadURL() { return Promise.resolve(); }
    isDestroyed() { return false; }
    getBounds() { return { x: 25, y: 25, width: 740, height: 520 }; }
    getContentBounds() { return { x: 29, y: 79, width: 732, height: 462 }; }
  }
  const fixtureFs = { ...fs, appendFileSync(file, line) {
    fs.appendFileSync(file, line);
    const match = line.match(/^\d+ ms presentation timing (requested|page capture) (.+)\n$/);
    if (!match) return;
    const data = JSON.parse(match[2]);
    if (match[1] === 'requested') {
      protocol.FRONTS.forEach((stage, index) => timers.setTimeout(() => {
        events.push(stage);
        if (index === 3) fs.writeFileSync(path.join(root, `presentation-${runId}-fronts_complete.ack`),
          JSON.stringify({ runId, stage: 'fronts_complete', captured: true, samples: protocol.FRONTS }));
      }, [0,250,1000,3000][index]));
      assert.equal(data.remainingMs <= 30000, true);
    } else {
      events.push('page');
      timers.setTimeout(() => {
        events.push('front_after_page_250');
        fs.writeFileSync(path.join(root, `presentation-${runId}-page_and_post_complete.ack`),
          JSON.stringify({ runId, stage: 'page_and_post_complete', captured: true, pixelPassed: false }));
      }, 250);
    }
  } };
  const context = { require(name) {
    if (name === 'electron') return { app, BrowserWindow, session: {} };
    if (name === 'node:fs') return fixtureFs;
    if (name === 'node:path') return path;
    if (name === './presentation-timing') return {
      runPresentationTiming: options => protocol.runPresentationTiming({ ...options, now: () => time, timers }),
    };
    if (name === './recovery-frame-wait') return {
      waitForRendererFrames: (contents, stage) => frameWait.waitForRendererFrames(contents, stage, 1500, timers),
    };
    throw new Error('Unexpected dependency ' + name);
  }, process: { env: { WEX_WEBVIEW2_LOG: logPath, WEX_WEBVIEW2_PROFILE: path.join(root, 'profile'),
    WEX_WEBVIEW2_PRESENTATION_TIMING: '1', WEX_WEBVIEW2_TIMING_RUN_ID: runId,
    WEX_WEBVIEW2_SECONDS: '45', ...(conflict ? { WEX_WEBVIEW2_GPU_RECOVERY: '1' } : {}) },
    versions: {}, argv: [], kill() { kills++; throw new Error('Timing reached process.kill'); } },
    Date: class extends Date { static now() { return time; } }, encodeURIComponent,
    setTimeout: timers.setTimeout, clearTimeout: timers.clearTimeout,
    setInterval: interval, clearInterval: timers.clearTimeout,
  };
  try {
    vm.runInNewContext(fs.readFileSync(path.join(__dirname, 'g4-electron-probe/main.js'), 'utf8'), context);
    for (let count = 0; !done && count < 5000; count++) {
      for (let i = 0; i < 40; i++) await Promise.resolve();
      if (done) break;
      const [id, next] = [...pending].sort((a, b) => a[1].at - b[1].at)[0];
      pending.delete(id); time = next.at; next.fn();
    }
    assert(done);
    assert.equal(kills, 0);
    assert.equal(metricsCalls, 0, 'fault dispatch is never entered');
    const text = fs.readFileSync(logPath, 'utf8');
    if (conflict) {
      assert.deepEqual(exits, [2]); assert.equal(pageCalls, 0);
    } else {
      assert.deepEqual(exits, []);
      assert.equal(pageCalls, 1, '12-second generic capturePage must never be scheduled');
      assert.deepEqual(events, [...protocol.FRONTS, 'page', 'front_after_page_250']);
      const result = JSON.parse(text.match(/^\d+ ms presentation timing result (.+)$/m)[1]);
      assert.equal(result.collectionValid, true, text);
      assert.equal(renderer.sceneFrozen, true);
      assert.equal(box.style.transform, 'translateX(160px)');
    }
  } finally { fs.rmSync(root, { recursive: true, force: true }); }
}
scenario().then(() => scenario(true)).then(() => {
  process.stdout.write('actual fixture no-fault dispatch, frozen scene, exactly one gated readback and exclusions: PASS\n');
}).catch(error => { process.stderr.write(`${error.stack}\n`); process.exitCode = 1; });
