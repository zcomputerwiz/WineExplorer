// SPDX-License-Identifier: GPL-2.0-or-later
// Original no-fault protocol. No Electron import, process control or repaint API.
'use strict';

const { performance } = require('node:perf_hooks');
const FRONTS = ['front_0', 'front_250', 'front_1000', 'front_3000'];
const OVERALL_MS = 30000;

async function runPresentationTiming({ runId, prepare, observe, capturePage, publishPage,
  readAck, log, now = () => performance.now(), timers = { setTimeout, clearTimeout } }) {
  const started = now();
  const deadline = started + OVERALL_MS;
  const bounded = async (label, operation, maximumMs = 3000) => {
    const remaining = deadline - now();
    if (remaining <= 0) throw new Error('Presentation timing overall timeout');
    let timer;
    const operationStarted = now();
    try {
      const result = await Promise.race([
        new Promise((resolve, reject) => {
          timer = timers.setTimeout(() => reject(new Error(`Presentation timing timeout: ${label}`)),
            Math.min(remaining, maximumMs));
        }),
        Promise.resolve().then(operation),
      ]);
      if (now() > deadline || now() - operationStarted > maximumMs)
        throw new Error(`Presentation timing timeout: ${label}`);
      return result;
    } finally { timers.clearTimeout(timer); }
  };
  const waitAck = async stage => {
    while (now() < deadline) {
      const ack = readAck(stage);
      if (ack !== null) {
        if (!ack || ack.runId !== runId || ack.stage !== stage || ack.captured !== true
            || (stage === 'fronts_complete' && JSON.stringify(ack.samples) !== JSON.stringify(FRONTS))
            || (stage === 'page_and_post_complete' && typeof ack.pixelPassed !== 'boolean')) {
          throw new Error('Invalid presentation timing acknowledgement: ' + stage);
        }
        log('acknowledged', ack);
        return ack;
      }
      await new Promise(resolve => timers.setTimeout(resolve, Math.min(50, deadline - now())));
    }
    throw new Error('Presentation timing missing acknowledgement: ' + stage);
  };
  try {
    if (typeof runId !== 'string' || !/^[a-f0-9]{32}$/.test(runId)) throw new Error('Invalid timing nonce');
    await bounded('prepare frozen scene', () => prepare(bounded), 8000);
    const observation = await bounded('geometry', observe, 1500);
    log('requested', { runId, ...observation, remainingMs: deadline - now(),
      requestedMonotonicMs: now(), overallTimeoutMs: OVERALL_MS });
    await waitAck('fronts_complete');
    const captureStarted = now();
    const page = await bounded('capturePage', capturePage);
    const captureEnded = now();
    const metadata = await bounded('page PNG', () => publishPage(page));
    const afterPage = await bounded('page geometry', observe, 1500);
    if (JSON.stringify(afterPage) !== JSON.stringify(observation)) throw new Error('Timing scene geometry changed');
    log('page capture', { runId, ...afterPage, ...metadata,
      captureStartedMonotonicMs: captureStarted, captureEndedMonotonicMs: captureEnded,
      captureDurationMs: captureEnded - captureStarted });
    await waitAck('page_and_post_complete');
    const final = await bounded('final geometry', observe, 1500);
    if (JSON.stringify(final) !== JSON.stringify(observation)) throw new Error('Timing final geometry changed');
    log('result', { runId, collectionValid: true, elapsedMs: now() - started });
    return true;
  } catch (error) {
    log('result', { runId, collectionValid: false, elapsedMs: now() - started, error: String(error) });
    return false;
  }
}

module.exports = { runPresentationTiming, FRONTS, OVERALL_MS };
