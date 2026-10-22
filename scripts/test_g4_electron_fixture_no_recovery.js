// SPDX-License-Identifier: GPL-2.0-or-later
'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const vm = require('node:vm');

async function main() {
  const fixtureSource = fs.readFileSync(path.join(__dirname, 'g4-electron-probe', 'main.js'), 'utf8');
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'wex-electron-no-recovery-'));
  const logPath = path.join(root, 'host.log');
  const profile = path.join(root, 'profile');
  fs.mkdirSync(profile);
  let helperLoads = 0;
  let loadedResolve;
  const loaded = new Promise(resolve => { loadedResolve = resolve; });
  const scheduled = [];
  const appEvents = [];
  const app = {
    setPath() {}, enableSandbox() {}, on(...args) { appEvents.push(args); },
    whenReady() { return Promise.resolve(); },
    getGPUInfo() { return Promise.resolve({}); }, getGPUFeatureStatus() { return {}; },
    quit() {}, exit(code) { throw new Error(`Unexpected app.exit(${code})`); },
  };
  class BrowserWindow {
    constructor() {
      this.webContents = {
        executeJavaScript() { throw new Error('Normal fixture should not need renderer evaluation here'); },
      };
    }
    loadURL() { loadedResolve(); return Promise.resolve(); }
  }
  const electron = { app, BrowserWindow, session: { defaultSession: { cookies: {} } } };
  function fixtureRequire(name) {
    if (name === 'electron') return electron;
    if (name === 'node:fs') return fs;
    if (name === 'node:path') return path;
    if (name === './recovery-frame-wait') {
      helperLoads++;
      throw new Error('Recovery-only helper is absent in this normal-flow fixture');
    }
    throw new Error(`Unexpected fixture dependency: ${name}`);
  }
  const fixtureProcess = { env: { WEX_WEBVIEW2_LOG: logPath, WEX_WEBVIEW2_PROFILE: profile },
    versions: { electron: 'test-double' }, argv: [] };
  const context = {
    require: fixtureRequire, process: fixtureProcess, Date, JSON, Promise,
    setTimeout(callback, timeoutMs) { scheduled.push({ callback, timeoutMs }); return scheduled.length; },
    clearTimeout() {}, setInterval(callback, intervalMs) { scheduled.push({ callback, intervalMs }); return scheduled.length; },
    clearInterval() {}, encodeURIComponent,
  };
  try {
    vm.runInNewContext(fixtureSource, context, { filename: 'g4-electron-probe/main.js' });
    await loaded;
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(helperLoads, 0, 'ordinary Electron startup never loads recovery-only helper');
    assert.equal(appEvents.length, 2);
    assert.equal(scheduled.length, 4, 'ordinary sample, capture and quit timers were installed');
    assert.match(fs.readFileSync(logPath, 'utf8'), /navigation completed/);
    process.stdout.write('Electron normal-flow startup without recovery helper: PASS\n');
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
}

main().catch(error => {
  process.stderr.write(`${error.stack || error}\n`);
  process.exitCode = 1;
});
