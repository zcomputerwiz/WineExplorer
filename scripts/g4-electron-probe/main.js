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
async function visualCheckpoint(stage) {
  if (process.env.WEX_WEBVIEW2_VISUAL_CHECKPOINTS !== '1') return;
  const ack = path.join(path.dirname(logPath), 'visual-checkpoint.ack');
  log('visual checkpoint', { stage });
  const deadline = Date.now() + 10000;
  while (Date.now() < deadline) {
    try {
      if (fs.readFileSync(ack, 'utf8') === stage) {
        log('visual checkpoint captured', { stage });
        return;
      }
    } catch (error) { if (error.code !== 'ENOENT') throw error; }
    await delay(100);
  }
  throw new Error('Front-window capture not acknowledged: ' + stage);
}
const childExits = [], rendererExits = [];

// Runs inside the ordinary sandboxed renderer, with no preload/Node access.
function probeRecoveryPixels() {
  window.g4Losses ||= { webgl: 0, webgl2: 0 };
  const oldLost = Object.fromEntries(Object.entries(window.g4Contexts || {}).map(([name, item]) =>
    [name, { lost: item.gl.isContextLost(), events: window.g4Losses[name] }]));
  for (const item of Object.values(window.g4Contexts || {})) item.canvas.remove();
  window.g4Contexts = {};
  const contexts = {};
  for (const name of ['webgl', 'webgl2']) {
    const canvas = document.createElement('canvas');
    canvas.width = canvas.height = 32;
    // Keep the diagnostic WebGL surface alive without adding a visible test
    // swatch or changing the app's front-window layout during recovery capture.
    canvas.style.cssText = 'position:absolute;left:-10000px;top:0';
    document.querySelector('input').before(canvas);
    const gl = canvas.getContext(name, { antialias: false, preserveDrawingBuffer: true });
    if (!gl) { contexts[name] = { available: false }; continue; }
    window.g4Contexts[name] = { gl, canvas };
    canvas.addEventListener('webglcontextlost', event => {
      window.g4Losses[name]++; event.preventDefault();
    });
    const pixel = () => { const bytes = new Uint8Array(4);
      gl.readPixels(0, 0, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, bytes); return Array.from(bytes); };
    gl.clearColor(1, 0, 0, 1); gl.clear(gl.COLOR_BUFFER_BIT);
    const defaultPixel = pixel(), defaultError = gl.getError();
    const texture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 2, 2, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
    const framebuffer = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, texture, 0);
    const framebufferStatus = gl.checkFramebufferStatus(gl.FRAMEBUFFER);
    gl.clearColor(0, 1, 0, 1); gl.clear(gl.COLOR_BUFFER_BIT);
    const texturePixel = pixel(), textureError = gl.getError();
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.deleteFramebuffer(framebuffer); gl.deleteTexture(texture);
    const info = gl.getExtension('WEBGL_debug_renderer_info');
    contexts[name] = { available: true, lost: gl.isContextLost(), defaultPixel, defaultError,
      framebufferStatus, texturePixel, textureError,
      renderer: info ? gl.getParameter(info.UNMASKED_RENDERER_WEBGL) : null };
  }
  return { contexts, oldLost, frames: window.framesCount };
}

