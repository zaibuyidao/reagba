const assert = require('node:assert/strict');
const path = require('node:path');
const fs = require('node:fs');
const { pathToFileURL } = require('node:url');
const { chromium } = require(require.resolve('playwright', {
    paths: [process.env.REAGBA_NODE_MODULES || path.resolve(__dirname, '../node_modules')],
}));
const entry = pathToFileURL(require('./ui_path.cjs')('index.html')).href;
const output = path.resolve(__dirname, '../verification/settings');

(async () => {
    // Keep real scrollbars enabled: Playwright hides them by default.
    const browser = await chromium.launch({headless: true, ignoreDefaultArgs: ['--hide-scrollbars'],
        ...(process.env.REAGBA_BROWSER_CHANNEL ? {channel: process.env.REAGBA_BROWSER_CHANNEL} : {}),
        env: Object.fromEntries(Object.entries(process.env).map(([k,v]) => [k.toUpperCase(),v]))});
    try {
        const page = await browser.newPage();
        const errors = [];
        page.on('pageerror', error => errors.push(error.message));
        page.on('console', msg => { if (msg.type() === 'error') errors.push(msg.text()); });
        await page.addInitScript(() => {
            let prefs = JSON.parse(localStorage.getItem('settings-test') || '{}');
            const game = {hash:'settings',code:'TEST',title:'Settings test',path:'test.gba',size:1024,play_seconds:0};
            const state = {loaded:false,running:false,fps:59.7,speed:1,base_speed:1,volume:.3,frame_skip:0,core:'mGBA',app_version:'test'};
            window.settingsRequests = [];
            window.audioTestChannels = ['Speakers L','Speakers R','Headphones L','Headphones R'];
            window.audioTestRevision = 1;
            window.nativeRequest = async cmd => {
                window.settingsRequests.push(cmd);
                let result = true;
                if (cmd.action === 'get_settings') result = prefs;
                if (cmd.action === 'set_settings') {
                    if(cmd.settings.gamepad_bindings&&window.rejectGamepad)return {ok:false,error:'Write failed: preferences.json'};
                    if(cmd.settings.gamepad_bindings&&window.ignoreGamepad)return {ok:true,result:prefs};
                    prefs = {...prefs, ...cmd.settings};
                    localStorage.setItem('settings-test', JSON.stringify(prefs));
                    result = prefs;
                }
                if (cmd.action === 'get_audio_outputs') result = {
                    reaper_available:true,reaper_device:{IDENT_OUT:'Test audio interface',SRATE:'48000',running:true,channels:window.audioTestChannels},
                    status:{audio_output:prefs.audio_output||'system',audio_track:prefs.audio_track||'preview',audio_channel:prefs.audio_channel||0,audio_mono:prefs.audio_mono===true,audio_error:'',audio_outputs_revision:window.audioTestRevision}};
                if (cmd.action === 'get_emulator_state') result = state;
                if (cmd.action === 'load_rom' || cmd.action === 'open_rom') result = {...state,loaded:true,running:true,game};
                if (cmd.action === 'scan_roms') result = [game];
                if (cmd.action === 'get_save_states') result = [];
                return {ok:true,result};
            };
        });
        await page.goto(entry);
        await page.waitForFunction(() => document.getElementById('about-version').textContent === 'test');
        await page.click('#settings-toggle');
        assert.equal(await page.locator('#volume').inputValue(), '30');
        assert.equal(await page.locator('#audio-output').inputValue(),'system');
        assert.equal(await page.locator('#audio-track-row').isVisible(),false);
        for(const mode of ['reaper_output','system','reaper_track']){
            await page.selectOption('#audio-output',mode);
            await page.waitForFunction(mode=>JSON.parse(localStorage.getItem('settings-test')).audio_output===mode,mode);
            await page.waitForFunction(()=>!document.getElementById('audio-output').disabled);
            if(mode==='reaper_output'){
                assert.equal(await page.locator('#audio-hardware-row').isVisible(),true);
                assert.equal(await page.locator('#audio-channels option').count(),7);
                await page.selectOption('#audio-channels','2:2');
                await page.waitForFunction(()=>JSON.parse(localStorage.getItem('settings-test')).audio_channel===2&&!document.getElementById('audio-channels').disabled);
                assert.match(await page.locator('#audio-device').textContent(),/Test audio interface.*48000 Hz/);
                await page.evaluate(()=>{window.audioTestChannels=['Speakers L','Speakers R'];window.audioTestRevision++;onNativeState({audio_outputs_revision:window.audioTestRevision,audio_state:'channel_unavailable'});});
                await page.waitForFunction(()=>document.querySelector('#audio-channels option:checked').textContent.includes('Unavailable'));
                assert.equal(await page.locator('#audio-channels').inputValue(),'2:2','missing channels retained');
                await page.evaluate(()=>{window.audioTestChannels=['Speakers L','Speakers R','Headphones L','Headphones R'];window.audioTestRevision++;onNativeState({audio_outputs_revision:window.audioTestRevision});});
                await page.waitForFunction(()=>!document.querySelector('#audio-channels option:checked').textContent.includes('Unavailable'));
                await page.selectOption('#audio-channels','3:1');
                await page.waitForFunction(()=>JSON.parse(localStorage.getItem('settings-test')).audio_mono===true&&!document.getElementById('audio-channels').disabled);
            }else assert.equal(await page.locator('#audio-hardware-row').isVisible(),false);
        }
        assert.equal(await page.locator('#audio-track-row').isVisible(),true);
        await page.selectOption('#audio-track','selected');
        await page.waitForFunction(()=>JSON.parse(localStorage.getItem('settings-test')).audio_track==='selected');
        assert.equal(await page.locator('#gamepad-bindings .key-pair').count(),13,'inline gamepad bindings include separate Turbo actions and fast-forward');
        assert.equal(await page.locator('#gamepad-bindings input[type=checkbox]').count(),0,'Turbo actions have separate binding buttons');
        const defaults = ['J','K','Space','Return','D','A','W','S','Q','O','L'];
        assert.deepEqual(await page.locator('#keys button').allTextContents(),defaults);
        await page.selectOption('#audio-output','reaper_output');
        await page.waitForFunction(()=>!document.getElementById('audio-output').disabled);
        const sizes = [[240,500],[298,1299],[320,500],[400,600],[440,900],[760,1200],
            [1280,540],[1600,900],[1920,400],[1920,2000]];
        const results = [];
        fs.mkdirSync(output, {recursive:true});
        for (const [width,height] of sizes) {
            await page.setViewportSize({width,height});
            const m = await page.evaluate(() => {
                const s = document.getElementById('settings-view');
                return {width:innerWidth,height:innerHeight,clientWidth:s.clientWidth,
                    scrollWidth:s.scrollWidth,overflow:[...s.querySelectorAll('*')].filter(e => {
                        const r=e.getBoundingClientRect(), p=s.getBoundingClientRect();
                        return r.right > p.left+s.clientWidth+.5;
                    }).map(e=>e.id||e.className||e.tagName)};
            });
            results.push(m);
            assert.ok(m.scrollWidth <= m.clientWidth, 'settings horizontal overflow: '+JSON.stringify(m));
            await page.locator('#save-bios').scrollIntoViewIfNeeded();
            assert.ok(await page.locator('#save-bios').isVisible(), 'BIOS controls reachable');
            if (width === 320 || width === 760) {
                await page.locator('#settings-view').evaluate(e => e.scrollTop=0);
                await page.screenshot({path:path.join(output, `settings-${width}.png`)});
            }
        }
        assert.equal(await page.locator('#reset-keys + h3').textContent(), 'BIOS (optional)');
        await page.selectOption('#audio-output','reaper_track');
        await page.waitForFunction(()=>!document.getElementById('audio-output').disabled);
        for (const shader of ['lcd3x','lcd-grid-v2','none','lcd-grid-v2']) {
            await page.selectOption('#shader', shader);
            await page.waitForFunction(value => JSON.parse(localStorage.getItem('settings-test')).shader === value, shader);
            assert.equal(await page.locator('#filter').isDisabled(), shader !== 'none');
        }
        await page.reload();
        await page.waitForFunction(() => document.getElementById('about-version').textContent === 'test');
        await page.click('#settings-toggle');
        assert.equal(await page.locator('#shader').inputValue(), 'lcd-grid-v2', 'preset restored on reopen');
        assert.equal(await page.locator('#audio-output').inputValue(),'reaper_track','audio output restored');
        assert.equal(await page.locator('#audio-track').inputValue(),'selected','target rule restored');
        assert.equal(await page.locator('#audio-channels').inputValue(),'3:1','hardware output restored');
        assert.equal(await page.locator('#filter').isDisabled(), true);
        const custom = ['F','G','Left Shift','Return','Right','Left','Up','Down','U','I','P'];
        const presses = ['f','g','ShiftLeft','Enter','ArrowRight','ArrowLeft','ArrowUp','ArrowDown','u','i','p'];
        for (let i=0;i<custom.length;i++) {
            await page.locator('#keys button').nth(i).click();await page.keyboard.press(presses[i]);
            await page.waitForFunction(({i,key})=>document.querySelectorAll('#keys button')[i].textContent===key,{i,key:custom[i]});
        }
        await page.click('#settings-toggle');
        await page.click('#toggle');
        const customHint = 'Up / Down / Left / Right = ↑ / ↓ / ← / → · F / G = A / B · U / I = R / L · Return = Start · Left Shift = Select · Hold P to fast-forward';
        await page.waitForFunction(hint=>document.getElementById('toast').textContent===hint,customHint);
        await page.reload();await page.waitForFunction(()=>document.getElementById('about-version').textContent==='test');
        await page.click('#open');
        await page.waitForFunction(hint=>document.getElementById('toast').textContent===hint,customHint);
        await page.click('#settings-toggle');
        assert.deepEqual(await page.locator('#keys button').allTextContents(),custom,'custom keys survive reopen');
        await page.click('#reset-keys');
        await page.waitForFunction(()=>document.querySelectorAll('#keys button')[10].textContent==='L');
        assert.deepEqual(await page.locator('#keys button').allTextContents(),defaults);
        await page.click('#settings-toggle');await page.click('#open');
        await page.waitForFunction(()=>document.getElementById('toast').textContent.includes('Q / O = R / L')&&document.getElementById('toast').textContent.endsWith('Hold L to fast-forward'));
        assert.deepEqual(errors, [], 'UI loads directly as four source files, without JS/CSP errors');
        fs.writeFileSync(path.join(output,'report.json'),JSON.stringify({passed:true,sizes:results},null,2));
        console.log('PASS: inline gamepad settings, keyboard/audio/shader regression and settings at 10 sizes');
    } finally { await browser.close(); }
})().catch(error => {console.error(error); process.exitCode=1;});
