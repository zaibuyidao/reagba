const assert=require('node:assert/strict'),vm=require('node:vm'),fs=require('node:fs');
const source=fs.readFileSync(require('./ui_path.cjs')('video.js'),'utf8');
let decoded;
const context={window:{devicePixelRatio:1},Uint8Array,settings:{vsync:false,integer_scaling:true,filter:'nearest'},toast:()=>{},atob:text=>Buffer.from(text,'base64').toString('latin1'),
 ImageData:class{constructor(data,width,height){this.data=data;this.width=width;this.height=height;}},
 document:{getElementById:()=>({clientWidth:240,clientHeight:160,append(){}}),createElement:()=>({setAttribute(){},addEventListener(){},getContext:type=>type==='webgl2'?null:{putImageData:image=>{decoded=image.data;},fillRect(){},drawImage(){}}})}};
vm.createContext(context);vm.runInContext(source.slice(source.indexOf('function createGameVideo(){')),context);
const renderer=vm.runInContext('createGameVideo()',context);
const pixels=Buffer.alloc(240*160*4),runs=[];
for(let y=0;y<160;y++){
 const color=[y,255-y,y%13,255];runs.push(240,0,...color);
 for(let x=0;x<240;x++)for(let c=0;c<4;c++)pixels[(y*240+x)*4+c]=color[c];
}
renderer.frame({width:240,height:160,data:new Uint8Array(pixels)});assert.deepEqual(Buffer.from(decoded),pixels);
for(const data of [new Uint8Array(10),new Uint8Array(240*160*4+4),'base64'])assert.throws(()=>renderer.frame({width:240,height:160,data}),/GBA frame/);
console.log('PASS: binary frame view, exact RGBA pixels and invalid frame bounds');
