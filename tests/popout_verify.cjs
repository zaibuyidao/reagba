const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),http=require('node:http');
const {chromium}=require(require.resolve('playwright',{paths:[process.env.REAGBA_NODE_MODULES||path.resolve(__dirname,'../node_modules')]}));
const server=http.createServer((req,res)=>{
 const name=req.url.slice(1);if(!/^[a-z0-9.]+$/.test(name)){res.writeHead(404).end();return;}
 const file=require('./ui_path.cjs')(name);if(!fs.existsSync(file)){res.writeHead(404).end();return;}
 res.setHeader('Content-Type',name.endsWith('.js')?'text/javascript':name.endsWith('.css')?'text/css':'text/html');res.end(fs.readFileSync(file));
});
function runtimeFixture({id,system}){
 const state={loaded:true,running:false,speed:1,base_speed:1,volume:.3,game:{hash:'test',title:'Test game',code:'TEST',system},app_version:'0.1.1'};
 let preferences={language:'en',integer_scaling:false,library_expanded:true},cleanup;
 window.sent=[];window.failOpen=false;
 const service={send:(method,payload)=>sent.push({method,payload}),invoke:async(method,payload)=>{
  if(method==='get_settings')return preferences;
  if(method==='set_settings')return preferences={...preferences,...payload.settings};
  if(method==='scan_roms'||method==='get_save_states')return [];
  return state;
 }};
 window.reaper={
  lifecycle:{ready:Promise.resolve({windowId:id}),on:async(name,fn)=>{cleanup=fn;}},
  host:{service:()=>service},GetResourcePath:async()=>'/resource',
  fs:{stat:async()=>({exists:false}),writeFile:async()=>{}},
  events:{on:async()=>()=>{}},
  system:{schedule:async(fn,{interval})=>{const handle=setInterval(fn,interval);return ()=>clearInterval(handle);}},
  window:{setIconVisible:async()=>{},setDocked:async()=>false,isDocked:async()=>false,focus:async()=>{},
   open:async file=>{if(window.failOpen)throw Error('Open failed');return window.openGame(file);},
   close:async()=>{await cleanup?.();return window.closeGame();}},
  stream:{open:async()=>({info:{width:240,height:160},on:(event,fn)=>{
   if(event==='data'){
    window.deliverFrame=(red=255,green=0)=>{const data=new Uint8Array(240*160*4);for(let i=0;i<data.length;i+=4){data[i]=red;data[i+1]=green;data[i+3]=255;}fn({data});};
    queueMicrotask(()=>window.deliverFrame());
   }
  },close:async()=>{}})}
 };
}
(async()=>{
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
 const browser=await chromium.launch({headless:true,channel:process.env.REAGBA_BROWSER_CHANNEL,env:Object.fromEntries(Object.entries(process.env).map(([k,v])=>[k.toUpperCase(),v]))});
 try{
  const context=await browser.newContext(),errors=[];let nextId=1,child;
  async function open(file){
   const page=await context.newPage(),id=nextId++;
   page.on('pageerror',error=>errors.push(error.message));
   await page.addInitScript(runtimeFixture,{id,system:process.env.REAGBA_TEST_SYSTEM||'GBA'});
   await page.exposeBinding('openGame',async(_,file)=>{child=await open(file);return child.id;});
   await page.exposeBinding('closeGame',()=>{setTimeout(()=>page.close().catch(()=>{}),0);return true;});
   await page.goto(`http://127.0.0.1:${server.address().port}/${file}`);
   return {page,id};
  }
  const {page}=await open('index.html');
  await page.waitForFunction(()=>document.body.classList.contains('playing'));
  const settle=()=>page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
  await settle();
  const size=await page.locator('#game-viewport').boundingBox();
  const headingGap=()=>page.evaluate(()=>document.querySelector('.now-label').getBoundingClientRect().top-document.getElementById('library-view').getBoundingClientRect().bottom);
  const initialGap=await headingGap();
  async function detach(){
   await settle();const before=await page.locator('#game-viewport').boundingBox();
   await page.click('#popout');
   await page.waitForFunction(()=>document.body.classList.contains('game-detached'),null,{timeout:5000}).catch(async error=>{console.error(await page.locator('#toast').textContent(),errors);throw error;});
   await child.page.waitForFunction(()=>window.ReaGBAPopout.ownsGame()&&document.getElementById('game-frame'),null,{timeout:5000}).catch(async error=>{console.error(await child.page.evaluate(()=>({storage:{...localStorage},toast:document.getElementById('toast').textContent,owns:ReaGBAPopout.ownsGame()})),errors);throw error;});
   await settle();
   assert.equal(await page.locator('#game-viewport').isVisible(),true,'expanded library retains the empty canvas');
   assert.equal(await page.locator('#game-frame').isVisible(),false,'only the game image is hidden');
   assert.deepEqual(await page.locator('#game-viewport').boundingBox(),before,'expanded canvas retains its position and size');
   assert.equal(await page.locator('#library-splitter').isVisible(),true,'expanded splitter remains available');
   assert.ok(Math.abs(await headingGap()-initialGap)<1,'detached status row keeps the original library spacing');
   return child.page;
  }
  let game=await detach();
  await game.waitForFunction(()=>document.title==='ReaGBA - Test game');
  assert.equal(await game.locator('header, #restore-game, #now-title').count(),0,'pop-out has no in-page title or return button');
  for(const [width,height] of [[320,500],[760,540],[1000,720]]){
   await game.setViewportSize({width,height});
   const gaps=await game.evaluate(()=>{
    const screen=document.getElementById('game-viewport').getBoundingClientRect();
    return {left:screen.left,right:innerWidth-screen.right,bottom:innerHeight-screen.bottom,top:screen.top};
   });
   for(const side of ['left','right','bottom','top'])assert.ok(Math.abs(gaps[side]-3)<.1,'compact game window has a single 3px gap '+side+': '+JSON.stringify(gaps));
  }
  fs.mkdirSync(path.resolve(__dirname,'../verification/popout'),{recursive:true});
  await game.screenshot({path:path.resolve(__dirname,'../verification/popout/compact.png')});
  await game.bringToFront();
  await game.keyboard.down('j');
  await game.waitForFunction(()=>sent.some(item=>item.payload.mask===1));
  const count=await page.evaluate(()=>sent.length);
  await page.waitForTimeout(450);
  assert.equal(await page.evaluate(()=>sent.length),count,'main input timer cannot clear detached input');
  await game.keyboard.up('j');
  await page.evaluate(()=>saveSettings({language:'zh-CN',shader:'lcd3x'}));
  await game.waitForFunction(()=>document.documentElement.lang==='zh-CN'&&settings.shader==='lcd3x');
  await page.evaluate(()=>window.deliverFrame(0,255));
  await Promise.all([game.waitForEvent('close'),page.click('#popout')]);
  await page.waitForFunction(()=>!document.body.classList.contains('game-detached'));
  await settle();
  assert.equal(await page.locator('#game-viewport').isVisible(),true);
  assert.ok(Math.abs(await headingGap()-initialGap)<1,'restored status row keeps the original library spacing');
  assert.ok(Math.abs((await page.locator('#game-viewport').boundingBox()).height-size.height)<1,'restore keeps main screen size');
  await page.waitForFunction(()=>{
   const canvas=document.getElementById('game-frame'),gl=canvas.getContext('webgl2'),pixels=new Uint8Array(12);
   gl.readPixels(Math.floor(canvas.width/2),Math.floor(canvas.height/2),3,1,gl.RGBA,gl.UNSIGNED_BYTE,pixels);
   return [1,5,9].some(index=>pixels[index]>0);
  },null,{timeout:5000});
  const collapsedMetrics=()=>page.evaluate(()=>{
   const box=selector=>document.querySelector(selector).getBoundingClientRect(),main=box('main'),style=getComputedStyle(document.querySelector('main')),library=box('#library-view'),heading=box('.player-heading'),controls=box('.player-controls'),screen=box('#game-viewport');
   return {topGap:heading.top-library.bottom-parseFloat(getComputedStyle(document.getElementById('library-view')).marginBottom),bottomGap:main.bottom-parseFloat(style.paddingBottom)-controls.bottom,screen:[screen.x,screen.y,screen.width,screen.height],heading:heading.top,controls:controls.top,frameHidden:getComputedStyle(document.getElementById('game-frame')).visibility==='hidden'};
  });
  for(const collapseFirst of [true,false]){
   if(collapseFirst){await page.click('#library-toggle');await settle();}
   const before=collapseFirst?await collapsedMetrics():null;
   await page.click('#popout');await page.waitForFunction(()=>document.body.classList.contains('game-detached'));game=child.page;
   await game.waitForFunction(()=>window.ReaGBAPopout.ownsGame());
   if(!collapseFirst)await page.click('#library-toggle');
   await settle();
   const detached=await collapsedMetrics();
   assert.ok(Math.abs(detached.topGap)<1&&Math.abs(detached.bottomGap)<1,'collapsed information and controls anchor to opposite edges');
   assert.ok(detached.frameHidden,'detached placeholder does not repeat the game image');
   assert.ok(await page.locator('#game-viewport').isVisible(),'collapsed layout retains the empty screen');
   if(before)assert.deepEqual(detached.screen,before.screen,'detaching does not move the collapsed screen');
   await page.screenshot({path:path.resolve(__dirname,'../verification/popout/collapsed-detached.png')});
   await Promise.all([game.waitForEvent('close'),page.click('#popout')]);await settle();
   const restored=await collapsedMetrics();
   assert.equal(restored.frameHidden,false,'restored frame becomes visible in the placeholder');
   assert.deepEqual(restored.screen,detached.screen,'returning the game keeps the placeholder bounds');
   assert.equal(restored.heading,detached.heading);assert.equal(restored.controls,detached.controls);
   await page.click('#library-toggle');await settle();
  }
  game=await detach();await game.close();
  await page.waitForFunction(()=>!document.body.classList.contains('game-detached'));
  game=await detach();await Promise.all([game.waitForEvent('close'),page.click('#popout')]);
  await page.evaluate(()=>{window.failOpen=true;});await page.click('#popout');
  assert.equal(await page.locator('#game-viewport').isVisible(),true,'open failure preserves main display');
  await page.evaluate(()=>{window.failOpen=false;});game=await detach();
  await Promise.all([game.waitForEvent('close'),page.reload()]);
  await page.waitForFunction(()=>document.body.classList.contains('playing'));
  assert.equal(await page.locator('#game-viewport').isVisible(),true,'parent reload restores the main page');
  game=await detach();await Promise.all([game.waitForEvent('close'),page.close()]);
  assert.deepEqual(errors,[]);
  console.log('PASS: independent page, exclusive input, shared preferences, return, native close, repeated open, failed open, parent reload/close');
 }finally{await browser.close();server.close();}
})().catch(error=>{console.error(error);server.close();process.exitCode=1;});
