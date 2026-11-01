// SPDX-License-Identifier: GPL-2.0-or-later
'use strict';
const assert=require('node:assert/strict');
const vm=require('node:vm');
const fs=require('node:fs');
const source=fs.readFileSync(require('node:path').join(__dirname,'g4-edge-probe/scene.js'),'utf8');
const nonce='a'.repeat(32);

async function execute(options={}) {
  let title='', frames=[],timers=[],cleared=[], clears=[], events={};
  const canvases={};
  for (const api of ['webgl','webgl2']) {
    let pixel=[0,0,0,255], bound=false;
    const gl={RGBA:1,UNSIGNED_BYTE:2,COLOR_BUFFER_BIT:3,TEXTURE_2D:4,TEXTURE_MIN_FILTER:5,
      TEXTURE_MAG_FILTER:6,NEAREST:7,FRAMEBUFFER:8,COLOR_ATTACHMENT0:9,FRAMEBUFFER_COMPLETE:10,NO_ERROR:0,RENDERER:11,
      clearColor(...values){pixel=values.map(v=>Math.round(v*255));},clear(){clears.push({api,bound,pixel:[...pixel]});},
      readPixels(x,y,w,h,f,t,out){out.set(options.badPixel ? [1,2,3,4] : pixel);},
      createTexture(){return {};},createFramebuffer(){return {};},bindTexture(){},texParameteri(){},texImage2D(){},
      bindFramebuffer(target,value){bound=!!value;},framebufferTexture2D(){},
      checkFramebufferStatus(){return options.badFbo ? 0 : 10;},deleteFramebuffer(){},deleteTexture(){},viewport(){},
      getExtension(){return null;},getParameter(){return options.renderer ?? 'ANGLE (VirGL test fake)';},
      getError(){return options.glError ? 1280 : 0;},isContextLost(){return !!options.lost;}};
    canvases[api]={addEventListener(name,callback){events[api+name]=callback;},
      getContext(name,attrs){assert.equal(name,api);assert.equal(attrs.alpha,false);return options.missing===api ? null : gl;},
      getBoundingClientRect(){return {x:api==='webgl'?32:160,y:112,width:96,height:96};}};
  }
  const document={body:{},getElementById(id){return id==='marker' ? {getBoundingClientRect(){return {x:32,y:32,width:48,height:48};}} : canvases[id];},
    set title(value){title=value;},get title(){return title;}};
  const context={document,location:{hash:'#nonce='+nonce},URLSearchParams,Uint8Array,
    innerWidth:640,innerHeight:480,devicePixelRatio:1,performance:{now:()=>42},
    getComputedStyle(){return {backgroundColor:'rgb(232, 245, 233)'};},
    requestAnimationFrame(cb){frames.push(cb);},setTimeout(cb){timers.push(cb);return timers.length;},clearTimeout(id){cleared.push(id);}};
  const promise=vm.runInNewContext(source,context);
  if (frames.length) {
    assert.equal(title,''); // No readiness before rendering opportunities.
    if (options.timeout) timers[0]();
    else {frames.shift()(10);assert.equal(title,'');frames.shift()(20);}
  }
  await promise;
  const record=JSON.parse(title.slice(8));
  return {record,title,clears,events,document,cleared};
}

(async()=>{
  const good=await execute();
  assert.equal(good.record.passed,true);assert.equal(good.record.nonce,nonce);
  assert.equal(good.record.viewport.width,640);assert.equal(good.clears.length,6);
  assert.deepEqual(good.clears.slice(-1)[0].pixel,[0,255,255,255]);
  assert.deepEqual(good.record.apis.webgl.defaultPixel,[255,0,0,255]);
  assert.deepEqual(good.record.apis.webgl2.fboPixel,[0,255,0,255]);
  assert.deepEqual(good.cleared,[1]);
  for (const options of [{missing:'webgl'},{missing:'webgl2'},{badPixel:true},{badFbo:true},{glError:true},{lost:true},{renderer:''},{timeout:true}]) {
    const bad=await execute(options);assert.equal(bad.record.passed,false,JSON.stringify(options));
  }
  good.events.webglwebglcontextlost();
  assert.equal(JSON.parse(good.document.title.slice(8)).passed,false);
  // Used by Python protocol tests: actual fixture execution, not a hand-authored passed object.
  if (process.argv.includes('--record')) process.stdout.write(good.title+'\n');
  else console.log('PASS actual Edge fixture: GL/FBO/final colors, two frames, timeout/API/error/loss negatives');
})().catch(error=>{console.error(error);process.exitCode=1;});
