"""Run a locally supplied ROM in an isolated REAPER profile. Never use a live project."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]

def check(output):
    if (output/'host-closed.json').exists():raise AssertionError('The ReaGBA test window was closed before the test ended')
    if (output/'host-error.json').exists():raise AssertionError((output/'host-error.json').read_text(encoding='utf-8'))
    assert 'action_name=zaibuyidao: ReaGBA' in (output/'startup.txt').read_text(encoding='utf-8'),'Native action name mismatch'
    replies=json.loads((output/'responses.json').read_text(encoding='utf-8'))
    stages=[r for r in replies if str(r.get('id','')).startswith('smoke-')]
    assert len(stages)==11 and all(r['ok'] for r in stages),stages
    states=json.loads((output/'status-history.json').read_text(encoding='utf-8'))
    assert any(s['frames']>300 and s['running'] for s in states),'ROM did not run'
    assert any(s['docked'] for s in states),'Docker was never entered'
    dock_index=next(i for i,s in enumerate(states) if s['docked'])
    assert any(not s['docked'] and s['host']['client_width']>0 and s['host']['viewport']['visible'] for s in states[dock_index:]),'Undocking did not restore the native view'
    assert not any(s['error'] for s in states),'Emulator reported an error'
    assert (output/'data/config/preferences.json').is_file(),'Settings did not persist'
    assert list((output/'data/states').rglob('*.state')),'Save state was not written'
    status=json.loads((output/'status.json').read_text(encoding='utf-8'))
    summary={'passed':True,'rom_code':next(s['game']['code'] for s in states if s['loaded']),
             'maximum_frames':max(s['frames'] for s in states),'audio_device':status['audio_device'],
             'command':'_REAGBA_SHOW','action_name':'zaibuyidao: ReaGBA','dock_and_undock':True,'save_and_load':True}
    (output/'summary.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
    print(json.dumps(summary,indent=2))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reaper',type=Path,required=True)
    parser.add_argument('--rom',type=Path,required=True)
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build/native')
    parser.add_argument('--output',type=Path,default=ROOT/'verification/extension')
    parser.add_argument('--interactive',action='store_true',help='Keep an empty test instance open for 3 minutes for keyboard/mouse checks')
    args=parser.parse_args();args.output=args.output.resolve()
    assert args.rom.is_file() and args.reaper.is_file()
    profile=ROOT/'build/extension-smoke';profile.mkdir(parents=True,exist_ok=True);args.output.mkdir(parents=True,exist_ok=True)
    cmake=shutil.which('cmake') or 'C:/Program Files/CMake/bin/cmake.exe'
    env={k.upper():v for k,v in os.environ.items()} if os.name=='nt' else dict(os.environ)
    subprocess.run([cmake,'--install',str(args.build_dir),'--config','Release','--component','ReaGBA','--prefix',str(profile)],env=env,check=True)
    # An unset VST path makes a fresh REAPER profile scan all system plug-ins,
    # potentially opening unrelated licensing dialogs before this test starts.
    empty_plugins=profile/'empty-vst';empty_plugins.mkdir(exist_ok=True)
    # REAPER 7.78 writes this VST configuration after its first initialization.
    # Keep it with the explicit empty path so it does not append system VST3
    # directories again on every fresh smoke run.
    # Startup modals block extension timers even while Lua defer keeps running.
    # Disable host update/device notices only in this empty test profile;
    # ReaGBA's separate SDL audio device is checked in status.json.
    ini='[REAPER]\nlastproject=\nshowlastproj=0\nverchk=0\nerrnowarn=5\nvstfullstate=49989\nvstpath='+str(empty_plugins)+'\nvstpath64='+str(empty_plugins)+'\n'
    if os.name=='nt':
        # Select the standard output in this empty profile so REAPER does not
        # stop at its first-run device question before executing the Lua test.
        ini+='[audioconfig]\nmode=0\nwaveout_devicein=-1\nwaveout_deviceout=0\n'
    (profile/'reaper.ini').write_text(ini,encoding='utf-8')
    (args.output/'rom-path.json').write_text(json.dumps(str(args.rom.resolve())),encoding='utf-8')
    env['REAGBA_EXTENSION_TEST_DIR']=str(args.output)
    if args.interactive:env['REAGBA_TEST_INTERACTIVE']='1'
    else:env.pop('REAGBA_TEST_INTERACTIVE',None)
    subprocess.run([str(args.reaper),'-newinst','-cfgfile',str(profile/'reaper.ini'),str(ROOT/'tests/ExtensionSmoke.lua')],env=env,check=True,timeout=220 if args.interactive else 70)
    if not args.interactive:check(args.output)

if __name__=='__main__':main()
