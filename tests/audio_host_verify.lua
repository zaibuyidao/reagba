local root = reaper.GetResourcePath() .. '/'
local session, measures = nil, {}
local function quote(s) return '"' .. tostring(s):gsub('\\','\\\\'):gsub('"','\\"'):gsub('\n','\\n'):gsub('\r','') .. '"' end
local function wait(seconds)
  local deadline=reaper.time_precise()+seconds
  repeat coroutine.yield() until reaper.time_precise()>=deadline
end
local function command(body)
  assert(reaper.ReaGBA_Request(session,body),reaper.ReaGBA_GetLastError())
  local deadline=reaper.time_precise()+5
  while true do
    local result=reaper.ReaGBA_Poll(session)
    if result~='' then assert(result:find('"ok":true',1,true),result);return result end
    assert(reaper.time_precise()<deadline,'Command timeout');coroutine.yield()
  end
end
local function settings(mode,target)
 command('{"action":"set_settings","settings":{"audio_output":"'..mode..'","audio_track":"'..(target or 'preview')..'"}}')
 wait(.7)
end
local function peak(track) if not track then return reaper.ReaGBA_TestOutputPeak(-1,true)/1000000 end;return math.max(reaper.Track_GetPeakInfo(track,0),reaper.Track_GetPeakInfo(track,1)) end
local function measure(name,track)
 local value=0
 if not track then reaper.ReaGBA_TestOutputPeak(-1,true) end
 for i=1,12 do value=math.max(value,peak(track));wait(.025) end
 measures[#measures+1]=quote(name)..':'..value
 return value
end
local function silent(name,track) assert(measure(name,track)<.0001,name..' should be silent') end
local function audible(name,track) local value=measure(name,track);assert(value>.005,name..' should be audible');return value end
local function add(name)
 reaper.InsertTrackAtIndex(reaper.CountTracks(0),true)
 local track=reaper.GetTrack(0,reaper.CountTracks(0)-1)
 reaper.GetSetMediaTrackInfo_String(track,'P_NAME',name,true)
 return track
end
local function run()
 local first=add('ReaGBA Preview');local duplicate=add('ReaGBA Preview');local selected=add('Selected');local bus=add('Bus')
 local master=reaper.GetMasterTrack(0)
 reaper.SetOnlyTrackSelected(selected)
 session=assert(reaper.ReaGBA_Create(root..'data'));assert(session>0)
 command('{"action":"load_rom","path":'..quote(root..'tone.gba')..'}')
 command('{"action":"start"}')
 settings('reaper_output')
 audible('reaper_output',nil);silent('output_does_not_feed_track',first)
 command('{"action":"set_settings","settings":{"audio_channel":1,"audio_mono":true}}');wait(.7)
 reaper.ReaGBA_TestOutputPeak(-1,true);wait(.25)
 assert(reaper.ReaGBA_TestOutputPeak(0,true)<100,'Mono selection leaked to first output')
 assert(reaper.ReaGBA_TestOutputPeak(1,true)>5000,'Mono selection did not reach second output')
 measures[#measures+1]='"mono_second_output":true'
 command('{"action":"set_settings","settings":{"audio_channel":1023,"audio_mono":false}}');wait(.5)
 silent('missing_channels',nil)
 assert(command('{"action":"get_audio_outputs"}'):find('channel_unavailable',1,true),'Missing channel status')
 command('{"action":"set_settings","settings":{"audio_channel":0,"audio_mono":false}}');wait(.7)
 audible('restored_channels',nil)
 reaper.Audio_Quit();wait(.5)
 assert(command('{"action":"get_audio_outputs"}'):find('engine_stopped',1,true),'Closed device status')
 reaper.Audio_Init();wait(.7);audible('device_restart',nil)
 settings('reaper_track')
 local base=audible('named_track',first);audible('named_master',master);silent('duplicate_name',duplicate);silent('named_precedes_selected',selected)
 reaper.SetMediaTrackInfo_Value(first,'D_VOL',.25);wait(.5)
 assert(measure('track_volume',first)<base*.4,'Track volume bypassed')
 reaper.SetMediaTrackInfo_Value(first,'D_VOL',1)
 reaper.SetMediaTrackInfo_Value(first,'D_PAN',-1);wait(.5)
 reaper.ReaGBA_TestOutputPeak(1,true);wait(.2);assert(reaper.ReaGBA_TestOutputPeak(1,true)<100,'Track pan bypassed')
 reaper.SetMediaTrackInfo_Value(first,'D_PAN',0)
 local fx=reaper.TrackFX_AddByName(first,'JS: reagba-test-gain',false,-1);assert(fx>=0,'Test FX missing')
 reaper.TrackFX_SetParam(first,fx,0,0);wait(.5);silent('track_fx',nil)
 reaper.TrackFX_SetParam(first,fx,0,1);wait(.5);audible('fx_restored',master)
 reaper.SetMediaTrackInfo_Value(first,'B_MUTE',1);wait(.5);silent('track_mute',nil)
 reaper.SetMediaTrackInfo_Value(first,'B_MUTE',0)
 reaper.SetMediaTrackInfo_Value(selected,'I_SOLO',1);wait(.5);silent('other_track_solo',nil)
 reaper.SetMediaTrackInfo_Value(selected,'I_SOLO',0)
 reaper.SetMediaTrackInfo_Value(first,'B_MAINSEND',0);wait(.5);silent('master_send_disabled',nil)
 reaper.CreateTrackSend(first,bus);wait(.5);audible('routing_bus',bus);audible('routing_master',master)
 settings('reaper_track','selected');audible('selected_track',selected);silent('named_after_switch',first)
 reaper.SetOnlyTrackSelected(duplicate);wait(.7);audible('selection_follows',duplicate);silent('old_selection',selected)
 settings('reaper_track','preview')
 reaper.DeleteTrack(first);wait(.7);audible('next_named',duplicate)
 reaper.DeleteTrack(duplicate);reaper.SetOnlyTrackSelected(selected);wait(.7);audible('selected_fallback',selected)
 reaper.SetTrackSelected(selected,false);wait(.7);silent('no_target',nil)
 reaper.SetOnlyTrackSelected(selected);wait(.7);audible('selection_recovers',selected)
 reaper.OnPlayButton();wait(.5);audible('transport_playing',selected)
 reaper.OnStopButton();wait(.5);audible('transport_stopped',selected)
 local original=reaper.EnumProjects(-1)
 reaper.Main_OnCommand(40859,0);wait(.7);silent('empty_project',nil)
 local alternate=add('Alternate');reaper.SetOnlyTrackSelected(alternate);wait(.7);audible('new_project_selection',alternate)
 reaper.SelectProjectInstance(original);wait(.7);audible('project_return',selected)
 command('{"action":"pause"}');wait(.5);silent('pause',nil)
 command('{"action":"start"}');wait(.5);audible('resume',master)
 command('{"action":"set_speed","value":4}');wait(.5);silent('fast_forward',nil)
 command('{"action":"set_speed","value":1}');wait(.5);audible('normal_speed',master)
 settings('system');silent('system_not_in_mixer',nil)
 settings('reaper_output');audible('reaper_output_again',nil)
 assert(reaper.ReaGBA_Destroy(session));session=nil;wait(.5);silent('destroy',nil)
end
local co=coroutine.create(run)
local function tick()
 local ok,err=coroutine.resume(co)
 if not ok or coroutine.status(co)=='dead' then
  if session then reaper.ReaGBA_Destroy(session) end
  local f=assert(io.open(root..'result.json','w'));f:write('{"ok":'..tostring(ok)..',"error":'..quote(err or '')..',"peaks":{'..table.concat(measures,',')..'}}');f:close()
 else reaper.defer(tick) end
end
reaper.defer(tick)
