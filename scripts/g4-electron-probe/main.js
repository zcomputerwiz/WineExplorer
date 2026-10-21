// SPDX-License-Identifier: GPL-2.0-or-later
// Original offline fixture; no npm packages, preload or renderer Node access.
const { app, BrowserWindow, session } = require('electron');
const fs = require('node:fs');
const path = require('node:path');

const logPath = process.env.WEX_WEBVIEW2_LOG;
const profile = process.env.WEX_WEBVIEW2_PROFILE;
if (!logPath || !profile) throw new Error('Use the isolated G4 runner');
const started = Date.now();
function log(event, data) {
  fs.appendFileSync(logPath, `${Date.now() - started} ms ${event} ${JSON.stringify(data)}\n`);
}
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
async function exerciseLifecycle(win) {
  const checks = {};
  const sample = () => win.webContents.executeJavaScript(`({frames:window.framesCount,
    text:document.querySelector('input').value,button:document.querySelector('button').textContent,
    scroll:window.scrollY,width:window.innerWidth,height:window.innerHeight})`);
  const click = async selector => {
    const p = await win.webContents.executeJavaScript(`(()=>{const r=document.querySelector(${JSON.stringify(selector)}).getBoundingClientRect();
      return {x:Math.round(r.left+r.width/2),y:Math.round(r.top+r.height/2)}})()`);
    win.webContents.sendInputEvent({ type: 'mouseDown', button: 'left', clickCount: 1, ...p });
    win.webContents.sendInputEvent({ type: 'mouseUp', button: 'left', clickCount: 1, ...p });
  };
  try {
    await delay(5000);
    win.focus();
    await click('input');
    for (const keyCode of 'G4 input') win.webContents.sendInputEvent({ type: 'char', keyCode });
    await click('button');
    await delay(1000);
    let state = await sample();
    checks.input = state.text === 'G4 input' && state.button === 'Clicked';
    log('lifecycle input', state);
    win.webContents.sendInputEvent({ type: 'mouseWheel', x: 400, y: 350,
      deltaY: -160, deltaX: 0, canScroll: true });
    await delay(1000);
    state = await sample();
    checks.scroll = state.scroll > 0;
    log('lifecycle scroll', state);
    win.setContentSize(620, 420);
    await delay(1000);
    state = await sample();
    checks.resize = state.width === 620 && state.height === 420;
    log('lifecycle resize', state);
    win.minimize();
    await delay(1000);
    checks.minimized = win.isMinimized();
    log('lifecycle minimize', { minimized: win.isMinimized() });
    win.restore(); win.focus();
    await delay(1000);
    const before = await sample();
    await delay(1000);
    state = await sample();
    checks.restored = !win.isMinimized() && state.frames > before.frames;
    log('lifecycle restore', { before, after: state, minimized: win.isMinimized() });
    const loaded = new Promise(resolve => win.webContents.once('did-finish-load', resolve));
    win.webContents.reload();
    await Promise.race([loaded, delay(5000).then(() => { throw new Error('Reload timed out'); })]);
    await delay(1000);
    state = await sample();
    checks.reload = state.text === '' && state.button === 'Click' && state.scroll === 0 && state.frames > 0;
    log('lifecycle reload', state);
    win.setContentSize(740, 520);
    await delay(1000);
    state = await sample();
    checks.resize_back = state.width === 740 && state.height === 520;
    log('lifecycle resize back', state);
    log('lifecycle result', { passed: Object.values(checks).every(Boolean), checks,
      note: 'Electron input injection and API lifecycle; not hardware-input or desktop acceptance' });
  } catch (error) {
    log('lifecycle result', { passed: false, checks, error: String(error) });
  }
}
app.setPath('userData', profile);
app.enableSandbox();
log('versions', { versions: process.versions, argv: process.argv, sandboxRequested: true });
app.on('child-process-gone', (_event, details) => log('child process gone', details));
app.on('render-process-gone', (_event, _contents, details) => log('renderer gone', details));
app.whenReady().then(async () => {
  if (process.env.WEX_WEBVIEW2_PROFILE_WRITE) {
    // No HTTP request is made. Chromium's persistent cookie store, not a file
    // written directly by this fixture, must carry the nonce across processes.
    const url = 'https://g4.invalid/';
    const name = 'wex-g4-restart-proof';
    const expected = process.env.WEX_WEBVIEW2_PROFILE_EXPECT || null;
    const written = process.env.WEX_WEBVIEW2_PROFILE_WRITE;
    const cookies = await session.defaultSession.cookies.get({ url, name });
    const previous = cookies.length === 0 ? null : cookies.length === 1 ? cookies[0].value : 'DUPLICATE';
    if (previous !== expected) {
      log('profile persistence', { passed: false, expected, previous, written, flushed: false });
      throw new Error('Persistent Chromium cookie did not match previous process');
    }
    await session.defaultSession.cookies.set({ url, name, value: written, secure: true,
      expirationDate: Date.now() / 1000 + 3600 });
    await session.defaultSession.cookies.flushStore();
    const check = await session.defaultSession.cookies.get({ url, name });
    const passed = check.length === 1 && check[0].value === written;
    log('profile persistence', { passed, expected, previous, written, flushed: true });
    if (!passed) throw new Error('Persistent Chromium cookie write/readback failed');
  }
  const win = new BrowserWindow({ x: 25, y: 25, width: 740, height: 520,
    webPreferences: { sandbox: true, contextIsolation: true, nodeIntegration: false } });
  app.getGPUInfo('complete').then(info => log('gpu info', info)).catch(error => log('gpu info failed', String(error)));
  log('gpu features', app.getGPUFeatureStatus());
  const html = `<!doctype html><meta charset="utf-8"><style>
    body{background:#e8f5e9;font:20px sans-serif;padding:24px}
    #box{background:#1565c0;width:40px;height:40px}</style>
    <h1>Original G4 rendering fixture</h1><p id="counter">Starting</p><div id="box"></div>
    <input placeholder="Typing test"><button onclick="this.textContent='Clicked'">Click</button>
    <div style="height:900px">Scroll test</div><script>
    window.framesCount=0;window.timerTicks=0;
    function frame(){framesCount++;if(!window.sceneFrozen)box.style.transform='translateX('+framesCount%240+'px)';requestAnimationFrame(frame)}
    requestAnimationFrame(frame);setInterval(()=>{timerTicks++;if(!window.sceneFrozen){counter.textContent='Frames: '+framesCount;
    document.body.style.background=timerTicks%2?'#e8f5e9':'#fff3e0'}},1000);</script>`;
  await win.loadURL('data:text/html;charset=utf-8,' + encodeURIComponent(html));
  fs.appendFileSync(logPath, `${Date.now() - started} ms navigation completed hr=0x00000000 success=1 web_error=0\n`);
  if (process.env.WEX_WEBVIEW2_LIFECYCLE === '1') void exerciseLifecycle(win);
  if (process.env.WEX_WEBVIEW2_FREEZE === '1') setTimeout(async () => {
    try {
      await win.webContents.executeJavaScript(`window.sceneFrozen=true;box.style.transform='translateX(160px)';
        counter.textContent='Frozen at x=160';`);
      log('scene frozen', { x: 160, note: 'RAF counters continue; no further fixture DOM updates' });
    } catch (error) { log('freeze failed', String(error)); }
  }, 8000);
  let pending = false;
  const timer = setInterval(async () => {
    if (pending || win.isDestroyed()) return;
    pending = true;
    try {
      const sample = await win.webContents.executeJavaScript(`({frames:window.framesCount,ticks:window.timerTicks,
        visibility:document.visibilityState,paints:performance.getEntriesByType('paint').length})`);
      fs.appendFileSync(logPath, `${Date.now() - started} ms sample hr=0x00000000 ${JSON.stringify(sample)}\n`);
    } catch (error) { log('sample failed', String(error)); }
    finally { pending = false; }
  }, 1000);
  setTimeout(async () => {
    try {
      const image = await win.webContents.capturePage();
      fs.writeFileSync(path.join(path.dirname(logPath), 'electron-capture.png'), image.toPNG());
      log('capture', { size: image.getSize(), empty: image.isEmpty(), note: 'Not proof of front-window presentation' });
    } catch (error) { log('capture failed', String(error)); }
  }, process.env.WEX_WEBVIEW2_FREEZE === '1' ? 20000 : 12000);
  setTimeout(() => { clearInterval(timer); app.quit(); }, Number(process.env.WEX_WEBVIEW2_SECONDS || 25) * 1000);
}).catch(error => { log('startup failed', String(error)); app.exit(2); });
// Bound startup even if a renderer/GPU promise never resolves.
setTimeout(() => app.exit(3), (Number(process.env.WEX_WEBVIEW2_SECONDS || 25) + 10) * 1000);
