local root = reaper.GetResourcePath() .. '/'
assert(reaper.NamedCommandLookup('_REAGBA_SHOW') == 0)
dofile(root .. 'web/zaibuyidao_ReaGBA.lua')
local f=assert(io.open(root .. 'launcher-returned.txt','w'))
f:write('returned')
f:close()