async function exerciseGPURecovery(win) {
  let before = null, after = null, fault = null;
  try {
    await delay(5000);
    const snapshot = async () => {
      const pixels = await win.webContents.executeJavaScript(`(${probeRecoveryPixels.toString()})()`);
      const gpu = app.getAppMetrics().filter(item => item.type === 'GPU');
      if (gpu.length !== 1 || gpu[0].pid === process.pid) throw new Error('Expected exactly one owned GPU child');
      return { ...pixels, gpu: { pid: gpu[0].pid, creationTime: gpu[0].creationTime },
        rendererPid: win.webContents.getOSProcessId(), features: app.getGPUFeatureStatus() };
    };
    before = await snapshot();
    log('gpu recovery before', before);
    const valid = item => item.available === true && !item.lost && item.defaultError === 0
      && item.textureError === 0 && item.framebufferStatus === 36053
      && JSON.stringify(item.defaultPixel) === '[255,0,0,255]'
      && JSON.stringify(item.texturePixel) === '[0,255,0,255]'
      && typeof item.renderer === 'string' && !/swiftshader|software rasterizer/i.test(item.renderer);
    if (!Object.values(before.contexts).every(valid) || before.features.gpu_compositing !== 'enabled')
      throw new Error('GPU recovery preconditions failed; no process killed');
    // Revalidate identity immediately before killing ONLY this app's GPU child.
    const current = app.getAppMetrics().filter(item => item.type === 'GPU');
    if (current.length !== 1 || current[0].pid !== before.gpu.pid
        || current[0].creationTime !== before.gpu.creationTime || childExits.length || rendererExits.length)
      throw new Error('GPU identity changed before fault injection');
    fault = { ...before.gpu, requested: true, ms: Date.now() - started };
    log('gpu recovery fault', fault);
    process.kill(fault.pid); // Win32 PID in this private Wine server, not a Linux/global PID
    const deadline = Date.now() + 10000;
    while (Date.now() < deadline) {
      const gpu = app.getAppMetrics().filter(item => item.type === 'GPU');
      if (childExits.some(item => item.type === 'GPU') && gpu.length === 1 && gpu[0].pid !== fault.pid) break;
      await delay(100);
    }
    await delay(2000); // allow the genuine context-loss events to reach the page
    after = await snapshot();
    const frames = after.frames;
    await delay(2000);
    after.laterFrames = await win.webContents.executeJavaScript('window.framesCount');
    const exits = childExits.filter(item => item.type === 'GPU');
    const passed = Object.values(after.contexts).every(valid) && after.laterFrames > frames
      && after.gpu.pid !== before.gpu.pid && after.rendererPid === before.rendererPid
      && exits.length === 1 && rendererExits.length === 0
      && ['webgl', 'webgl2'].every(name => after.oldLost[name]?.events >= 1)
      && JSON.stringify(after.features) === JSON.stringify(before.features)
      && ['webgl', 'webgl2'].every(name => after.contexts[name].renderer === before.contexts[name].renderer);
    log('gpu recovery result', { passed, before, after, fault, exits, rendererExits });
    if (passed) await win.webContents.executeJavaScript(`window.sceneFrozen=true;
      box.style.transform='translateX(160px)';counter.textContent='GPU recovery verified';
      document.body.style.background='#fff3e0';`);
  } catch (error) {
    log('gpu recovery result', { passed: false, before, after, fault,
      exits: childExits.filter(item => item.type === 'GPU'), rendererExits, error: String(error) });
  }
}
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
    await visualCheckpoint('ready');
    await click('input');
    for (const keyCode of 'G4 input') win.webContents.sendInputEvent({ type: 'char', keyCode });
    await click('button');
    await delay(1000);
    let state = await sample();
    checks.input = state.text === 'G4 input' && state.button === 'Clicked';
    log('lifecycle input', state);
    await visualCheckpoint('input');
    win.webContents.sendInputEvent({ type: 'mouseWheel', x: 400, y: 350,
      deltaY: -160, deltaX: 0, canScroll: true });
    await delay(1000);
    state = await sample();
    checks.scroll = state.scroll > 0;
    log('lifecycle scroll', state);
    await visualCheckpoint('scroll');
    win.setContentSize(620, 420);
    await delay(1000);
    state = await sample();
    checks.resize = state.width === 620 && state.height === 420;
    log('lifecycle resize', state);
    await visualCheckpoint('resize');
    win.minimize();
    await delay(1000);
    checks.minimized = win.isMinimized();
    log('lifecycle minimize', { minimized: win.isMinimized() });
    await visualCheckpoint('minimized');
    win.restore(); win.focus();
    await delay(1000);
    const before = await sample();
    await delay(1000);
    state = await sample();
    checks.restored = !win.isMinimized() && state.frames > before.frames;
    log('lifecycle restore', { before, after: state, minimized: win.isMinimized() });
    await visualCheckpoint('restored');
    const loaded = new Promise(resolve => win.webContents.once('did-finish-load', resolve));
    win.webContents.reload();
    await Promise.race([loaded, delay(5000).then(() => { throw new Error('Reload timed out'); })]);
    await delay(1000);
    state = await sample();
    checks.reload = state.text === '' && state.button === 'Click' && state.scroll === 0 && state.frames > 0;
    log('lifecycle reload', state);
    await visualCheckpoint('reload');
    if (process.env.WEX_WEBVIEW2_RESIZE_BORDER === '1') {
      await win.webContents.executeJavaScript(`window.sceneFrozen=true;
        box.style.transform='translateX(160px)';counter.textContent='Frozen resize border';
        document.body.style.background='#e8f5e9';`);
      log('resize border frozen', { x: 160, note: 'No further DOM updates during growth/held captures' });
    }
    win.setContentSize(740, 520);
    await delay(1000);
    state = await sample();
    checks.resize_back = state.width === 740 && state.height === 520;
    log('lifecycle resize back', state);
    await visualCheckpoint('resize_back');
    log('lifecycle result', { passed: Object.values(checks).every(Boolean), checks,
      note: 'Electron input injection and API lifecycle; not hardware-input or desktop acceptance' });
  } catch (error) {
    log('lifecycle result', { passed: false, checks, error: String(error) });
  }
}
app.setPath('userData', profile);
app.enableSandbox();
log('versions', { versions: process.versions, argv: process.argv, sandboxRequested: true });
app.on('child-process-gone', (_event, details) => { childExits.push(details); log('child process gone', details); });
app.on('render-process-gone', (_event, _contents, details) => { rendererExits.push(details); log('renderer gone', details); });
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
  if (process.env.WEX_WEBVIEW2_GPU_RECOVERY === '1') void exerciseGPURecovery(win);
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
