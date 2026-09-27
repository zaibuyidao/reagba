"""Verify live audio through an isolated REAPER mixer using an original GBA tone fixture."""
import argparse,json,os,shutil,struct,subprocess,sys,time
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--reaper',type=Path,default=Path('C:/REAPER/reaper.exe'))
p.add_argument('--core',type=Path,default=Path('build/native/bin/Release/reaper_reagba-x64.dll'))
p.add_argument('--probe',type=Path)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();root=a.output.resolve();root.mkdir(parents=True,exist_ok=False)
(root/'UserPlugins').mkdir();(root/'Effects').mkdir();(root/'empty-vst').mkdir()
shutil.copy2(a.core,root/'UserPlugins'/a.core.name)
probe=a.probe or a.core.with_name('reaper_reagba_audio_probe'+a.core.suffix)
shutil.copy2(probe,root/'UserPlugins'/probe.name)
ini='[REAPER]\nshowlastproj=0\nvstfullstate=49989\nverchk=0\nerrnowarn=5\naudiocloseinactive=0\nvstpath='+str(root/'empty-vst')+'\nvstpath64='+str(root/'empty-vst')+'\n'
if os.name=='nt':ini+='[audioconfig]\nmode=4\ndummy_srate=48000\ndummy_blocksize=512\n'
elif sys.platform=='darwin':
 ini+='hasrecentlyopened=1\n[audioconfig]\nmode=4\ndummy_srate=48000\ndummy_blocksize=512\n'
 for binary in (a.core,probe):subprocess.run(['codesign','--force','--sign','-',str(root/'UserPlugins'/binary.name)],check=True)
else:ini+='linux_audio_mode=3\nlinux_audio_srate=48000\nlinux_audio_bsize=512\n'
(root/'reaper.ini').write_text(ini,encoding='utf-8')
(root/'Effects/reagba-test-gain').write_text('desc:ReaGBA Test Gain\nslider1:1<0,1,1>Gain\n@sample\nspl0 *= slider1; spl1 *= slider1;\n')
rom=bytearray(512);struct.pack_into('<I',rom,0,0xea00002e);rom[0xa0:0xac]=b'REAGBA TONE ';rom[0xac:0xb0]=b'TONE';rom[0xb2]=0x96;rom[0xbd]=(-sum(rom[0xa0:0xbd])-0x19)&255
registers=[(0x04000084,0x80),(0x04000080,0x1177),(0x04000082,2),(0x04000060,0),(0x04000062,0xf080),(0x04000064,0x86d6)]
words=[];literal=0xc0+len(registers)*12+4
for i,(address,value) in enumerate(registers):
 pos=0xc0+i*12
 words += [0xe59f0000|(literal+i*8-pos-8),0xe59f1000|(literal+i*8+4-pos-12),0xe1c010b0]
words.append(0xeafffffe)
for address,value in registers:words.extend([address,value])
for i,value in enumerate(words):struct.pack_into('<I',rom,0xc0+i*4,value)
(root/'tone.gba').write_bytes(rom)
shutil.copy2(Path(__file__).with_name('audio_host_verify.lua'),root/'verify.lua')
env=dict(os.environ)
if os.name=='nt':
 env={k.upper():v for k,v in env.items()};env['Path']=env.pop('PATH', '')
startup=None
if os.name=='nt':startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=7
with (root/'process.log').open('w') as log:
 process=subprocess.Popen([str(a.reaper.resolve()),'-newinst','-cfgfile',str(root/'reaper.ini'),'-new','-nosplash',str(root/'verify.lua')],env=env,stdout=log,stderr=log,startupinfo=startup)
 try:
  deadline=time.monotonic()+90
  while process.poll() is None and time.monotonic()<deadline and not (root/'result.json').exists():time.sleep(.2)
  result=json.loads((root/'result.json').read_text()) if (root/'result.json').exists() else {'ok':False,'error':'No report'}
  print(json.dumps(result));assert result['ok'],result
 finally:
  if process.poll() is None:
   process.terminate()
   try:process.wait(timeout=5)
   except subprocess.TimeoutExpired:process.kill();process.wait()
