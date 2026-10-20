// SPDX-License-Identifier: GPL-2.0-or-later
// Original offline fixture; no npm packages, preload or renderer Node access.
const { app, BrowserWindow } = require('electron');
const fs = require('node:fs');
const path = require('node:path');

const logPath = process.env.WEX_WEBVIEW2_LOG;
const profile = process.env.WEX_WEBVIEW2_PROFILE;
if (!logPath || !profile) throw new Error('Use the isolated G4 runner');
const started = Date.now();
function log(event, data) {
  fs.appendFileSync(logPath, `${Date.now() - started} ms ${event} ${JSON.stringify(data)}\n`);
}
app.setPath('userData', profile);
app.enableSandbox();
log('versions', { versions: process.versions, argv: process.argv, sandboxRequested: true });
app.on('child-process-gone', (_event, details) => log('child process gone', details));
app.on('render-process-gone', (_event, _contents, details) => log('renderer gone', details));
app.whenReady().then(async () => {
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
