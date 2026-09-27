"""Run the native service/stream UI in an isolated REAPER with a supplied or generated test ROM."""
import argparse,configparser,json,os,shutil,struct,subprocess,sys,time
from pathlib import Path
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reaper',type=Path,default=Path('C:/REAPER/reaper.exe'))
parser.add_argument('--reawebapi',type=Path,required=True)
parser.add_argument('--core',type=Path,default=Path('build/native/bin/Release/reaper_reagba-x64.dll'))
parser.add_argument('--ui',type=Path,default=Path('web'))
parser.add_argument('--rom',type=Path)
parser.add_argument('--audio-config',type=Path)
parser.add_argument('--output',type=Path,required=True)
a=parser.parse_args();root=a.output.resolve();root.mkdir(parents=True,exist_ok=False)
(root/'UserPlugins').mkdir();(root/'empty-vst').mkdir()
ini='[REAPER]\nlastproject=\nshowlastproj=0\nverchk=0\nerrnowarn=5\nvstfullstate=49989\nvstpath='+str(root/'empty-vst')+'\nvstpath64='+str(root/'empty-vst')+'\n'
if sys.platform.startswith('linux'):ini+='linux_audio_mode=3\nlinux_audio_srate=48000\nlinux_audio_bsize=512\naudiocloseinactive=0\n'
if os.name=='nt':ini+='[audioconfig]\nmode=0\nwaveout_devicein=-1\nwaveout_deviceout=0\n'
if a.audio_config:
 source=configparser.ConfigParser(strict=False,interpolation=None);source.read(a.audio_config,encoding='utf-8-sig')
 ini=ini.split('[audioconfig]')[0]+'[audioconfig]\n'+''.join(f'{k}={v}\n' for k,v in source['audioconfig'].items())
if sys.platform=='darwin':ini+='hasrecentlyopened=1\n'
(root/'reaper.ini').write_text(ini,encoding='utf-8')
for dll in (a.core,a.reawebapi):
 target=root/'UserPlugins'/dll.name;shutil.copy2(dll,target)
 if sys.platform=='darwin':subprocess.run(['codesign','--force','--sign','-',str(target)],check=True)
if sys.platform.startswith('linux'):
 for helper in a.reawebapi.parent.glob('reawebapi-webview-*'):shutil.copy2(helper,root/'UserPlugins'/helper.name)
shutil.copytree(a.ui,root/'web')
shutil.copy2(Path(__file__).with_name('ReaWebAPIProbe.js'),root/'web/probe.js')
with (root/'web/index.html').open('a',encoding='utf-8') as f:f.write('\n<script src="probe.js"></script>\n')
shutil.copy2(Path(__file__).with_name('ReaWebAPIGameProbe.js'),root/'web/game-probe.js')
with (root/'web/game.html').open('a',encoding='utf-8') as f:f.write('\n<script src="game-probe.js"></script>\n')
shutil.copy2(Path(__file__).with_name('ReaWebAPISmoke.lua'),root/'smoke.lua')
# Original ARM test program, no commercial ROM or Nintendo logo required.
# Mode 3, BG2 enabled; fill VRAM red, then idle forever.
rom=bytearray(512);struct.pack_into('<I',rom,0,0xea00002e)
rom[0xa0:0xac]=b'REAGBA TEST ';rom[0xac:0xb0]=b'TEST';rom[0xb2]=0x96
rom[0xbd]=(-sum(rom[0xa0:0xbd])-0x19)&255
code=[0xe3a00301,0xe3a01003,0xe3811b01,0xe1c010b0,0xe3a00406,0xe3a0101f,0xe3a02c96,0xe0c010b2,0xe2522001,0x1afffffc,0xeafffffe]
for i,value in enumerate(code):struct.pack_into('<I',rom,0xc0+i*4,value)
fixture='fixture'+(a.rom.suffix.lower() if a.rom else '.gba')
(root/fixture).write_bytes(a.rom.read_bytes() if a.rom else rom)
(root/'fixture-name.txt').write_text(fixture,encoding='utf-8')
env={k.upper():v for k,v in os.environ.items()}
startup=None
if os.name=='nt':
 startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
with (root/'process.log').open('w',encoding='utf-8') as log:
 p=subprocess.Popen([str(a.reaper.resolve()),'-newinst','-cfgfile',str(root/'reaper.ini'),'-new','-nosplash',str(root/'smoke.lua')],env=env,stdout=log,stderr=log,startupinfo=None)
 try:
  deadline=time.monotonic()+100
  while not (root/'result.json').exists() and p.poll() is None and time.monotonic()<deadline:time.sleep(.2)
  result=json.loads((root/'result.json').read_text(encoding='utf-8')) if (root/'result.json').exists() else {'ok':False,'error':'No report'}
  print(json.dumps({'output':str(root),**result},ensure_ascii=False))
 finally:
  if p.poll() is None:
   p.terminate()
   try:p.wait(timeout=5)
   except subprocess.TimeoutExpired:p.kill();p.wait()
raise SystemExit(0 if result['ok'] else 1)
