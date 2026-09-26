const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {pathToFileURL} = require('node:url');
const {chromium} = require(require.resolve('playwright', {paths:[process.env.REAGBA_NODE_MODULES || path.resolve(__dirname,'../node_modules')]}));
const output=path.resolve(__dirname,'../verification/library');
const cached=path.resolve(__dirname,'../verification/covers-online/BZME.png');
const image=fs.existsSync(cached)?'data:image/png;base64,'+fs.readFileSync(cached).toString('base64'):
    'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII=';
(async()=>{
 const browser=await chromium.launch({headless:true,ignoreDefaultArgs:['--hide-scrollbars'],
  ...(process.env.REAGBA_BROWSER_CHANNEL?{channel:process.env.REAGBA_BROWSER_CHANNEL}:{}),
  env:Object.fromEntries(Object.entries(process.env).map(([k,v])=>[k.toUpperCase(),v]))});
 try{
  const page=await browser.newPage({viewport:{width:760,height:1200}}),errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.addInitScript(({image})=>{
   let prefs=JSON.parse(localStorage.getItem('library-test')||'{"library_view":"details","auto_download_covers":false}');
   const attempts={};window.requests=[];
   const list=Array.from({length:18},(_,i)=>({path:'fixture-'+i+'.gba',code:['BZME','ABCE','HOME','BAD0'][i%4],
    title:i===0?'The Legend of Zelda - The Minish Cap':i===1?'ABC Adventure':i===2?'自制游戏 · Homebrew':'游戏库演示 '+String(i).padStart(2,'0')+' · 长游戏名称显示测试',
    size:16777216,play_seconds:i*300,last_played:i,favorite:false}));
   const state={loaded:false,running:false,speed:1,base_speed:1,volume:.7,app_version:'test',core:'mGBA'};
   window.nativeRequest=async cmd=>{
    window.requests.push(cmd);let result=true;
    if(cmd.action==='get_settings')result=prefs;
    if(cmd.action==='set_settings'){prefs={...prefs,...cmd.settings};localStorage.setItem('library-test',JSON.stringify(prefs));result=prefs;}
    if(cmd.action==='scan_roms')result=list;
    if(cmd.action==='get_emulator_state')result=state;
    if(cmd.action==='load_rom')result={...state,loaded:true,game:list.find(g=>g.path===cmd.path)};
    if(cmd.action==='get_save_states')result=[];
    if(cmd.action==='favorite')list.find(g=>g.path===cmd.path).favorite=cmd.value;
    if(cmd.action==='get_cover'){
     const code=cmd.code;attempts[code]=(attempts[code]||0)+1;
     result=code==='BZME'?{status:'ready',image}:!prefs.auto_download_covers?{status:'disabled'}:
      code==='ABCE'?(attempts[code]===2?{status:'pending'}:{status:'ready',image}):
      code==='BAD0'?{status:'ready',image:'data:image/png;base64,YmFk'}:{status:'missing'};
    }
    return {ok:true,result:structuredClone(result)};
   };
  },{image});
  const settle=()=>page.evaluate(()=>new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r))));
  await page.goto(pathToFileURL(require('./ui_path.cjs')('index.html')).href);
  await page.waitForFunction(()=>document.getElementById('about-version').textContent==='test'&&!coversLoading);
  assert.equal(await page.locator('#auto-covers').isChecked(),false);
  await page.waitForFunction(()=>document.querySelector('.cover[data-code="BZME"]').classList.contains('has-image'));
  assert.equal(await page.locator('.cover[data-code="ABCE"]').first().textContent(),'AADVANCE');
  fs.mkdirSync(output,{recursive:true});
  for(const width of [240,298,440,760,1280]){
   await page.setViewportSize({width,height:1200});
   await page.evaluate(()=>setLibraryExpanded(true));
   for(const mode of ['details','grid','compact']){
    await page.selectOption('#library-display',mode);
    await page.waitForFunction(mode=>document.getElementById('games').dataset.view===mode,mode);await settle();
    const metrics=await page.evaluate(()=>{
     const list=document.getElementById('games'),card=list.firstElementChild,cover=card.querySelector('.cover'),image=list.querySelector('.cover[data-code="BZME"] img');
     return {width:innerWidth,pageWidth:document.body.scrollWidth,listWidth:list.clientWidth,listScroll:list.scrollWidth,
      cardHeight:card.getBoundingClientRect().height,coverWidth:cover.getBoundingClientRect().width,
      coverHeight:cover.getBoundingClientRect().height,listHeight:list.clientHeight,
      fit:getComputedStyle(image).objectFit,setting:document.getElementById('library-display-setting').value};
    });
    assert.ok(metrics.pageWidth<=width+1,'page overflow '+JSON.stringify(metrics));
    assert.ok(metrics.listScroll<=metrics.listWidth+1,'list overflow '+JSON.stringify(metrics));
    assert.equal(metrics.setting,mode);assert.equal(metrics.fit,'contain');
    if(mode==='compact')assert.ok(metrics.cardHeight<=48,'compact rows are too tall');
    if(mode==='grid'){
     assert.ok(metrics.cardHeight>=metrics.coverWidth+95,'grid content overlaps '+JSON.stringify(metrics));
     assert.ok(Math.abs(metrics.coverHeight-metrics.coverWidth)<1,'grid cover must be square');
     assert.ok(metrics.listHeight>=metrics.cardHeight,'grid must show one complete row by default');
    }
    if(width===760)await page.screenshot({path:path.join(output,mode+'.png')});
   }
  }
  await page.reload();await page.waitForFunction(()=>document.getElementById('about-version').textContent==='test');
  assert.equal(await page.locator('#library-display').inputValue(),'compact','view survives reopen');
  for(const mode of ['grid','details','compact']){
   await page.selectOption('#library-display',mode);
   await page.waitForFunction(mode=>JSON.parse(localStorage.getItem('library-test')).library_view===mode,mode);
   await page.reload();await page.waitForFunction(()=>document.getElementById('about-version').textContent==='test');
   assert.equal(await page.locator('#library-display').inputValue(),mode,'each view survives reopening');
  }
  await page.click('#settings-toggle');await page.check('#auto-covers');
  await page.waitForFunction(()=>!coversLoading&&coverCache.get('ABCE')?.status==='ready');
  assert.equal(await page.locator('#auto-covers').isChecked(),true);
  await page.click('#settings-toggle');
  await page.waitForFunction(()=>document.querySelector('.cover[data-code="ABCE"]').classList.contains('has-image'));
  assert.equal(await page.locator('.cover[data-code="HOME"]').first().textContent(),'HADVANCE');
  await page.waitForFunction(()=>coverCache.get('BAD0')?.status==='error');
  assert.equal(await page.locator('.cover[data-code="BAD0"] img').count(),0,'broken images retain letter fallback');
  await page.click('#settings-toggle');await page.uncheck('#auto-covers');
  await page.waitForFunction(()=>!coversLoading);await page.click('#settings-toggle');
  assert.equal(await page.locator('.cover[data-code="ABCE"] img').count(),5,'downloaded covers survive disabling');
  // Each view retains search, favorites, selected card and a single keyboard launch.
  for(const mode of ['details','grid','compact']){
   await page.selectOption('#library-display',mode);await page.fill('#search','ABC Adventure');
   assert.equal(await page.locator('.game').count(),1);
   const button=page.locator('.game .favorite');await button.focus();await page.keyboard.press('Enter');
   await page.waitForFunction(()=>requests.filter(r=>r.action==='favorite').length>0);
   const launches=await page.evaluate(()=>requests.filter(r=>r.action==='load_rom').length);
   await page.locator('.game').focus();await page.keyboard.press('Enter');
   await page.waitForFunction(n=>requests.filter(r=>r.action==='load_rom').length===n+1,launches);
   assert.equal(await page.locator('.game.selected').count(),1);
   await page.fill('#search','');
  }
  await page.click('.tab[data-filter="favorite"]');assert.equal(await page.locator('.game').count(),1);
  await page.click('.tab[data-filter="all"]');await page.selectOption('#sort','time');
  assert.match(await page.locator('.game h2').first().textContent(),/17/);
  assert.deepEqual(errors,[]);
  console.log('PASS: 3 views at 5 widths, offline and downloaded covers, broken-image fallback, persistence, search, favorite and keyboard launch');
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
