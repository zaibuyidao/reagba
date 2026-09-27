const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),http=require('node:http');
const {chromium}=require(require.resolve('playwright',{paths:[process.env.REAGBA_NODE_MODULES||path.resolve(__dirname,'../node_modules')]}));
const server=http.createServer((req,res)=>{
 const name=req.url.slice(1);
 if(!/^[a-z0-9.]+$/.test(name)){res.writeHead(404).end();return;}
 const file=require('./ui_path.cjs')(name);if(!fs.existsSync(file)){res.writeHead(404).end();return;}
 res.setHeader('Content-Type',name.endsWith('.js')?'text/javascript':name.endsWith('.css')?'text/css':'text/html');res.end(fs.readFileSync(file));
});
function runtimeFixture(id){
 let cleanup;
 window.reaper={
  lifecycle:{ready:Promise.resolve({windowId:id}),on:async(name,fn)=>{cleanup=fn;}},
  host:{service:()=>({invoke:(method,payload)=>window.hostRequest(method,payload),send:(method,payload)=>window.hostRequest(method,payload)})},
  GetResourcePath:async()=>'/resource',fs:{stat:async()=>({exists:false}),writeFile:async()=>{}},
  events:{on:async()=>{}},
  system:{schedule:async(fn,{interval})=>{const timer=setInterval(fn,interval);return ()=>clearInterval(timer);}},
  window:{setIconVisible:async()=>{},setDocked:async()=>false,isDocked:async()=>false,focus:async()=>{},
   open:file=>window.openPage(file),close:async()=>{await cleanup?.();return window.closePage();}},
  stream:{open:async()=>{await window.hostRequest('streamOpen');return {info:{width:240,height:160},on(){},close:async()=>{}};}}
 };
}
(async()=>{
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
 const browser=await chromium.launch({headless:true,channel:process.env.REAGBA_BROWSER_CHANNEL,env:Object.fromEntries(Object.entries(process.env).map(([k,v])=>[k.toUpperCase(),v]))});
 try{
  const context=await browser.newContext(),requests=[],errors=[];
  let nextId=1,child,ownerOpen=true,failOpen=false,failSave=false,ignoreSave=false,prefs={language:'en'},opens=0;
  let controller={connected:true,instance:9,inputs:[],aliases:{}};
  const state={loaded:false,running:false,speed:1,base_speed:1,volume:.3,app_version:'0.1.5'};
  async function open(file){
   const page=await context.newPage(),id=nextId++;
   page.on('pageerror',error=>errors.push(error.message));
   await page.addInitScript(runtimeFixture,id);
   await page.exposeFunction('hostRequest',async(method,payload)=>{
    requests.push({id,method,payload});
    if(!ownerOpen)throw Error('ReaGBA window is not attached');
    if(method==='get_settings')return prefs;
    if(method==='get_gamepad_input')return controller;
    if(method==='set_settings'){
     if(failSave)throw Error('Write failed: preferences.json');
     if(!ignoreSave)prefs={...prefs,...payload.settings};
     return prefs;
    }
    if(method==='scan_roms'||method==='get_save_states')return [];
    if(method==='get_audio_outputs')return {};
    return state;
   });
   await page.exposeFunction('openPage',async file=>{if(failOpen)throw Error('Open failed');opens++;child=await open(file);return child.id;});
   await page.exposeFunction('closePage',()=>{setTimeout(()=>page.close().catch(()=>{}),0);return true;});
   await page.goto(`http://127.0.0.1:${server.address().port}/${file}`);
   return {page,id};
  }
  const main=await open('index.html'),page=main.page;
  await page.waitForFunction(()=>document.getElementById('about-version').textContent==='0.1.5');
  await page.click('#settings-toggle');
  assert.equal(await page.locator('#gamepad-bindings').count(),0);
  async function show(){
   await page.click('#open-gamepad-settings');
   await page.waitForFunction(()=>!document.getElementById('open-gamepad-settings').disabled);
   const gamepad=child.page;
   await gamepad.waitForFunction(()=>document.querySelectorAll('#gamepad-bindings .gamepad-action').length===10);
   return gamepad;
  }
  let gamepad=await show(),childId=child.id;
  assert.equal(requests.find(r=>r.id===childId&&r.method==='attach').payload.owner,false);
  assert.equal(requests.filter(r=>r.id===childId&&r.method==='streamOpen').length,0);
  const pad=target=>gamepad.locator(`input[type=checkbox][data-target="${target}"]`);
  const settled=()=>gamepad.waitForFunction(()=>!document.getElementById('reset-gamepad').disabled);
  const bind=async(target,code,aliases={})=>{
   controller={connected:true,instance:9,inputs:[],aliases:{}};
   await gamepad.locator(`.gamepad-source[data-target="${target}"]`).click();
   assert.equal(await gamepad.locator(`.gamepad-source[data-target="${target}"]`).textContent(),await gamepad.evaluate(()=>t('pressKey')));
   await gamepad.waitForFunction(()=>capture?.instance===9);
   controller={connected:true,instance:9,inputs:[code],aliases};
   await settled();
  };
  assert.equal(await gamepad.locator('.gamepad-target').count(),10);
  assert.equal(await gamepad.locator('.gamepad-source').count(),10,'one binding button per GBA action');
  assert.equal(await gamepad.locator('#gamepad-bindings button').count(),10,'no add or remove buttons');
  assert.equal(await gamepad.locator('select').count(),0,'no preset input or trigger mode dropdown');
  for(const source of ['select','start','up','down','left','right'])
   assert.equal(await pad(source).count(),0,'non-action bindings have no Turbo control');
  await bind('a','button:40',{'button:40':['x']});
  assert.equal(await pad('a').isChecked(),false,'new binding uses standard behavior');
  await pad('a').check();await settled();
  await bind('b','button:41',{'button:41':['y']});
  await pad('b').check();await settled();
  await pad('b').uncheck();await settled();
  assert.equal(prefs.gamepad_bindings['button:41'].mode,'hold','turning Turbo off restores standard behavior');
  await pad('b').check();await settled();
  await bind('l','axis:3:-');
  assert.equal(prefs.gamepad_bindings['button:40'].target,'a');
  assert.equal(prefs.gamepad_bindings.a.target,'none');
  await gamepad.keyboard.press('j');await gamepad.waitForTimeout(300);
  assert.equal(requests.filter(r=>r.id===childId&&r.method==='input').length,0,'settings never sends game input');
  await Promise.all([gamepad.waitForEvent('close'),gamepad.click('#close-gamepad-settings')]);
  assert.ok(!page.isClosed(),'closing settings keeps main window');
  gamepad=await show();
  assert.equal(await pad('a').isChecked(),true);assert.equal(await pad('b').isChecked(),true);
  assert.equal(await gamepad.locator('.gamepad-source[data-target=l]').textContent(),'Axis 3 -');
  // Inputs already held when capture starts must be released before they can bind.
  controller={connected:true,instance:9,inputs:['button:42'],aliases:{}};
  await gamepad.locator('.gamepad-source[data-target=a]').click();
  await gamepad.waitForFunction(()=>capture?.instance===9);
  await gamepad.waitForTimeout(150);assert.equal(await gamepad.locator('#cancel-gamepad-capture').isVisible(),true);
  await gamepad.keyboard.press('Escape');await settled();
  assert.equal(prefs.gamepad_bindings['button:42'],undefined);
  assert.equal(await gamepad.locator('.gamepad-source[data-target=a]').textContent(),'Button 40','cancel restores original caption');
  controller={connected:false,inputs:[],aliases:{}};
  await gamepad.locator('.gamepad-source[data-target=a]').click();
  await gamepad.waitForFunction(()=>document.getElementById('gamepad-status').textContent===t('gamepadDisconnected'));
  await gamepad.click('#cancel-gamepad-capture');await settled();
  for(const failure of ['reject','ignore']){
   failSave=failure==='reject';ignoreSave=failure==='ignore';
   await pad('a').click();await settled();
   assert.equal(await pad('a').isChecked(),true);
   assert.ok(await gamepad.locator('#toast').evaluate(e=>e.classList.contains('error')));
  }
  failSave=ignoreSave=false;
  await gamepad.click('#reset-gamepad');await settled();
  assert.equal(await pad('a').isChecked(),false);assert.equal(prefs.gamepad_bindings['button:40'],undefined);
  await bind('b','button:0',{'button:0':['a']});
  assert.equal(prefs.gamepad_bindings.a.target,'none','raw rebinding removes default alias to avoid dual actions');
  assert.equal(prefs.gamepad_bindings['button:0'].target,'b');
  await bind('up','hat:0:1',{'hat:0:1':['dpup']});
  assert.equal(prefs.gamepad_bindings['hat:0:1'].target,'up');
  assert.equal(prefs.gamepad_bindings.dpup.target,'none');
  assert.equal(prefs.gamepad_bindings.leftup.target,'none','capture replaces every previous input for the target');
  await bind('a','button:42');
  assert.equal(prefs.gamepad_bindings['button:42'].target,'a','unbound target can be assigned directly');
  await bind('a','button:43');
  assert.equal(prefs.gamepad_bindings['button:42'].target,'none','rebinding replaces the previous raw input');
  assert.equal(await pad('up').count(),0,'raw direction binding uses standard behavior');
  const output=path.resolve(__dirname,'../verification/gamepad-window');fs.mkdirSync(output,{recursive:true});
  for(const language of ['en','zh-CN','zh-TW','ja','ko','es','de','fr']){
   prefs.language=language;await gamepad.reload();await settled();
   await gamepad.waitForFunction(language=>document.documentElement.lang===language,language);
   for(const width of [240,320,760]){
    await gamepad.setViewportSize({width,height:700});
    const fits=await gamepad.evaluate(()=>document.body.scrollWidth<=innerWidth&&document.querySelector('main').scrollWidth<=document.querySelector('main').clientWidth);
    assert.ok(fits,language+' '+width+' no horizontal overflow');
    if(language==='zh-CN'&&width!==240)await gamepad.screenshot({path:path.join(output,`gamepad-${width}.png`)});
   }
  }
  await gamepad.close();failOpen=true;
  const before=opens;await page.click('#open-gamepad-settings');
  await page.waitForFunction(()=>!document.getElementById('open-gamepad-settings').disabled&&document.getElementById('toast').classList.contains('error'));
  assert.equal(opens,before);failOpen=false;
  gamepad=await show();await page.close();ownerOpen=false;
  await gamepad.waitForEvent('close');
  assert.deepEqual(errors,[]);
  console.log('PASS: independent settings, raw button/axis/hat capture, alias replacement, held-input guard, cancellation, modes/persistence/reset, errors, 8 languages, layout and lifecycle');
 }finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
})().catch(error=>{console.error(error);process.exitCode=1;});
