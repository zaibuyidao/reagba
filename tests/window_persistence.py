"""Verify window restoration across real REAPER restarts in an isolated profile."""
import argparse
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reaper', type=Path, required=True)
    args = parser.parse_args()
    profile = ROOT / 'build/window-persistence'
    output = ROOT / 'verification/window-persistence'
    config = output / 'data/config/window.json'
    profile.mkdir(parents=True, exist_ok=True)
    config.parent.mkdir(parents=True, exist_ok=True)
    env = {k.upper(): v for k, v in os.environ.items()} if os.name == 'nt' else dict(os.environ)
    subprocess.run(['cmake', '--install', str(ROOT / 'build/native'), '--config', 'Release',
                    '--component', 'ReaGBA', '--prefix', str(profile)], env=env, check=True)
    empty = profile / 'empty-vst'
    empty.mkdir(exist_ok=True)
    ini = profile / 'reaper.ini'
    ini.write_text('[REAPER]\nlastproject=\nshowlastproj=0\nverchk=0\nerrnowarn=5\n'
                   'vstfullstate=49989\nvstpath=' + str(empty) + '\nvstpath64=' + str(empty) +
                   '\n[audioconfig]\nmode=0\nwaveout_devicein=-1\nwaveout_deviceout=0\n', encoding='utf-8')
    expected = dict(x=180, y=160, width=880, height=640)
    config.write_text(json.dumps({**expected, 'docked': False}), encoding='utf-8')
    env['REAGBA_EXTENSION_TEST_DIR'] = str(output)
    results = []
    # Start floating, dock and exit, restart docked, undock and exit, restart floating.
    for phase, toggle, docked in [('floating', False, False), ('dock', True, True),
                                  ('docked-restart', False, True), ('undock', True, False),
                                  ('floating-restart', False, False)]:
        lua = output / 'start.lua'
        lua.write_text("""assert(reaper.GetResourcePath():gsub('\\\\','/'):match('/build/window%%-persistence$'))
assert(reaper.CountTracks(0)==0,'This test requires an empty project')
local action=reaper.NamedCommandLookup('_REAGBA_SHOW')
assert(action~=0,'Extension did not load')
reaper.SetExtState('ReaGBA','extension_test_request','',false)
reaper.Main_OnCommand(action,0)
local started=reaper.time_precise()
local sent=false
local function tick()
  local elapsed=reaper.time_precise()-started
  if elapsed>2 and not sent then
    sent=true
    %s
  end
  if elapsed>4 then reaper.Main_OnCommand(40004,0);return end
  reaper.defer(tick)
end
reaper.defer(tick)
""" % ("reaper.SetExtState('ReaGBA','extension_test_request','{\"id\":\"window-test\",\"action\":\"toggle_dock\"}',false)" if toggle else ''), encoding='utf-8')
        subprocess.run([str(args.reaper), '-newinst', '-cfgfile', str(ini), str(lua)],
                       env=env, check=True, timeout=30)
        saved = json.loads(config.read_text(encoding='utf-8'))
        history = json.loads((output / 'status-history.json').read_text(encoding='utf-8'))
        live = [s for s in history if s.get('host', {}).get('client_width', 0) > 0]
        assert live and live[-1]['docked'] == docked, (phase, live)
        assert live[0]['docked'] == (results[-1]['state']['docked'] if results else False), 'Initial docking state was not restored'
        if not docked:
            assert expected['width'] - 48 <= live[-1]['host']['client_width'] <= expected['width'], live[-1]
            assert expected['height'] - 100 <= live[-1]['host']['client_height'] <= expected['height'], live[-1]
        assert all(saved[key] == value for key, value in expected.items()), (phase, saved)
        assert saved['docked'] == docked, (phase, saved)
        if docked:
            assert saved['dock_id'] >= 0, saved
        if toggle:
            replies = json.loads((output / 'responses.json').read_text(encoding='utf-8'))
            assert any(r.get('id') == 'window-test' and r.get('ok') for r in replies), replies
        if phase == 'docked-restart':
            assert saved['dock_id'] == results[-1]['state']['dock_id'], 'Docker changed on restart'
        results.append(dict(phase=phase, state=saved, live_size={key:live[-1]['host'][key] for key in ('client_width','client_height')}))
    report = dict(passed=True, restarts=results)
    (output / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
