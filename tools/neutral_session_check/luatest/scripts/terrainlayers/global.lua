-- tools/neutral_session_check (task 0149, Skyrim terrain texture layers): leaves the Mare, visits a list of spots of
-- Whiterun's plains, looks at the ground from a pitched camera and takes a picture at each. Real seconds. Spots print as
-- TLAYERS lines. The spots come from OPENEDEN_TERRAINLAYERS_SPOTS="x,y;x,y" (when os.getenv exists) or a built-in list.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

-- Roads: the 'dirtpath01' quadrants of the data (cell 7,-3 q0 and q1; cell 9,-4 q1 and q3) and the border spots between them.
local DEFAULT = '30720,-12150;39400,-14336;30000,-12288;40960,-15000;30000,-10240;39300,-14900;31500,-12060'
local spots = {}
for x, y in string.gmatch(DEFAULT, '([%-%d%.]+),([%-%d%.]+)') do spots[#spots + 1] = { tonumber(x), tonumber(y) } end
local start, hourSet, nextAt, index = nil, false, 8, 0
local SECONDS = 5

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            start = start or core.getRealTime()
            local t = core.getRealTime() - start
            if not hourSet and t >= 1 then
                hourSet = true
                pcall(function() world.mwscript.getGlobalVariables(player).gamehour = 12 end)
            end
            if t < nextAt or index > #spots then return end
            if index == 0 then
                local door
                for _, obj in ipairs(player.cell:getAll()) do
                    local ok, id = pcall(function() return obj.recordId end)
                    if ok and id == 'skyrimse:b152263' then door = obj break end
                end
                if door then door:activateBy(player) end
                index = 1
                nextAt = t + 8
                return
            end
            local s = spots[index]
            print(string.format('TLAYERS spot %d at %.0f,%.0f', index, s[1], s[2]))
            local z = -2600
            local ok, h = pcall(function() return core.land.getHeightAt(util.vector3(s[1], s[2], 0), player.cell) end)
            if ok and h then z = h + 40 end
            print(string.format('TLAYERS ground %.0f', z))
            player:teleport(player.cell, util.vector3(s[1], s[2], z), { onGround = true })
            player:sendEvent('TLook')
            index = index + 1
            nextAt = t + SECONDS
        end,
    },
}
