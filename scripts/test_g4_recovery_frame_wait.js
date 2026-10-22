// SPDX-License-Identifier: GPL-2.0-or-later
'use strict';

const assert = require('node:assert/strict');
const vm = require('node:vm');
const { waitForRendererFrames } = require('./g4-electron-probe/recovery-frame-wait');

function rendererHarness() {
  const frames = [];
  const timers = [];
  const cleared = [];
  const mainTimers = [];
  const clearedMain = [];
  let clock = 10;
  const window = {
    requestAnimationFrame(callback) { frames.push(callback); return frames.length; },
    setTimeout(callback, timeoutMs) { timers.push({ callback, timeoutMs }); return timers.length; },
    clearTimeout(id) { cleared.push(id); },
  };
  const executeJavaScript = source => vm.runInNewContext(source, {
    window,
    performance: { now: () => clock },
  });
  return {
    webContents: { executeJavaScript }, frames, timers, cleared,
    mainTimers: {
      setTimeout(callback, timeoutMs) { mainTimers.push({ callback, timeoutMs }); return mainTimers.length; },
      clearTimeout(id) { clearedMain.push(id); },
    }, mainTimerEvents: mainTimers, clearedMain,
    advance(ms) { clock += ms; },
  };
}

async function callbackSequence() {
  const h = rendererHarness();
  const pending = waitForRendererFrames(h.webContents, 'pre_fault', 1500, h.mainTimers);
  assert.equal(h.frames.length, 1, 'first renderer frame is requested immediately');
  assert.equal(h.timers.length, 1);
  assert.equal(h.timers[0].timeoutMs, 1500);
  h.advance(16);
  h.frames.shift()(26);
  assert.equal(h.frames.length, 1, 'second callback is scheduled only after the first');
  h.advance(17);
  h.frames.shift()(43);
  const result = await pending;
  assert.deepEqual(JSON.parse(JSON.stringify({ passed: result.passed, stage: result.stage, reason: result.reason,
    requiredFrames: result.requiredFrames, observedFrames: result.observedFrames,
    timestamps: result.timestamps, elapsedMs: result.elapsedMs })),
  { passed: true, stage: 'pre_fault', reason: 'frames', requiredFrames: 2,
    observedFrames: 2, timestamps: [26, 43], elapsedMs: 33 });
  assert.deepEqual(h.cleared, [1], 'successful completion cancels the timeout');
  assert.deepEqual(h.clearedMain, [1], 'successful IPC response cancels the browser-main deadline');
}

async function timeoutControl() {
  const h = rendererHarness();
  const pending = waitForRendererFrames(h.webContents, 'recovered', 1500, h.mainTimers);
  h.advance(16);
  h.frames.shift()(26);
  assert.equal(h.frames.length, 1, 'a single callback is not enough to pass');
  h.advance(1500);
  h.timers[0].callback();
  const result = await pending;
  assert.equal(result.passed, false);
  assert.equal(result.stage, 'recovered');
  assert.equal(result.reason, 'timeout');
  assert.equal(result.observedFrames, 1);
  assert.deepEqual(Array.from(result.timestamps), [26]);
  assert.equal(result.elapsedMs, 1516);
  assert.deepEqual(h.cleared, [1]);
  assert.deepEqual(h.clearedMain, [1]);
}

async function unresolvedRendererControl() {
  const h = rendererHarness();
  const webContents = { executeJavaScript() { return new Promise(() => {}); } };
  const pending = waitForRendererFrames(webContents, 'pre_fault', 1500, h.mainTimers);
  assert.equal(h.mainTimerEvents.length, 1);
  assert.equal(h.mainTimerEvents[0].timeoutMs, 1500);
  h.advance(1500);
  h.mainTimerEvents[0].callback();
  await assert.rejects(pending, /Renderer frame request timed out before pre_fault/);
  assert.deepEqual(h.clearedMain, [1], 'deadline is cleaned after an unresolved renderer request');
}

Promise.resolve().then(callbackSequence).then(timeoutControl).then(unresolvedRendererControl).then(() => {
  process.stdout.write('renderer-frame-wait callback sequence, timeout, IPC deadline and cleanup: PASS\n');
}).catch(error => {
  process.stderr.write(`${error.stack || error}\n`);
  process.exitCode = 1;
});
