const assert = require('node:assert/strict');
const path = require('node:path');
const fs = require('node:fs');
const { pathToFileURL } = require('node:url');
const { chromium } = require(require.resolve('playwright', {
    paths: [process.env.REAGBA_NODE_MODULES || path.resolve(__dirname, '../node_modules')],
}));
const entry = pathToFileURL(path.resolve(__dirname, '../ui/index.html')).href;
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
            window.nativeRequest = async cmd => {
                window.settingsRequests.push(cmd);
                let result = true;
                if (cmd.action === 'get_settings') result = prefs;
                if (cmd.action === 'set_settings') {
                    prefs = {...prefs, ...cmd.settings};
                    localStorage.setItem('settings-test', JSON.stringify(prefs));
                    result = prefs;
                }
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
        const defaults = ['J','K','Space','Return','D','A','W','S','Q','O','L'];
        assert.deepEqual(await page.locator('#keys button').allTextContents(),defaults);
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
        assert.equal(await page.locator('#reset-keys + h3').textContent(), 'BIOS（可选）');
        for (const shader of ['lcd3x','lcd-grid-v2','none','lcd-grid-v2']) {
            await page.selectOption('#shader', shader);
            await page.waitForFunction(value => JSON.parse(localStorage.getItem('settings-test')).shader === value, shader);
            assert.equal(await page.locator('#filter').isDisabled(), shader !== 'none');
        }
        await page.reload();
        await page.waitForFunction(() => document.getElementById('about-version').textContent === 'test');
        await page.click('#settings-toggle');
        assert.equal(await page.locator('#shader').inputValue(), 'lcd-grid-v2', 'preset restored on reopen');
        assert.equal(await page.locator('#filter').isDisabled(), true);
        const custom = ['F','G','Left Shift','Return','Right','Left','Up','Down','U','I','P'];
        const presses = ['f','g','ShiftLeft','Enter','ArrowRight','ArrowLeft','ArrowUp','ArrowDown','u','i','p'];
        for (let i=0;i<custom.length;i++) {
            await page.locator('#keys button').nth(i).click();await page.keyboard.press(presses[i]);
            await page.waitForFunction(({i,key})=>document.querySelectorAll('#keys button')[i].textContent===key,{i,key:custom[i]});
        }
        await page.click('#settings-toggle');
        await page.click('#toggle');
        const customHint = 'Up / Down / Left / Right = ↑ / ↓ / ← / → · F / G = A / B · U / I = R / L · Return = Start · Left Shift = Select · 按住 P 加速';
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
        await page.waitForFunction(()=>document.getElementById('toast').textContent.includes('Q / O = R / L')&&document.getElementById('toast').textContent.endsWith('按住 L 加速'));
        assert.deepEqual(errors, [], 'UI loads directly as three source files, without JS/CSP errors');
        fs.writeFileSync(path.join(output,'report.json'),JSON.stringify({passed:true,sizes:results},null,2));
        console.log('PASS: settings at 10 sizes, shader selection/persistence, no horizontal overflow or script errors');
    } finally { await browser.close(); }
})().catch(error => {console.error(error); process.exitCode=1;});
