// Real REAPER verification of the native service, binary stream and existing UI.
(async()=>{
 const root=await reaper.GetResourcePath(),checks=[],metrics={};
 const fixturePath=root+'/'+await reaper.fs.readText(root+'/fixture-name.txt');
 const sleep=ms=>new Promise(r=>setTimeout(r,ms));
 const wait=async(test,label)=>{const end=Date.now()+12000;while(!await test()){if(Date.now()>end)throw Error('Timeout: '+label);await sleep(30);}checks.push(label);};
 const check=(ok,label)=>{if(!ok)throw Error(label);checks.push(label);};
 const service=reaper.host.service('reagba');
 try{
  await reaper.lifecycle.ready;
  check(await reaper.fs.readText(root+'/launcher-returned.txt')==='returned','Lua launcher has returned');
  await wait(()=>document.getElementById('footer-version').textContent!=='v0.0.0'&&Object.keys(settings).length>0,'UI initialization');
  if(await reaper.GetExtState('ReaGBA.test','phase')==='reopen'){
   check(settings.language==='zh-CN'&&settings.shader==='lcd-grid-v2','UI preferences persist: '+settings.language+' / '+settings.shader);
   const reopened=await service.invoke('getState');
   check(!reopened.loaded&&!reopened.running,'closed main window released the core');
   await service.invoke('loadRom',{path:fixturePath});
   const video=await reaper.stream.open('reagba.video');
   await wait(()=>video.latest(),'fresh frame after window reopen');
   await service.invoke('loadState',{slot:1});await service.invoke('resume');await sleep(200);
   await service.invoke('closeRom');check(!(await service.invoke('getState')).loaded,'close ROM');
   await service.invoke('loadRom',{path:fixturePath});await wait(()=>video.latest().sequence>1n,'reload ROM');
   await service.invoke('reset');await service.invoke('pause');await video.close();
   await reaper.fs.writeText(root+'/result.json',JSON.stringify({ok:true,checks,metrics:JSON.parse(await reaper.GetExtState('ReaGBA.test','metrics'))}));return;
  }
  await call('set_settings',{settings:{rom_directory:root,auto_download_covers:false}});await scan();check(games.length===1,'ROM library');
  await playGame(games[0]);
  const video=await reaper.stream.open('reagba.video');let received=0;video.on('data',()=>received++);
  await wait(()=>video.latest()&&document.getElementById('game-frame'),'native framebuffer');
  check(video.info.width===240&&video.info.height===160&&video.latest().bytes.length===153600,'RGBA frame metadata');
  await sleep(300);const canvas=document.getElementById('game-frame'),gl=canvas.getContext('webgl2');check(gl&&gl.getError()===gl.NO_ERROR,'existing WebGL renderer');
  const system=(await service.invoke('getState')).system,viewport=document.getElementById('game-viewport').getBoundingClientRect();
  check(Math.abs(viewport.width/viewport.height-(system==='GB'||system==='GBC'?160/144:1.5))<.01,'native system viewport ratio');
  check(video.latest().bytes.some((v,i)=>i%4!==3&&v>0),'ROM pixels present');
  await call('focus_game');await wait(()=>document.hasFocus(),'WebView focus');
  document.dispatchEvent(new KeyboardEvent('keydown',{code:'KeyJ',key:'j',bubbles:true}));
  await wait(async()=>((await service.invoke('getState')).input_mask&1)!==0,'native key down');
  document.dispatchEvent(new KeyboardEvent('keyup',{code:'KeyJ',key:'j',bubbles:true}));
  await wait(async()=>(await service.invoke('getState')).input_mask===0,'native key up');
  service.send('input',{mask:1,fast:true,active:true});
  await wait(async()=>(await service.invoke('getState')).speed===4,'atomic fast-forward');
  service.send('input',{mask:0,fast:false,active:false});await wait(async()=>(await service.invoke('getState')).speed===1,'input release');
  await service.invoke('pause');await service.invoke('saveState',{slot:1});await service.invoke('loadState',{slot:1});await call('screenshot');checks.push('pause save load screenshot');
  for(const shader of ['none','lcd3x','lcd-grid-v2']){await saveSettings({shader});await sleep(100);check(gl.getError()===gl.NO_ERROR,'shader '+shader);}
  localStorage.removeItem('reagba:test:popout');
  await call('popout');check(document.body.classList.contains('game-detached')&&document.getElementById('game-viewport').getBoundingClientRect().width>0&&getComputedStyle(canvas).visibility==='hidden','main canvas retained without duplicate game image');
  await wait(()=>localStorage.getItem('reagba:test:popout'),'independent game window report');
  const popout=JSON.parse(localStorage.getItem('reagba:test:popout'));check(popout.ok,'detached renderer and native keyboard: '+JSON.stringify(popout));
  await wait(()=>!document.body.classList.contains('game-detached'),'game restored after child close');
  localStorage.removeItem('reagba:test:popout');
  await saveSettings({language:'zh-CN',library_view:'grid',library_split:.4});
  await call('toggle_dock');check(await reaper.window.isDocked(),'dock');await call('toggle_dock');check(!await reaper.window.isDocked(),'undock');
  await service.invoke('resume');const before=await service.invoke('getState'),start=performance.now(),frames=received;await sleep(3000);
  const after=await service.invoke('getState');metrics.producerFps=(after.frames-before.frames)*1000/(performance.now()-start);metrics.deliveredFps=(received-frames)/3;
  check(metrics.producerFps>52&&metrics.producerFps<67,'~59.73 FPS native emulator');
  const seq=video.latest().sequence,stall=performance.now()+800;while(performance.now()<stall){}await sleep(180);
  check(video.latest().sequence>seq+35n,'WebView stall does not stall emulator');
  await service.invoke('pause');await video.close();
  await reaper.SetExtState('ReaGBA.test','metrics',JSON.stringify({...metrics,checks}),false);
  await reaper.SetExtState('ReaGBA.test','phase','reopen',false);
  await reaper.SetExtState('ReaGBA.test','closingWindow',String((await reaper.lifecycle.ready).windowId),false);
  await reaper.window.close();
 }catch(error){await reaper.fs.writeText(root+'/result.json',JSON.stringify({ok:false,checks,metrics,error:String(error),stack:error.stack,toast:document.getElementById('toast').textContent,diagnostics:await reaper.debug.getDiagnostics()}),{overwrite:true});}
})();
