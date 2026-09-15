// Cross-backend pixel check: first run gpu_shaders with verification/shaders as
// its output directory. The GLSL is exported by the production C++ generator.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {chromium} = require(require.resolve('playwright', {
    paths:[process.env.REAGBA_NODE_MODULES || path.resolve(__dirname,'../node_modules')],
}));
const root=path.resolve(__dirname,'../verification/shaders');
const glsl = name => fs.readFileSync(path.join(root,name),'utf8').replace('#version 150',
    '#version 300 es\nprecision highp float;\nprecision highp int;');
(async()=>{
    const browser=await chromium.launch({headless:true,
        ...(process.env.REAGBA_BROWSER_CHANNEL?{channel:process.env.REAGBA_BROWSER_CHANNEL}:{}),
        env:Object.fromEntries(Object.entries(process.env).map(([k,v])=>[k.toUpperCase(),v]))});
    try {
        const page=await browser.newPage();
        await page.setContent('<canvas></canvas>');
        await page.evaluate(({vertex,fragment,input})=>{
            const canvas=document.querySelector('canvas');
            const gl=canvas.getContext('webgl2',{antialias:false,alpha:false,preserveDrawingBuffer:true});
            if(!gl)throw new Error('WebGL2 unavailable');
            const compile=(type,source)=>{
                const s=gl.createShader(type);gl.shaderSource(s,source);gl.compileShader(s);
                if(!gl.getShaderParameter(s,gl.COMPILE_STATUS))throw new Error(gl.getShaderInfoLog(s));
                return s;
            };
            const program=gl.createProgram();
            gl.attachShader(program,compile(gl.VERTEX_SHADER,vertex));
            gl.attachShader(program,compile(gl.FRAGMENT_SHADER,fragment));gl.linkProgram(program);
            if(!gl.getProgramParameter(program,gl.LINK_STATUS))throw new Error(gl.getProgramInfoLog(program));
            gl.useProgram(program);
            gl.bindTexture(gl.TEXTURE_2D,gl.createTexture());
            gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA8,240,160,0,gl.RGBA,gl.UNSIGNED_BYTE,new Uint8Array(input));
            for(const name of [gl.TEXTURE_MIN_FILTER,gl.TEXTURE_MAG_FILTER])gl.texParameteri(gl.TEXTURE_2D,name,gl.NEAREST);
            for(const name of [gl.TEXTURE_WRAP_S,gl.TEXTURE_WRAP_T])gl.texParameteri(gl.TEXTURE_2D,name,gl.CLAMP_TO_EDGE);
            gl.uniform1i(gl.getUniformLocation(program,'frame'),0);
            gl.uniform2f(gl.getUniformLocation(program,'sourceSize'),240,160);
            gl.disable(gl.DITHER);
            window.draw=(width,mode)=>{
                const height=Math.floor(width*2/3);
                canvas.width=width;canvas.height=height;gl.viewport(0,0,width,height);
                gl.uniform2f(gl.getUniformLocation(program,'outputSize'),width,height);
                gl.uniform1i(gl.getUniformLocation(program,'shaderPreset'),mode);
                gl.drawArrays(gl.TRIANGLES,0,3);
                const result=new Uint8Array(width*height*4);
                gl.readPixels(0,0,width,height,gl.RGBA,gl.UNSIGNED_BYTE,result);
                if(gl.getError()!==gl.NO_ERROR)throw new Error('GL render error');
                // OpenGL readback starts at bottom, D3D readback at top.
                const topDown=new Uint8Array(result.length);
                for(let y=0;y<height;y++)topDown.set(result.subarray(y*width*4,(y+1)*width*4),(height-y-1)*width*4);
                return Array.from(topDown);
            };
        },{vertex:glsl('vertex.glsl'),fragment:glsl('fragment.glsl'),input:Array.from(fs.readFileSync(path.join(root,'input.rgba')))});
        let maximum=0,cases=0;
        for(const width of [144,240,480,720,960,333])for(const mode of [0,1,2]){
            const gpu=await page.evaluate(([w,m])=>window.draw(w,m),[width,mode]);
            const d3d=fs.readFileSync(path.join(root,`${width}-${mode}.rgba`));
            assert.equal(gpu.length,d3d.length);
            const height=Math.floor(width*2/3);
            for(let i=0;i<gpu.length;i++){
                const pixel=Math.floor(i/4),x=pixel%width,y=Math.floor(pixel/width);
                const sx=(x+.5)*240/width,sy=(y+.5)*160/height;
                if(mode<2&&(Math.abs(sx-Math.round(sx))<1e-6||Math.abs(sy-Math.round(sy))<1e-6))continue;
                const difference=Math.abs(gpu[i]-d3d[i]);maximum=Math.max(maximum,difference);
                assert.ok(difference<=2,`GL/D3D mismatch width=${width} mode=${mode} x=${x} y=${y}: ${gpu[i]} vs ${d3d[i]}`);
            }
            if(width===720)await page.locator('canvas').screenshot({path:path.join(root,`shader-${mode}.png`)});
            cases++;
        }
        console.log(`PASS: GLSL ES compile, ${cases} GL/D3D comparisons; maximum channel difference ${maximum}/255`);
    } finally {await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
