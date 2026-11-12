-- tools/neutral_session_check (task 0149, Skyrim terrain texture layers): leaves the Mare, visits a list of spots of
-- Whiterun's plains, looks at the ground from a pitched camera and takes a picture at each. Real seconds. Spots print as
-- TLAYERS lines. The spots come from OPENEDEN_TERRAINLAYERS_SPOTS="x,y;x,y" (when os.getenv exists) or a built-in list.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

local DEFAULT = '24500,-15000;26500,-17500;28500,-19500;22000,-14000;30000,-12000;20000,-9000;27500,-9500;25670,-7633'
local spots = {}
for x, y in string.gmatch(DEFAULT, '([%-%d%.]+),([%-%d%.]+)') do spots[#spots + 1] = { tonumber(x), tonumber(y) } end
local start, hourSet, nextAt, index = nil, false, 8, 0
local SECONDS = 4

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
            player:teleport(player.cell, util.vector3(s[1], s[2], -2600), { onGround = true })
            player:sendEvent('TLook')
            index = index + 1
            nextAt = t + SECONDS
        end,
    },
}
