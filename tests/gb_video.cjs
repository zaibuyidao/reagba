const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {chromium}=require(require.resolve('playwright',{paths:[process.env.REAGBA_NODE_MODULES||path.resolve(__dirname,'../node_modules')]}));
(async()=>{
 const browser=await chromium.launch({headless:true,channel:process.env.REAGBA_BROWSER_CHANNEL,
  env:Object.fromEntries(Object.entries(process.env).map(([k,v])=>[k.toUpperCase(),v]))});
 try{
  const results=[];
  for(const cpu of [false,true]){
   const page=await browser.newPage();await page.setContent('<div id="game-viewport" style="width:480px;height:432px"></div>');
   await page.evaluate(cpu=>{
    window.settings={integer_scaling:true,filter:'nearest',vsync:false};window.state={game:{system:'GBA'}};
    window.toast=message=>{throw Error(message);};
    if(cpu){const original=HTMLCanvasElement.prototype.getContext;HTMLCanvasElement.prototype.getContext=function(type,...args){return type==='webgl2'?null:original.call(this,type,...args);};}
   },cpu);
   await page.addScriptTag({content:fs.readFileSync(require('./ui_path.cjs')('video.js'),'utf8')});
   const output=await page.evaluate(cpu=>{
    const video=createGameVideo(),data=new Uint8Array(240*160*4);
    for(let y=0;y<160;y++)for(let x=0;x<240;x++){
     const offset=(y*240+x)*4;
     data.set(x>=40&&x<200&&y>=8&&y<152?[x-40,y-8,180,255]:[0,0,0,255],offset);
    }
    const read=()=>{
     const canvas=document.getElementById('game-frame');
     if(cpu)return canvas.getContext('2d').getImageData(0,0,480,432).data;
     const gl=canvas.getContext('webgl2'),raw=new Uint8Array(480*432*4),pixels=new Uint8Array(raw.length);
     gl.readPixels(0,0,480,432,gl.RGBA,gl.UNSIGNED_BYTE,raw);
     if(gl.getError()!==gl.NO_ERROR)throw Error('WebGL error');
     for(let y=0;y<432;y++)pixels.set(raw.subarray(y*480*4,(y+1)*480*4),(431-y)*480*4);
     return pixels;
    };
    // Frame can arrive before status when reopening a paused game window.
    video.frame({width:240,height:160,data});state.game.system='GB';video.draw();
    const pixels=read();
    for(let y=0;y<432;y++)for(let x=0;x<480;x++){
     const offset=(y*480+x)*4;
     if(pixels[offset]!==Math.floor(x/3)||pixels[offset+1]!==Math.floor(y/3)||pixels[offset+2]!==180)
      throw Error('GB crop/aspect or paused redraw mismatch');
    }
    state.game.system='GBC';video.frame({width:240,height:160,data});
    const shaders=[];
    for(const shader of ['lcd3x','lcd-grid-v2']){settings.shader=shader;video.draw();shaders.push(Array.from(read()));}
    settings.shader='none';state.game.system='GBA';data.fill(255);video.frame({width:240,height:160,data});
    const gba=read();
    for(let y=0;y<432;y++)for(let x=0;x<480;x++)if(gba[(y*480+x)*4]!==((y>=56&&y<376)?255:0))throw Error('GBA aspect changed after GB');
    return shaders;
   },cpu);
   results.push(output);await page.close();
  }
  for(let shader=0;shader<2;shader++)for(let i=0;i<results[0][shader].length;i++)
   assert(Math.abs(results[0][shader][i]-results[1][shader][i])<=2,`GB shader ${shader} channel ${i}`);
  console.log('PASS: GB/GBC native crop, aspect, paused redraw, GPU/CPU shaders and GBA switching');
 }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
