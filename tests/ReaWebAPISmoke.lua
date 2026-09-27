local root = reaper.GetResourcePath() .. '/'
assert(reaper.NamedCommandLookup('_REAGBA_SHOW') == 0)
dofile(root .. 'web/zaibuyidao_ReaGBA.lua')
local f=assert(io.open(root .. 'launcher-returned.txt','w'))
f:write('returned')
f:close()
local function reopen_after_close()
  local id = tonumber(reaper.GetExtState('ReaGBA.test', 'closingWindow'))
  if id and not reaper.ReaWeb_IsOpen(id) then
    dofile(root .. 'web/zaibuyidao_ReaGBA.lua')
    return
  end
  reaper.defer(reopen_after_close)
end
reaper.defer(reopen_after_close)
