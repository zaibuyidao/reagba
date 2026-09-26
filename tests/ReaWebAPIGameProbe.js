(async()=>{
 const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));
 const wait=async test=>{const deadline=Date.now()+12000;while(!await test()){if(Date.now()>deadline)throw Error('Game window timeout');await sleep(30);}};
 try{
  await wait(()=>window.ReaGBAPopout.ownsGame()&&document.getElementById('game-frame'));
  const screen=document.getElementById('game-viewport').getBoundingClientRect();
  if([screen.left,innerWidth-screen.right,innerHeight-screen.bottom,screen.top].some(gap=>Math.abs(gap-3)>.1))throw Error('Game window spacing is not 3px');
  await wait(async()=>state.game?.title&&(await reaper.window.getState()).title==='ReaGBA - '+state.game.title);
  await call('get_settings');
  await call('focus_game');await wait(()=>document.hasFocus());
  const service=reaper.host.service('reagba');
  await service.invoke('resume');
  document.dispatchEvent(new KeyboardEvent('keydown',{code:'KeyJ',key:'j',bubbles:true}));
  await sleep(450);
  if(!((await service.invoke('getState')).input_mask&1))throw Error('Detached key was cleared');
  document.dispatchEvent(new KeyboardEvent('keyup',{code:'KeyJ',key:'j',bubbles:true}));
  await wait(async()=>(await service.invoke('getState')).input_mask===0);
  await service.invoke('pause');
  const canvas=document.getElementById('game-frame'),gl=canvas.getContext('webgl2');
  if(!gl||gl.getError()!==gl.NO_ERROR)throw Error('Detached renderer failed');
  localStorage.setItem('reagba:test:popout',JSON.stringify({ok:true}));
  reaper.window.close().catch(()=>{});
 }catch(error){localStorage.setItem('reagba:test:popout',JSON.stringify({ok:false,error:String(error)}));}
})();
