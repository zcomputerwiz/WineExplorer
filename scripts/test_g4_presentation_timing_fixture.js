// SPDX-License-Identifier: GPL-2.0-or-later
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const vm = require('node:vm');
const protocol = require('./g4-electron-probe/presentation-timing');
const frameWait = require('./g4-electron-probe/recovery-frame-wait');

async function scenario(conflict = false, visible = false, recovery = false, badPre = false, badPost = false) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'wex-timing-fixture-'));
  const logPath = path.join(root, 'host.log'), runId = 'c'.repeat(32);
  const events = [], pending = new Map(), created = [], appEvents = {};
  let currentStage;
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
  function makeGL() { return { default: true, lost: false, colors: [], reads: [], color: [0,0,0,0],
    isContextLost() { return this.lost; }, clearColor(...color) { this.color=color.map(v=>Math.round(v*255)); this.colors.push(this.color); },
    clear() { this[this.default?'defaultColor':'fboColor']=this.color; }, getError: () => 0,
    readPixels(_x, _y, _w, _h, _format, _type, data) {
      const color=this[this.default?'defaultColor':'fboColor']; this.reads.push(color); data.set(color);
    },
    createTexture() { return {}; }, bindTexture() {}, texImage2D() {}, createFramebuffer() { return {}; },
    bindFramebuffer(_kind, framebuffer) { this.default = framebuffer === null; }, framebufferTexture2D() {},
    checkFramebufferStatus: () => 36053, deleteFramebuffer() {}, deleteTexture() {},
    getExtension: () => ({ UNMASKED_RENDERER_WEBGL: 1 }), getParameter: () => 'ANGLE hardware test',
  }; }
  const renderer = { box, counter: { textContent: '' }, innerWidth: 732, innerHeight: 462,
    framesCount: 14, timerTicks: 1, performance: { now: () => time, getEntriesByType: () => [] },
    getComputedStyle: () => ({ backgroundColor: 'rgb(232,245,233)' }),
    document: { body: { style: {} }, visibilityState: 'visible', querySelector: () => ({ before() {},
      getBoundingClientRect: () => ({left:32,top:222,width:100,height:24}) }),
      createElement: () => { const canvas = { style: {}, listeners:{}, remove() {},
        addEventListener(name, callback) { this.listeners[name]=callback; },
        getBoundingClientRect() { return {left: parseFloat(this.style.cssText.match(/left:([^p]+)/)[1]),
          top:300,width:this.width,height:this.height}; },
        getContext(api, options) { this.api=api; this.options=options; this.gl=makeGL(); created.push(this); return this.gl; } };
        return canvas;
      } },
    requestAnimationFrame: callback => timers.setTimeout(() => callback(time), 16),
    setTimeout: timers.setTimeout, clearTimeout: timers.clearTimeout,
  };
  renderer.window = renderer;
  vm.createContext(renderer);
  const app = { setPath() {}, enableSandbox() {}, on(name, callback) { appEvents[name]=callback; }, whenReady: () => Promise.resolve(),
    getGPUInfo: () => Promise.resolve({}), getGPUFeatureStatus: () => ({ gpu_compositing: 'enabled' }),
    getAppMetrics() { metricsCalls++; if (!recovery) throw new Error('Timing dispatched GPU recovery metrics');
      return [{pid:10,creationTime:1},{pid:20,creationTime:2},{type:'GPU',pid:kills?31:30,creationTime:kills?4:3}]; },
    quit() { done = true; }, exit(code) { exits.push(code); done = true; } };
  class BrowserWindow {
    constructor() {
      this.webContents = {
        executeJavaScript: source => Promise.resolve(vm.runInContext(source, renderer)),
        getOSProcessId: () => 20,
        capturePage() {
          pageCalls++;
          const frontAck = path.join(root, recovery ? `gpu-recovery-${runId}-${currentStage}.ack`
            : `presentation-${runId}-fronts_complete.ack`);
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
    const recover = line.match(/^\d+ ms gpu recovery (front capture requested|page capture) (\{.+\})\n$/);
    if (recover) {
      const data = JSON.parse(recover[2]); currentStage=data.stage;
      assert.deepEqual(data.background, renderer.document.body.style.background === '#fff3e0' ? [255,243,224] : [232,245,233]);
      const isPage = recover[1] === 'page capture'; events.push(data.stage+(isPage?':page':':front'));
      if (visible) {
        for (const api of ['webgl','webgl2']) {
          assert.equal(data.visibleCanvases[api].api,api);
          assert.equal(data.visibleCanvases[api].stage,data.stage==='pre_fault'?'pre_fault':'recovered');
        }
      }
      fs.writeFileSync(path.join(root,`gpu-recovery-${runId}-${data.stage}${isPage?'.page':''}.ack`),
        JSON.stringify({runId,stage:data.stage,captured:true,pixelPassed:isPage||!(data.stage==='pre_fault'?badPre:badPost)}));
    }
    const match = line.match(/^\d+ ms presentation timing (requested|page capture) (.+)\n$/);
    if (!match) return;
    const data = JSON.parse(match[2]);
    assert.deepEqual(data.background, [232,245,233], 'actual observeScene parses RGB with a digit regex');
    if (visible) {
      assert.deepEqual(Object.keys(data.canvasExclusions), ['h1','#counter','input','button','#scroll-label']);
      for (const [api,left] of [['webgl',360],['webgl2',456]]) {
        assert.deepEqual(data.visibleCanvases[api], {api,stage:'pre_fault',rect:{x:left,y:300,width:64,height:64}});
      }
    }
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
    ...(recovery ? {WEX_WEBVIEW2_GPU_RECOVERY:'1', WEX_WEBVIEW2_RECOVERY_CAPTURE_RUN_ID:runId,
      WEX_WEBVIEW2_RECOVERY_CAPTURE_DIR:root} : {WEX_WEBVIEW2_PRESENTATION_TIMING:'1'}),
    ...(visible ? {WEX_WEBVIEW2_VISIBLE_WEBGL:'1'} : {}), WEX_WEBVIEW2_TIMING_RUN_ID: runId,
    WEX_WEBVIEW2_SECONDS: '45', ...(conflict ? { WEX_WEBVIEW2_GPU_RECOVERY: '1' } : {}) },
    versions: {}, argv: [], pid:10, kill(pid) {
      assert(recovery); assert.equal(pid,30); kills++;
      for (const item of Object.values(renderer.g4Contexts)) {
        item.gl.lost=true; item.canvas.listeners.webglcontextlost({preventDefault(){}});
      }
      appEvents['child-process-gone']({}, {type:'GPU',reason:'killed',exitCode:1});
    } },
    Date: class extends Date { static now() { return time; } }, encodeURIComponent,
    setTimeout: timers.setTimeout, clearTimeout: timers.clearTimeout,
    setInterval: interval, clearInterval: timers.clearTimeout,
  };
  try {
    renderer.getComputedStyle = () => ({backgroundColor: renderer.document.body.style.background === '#fff3e0'
      ? 'rgb(255,243,224)' : 'rgb(232,245,233)'});
    if (recovery) interval(() => { renderer.framesCount++;
      if (!renderer.sceneFrozen) box.style.transform=`translateX(${renderer.framesCount%240}px)`; },16);
    vm.runInNewContext(fs.readFileSync(path.join(__dirname, 'g4-electron-probe/main.js'), 'utf8'), context);
    for (let count = 0; !done && count < 5000; count++) {
      for (let i = 0; i < 40; i++) await Promise.resolve();
      if (done) break;
      const [id, next] = [...pending].sort((a, b) => a[1].at - b[1].at)[0];
      pending.delete(id); time = next.at; next.fn();
    }
    assert(done);
    assert.equal(kills, recovery && !badPre ? 1 : 0);
    if (!recovery) assert.equal(metricsCalls, 0, 'fault dispatch is never entered');
    const text = fs.readFileSync(logPath, 'utf8');
    if (conflict) {
      assert.deepEqual(exits, [2]); assert.equal(pageCalls, 0);
    } else if (recovery) {
      assert(time > 12000, 'execute beyond the suppressed generic readback deadline');
      assert.equal(pageCalls,badPre?1:4, text);
      assert.deepEqual(events,(badPre?['pre_fault']:['pre_fault','recovered','settled_2s','settled_4s'])
        .flatMap(stage=>[stage+':front',stage+':page']));
      assert(!fs.existsSync(path.join(root,'electron-capture.png')), 'no generic recovery readback after 12 seconds');
    } else {
      assert.deepEqual(exits, []);
      assert.equal(pageCalls, 1, '12-second generic capturePage must never be scheduled');
      assert.deepEqual(events, [...protocol.FRONTS, 'page', 'front_after_page_250']);
      const result = JSON.parse(text.match(/^\d+ ms presentation timing result (.+)$/m)[1]);
      assert.equal(result.collectionValid, true, text);
      assert.equal(renderer.sceneFrozen, true);
      assert.equal(box.style.transform, 'translateX(160px)');
    }
    if (visible && !conflict) for (const canvas of created) {
      assert.equal(canvas.options.alpha,false); assert.equal(canvas.width,64); assert.equal(canvas.height,64);
      assert(!canvas.style.cssText.includes('background'));
      assert.deepEqual(canvas.gl.reads,[[255,0,0,255],[0,255,0,255]], 'independent default/FBO API readbacks retained');
      const phase = created.indexOf(canvas)<2 ? 'pre_fault':'recovered';
      const expected = {pre_fault:{webgl:[255,0,255,255],webgl2:[0,255,255,255]},
        recovered:{webgl:[255,255,0,255],webgl2:[0,0,255,255]}}[phase][canvas.api];
      assert.deepEqual(canvas.gl.defaultColor,expected,'actual final WebGL clear, not CSS imitation');
    }
  } finally { fs.rmSync(root, { recursive: true, force: true }); }
}
scenario().then(() => scenario(true)).then(() => scenario(false,true))
  .then(() => scenario(false,true,true,true)).then(() => scenario(false,true,true,false,true)).then(() => {
  process.stdout.write('actual fixture timing/recovery, RGB observation, visible GL palettes, pre-fault gate, bad-front pairs and incidental-readback suppression: PASS\n');
}).catch(error => { process.stderr.write(`${error.stack}\n`); process.exitCode = 1; });
