// SPDX-License-Identifier: GPL-2.0-or-later
// Renderer-side bounded rendering-opportunity wait; it is not a presentation proof.
'use strict';

function collectRendererFrames(requestAnimationFrame, setTimeoutFn, clearTimeoutFn,
                               now, requiredFrames, timeoutMs) {
  if (!Number.isSafeInteger(requiredFrames) || requiredFrames < 1
      || !Number.isFinite(timeoutMs) || timeoutMs <= 0) {
    throw new TypeError('Invalid renderer-frame wait bounds');
  }
  const started = now();
  return new Promise(resolve => {
    let observedFrames = 0;
    let settled = false;
    let timer;
    const timestamps = [];
    const finish = (passed, reason) => {
      if (settled) return;
      settled = true;
      clearTimeoutFn(timer);
      resolve({ passed, reason, requiredFrames, observedFrames, timestamps,
        elapsedMs: Math.max(0, now() - started), timeoutMs });
    };
    const onFrame = timestamp => {
      if (settled) return;
      observedFrames++;
      timestamps.push(Number.isFinite(timestamp) ? timestamp : now());
      if (observedFrames >= requiredFrames) finish(true, 'frames');
      else requestAnimationFrame(onFrame);
    };
    timer = setTimeoutFn(() => finish(false, 'timeout'), timeoutMs);
    requestAnimationFrame(onFrame);
  });
}

async function waitForRendererFrames(webContents, stage, timeoutMs = 1500) {
  const expression = `(${collectRendererFrames.toString()})(
    window.requestAnimationFrame.bind(window),
    window.setTimeout.bind(window), window.clearTimeout.bind(window),
    () => performance.now(), 2, ${JSON.stringify(timeoutMs)})`;
  const result = await webContents.executeJavaScript(expression);
  if (!result || typeof result !== 'object' || typeof result.passed !== 'boolean'
      || result.requiredFrames !== 2 || !Number.isSafeInteger(result.observedFrames)
      || !Array.isArray(result.timestamps) || !Number.isFinite(result.elapsedMs)
      || result.elapsedMs < 0 || result.timeoutMs !== timeoutMs
      || !['frames', 'timeout'].includes(result.reason)) {
    throw new Error(`Invalid renderer-frame wait result: ${stage}`);
  }
  return { ...result, stage };
}

module.exports = { collectRendererFrames, waitForRendererFrames };
