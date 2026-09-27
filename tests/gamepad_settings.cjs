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
  const state={loaded:false,running:false,speed:1,base_speed:1,volume:.3,app_version:'0.1.6'};
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
  await page.waitForFunction(()=>document.getElementById('about-version').textContent==='0.1.6');
  await page.click('#settings-toggle');
  let gamepad=page,childId=main.id;
  const show=async()=>{await page.reload();await page.waitForFunction(()=>document.querySelectorAll('#gamepad-bindings .gamepad-action').length===13);await page.click('#settings-toggle');return page;};
  const pad=target=>gamepad.locator(`.gamepad-source[data-target="${target}"]`);
  const settled=()=>gamepad.waitForFunction(()=>!document.getElementById('reset-gamepad').disabled);
  const bind=async(target,code,aliases={})=>{
   controller={connected:true,instance:9,inputs:[],aliases:{}};
   await gamepad.locator(`.gamepad-source[data-target="${target}"]`).click();
   assert.equal(await gamepad.locator(`.gamepad-source[data-target="${target}"]`).textContent(),await gamepad.evaluate(()=>t('pressKey')));
   await gamepad.waitForFunction(()=>capture?.instance===9);
   controller={connected:true,instance:9,inputs:[code],aliases};
   await settled();
  };
  assert.equal(await gamepad.locator('.gamepad-target').count(),13);
  assert.equal(await gamepad.locator('.gamepad-source').count(),13,'one binding button per GBA action');
  assert.equal(await gamepad.locator('#gamepad-bindings button').count(),13,'no add or remove buttons');
  assert.equal(await gamepad.locator('#gamepad-bindings select').count(),0,'no preset input or trigger mode dropdown');
  assert.equal(await gamepad.locator('#gamepad-bindings input[type=checkbox]').count(),0,'no Turbo checkboxes');
  assert.deepEqual((await gamepad.locator('.gamepad-target').allTextContents()).slice(0,4),['A','B','A Turbo','B Turbo']);
  await bind('a','button:40',{'button:40':['x']});
  await bind('b','button:41',{'button:41':['y']});
  await bind('a_turbo','button:45');
  await bind('b_turbo','button:46');
  assert.deepEqual(prefs.gamepad_bindings['button:40'],{target:'a',mode:'hold'});
  assert.deepEqual(prefs.gamepad_bindings['button:41'],{target:'b',mode:'hold'});
  assert.deepEqual(prefs.gamepad_bindings['button:45'],{target:'a',mode:'turbo'});
  assert.deepEqual(prefs.gamepad_bindings['button:46'],{target:'b',mode:'turbo'});
  await bind('a_turbo','button:47');
  assert.equal(prefs.gamepad_bindings['button:45'].target,'none');
  assert.equal(prefs.gamepad_bindings['button:40'].target,'a','Turbo rebinding preserves ordinary A');
  await bind('l','axis:3:-');
  await bind('fast_forward','button:44');
  assert.equal(prefs.gamepad_bindings['button:44'].target,'fast_forward');
  assert.equal(prefs.gamepad_bindings['button:40'].target,'a');
  assert.equal(prefs.gamepad_bindings.a.target,'none');
  await gamepad.keyboard.press('j');await gamepad.waitForTimeout(300);
  assert.ok(requests.filter(r=>r.id===childId&&r.method==='input').every(r=>!r.payload.active),'settings blocks gameplay input');
  gamepad=await show();
  assert.equal(await pad('a').textContent(),'Button 40');assert.equal(await pad('b').textContent(),'Button 41');
  assert.equal(await pad('a_turbo').textContent(),'Button 47');assert.equal(await pad('b_turbo').textContent(),'Button 46');
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
   await bind('a_turbo','button:48');
   assert.equal(await pad('a_turbo').textContent(),'Button 47');
   assert.equal(await pad('a').textContent(),'Button 40');
   assert.ok(await gamepad.locator('#toast').evaluate(e=>e.classList.contains('error')));
  }
  failSave=ignoreSave=false;
  await gamepad.click('#reset-gamepad');await settled();
  assert.equal(await pad('a').textContent(),'A');assert.equal(prefs.gamepad_bindings['button:40'],undefined);
  assert.equal(await pad('a_turbo').textContent(),await gamepad.evaluate(()=>t('gamepadBind')));
  // Existing target/mode configurations populate the independent Turbo row unchanged.
  prefs.gamepad_bindings['button:49']={target:'a',mode:'turbo'};
  await show();assert.equal(await pad('a_turbo').textContent(),'Button 49');assert.equal(await pad('a').textContent(),'A');
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
  assert.equal(prefs.gamepad_bindings['button:49'].mode,'turbo','normal rebinding preserves Turbo');
  assert.equal(await pad('a_turbo').textContent(),'Button 49');
  const output=path.resolve(__dirname,'../verification/gamepad-settings');fs.mkdirSync(output,{recursive:true});
  for(const language of ['en','zh-CN','zh-TW','ja','ko','es','de','fr']){
   prefs.language=language;await show();await settled();
   await gamepad.waitForFunction(language=>document.documentElement.lang===language,language);
   for(const width of [240,320,760]){
    await gamepad.setViewportSize({width,height:700});
    const fits=await gamepad.evaluate(()=>document.body.scrollWidth<=innerWidth&&document.getElementById('settings-view').scrollWidth<=document.getElementById('settings-view').clientWidth);
    assert.ok(fits,language+' '+width+' no horizontal overflow');
    if(width===760){const rows=await gamepad.locator('#gamepad-bindings .gamepad-action').evaluateAll(rows=>rows.slice(0,4).map(row=>({x:row.offsetLeft,y:row.offsetTop})));assert.equal(rows[0].y,rows[1].y);assert.equal(rows[2].y,rows[3].y);assert.ok(rows[2].y>rows[0].y);assert.equal(rows[0].x,rows[2].x);assert.equal(rows[1].x,rows[3].x);await gamepad.locator('#gamepad-bindings').scrollIntoViewIfNeeded();assert.equal(await gamepad.locator('#gamepad-bindings').evaluate(e=>getComputedStyle(e).gridTemplateColumns.split(' ').length),2,await gamepad.locator('#gamepad-bindings').evaluate(e=>JSON.stringify({grid:getComputedStyle(e).gridTemplateColumns,display:getComputedStyle(e).display,rect:e.getBoundingClientRect()})));}
    if(language==='zh-CN'&&width!==240){await gamepad.locator('#gamepad-bindings').scrollIntoViewIfNeeded();await gamepad.screenshot({path:path.join(output,`gamepad-${width}.png`)});}
   }
  }
  controller={connected:true,instance:9,inputs:[],aliases:{}};
  await gamepad.locator('.gamepad-source[data-target=a]').click();
  await gamepad.waitForFunction(()=>capture?.instance===9);
  await page.click('#settings-toggle');
  assert.equal(await gamepad.evaluate(()=>capture),null,'closing settings cancels capture');
  state.loaded=true;state.game={title:'Shortcut test',hash:'shortcut',path:'test.gba',code:'TEST'};
  await page.waitForFunction(()=>state.loaded);
  for(let slot=1;slot<=9;slot++)for(const [modifier,method] of [['Control','save_state'],['Shift','load_state']]){
   const before=requests.filter(r=>r.method===method).length;
   await page.keyboard.press(modifier+'+Digit'+slot);
   await page.waitForTimeout(80);
   await page.waitForFunction(slot=>document.querySelector('#slots button[aria-pressed=true]').textContent===String(slot),slot);
   assert.equal(requests.filter(r=>r.method===method).length,before+1,'one shortcut request');
   assert.equal(requests.filter(r=>r.method===method).at(-1).payload.slot,slot);
  }
  assert.equal(opens,0,'gamepad settings never opens a window');
  assert.deepEqual(errors,[]);
  console.log('PASS: inline settings and fast-forward, raw button/axis/hat capture, alias replacement, held-input guard, cancellation, independent normal/Turbo bindings, legacy preferences, persistence/reset, errors, 8 languages, layout, capture cleanup and all 18 save/load shortcuts');
 }finally{await browser.close();await new Promise(resolve=>server.close(resolve));}
})().catch(error=>{console.error(error);process.exitCode=1;});
