// SPDX-License-Identifier: GPL-2.0-or-later
'use strict';
const assert = require('node:assert/strict');
const { runPresentationTiming, FRONTS, OVERALL_MS } = require('./g4-electron-probe/presentation-timing');

function clock() {
  let time = 0, serial = 0;
  const pending = new Map();
  return { now: () => time, pending,
    timers: {
      setTimeout(fn, ms) { const id = ++serial; pending.set(id, { at: time + ms, fn }); return id; },
      clearTimeout(id) { pending.delete(id); },
    },
    next() {
      const [id, event] = [...pending].sort((a, b) => a[1].at - b[1].at)[0] || [];
      if (!event) return false;
      time = event.at; pending.delete(id); event.fn(); return true;
    },
  };
}

async function scenario(options = {}) {
  const c = clock(), events = [], acks = new Map();
  const runId = 'a'.repeat(32);
  let pageCalls = 0, settled = false, value;
  const observation = { windowBounds: { x: 25, y: 25, width: 740, height: 520 },
    clientBounds: { x: 29, y: 79, width: 732, height: 462 }, viewport: { width: 732, height: 462 },
    marker: { x: 192, y: 177, width: 40, height: 40 }, background: [232, 245, 233] };
  const pending = runPresentationTiming({ runId, now: c.now, timers: c.timers,
    prepare: () => options.hungPrepare ? new Promise(() => {}) : Promise.resolve(),
    observe: () => Promise.resolve(observation),
    capturePage: () => {
      pageCalls++;
      assert.equal(acks.has('fronts_complete'), true, 'no early readback');
      assert.equal(c.now() >= 3000, true);
      events.push(['capturePage', c.now()]);
      return options.hungPage ? new Promise(() => {}) : Promise.resolve({ image: true });
    },
    publishPage: () => ({ size: { width: 732, height: 462 }, empty: false, bytes: 100 }),
    readAck: stage => acks.get(stage) ?? null,
    log: (event, data) => {
      events.push([event, c.now(), data]);
      if (event === 'requested' && !options.missingAck) c.timers.setTimeout(() => {
        acks.set('fronts_complete', { runId: options.staleAck ? 'b'.repeat(32) : runId,
          stage: 'fronts_complete', captured: !options.badAck, samples: FRONTS });
      }, 3000);
      if (event === 'page capture') c.timers.setTimeout(() => {
        acks.set('page_and_post_complete', { runId, stage: 'page_and_post_complete',
          captured: true, pixelPassed: false }); // stale pixels do not abort collection
      }, 250);
    },
  }).then(result => { settled = true; value = result; });
  for (let count = 0; !settled && count < 3000; count++) {
    // Drain Promise.race/async continuations before advancing the fake clock.
    for (let i = 0; i < 30; i++) await Promise.resolve();
    if (!settled) assert(c.next(), 'pending operation must have a deadline');
  }
  await pending;
  assert.equal(c.pending.size, 0, 'all protocol and IPC timers are cleaned');
  return { value, events, pageCalls, elapsed: c.now() };
}

async function main() {
  const normal = await scenario();
  assert.equal(normal.value, true);
  assert.equal(normal.pageCalls, 1);
  assert.deepEqual(normal.events.map(e => e[0]),
    ['requested', 'acknowledged', 'capturePage', 'page capture', 'acknowledged', 'result']);
  assert.equal(normal.events.find(e => e[0] === 'page capture')[1], 3000);
  assert.equal(normal.events.at(-1)[1], 3250);
  for (const option of ['missingAck', 'staleAck', 'badAck', 'hungPrepare', 'hungPage']) {
    const result = await scenario({ [option]: true });
    assert.equal(result.value, false, option);
    assert.equal(result.pageCalls, option === 'hungPage' ? 1 : 0, option);
    assert(result.elapsed <= OVERALL_MS, option);
    assert.equal(result.events.at(-1)[2].collectionValid, false, option);
  }
  process.stdout.write('presentation timing callback order, no early readback, stale evidence, ACK/IPC timeouts: PASS\n');
}
main().catch(error => { process.stderr.write(`${error.stack}\n`); process.exitCode = 1; });
