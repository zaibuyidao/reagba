const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const source=fs.readFileSync(require('./ui_path.cjs')('bridge.js'),'utf8')+'\ninitReaGBABridge();';
function fixture(child=false){
 const sent=[],requests=[],events={},errors=[],timers=[];let focus=0,frames=0;
 const service={send:(method,payload)=>sent.push({method,payload}),invoke:(method,payload)=>method==='attach'?(sent.push({method,payload}),Promise.resolve(true)):method==='getState'?Promise.resolve({loaded:false}):new Promise((resolve,reject)=>requests.push({method,payload,resolve,reject}))};
 const runtime={lifecycle:{ready:Promise.resolve({windowId:1}),on:async(name,fn)=>events['lifecycle:'+name]=fn},host:{service:name=>{assert.equal(name,'reagba');return service;}},
  stream:{open:async name=>({info:{width:240,height:160},on:(event,fn)=>events['stream:'+event]=fn,close:async()=>sent.push({method:'detach'})})},
  system:{schedule:async(fn,options)=>{const timer={fn,options,stopped:false};timers.push(timer);return async()=>{timer.stopped=true;};}},
  window:{setIconVisible:async()=>{},isDocked:async()=>false,setDocked:async value=>value,focus:async()=>{focus++;}},
  events:{on:async(name,callback)=>{events[name]=callback;}},dialog:{openFile:async()=>null,selectFolder:async()=>null}};
 const window={reaper:runtime,addEventListener:(name,fn)=>events[name]=fn};
 const document={body:{classList:{contains:name=>child&&name==='game-window'}},hidden:false,hasFocus:()=>true,addEventListener:(name,fn)=>events['dom:'+name]=fn,getElementById:()=>({focus(){}})};
 const context={window,document,settings:{},defaultKeys:['J','K','Space','Return','D','A','W','S','Q','O'],state:{loaded:false},editing:()=>false,
  toast:message=>errors.push(message),createGameVideo:()=>({frame:()=>{frames++;},draw(){}}),queueMicrotask,requestAnimationFrame(){}};
 vm.runInNewContext(source,context);
 return {context,window,sent,requests,timers,events,runtime,errors,get frames(){return frames;},get focus(){return focus;}};
}
const flush=async()=>{for(let i=0;i<30;i++)await Promise.resolve();};
(async()=>{
 const f=fixture();await flush();assert.equal(f.requests.length,0);
 assert.equal(f.sent.find(v=>v.method==='attach').payload.owner,true);
 const child=fixture(true);await flush();assert.equal(child.sent.find(v=>v.method==='attach').payload.owner,false);
 const first=f.window.nativeRequest({action:'pause'}),second=f.window.nativeRequest({action:'reset'});await flush();
 assert.equal(f.requests.length,2);f.requests[1].resolve({loaded:true,title:'中文'});assert.equal((await second).result.title,'中文');
 f.requests[0].reject(Error('Invalid ROM'));assert.equal((await first).error,'Invalid ROM');
 let status;f.window.onNativeState=value=>status=value;await f.timers.find(t=>t.options.interval===250).fn();assert.equal(status.reaper,true);
 await f.window.nativeRequest({action:'toggle_dock'});assert.equal(status.docked,true);
 let picker;
 f.runtime.dialog.openFile=async options=>{picker=options;return null;};
 assert.equal((await f.window.nativeRequest({action:'open_rom'})).result,null);
 assert.deepEqual(Array.from(picker.filters[0].extensions),['gba','gb','gbc']);
 const large=f.window.nativeRequest({action:'get_cover',code:'TEST'});await flush();
 const json=JSON.stringify({image:'cover',title:'中文'}),bytes=Buffer.byteLength(json);
 f.requests.at(-1).resolve({__reagbaResult:{token:'1.9',bytes}});await flush();
 assert.equal(f.requests.at(-1).method,'readResult');f.requests.at(-1).resolve({text:json,offset:0,bytes});
 assert.equal((await large).result.title,'中文');
 f.events['stream:data']({data:new Uint8Array(240*160*4)});assert.equal(f.frames,1);assert(!f.sent.some(v=>v.method==='frame:ack'));
 f.events['dom:keydown']({code:'KeyJ',preventDefault(){}});f.events['dom:keyup']({code:'KeyJ',preventDefault(){}});
 assert.equal(f.sent.at(-2).payload.mask,1);assert.equal(f.sent.at(-1).payload.mask,0);
 f.events.blur();assert.equal(f.sent.at(-1).payload.active,false);
 for(const childWindow of [false,true]){
  const shortcuts=fixture(childWindow);await flush();shortcuts.context.state.loaded=true;
  shortcuts.context.i18n={number:String,error:value=>value};shortcuts.context.t=key=>key;
  shortcuts.context.settings.turbo_keys=['H','V'];
  shortcuts.events['dom:keydown']({code:'KeyH',preventDefault(){}});assert.equal(shortcuts.sent.at(-1).payload.turbo,1);assert.equal(shortcuts.sent.at(-1).payload.mask,0);
  shortcuts.events['dom:keydown']({code:'KeyV',preventDefault(){}});assert.equal(shortcuts.sent.at(-1).payload.turbo,3);
  shortcuts.events['dom:keydown']({code:'KeyJ',preventDefault(){}});assert.equal(shortcuts.sent.at(-1).payload.mask,1);assert.equal(shortcuts.sent.at(-1).payload.turbo,3);
  shortcuts.events['dom:keyup']({code:'KeyH',preventDefault(){}});assert.equal(shortcuts.sent.at(-1).payload.turbo,2);
  shortcuts.events.blur();assert.equal(shortcuts.sent.at(-1).payload.turbo,0);assert.equal(shortcuts.sent.at(-1).payload.mask,0);
  const selected=[];shortcuts.window.onStateSlot=async slot=>selected.push(slot);
  for(let slot=1;slot<=9;slot++)for(const save of [true,false]){
   let prevented=false;
   const event={code:'Digit'+slot,ctrlKey:save,shiftKey:!save,preventDefault(){prevented=true;}};
   shortcuts.events['dom:keydown'](event);await flush();assert(prevented);
   const cmd=shortcuts.requests.at(-1);assert.equal(cmd.method,save?'save_state':'load_state');assert.equal(cmd.payload.slot,slot);
   const count=shortcuts.requests.length;shortcuts.events['dom:keydown']({...event,repeat:true});await flush();assert.equal(shortcuts.requests.length,count);
   cmd.resolve(save?true:{loaded:true});await flush();assert.equal(selected.at(-1),slot);
  }
  const count=shortcuts.requests.length;
  for(const modifiers of [{ctrlKey:true,shiftKey:true},{ctrlKey:true,altKey:true},{shiftKey:true,metaKey:true}])
   shortcuts.events['dom:keydown']({code:'Digit1',...modifiers,preventDefault(){}});
  shortcuts.context.editing=()=>true;shortcuts.events['dom:keydown']({code:'Digit1',ctrlKey:true,preventDefault(){}});
  shortcuts.context.editing=()=>false;shortcuts.context.state.loaded=false;shortcuts.events['dom:keydown']({code:'Digit1',ctrlKey:true,preventDefault(){}});
  await flush();assert.equal(shortcuts.requests.length,count,'editing, no game and extra modifiers do not save');
 }
 const prefs=fixture();let saved=null;prefs.runtime.GetResourcePath=async()=>'/resource';
 const displayPath='/resource/Scripts/zaibuyidao Scripts/Modules/ReaGBA/config/ui.json';
 prefs.runtime.fs={stat:async path=>{assert.equal(path,displayPath);return {exists:!!saved};},readFile:async path=>{assert.equal(path,displayPath);return saved;},writeFile:async(path,text)=>{assert.equal(path,displayPath);saved=text;}};await flush();
 const corePreferences={keys:['J'],language:'ja',shader:'lcd3x'};
 const answer=()=>{const cmd=prefs.requests.at(-1);cmd.resolve(corePreferences);return cmd;};
 let getting=prefs.window.nativeRequest({action:'get_settings'});await flush();answer();let combined=(await getting).result;
 assert.equal(combined.language,'ja');assert.equal(combined.shader,'lcd3x');assert.equal(combined.library_view,'details');
 let saving=prefs.window.nativeRequest({action:'set_settings',settings:{language:'zh-CN',shader:'lcd-grid-v2'}});await flush();
 assert.equal(answer().method,'get_settings');combined=(await saving).result;assert.equal(combined.language,'zh-CN');assert.equal(JSON.parse(saved).shader,'lcd-grid-v2');
 await assert.rejects(prefs.window.nativeRequest({action:'set_settings',settings:{shader:'broken'}}),/Unknown shader/);
 getting=prefs.window.nativeRequest({action:'get_settings'});await flush();answer();assert.equal((await getting).result.language,'zh-CN');
 await f.events['lifecycle:cleanup']();f.events.pagehide();await flush();assert(f.timers.every(t=>t.stopped));assert(f.sent.some(v=>v.method==='detach'));
 const count=f.requests.length;assert.equal((await f.window.nativeRequest({action:'start'})).ok,false);assert.equal(f.requests.length,count);
 const sent=f.sent.length;await f.timers.find(t=>t.options.interval===200).fn();assert.equal(f.sent.length,sent,'cleanup disables late input');
 const early=fixture();early.events.pagehide();await flush();assert(!early.sent.some(v=>v.method==='attach'));assert.equal(early.timers.length,0,'closing during initialization cannot restart polling');
 const existing=()=>{};const window={nativeRequest:existing};vm.runInNewContext(source,{window});assert.equal(window.nativeRequest,existing);
 assert.deepEqual(f.errors,[]);console.log('PASS: Native Service commands, errors, state, docking, binary frames, latest input, preferences and cleanup');
})().catch(error=>{console.error(error);process.exitCode=1;});
