'use strict';
const $=id=>document.getElementById(id);
const i18n=window.ReaGBAI18n,t=(key,values)=>i18n.t(key,values);
const labels=['A','B','Select','Start','→','←','↑','↓','R','L'];
let settings={},toastTimer;
function toast(message,error=false){$('toast').textContent=message;$('toast').className='visible'+(error?' error':'');clearTimeout(toastTimer);toastTimer=setTimeout(()=>$('toast').className='',3500);}
async function call(action,values={}){if(!window.nativeRequest)throw Error(t('openInReaper'));const result=await window.nativeRequest({action,...values});if(!result.ok)throw Error(result.error?i18n.error(result.error):t('failed'));return result.result;}
function run(fn){return async(...args)=>{try{await fn(...args);}catch(error){toast(error.message,true);}};}
function make(tag,cls,text){const element=document.createElement(tag);if(cls)element.className=cls;if(text!==undefined)element.textContent=text;return element;}
async function saveSettings(values){settings=await call('set_settings',{settings:values});}
const gamepadSources=['a','b','x','y','back','guide','start','leftstick','rightstick','leftshoulder','rightshoulder','dpup','dpdown','dpleft','dpright','touchpad','leftup','leftdown','leftleft','leftright','rightup','rightdown','rightleft','rightright','lefttrigger','righttrigger'];
const gamepadTargets=['a','b','select','start','right','left','up','down','r','l'];
const gamepadDefaultTargets=['a','b','none','none','select','none','start','none','none','l','r','up','down','left','right','none','up','down','left','right','none','none','none','none','none','none'];
function defaultGamepadBindings(){return Object.fromEntries(gamepadSources.map((source,i)=>[source,{target:gamepadDefaultTargets[i],mode:'hold'}]));}
let gamepadSaving=false,capture=null,captureTimer=null;
function sourceLabel(source){
 if(source.startsWith('button:'))return t('gamepadButton',{code:source.slice(7)});
 if(source.startsWith('axis:')){const [,code,direction]=source.split(':');return t('gamepadAxis',{code,direction});}
 if(source.startsWith('hat:')){const [,code,direction]=source.split(':');return t('gamepadHat',{code,direction:{1:'↑',2:'→',4:'↓',8:'←'}[direction]});}
 return t(`pad_${source}`);
}
function allBindings(){return {...defaultGamepadBindings(),...settings.gamepad_bindings};}
function renderGamepad(){
 const bindings=allBindings();$('gamepad-bindings').replaceChildren();
 for(const target of gamepadTargets){
  const row=make('div','gamepad-action'),name=labels[gamepadTargets.indexOf(target)],entries=Object.entries(bindings).filter(([,value])=>value.target===target);
  const listening=capture?.target===target,caption=listening?t('pressKey'):entries.map(([source])=>sourceLabel(source)).join(' / ')||t('gamepadBind');
  const entry=make('div','gamepad-binding'),button=make('button','gamepad-source',caption);
  button.disabled=gamepadSaving||!!capture;button.dataset.target=target;button.setAttribute('aria-label',name+' · '+caption);button.onclick=run(()=>startCapture(target));
  button.classList.toggle('listening',listening);entry.append(button);
  if(['a','b','l','r'].includes(target)){
   const label=make('label','gamepad-turbo'),turbo=make('input','');
   turbo.type='checkbox';turbo.dataset.target=target;turbo.checked=entries.some(([,value])=>value.mode==='turbo');
   turbo.indeterminate=turbo.checked&&entries.some(([,value])=>value.mode!=='turbo');turbo.disabled=gamepadSaving||!!capture||!entries.length;
   turbo.setAttribute('aria-label',name+' · '+t('gamepad_turbo'));
   turbo.onchange=run(async()=>{const next=allBindings();for(const [source] of entries)next[source]={...next[source],mode:turbo.checked?'turbo':'hold'};await saveGamepad(next);});
   label.append(turbo,make('span','',t('gamepad_turbo')));entry.append(label);
  }else entry.classList.add('gamepad-plain');
  row.append(make('span','gamepad-target',name),entry);$('gamepad-bindings').append(row);
 }
 $('reset-gamepad').disabled=gamepadSaving||!!capture;
 $('cancel-gamepad-capture').hidden=!capture;
}
async function saveGamepad(bindings){
 if(gamepadSaving)return;
 gamepadSaving=true;renderGamepad();
 try{
  await saveSettings({gamepad_bindings:bindings});
  if(!Object.entries(bindings).every(([source,binding])=>['target','mode'].every(field=>settings.gamepad_bindings?.[source]?.[field]===binding[field])))throw Error(t('gamepadSaveFailed'));
 }finally{gamepadSaving=false;renderGamepad();}
}
function cancelCapture(){clearTimeout(captureTimer);capture=null;$('gamepad-status').textContent='';renderGamepad();}
async function startCapture(target){
 cancelCapture();
 capture={target,instance:null,previous:new Set(),expires:Date.now()+15000};renderGamepad();
 await pollCapture(capture);
}
async function pollCapture(current){
 try{
  const input=await call('get_gamepad_input');
  if(capture!==current)return;
  if(Date.now()>current.expires){cancelCapture();toast(t('gamepadCaptureTimeout'));return;}
  if(!input.connected){current.instance=null;$('gamepad-status').textContent=t('gamepadDisconnected');}
  else{
   $('gamepad-status').textContent='';
   const held=new Set(input.inputs);
   if(current.instance===input.instance){
    const source=input.inputs.find(code=>!current.previous.has(code));
    if(source){
     const next=allBindings(),entries=Object.entries(next).filter(([,value])=>value.target===current.target),mode=entries.some(([,value])=>value.mode==='turbo')?'turbo':'hold';
     for(const [previous] of entries)next[previous]={target:'none',mode:'hold'};
     for(const alias of input.aliases?.[source]||[])next[alias]={target:'none',mode:'hold'};
     next[source]={target:current.target,mode};cancelCapture();await saveGamepad(next).catch(error=>toast(error.message,true));return;
    }
   }
   current.instance=input.instance;current.previous=held;
  }
  captureTimer=setTimeout(()=>pollCapture(current),50);
 }catch(error){if(capture!==current)return;cancelCapture();toast(error.message,true);}
}
$('cancel-gamepad-capture').onclick=cancelCapture;
document.addEventListener('keydown',event=>{if(event.key==='Escape'&&capture){event.preventDefault();cancelCapture();}});
window.addEventListener('blur',()=>{if(capture)cancelCapture();});
window.addEventListener('pagehide',()=>{capture=null;clearTimeout(captureTimer);});
$('reset-gamepad').onclick=run(()=>saveGamepad(defaultGamepadBindings()));
$('close-gamepad-settings').onclick=run(()=>call('close_gamepad_settings'));
initReaGBABridge();
run(async()=>{
 settings=await call('get_settings');i18n.set(settings.language);document.documentElement.lang=i18n.language;i18n.apply();document.title='ReaGBA - '+t('gamepadTitle');
 renderGamepad();$('gamepad-bindings').hidden=false;
})();
