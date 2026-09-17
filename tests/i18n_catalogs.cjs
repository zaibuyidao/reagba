// Catalog integrity and fallback checks do not require a browser or native host.
const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm'),path=require('node:path');
const root=path.resolve(__dirname,'..'),window={};
vm.runInNewContext(fs.readFileSync(path.join(root,'ui/i18n.js'),'utf8'),{window,Intl});
const i18n=window.ReaGBAI18n,base=i18n.catalogs.en.messages;
const tokens=s=>[...s.matchAll(/\{(\w+)\}/g)].map(m=>m[1]).sort();
assert.deepEqual(Object.keys(i18n.catalogs),['en','zh-CN','zh-TW','ja','ko','es','de','fr']);
assert.equal(i18n.language,'en');
for(const [language,catalog] of Object.entries(i18n.catalogs)){
 assert.deepEqual(Object.keys(catalog.messages).sort(),Object.keys(base).sort(),language+' missing/extra keys');
 i18n.set(language);
 for(const [key,value] of Object.entries(catalog.messages)){
  assert.equal(typeof value,'string',language+': '+key);assert.ok(value.trim(),language+': '+key);
  assert.deepEqual(tokens(value),tokens(base[key]),language+': '+key+' placeholders');
  assert.equal(i18n.t(key),value);
 }
 assert.ok(!i18n.t('keyHint',Object.fromEntries(tokens(base.keyHint).map(k=>[k,'X']))).includes('{'));
 assert.ok(!i18n.error('Invalid GBA ROM header').includes('Invalid GBA ROM header'));
}
for(const language of [null,{},'constructor','__proto__','xx-XX']){i18n.set(language);assert.equal(i18n.language,'en');}
// Future partial catalogs use English until translations are ready.
i18n.catalogs.it={name:'Italiano',messages:{settings:'Impostazioni'}};
i18n.set('it');assert.equal(i18n.t('settings'),'Impostazioni');assert.equal(i18n.t('save'),base.save);
assert.equal(i18n.t('coverAlt',{title:'<img onerror=alert(1)>'}),'Cover for <img onerror=alert(1)>');
i18n.set('de');assert.equal(i18n.number(12.5),'12,5');
const html=fs.readFileSync(path.join(root,'ui/index.html'),'utf8');
for(const match of html.matchAll(/data-i18n(?:-title|-aria-label|-placeholder)?="([^"]+)"/g))assert.ok(Object.hasOwn(base,match[1]),'Unknown DOM key: '+match[1]);
const app=fs.readFileSync(path.join(root,'ui/app.js'),'utf8');
for(const match of app.matchAll(/\bt\('([^']+)'/g))assert.ok(Object.hasOwn(base,match[1]),'Unknown JS key: '+match[1]);
assert.ok(!/[\p{Script=Han}\p{Script=Hiragana}\p{Script=Hangul}]/u.test(app),'Keep translations in the catalog');
console.log(`PASS: ${Object.keys(base).length} messages in 8 languages, placeholders, fallback, formatting and UI keys`);
