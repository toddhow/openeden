-- tools/neutral_session_check (task 0152): prints the land height round a point ("LANDH dx dy z") 8 real seconds after the
-- start, for the Tamriel point of the MountainTrim01 placement (skyrimse:b197124).
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

local CX, CY = 104403, 18631
local start, done = nil, false
return {
    engineHandlers = {
        onUpdate = function()
            if done or world.players[1] == nil then return end
            start = start or core.getRealTime()
            if core.getRealTime() - start < 10 then return end
            done = true
            for dy = -256, 256, 128 do
                local row = {}
                for dx = -256, 256, 128 do
                    local ok, h = pcall(function() return core.land.getHeightAt(util.vector3(CX + dx, CY + dy, 0), world.players[1].cell) end)
                    row[#row + 1] = ok and string.format('%.0f', h) or 'ERR'
                end
                print('LANDH dy=' .. dy .. ' ' .. table.concat(row, ' '))
            end
        end,
    },
}
