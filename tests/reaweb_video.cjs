// Compare the shipped WebView renderer (including CPU fallback) with the former
// native renderer's independently checked D3D reference, across sizes and shaders.
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {chromium}=require(require.resolve('playwright',{paths:[process.env.REAGBA_NODE_MODULES||path.resolve(__dirname,'../node_modules')]}));
const root=path.resolve(__dirname,'../verification/shaders');
const app=fs.readFileSync(require('./ui_path.cjs')('video.js'),'utf8');
const renderer=app.slice(app.indexOf('function createGameVideo(){'));
(async()=>{
 const browser=await chromium.launch({headless:true,channel:process.env.REAGBA_BROWSER_CHANNEL,
  env:Object.fromEntries(Object.entries(process.env).map(([k,v])=>[k.toUpperCase(),v]))});
 try{
  let maximum=0,cases=0;
  for(const cpu of [false,true]){
   const page=await browser.newPage();await page.setContent('<div id="game-viewport"></div>');
   await page.evaluate(cpu=>{
    window.settings={integer_scaling:false,filter:'nearest',vsync:false};window.toast=message=>{throw Error(message);};
    if(cpu){const original=HTMLCanvasElement.prototype.getContext;HTMLCanvasElement.prototype.getContext=function(type,...args){return type==='webgl2'?null:original.call(this,type,...args);};}
   },cpu);
   await page.addScriptTag({content:renderer});
   await page.evaluate(rgba=>{window.video=createGameVideo();window.fixture={width:240,height:160,data:Uint8Array.from(atob(rgba),c=>c.charCodeAt(0))};},fs.readFileSync(path.join(root,'input.rgba')).toString('base64'));
   for(const width of [144,240,480,720,960,333])for(const mode of [0,1,2]){
    const gpu=await page.evaluate(({width,mode,cpu})=>{
     const height=Math.floor(width*2/3),holder=document.getElementById('game-viewport');holder.style.width=width+'px';holder.style.height=height+'px';
     settings.shader=['none','lcd3x','lcd-grid-v2'][mode];video.frame(fixture);
     const canvas=document.getElementById('game-frame');
     if(cpu)return Array.from(canvas.getContext('2d').getImageData(0,0,width,height).data);
     const gl=canvas.getContext('webgl2'),raw=new Uint8Array(width*height*4),pixels=new Uint8Array(raw.length);gl.readPixels(0,0,width,height,gl.RGBA,gl.UNSIGNED_BYTE,raw);
     if(gl.getError()!==gl.NO_ERROR)throw Error('GPU rendering error');
     for(let y=0;y<height;y++)pixels.set(raw.subarray(y*width*4,(y+1)*width*4),(height-y-1)*width*4);
     return Array.from(pixels);
    },{width,mode,cpu});
    const d3d=fs.readFileSync(path.join(root,`${width}-${mode}.rgba`)),height=Math.floor(width*2/3);assert.equal(gpu.length,d3d.length);
    for(let i=0;i<gpu.length;i++){
     const pixel=Math.floor(i/4),x=pixel%width,y=Math.floor(pixel/width),sx=(x+.5)*240/width,sy=(y+.5)*160/height;
     if(mode<2&&(Math.abs(sx-Math.round(sx))<1e-6||Math.abs(sy-Math.round(sy))<1e-6))continue;
     const diff=Math.abs(gpu[i]-d3d[i]);maximum=Math.max(maximum,diff);
     assert(diff<=2,`${cpu?'CPU':'WebGL'}/D3D width=${width} mode=${mode} x=${x} y=${y}: ${gpu[i]} vs ${d3d[i]}`);
    }
    cases++;
   }
   await page.close();
  }
  console.log(`PASS: production WebGL + Canvas fallback, ${cases} D3D comparisons, maximum channel difference ${maximum}/255`);
 }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
