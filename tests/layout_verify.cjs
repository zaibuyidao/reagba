const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const moduleRoot = process.env.REAGBA_NODE_MODULES || rootModules();
function rootModules(){return path.resolve(__dirname,'..','node_modules');}
const { chromium } = require(require.resolve('playwright', { paths: [moduleRoot] }));
const root = path.resolve(__dirname, '..');
const system = process.env.REAGBA_TEST_SYSTEM || 'GBA';
assert.ok(['GBA','GB','GBC'].includes(system));
const aspect = system === 'GBA' ? 1.5 : 160/144;
const output = path.join(root, 'verification/responsive'+(system==='GBA'?'':'-'+system.toLowerCase()));
fs.mkdirSync(output, { recursive: true });
const mock = `
window.preferences={keys:['J','K','Space','Return','D','A','W','S','E','Q'],fast_forward_key:'R',layout:'horizontal',rom_directory:'C:/ROM',...JSON.parse(localStorage.getItem('reagba-test-settings')||'{}')};
window.requests=[];
window.fixture={loaded:true,running:true,fps:59.7,speed:1,base_speed:1,volume:.7,app_version:'0.0.2',core:'mGBA 0.10.5',game:{system:'${system}',hash:'fixture',code:'TEST',title:'${system} 布局测试',path:'C:/ROM/fixture.${system.toLowerCase()}'},reaper:true,docked:true};
window.nativeRequest=async cmd=>{requests.push(cmd);let result=true;
if(cmd.action==='get_settings')result=preferences;
if(cmd.action==='set_settings'){result=preferences={...preferences,...cmd.settings};localStorage.setItem('reagba-test-settings',JSON.stringify(preferences));}
if(cmd.action==='select_rom_directory'){result=preferences={...preferences,rom_directory:'D:/GBA Games',last_rom_directory:'D:/GBA Games'};localStorage.setItem('reagba-test-settings',JSON.stringify(preferences));}
if(cmd.action==='get_emulator_state')result=fixture;
if(cmd.action==='load_rom')result=fixture;
if(cmd.action==='scan_roms')result=Array.from({length:18},(_,i)=>({...fixture.game,path:'fixture-'+i+'.gba',title:i?'游戏库测试 '+String(i+1).padStart(2,'0'):fixture.game.title,size:16777216,play_seconds:0,last_played:0,favorite:false}));
if(cmd.action==='get_save_states')result=[];
return {ok:true,result};};
`;
const entry = 'file:///'+require('./ui_path.cjs')('index.html').replaceAll('\\','/');
(async()=>{
    const env=Object.fromEntries(Object.entries(process.env).map(([key,value])=>[key.toUpperCase(),value]));
    const browser=await chromium.launch({headless:true,...(process.env.REAGBA_BROWSER_CHANNEL?{channel:process.env.REAGBA_BROWSER_CHANNEL}:{}),env});
    try {
        const page=await browser.newPage();
        await page.addInitScript({content:mock});
        const settle=()=>page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
        const errors=[];
        page.on('pageerror', e=>errors.push(e.message));
        const results=[];
        for(const [width,height] of [[298,1299],[320,500],[400,600],[440,900],[760,1200],[1280,540],[1600,900],[1920,400]]) {
            await page.setViewportSize({width,height});
            await page.goto(entry);
            await page.waitForFunction(()=>document.body.classList.contains('playing'));
            await page.evaluate(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))));
            const metrics=await page.evaluate(()=>layoutMetrics());
            assert.ok(metrics.game.height>=63.99 && metrics.game.width>=64*aspect-.01,JSON.stringify(metrics));
            assert.ok(metrics.save.bottom<=height-24,'save button visible '+JSON.stringify(metrics));
            assert.ok(metrics.load.bottom<=height-24,'load button visible');
            if(metrics.libraryExpanded)assert.ok(Math.abs(metrics.load.bottom-metrics.mainBottom)<22,'expanded library uses remaining height');
            assert.ok(Math.abs(metrics.game.width/metrics.game.height-aspect)<.01,'game canvas keeps native aspect ratio');
            assert.ok(metrics.scrollHeight<=metrics.clientHeight+2,'normal dock size needs no page scrolling');
            assert.ok(await page.evaluate(()=>document.body.scrollWidth<=innerWidth+1),'no horizontal page overflow');
            const orientation=await page.evaluate(()=>{const g=document.getElementById('game-viewport').getBoundingClientRect(),p=document.querySelector('.player-controls').getBoundingClientRect();return {gameRight:g.right,gameBottom:g.bottom,controlsX:p.x,controlsY:p.y};});
            if(metrics.mode==='horizontal')assert.ok(orientation.gameRight<=orientation.controlsX);
            else assert.ok(orientation.gameBottom<=orientation.controlsY);
            await page.screenshot({path:path.join(output,`layout-${width}x${height}.png`)});
            if(width===298){
                const visible=await page.evaluate(()=>{const list=document.getElementById('games').getBoundingClientRect();return [...document.querySelectorAll('.game')].filter(card=>{const r=card.getBoundingClientRect();return r.top>=list.top&&r.bottom<=list.bottom;}).length;});
                assert.ok(visible>=4,'tall narrow Docker shows at least four complete detail cards');
                metrics.fullyVisibleCards=visible;
                assert.ok(metrics.game.height<300/aspect,'narrow game background is compact');
            }
            results.push(metrics);
        }
        await page.setViewportSize({width:760,height:900});
        await settle();
        if(!await page.locator('#library-body').isVisible())await page.click('#library-toggle');
        await settle();
        const full=await page.evaluate(()=>layoutMetrics());
        await page.setViewportSize({width:760,height:430});await settle();
        const short=await page.evaluate(()=>layoutMetrics());
        assert.ok(Math.abs(short.game.height-full.game.height)<1,'height-only resize preserves game size');
        assert.ok(short.libraryExpanded,'height-only resize keeps expanded library');
        assert.ok(short.scrollHeight>short.clientHeight,'short windows scroll instead of shrinking the game');
        await page.setViewportSize({width:760,height:900});await settle();
        assert.equal(await page.locator('#layout').count(),0,'horizontal layout selector removed');
        await page.click('#settings-toggle');
        assert.equal(await page.locator('#game-viewport').isVisible(),false,'settings hides native viewport');
        await page.click('#choose-rom-directory');
        assert.equal(await page.locator('#rom-directory').inputValue(),'D:/GBA Games','ROM folder chooser saves and displays the selected path');
        await page.click('#settings-toggle');
        await settle();
        await page.click('#library-toggle');
        await settle();
        assert.equal(await page.locator('#library-body').isVisible(),false,'library can be collapsed');
        const centered=await page.evaluate(()=>{
            const main=document.querySelector('main'),p=document.getElementById('player').getBoundingClientRect(),l=document.getElementById('library-view').getBoundingClientRect(),s=getComputedStyle(main);
            return {top:p.top-l.bottom-parseFloat(getComputedStyle(document.getElementById('library-view')).marginBottom),bottom:main.getBoundingClientRect().bottom-parseFloat(s.paddingBottom)-p.bottom,height:document.getElementById('game-viewport').getBoundingClientRect().height};
        });
        assert.ok(Math.abs(centered.top-centered.bottom)<2,'collapsed player is vertically centered in the remaining space');
        assert.ok(Math.abs(centered.height-full.game.height)<1,'collapse preserves the game size');
        await page.waitForFunction(()=>preferences.library_expanded===false);
        await page.reload();await page.waitForFunction(()=>document.body.classList.contains('playing'));await settle();
        assert.equal(await page.locator('#library-body').isVisible(),false,'collapsed library survives reopening');
        await page.click('#library-toggle');
        await settle();
        assert.equal(await page.locator('#library-body').isVisible(),true,'library can be reopened');
        await page.waitForFunction(()=>preferences.library_expanded===true);
        await page.reload();await page.waitForFunction(()=>document.body.classList.contains('playing'));await settle();
        assert.equal(await page.locator('#library-body').isVisible(),true,'expanded library survives reopening');
        // Mouse capture must keep resizing when the pointer leaves the divider.
        await page.setViewportSize({width:298,height:1299});
        await settle();
        const before=await page.evaluate(()=>layoutMetrics());
        const drag=async delta=>{
            const b=await page.locator('#library-splitter').boundingBox();
            await page.mouse.move(b.x+b.width/2,b.y+b.height/2);await page.mouse.down();
            await page.mouse.move(b.x+b.width/2,b.y+b.height/2+delta,{steps:12});await page.mouse.up();await settle();
            return page.evaluate(()=>layoutMetrics());
        };
        const dragged=await drag(70);
        assert.ok(dragged.library.height>before.library.height+60,'drag gives space to library');
        assert.ok(dragged.game.height<before.game.height-60,'drag shrinks screen proportionally');
        const saved=await page.evaluate(()=>({...preferences}));
        assert.ok(saved.library_split>0,'split persisted through native settings bridge');
        const zoomed=await drag(-1200);
        assert.ok(zoomed.game.height<=zoomed.game.width/aspect+.1,'drag cannot stretch black background');
        const smallest=await drag(1200);
        assert.ok(smallest.game.height>=63.99,'drag leaves a usable minimum game area');
        assert.ok(smallest.load.bottom<1299-24,'drag never clips save controls');
        await page.goto(entry);
        await page.waitForFunction(()=>document.body.classList.contains('playing'));await settle();
        const reopened=await page.evaluate(()=>layoutMetrics());
        assert.ok(Math.abs(reopened.library.height-smallest.library.height)<1,'last saved split restored on reopening');
        await page.locator('.launch').first().click();await settle();
        assert.ok(await page.locator('#library-body').isVisible(),'playing a game preserves expanded library');
        await page.fill('#search','no matching game');await settle();
        assert.ok(await page.locator('#empty').isVisible(),'empty search remains usable');
        await page.fill('#search','');
        await page.locator('#library-splitter').focus();await page.keyboard.press('Home');await settle();
        const minimum=await page.evaluate(()=>layoutMetrics());
        assert.ok(minimum.library.height<reopened.library.height-60,'keyboard can resize separator');
        await page.keyboard.press('End');await settle();
        assert.equal(await page.evaluate(()=>requests.filter(r=>r.action==='keyboard_context').at(-1).blocked),true,'splitter keyboard does not reach game');
        await page.locator('#library-splitter').dblclick();await settle();
        assert.equal(await page.evaluate(()=>preferences.library_split),null,'double click restores automatic size');
        for(const next of ['GB','GBC','GBA']){
            await page.evaluate(next=>{fixture.game.system=next;onNativeState({...fixture,game:{...fixture.game}});},next);await settle();
            const switched=await page.evaluate(()=>layoutMetrics());
            assert.ok(Math.abs(switched.game.width/switched.game.height-(next==='GBA'?1.5:160/144))<.01,'switching system restores native aspect ratio');
        }
        assert.deepEqual(errors,[],'no JavaScript errors');
        fs.writeFileSync(path.join(output,'report.json'),JSON.stringify({passed:true,vertical_only:true,settings_and_library:true,splitter:{mouse:true,keyboard:true,persist_and_reopen:true,bounds:true,double_click_reset:true,before,dragged,reopened},cases:results},null,2));
        console.log(JSON.stringify({passed:true,cases:results.map(r=>({size:r.window,mode:r.mode,bottomGap:r.mainBottom-r.load.bottom}))},null,2));
    } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
