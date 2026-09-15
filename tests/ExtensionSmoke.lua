-- Run only with -newinst -cfgfile build/extension-smoke/reaper.ini.
local root=reaper.GetResourcePath():gsub('\\','/')
assert(root:match('/build/extension%-smoke$'),'Use the isolated extension-smoke profile')
assert(reaper.CountTracks(0)==0,'The fixture requires an empty project')
local output=assert(os.getenv('REAGBA_EXTENSION_TEST_DIR'))
-- Read JSON written as UTF-8 by the runner. Windows Lua's os.getenv uses the
-- system ANSI code page and cannot round-trip a Chinese ROM filename reliably.
local romFile=assert(io.open(output..'/rom-path.json','r'))
local rom=romFile:read('*a');romFile:close()
local action=reaper.NamedCommandLookup('_REAGBA_SHOW')
local actionName=reaper.kbd_getTextFromCmd(action,0)
local file=assert(io.open(output..'/startup.txt','w'))
file:write('REAPER=',reaper.GetAppVersion(),'\naction=',action,'\naction_name=',actionName,'\nresource=',root,'\n');file:close()
assert(action~=0,'Native extension did not register REAGBA_SHOW')
assert(actionName=='zaibuyidao: ReaGBA','Native action has the wrong display name')
reaper.SetExtState('ReaGBA','extension_test_request','',false)
reaper.Main_OnCommand(action,0)
local stages={
  {3,'"action":"load_rom","path":'..rom},
  {7,'"action":"pause"'},
  {8,'"action":"save_state","slot":9'},
  {9,'"action":"reset"'},
  {10,'"action":"load_state","slot":9'},
  {11,'"action":"start"'},
  {12,'"action":"toggle_dock"'},
  {16,'"action":"toggle_dock"'},
  {18,'"action":"set_settings","settings":{"library_split":0.7}'},
  {20,'"action":"screenshot"'},
  {22,'"action":"pause"'},
}
local start,index=reaper.time_precise(),1
local interactive=os.getenv('REAGBA_TEST_INTERACTIVE')=='1'
if interactive then stages={stages[1]} end
local duration=interactive and 180 or 25
local function log(text)
  local f=assert(io.open(output..'/stages.txt','a'));f:write(text,'\n');f:close()
end
log('start '..start)
local function tick()
  local elapsed=reaper.time_precise()-start
  if index<=#stages and elapsed>=stages[index][1] then
    log('stage '..index..' elapsed '..elapsed)
    reaper.SetExtState('ReaGBA','extension_test_request','{"id":"smoke-'..index..'",'..stages[index][2]..'}',false)
    index=index+1
  end
  if elapsed>=duration then log('quit '..elapsed);reaper.Main_OnCommand(40004,0);return end
  reaper.defer(tick)
end
reaper.defer(tick)
