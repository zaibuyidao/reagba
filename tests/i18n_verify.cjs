const assert=require('node:assert/strict'),path=require('node:path'),fs=require('node:fs');
const {pathToFileURL}=require('node:url');
const {chromium}=require(require.resolve('playwright',{paths:[process.env.REAGBA_NODE_MODULES||path.resolve(__dirname,'../node_modules')]}));
const entry=pathToFileURL(path.resolve(__dirname,'../ui/index.html')).href;
const output=path.resolve(__dirname,'../verification/i18n');
(async()=>{
 const browser=await chromium.launch({headless:true,ignoreDefaultArgs:['--hide-scrollbars'],
  ...(process.env.REAGBA_BROWSER_CHANNEL?{channel:process.env.REAGBA_BROWSER_CHANNEL}:{}),
  env:Object.fromEntries(Object.entries(process.env).map(([k,v])=>[k.toUpperCase(),v]))});
 try{
  const page=await browser.newPage({locale:'zh-CN'}),errors=[],requests=[];
  let prefs={},rejectLanguage=false,ignoreLanguage=false;
  const game={title:'Zelda <test>',hash:'test',path:'test.gba',code:'TEST',size:16777216,play_seconds:660,last_played:100,favorite:false};
  let state={loaded:true,running:false,fps:59.7,speed:1,base_speed:1,volume:.3,frame_skip:0,app_version:'i18n-test',core:'mGBA',reaper:true,docked:false,game};
  page.on('pageerror',e=>errors.push(e.message));page.on('console',m=>{if(m.type()==='error')errors.push(m.text());});
  await page.exposeFunction('hostRequest',async cmd=>{
   requests.push(cmd);let result=true;
   if(cmd.action==='get_settings')result=prefs;
   if(cmd.action==='set_settings'){
    if('language' in cmd.settings&&rejectLanguage)return {ok:false,error:'Write failed: preferences.json'};
    if(!('language' in cmd.settings&&ignoreLanguage))prefs={...prefs,...cmd.settings};
    result=prefs;
   }
   if(cmd.action==='get_emulator_state')result=state;
   if(cmd.action==='scan_roms')result=[game];
   if(cmd.action==='get_save_states')result=[{slot:4,exists:true,metadata:{timestamp:1700000000}}];
   if(cmd.action==='get_cover')result={status:'missing'};
   if(cmd.action==='load_rom'||cmd.action==='open_rom')result=state;
   if(cmd.action==='select_rom_directory')result=null;
   if(cmd.action==='screenshot')return {ok:false,error:'Invalid GBA ROM header'};
   if(cmd.action==='pause'||cmd.action==='start')result=state={...state,running:cmd.action==='start'};
   return {ok:true,result};
  });
  await page.addInitScript(()=>{
   // The real Linux host disables localStorage; persistence must use the bridge.
   Object.defineProperty(window,'localStorage',{get(){throw Error('localStorage unavailable');}});
   window.nativeRequest=cmd=>window.hostRequest(cmd);
  });
  const settle=()=>page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
  const ready=async()=>{await page.waitForFunction(()=>document.getElementById('about-version').textContent==='i18n-test');await settle();};
  await page.goto(entry);await ready();
  assert.equal(await page.locator('html').getAttribute('lang'),'en','English default despite Chinese browser locale');
  assert.equal(prefs.language,undefined,'Opening does not overwrite a preference');
  const languages=await page.evaluate(()=>Object.keys(window.ReaGBAI18n.catalogs));
  fs.mkdirSync(output,{recursive:true});
  const layout=[];
  for(const language of languages){
   await page.setViewportSize({width:760,height:900});await settle();
   await page.locator('#slots button').nth(3).click();
   await page.fill('#search','Zelda');
   await page.click('#settings-toggle');await page.fill('#bios','unsaved BIOS path');
   const before=requests.filter(c=>['load_rom','open_rom','start','pause','stop','reset'].includes(c.action)).length;
   await page.selectOption('#language',language);
   await page.waitForFunction(lang=>document.documentElement.lang===lang&&!document.getElementById('language').disabled,language);
   assert.equal(prefs.language,language,'Native settings received language');
   assert.equal(await page.locator('#bios').inputValue(),'unsaved BIOS path','Unsubmitted input preserved');
   assert.equal(requests.filter(c=>['load_rom','open_rom','start','pause','stop','reset'].includes(c.action)).length,before,'Language switch does not change emulation');
   assert.equal(await page.locator('#search').inputValue(),'Zelda');
   assert.equal(await page.locator('#slots .selected').textContent(),'4');
   for(const [width,height] of [[240,500],[298,1299],[320,600],[440,900],[760,900],[1280,540]]){
    await page.setViewportSize({width,height});await settle();
    const check=async view=>{
     const m=await page.evaluate(()=>{const el=document.getElementById(document.body.classList.contains('settings-open')?'settings-view':'library-view');return {scroll:el.scrollWidth,client:el.clientWidth,body:document.body.scrollWidth,width:innerWidth};});
     assert.ok(m.scroll<=m.client+1&&m.body<=m.width+1,`${language} ${view} ${width}: ${JSON.stringify(m)}`);
     layout.push({language,view,width,...m});
    };
    await check('settings');
    if(width===320){await page.locator('#settings-view').evaluate(e=>e.scrollTop=0);await page.screenshot({path:path.join(output,language+'-settings.png')});}
    await page.click('#settings-toggle');await settle();await check('library');
    // A valid locale reaches static text, tooltips, dynamic status and save slots.
    const matching=await page.evaluate(()=>{const t=window.ReaGBAI18n.t;return document.getElementById('play-status').textContent===t('paused')&&document.getElementById('save').textContent===t('save')&&document.getElementById('refresh').title===t('rescan')&&document.getElementById('search').placeholder===t('searchPlaceholder')&&document.getElementById('settings-toggle').title===t('settings')&&document.getElementById('save-hint').textContent===t('slotSaved',{slot:'4'});});
    assert.ok(matching,language+' dynamic labels');
    if(width===320)await page.screenshot({path:path.join(output,language+'-library.png')});
    await page.click('#settings-toggle');await settle();
   }
   await page.setViewportSize({width:298,height:1299});
   for(const mode of ['details','grid','compact']){
    await page.selectOption('#library-display-setting',mode);await page.click('#settings-toggle');await settle();
    const card=await page.evaluate(()=>{
     const list=document.getElementById('games'),card=list.firstElementChild,play=card.querySelector('.launch').getBoundingClientRect(),info=card.querySelector('.game-info').getBoundingClientRect();
     return {width:list.clientWidth,scroll:list.scrollWidth,overlap:Math.min(play.right,info.right)>Math.max(play.left,info.left)&&Math.min(play.bottom,info.bottom)>Math.max(play.top,info.top)};
    });
    assert.ok(card.scroll<=card.width+1&&!card.overlap,language+' '+mode+' card labels must not overlap');
    if(mode==='details')await page.screenshot({path:path.join(output,language+'-details.png')});
    await page.click('#settings-toggle');
   }
   await page.selectOption('#library-display-setting','details');
   await page.reload();await ready();
   assert.equal(await page.locator('html').getAttribute('lang'),language,'Language restored without browser storage');
  }
  await page.setViewportSize({width:760,height:900});await page.click('#settings-toggle');
  rejectLanguage=true;await page.selectOption('#language','de');
  await page.waitForFunction(()=>!document.getElementById('language').disabled);
  assert.equal(await page.locator('html').getAttribute('lang'),'fr','Rollback on write failure');
  rejectLanguage=false;ignoreLanguage=true;await page.selectOption('#language','de');
  await page.waitForFunction(()=>!document.getElementById('language').disabled);
  assert.equal(await page.locator('html').getAttribute('lang'),'fr','Old extensions cannot silently claim persistence');
  assert.ok(await page.locator('#toast').evaluate(e=>e.classList.contains('error')));
  ignoreLanguage=false;
  await page.selectOption('#language','ja');await page.waitForFunction(()=>document.documentElement.lang==='ja'&&!document.getElementById('language').disabled);
  await page.click('#choose-rom-directory');assert.equal(requests.at(-1).dialog_title,'ReaGBA：ROMフォルダーを選択');
  await page.click('#settings-toggle');await page.click('#shot');
  await page.waitForFunction(()=>document.getElementById('toast').textContent===window.ReaGBAI18n.t('errorROM'));
  await page.click('#open');assert.ok(requests.some(c=>c.action==='open_rom'&&c.dialog_title==='ReaGBA：GBA ROMを開く'));
  for(const language of ['not-installed','__proto__',42,null]){
   prefs={...prefs,language};await page.reload();await ready();
   assert.equal(await page.locator('html').getAttribute('lang'),'en','Invalid/absent catalog uses English');
   assert.equal(prefs.language,language,'Fallback does not overwrite stored language');
  }
  assert.deepEqual(errors,[],'No JavaScript or CSP errors');
  fs.writeFileSync(path.join(output,'report.json'),JSON.stringify({passed:true,languages,layout},null,2));
  console.log('PASS: 8 languages, 6 sizes, live switching, bridge persistence, rollback, dialogs, errors and fallback');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
