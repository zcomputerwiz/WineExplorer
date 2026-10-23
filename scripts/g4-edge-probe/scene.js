// SPDX-License-Identifier: GPL-2.0-or-later
// Original offline page. No network, privileged renderer API or CSS canvas fallback.
'use strict';
(async function () {
  const nonce = new URLSearchParams(location.hash.slice(1)).get('nonce');
  if (!/^[a-f0-9]{32}$/.test(nonce || '')) throw new Error('Invalid nonce');
  const rect = element => {
    const r = element.getBoundingClientRect();
    return {x:r.x, y:r.y, width:r.width, height:r.height};
  };
  const result = {v:1, nonce, kind:'ready', passed:false, apis:{}};
  let lostEvent=false;
  try {
    for (const [api, color] of [['webgl',[255,0,255,255]], ['webgl2',[0,255,255,255]]]) {
      const canvas = document.getElementById(api);
      canvas.addEventListener('webglcontextlost',()=>{
        lostEvent=true; result.passed=false; result.error=api+' context lost';
        document.title='WEXEDGE '+JSON.stringify(result);
      });
      const gl = canvas.getContext(api, {alpha:false, antialias:false, preserveDrawingBuffer:true});
      if (!gl) throw new Error('Missing '+api);
      const rgba = () => {
        const p = new Uint8Array(4);
        gl.readPixels(0,0,1,1,gl.RGBA,gl.UNSIGNED_BYTE,p);
        return Array.from(p);
      };
      gl.clearColor(1,0,0,1); gl.clear(gl.COLOR_BUFFER_BIT);
      const defaultPixel = rgba();
      const texture = gl.createTexture(); const framebuffer = gl.createFramebuffer();
      if (!texture || !framebuffer) throw new Error('Missing GL resource');
      gl.bindTexture(gl.TEXTURE_2D,texture);
      gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.NEAREST);
      gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,1,1,0,gl.RGBA,gl.UNSIGNED_BYTE,null);
      gl.bindFramebuffer(gl.FRAMEBUFFER,framebuffer);
      gl.framebufferTexture2D(gl.FRAMEBUFFER,gl.COLOR_ATTACHMENT0,gl.TEXTURE_2D,texture,0);
      const fboComplete = gl.checkFramebufferStatus(gl.FRAMEBUFFER) === gl.FRAMEBUFFER_COMPLETE;
      gl.clearColor(0,1,0,1); gl.clear(gl.COLOR_BUFFER_BIT);
      const fboPixel = rgba();
      gl.bindFramebuffer(gl.FRAMEBUFFER,null);
      gl.deleteFramebuffer(framebuffer); gl.deleteTexture(texture);
      gl.viewport(0,0,96,96);
      gl.clearColor(...color.map(c=>c/255)); gl.clear(gl.COLOR_BUFFER_BIT);
      const finalPixel = rgba();
      const debug = gl.getExtension('WEBGL_debug_renderer_info');
      const renderer = String(gl.getParameter(debug ? debug.UNMASKED_RENDERER_WEBGL : gl.RENDERER));
      const error = gl.getError(), lost = gl.isContextLost();
      result.apis[api] = {defaultPixel,fboPixel,finalPixel,fboComplete,error,lost,renderer,rect:rect(canvas)};
      if (!fboComplete || error !== gl.NO_ERROR || lost || !renderer || renderer === 'null'
          || JSON.stringify(defaultPixel)!=='[255,0,0,255]'
          || JSON.stringify(fboPixel)!=='[0,255,0,255]'
          || JSON.stringify(finalPixel)!==JSON.stringify(color)) throw new Error(api+' readback failed');
    }
    // Rendering opportunities only, not proof of front presentation. Main runner also times out.
    await new Promise((resolve,reject)=>{
      const timer=setTimeout(()=>reject(new Error('rAF timeout')),1500);
      requestAnimationFrame(()=>requestAnimationFrame(()=>{clearTimeout(timer);resolve();}));
    });
    result.viewport={width:innerWidth,height:innerHeight,dpr:devicePixelRatio};
    result.marker=rect(document.getElementById('marker'));
    result.background=getComputedStyle(document.body).backgroundColor;
    result.frozenAtMs=performance.now();
    if (lostEvent) throw new Error('Context lost before ready');
    result.passed=true;
  } catch (error) { result.error=String(error); }
  document.title='WEXEDGE '+JSON.stringify(result);
})();
