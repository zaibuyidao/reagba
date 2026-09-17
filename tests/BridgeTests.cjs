// Exercise real native transport without a browser, ROM, or DOM fixture.
const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm'),path=require('node:path');
const source=fs.readFileSync(path.join(__dirname,'../ui/app.js'),'utf8').split('\nconst $=')[0];
function fixture(platform){
 const sent=[],timers=new Map(),events={};let timerId=0;
 const transport={postMessage:m=>sent.push(m),addEventListener:(name,fn)=>events[name]=fn};
 const window=platform==='windows'?{chrome:{webview:transport}}:{webkit:{messageHandlers:{reagba:transport}}};
 const context={window,setTimeout:fn=>{timers.set(++timerId,fn);return timerId;},clearTimeout:id=>timers.delete(id)};
 vm.runInNewContext(source,context);return {window,sent,timers,events,transport};
}
(async()=>{
 for(const platform of ['windows','webkit']){
  const f=fixture(platform),first=f.window.nativeRequest({action:'load_rom',path:'中文.gba'}),second=f.window.nativeRequest({action:'pause'});
  assert.equal(f.sent[0].path,'中文.gba');assert.notEqual(f.sent[0].id,f.sent[1].id);
  const result={type:'reply',id:f.sent[1].id,ok:true,result:{running:false}};
  if(platform==='windows')f.events.message({data:result});else f.window.ReaGBAReceive(result);
  assert.equal((await second).result.running,false);assert.equal(f.timers.size,1);
  let state;f.window.onNativeState=value=>state=value;
  f.window.ReaGBAReceive({type:'state',result:{frames:42}});assert.equal(state.frames,42);
  f.window.ReaGBAReceive({type:'reply',id:f.sent[0].id,ok:false,error:'Invalid ROM'});
  assert.equal((await first).error,'Invalid ROM');assert.equal(f.timers.size,0);
  f.window.ReaGBAReceive({type:'reply',id:999,ok:true});
  const timed=f.window.nativeRequest({action:'start'});const rejected=assert.rejects(timed,/did not respond/);
  [...f.timers.values()][0]();await rejected;
  f.transport.postMessage=()=>{throw Error('bridge unavailable');};
  await assert.rejects(f.window.nativeRequest({action:'pause'}),/bridge unavailable/);
 }
 const existing=()=>{};const window={nativeRequest:existing};vm.runInNewContext(source,{window});assert.equal(window.nativeRequest,existing);
 console.log('Windows/WebKit replies, out-of-order requests, state events, errors and timeouts passed');
})().catch(e=>{console.error(e);process.exitCode=1;});
