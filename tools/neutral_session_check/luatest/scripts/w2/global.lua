-- tools/neutral_session_check (task 0127, stage W-S2): leaves the Bannered Mare by its front door into WhiterunWorld
-- with a forced weather (OPENEDEN_NEUTRAL_FORCE_WEATHER), lets the game clock run fast so that the ground gets wet or
-- snowed on, and takes pictures looking ahead. Logs "W2 ..." lines. Real seconds.
local core = require('openmw.core')
local world = require('openmw.world')

local start = nil
local step = 0
local DOORBASE = 'skyrimse:b152263'
local SHOTS = { 18, 30, 45, 60 }
local shotIndex = 0

local function nearestDoor(cell, base, near)
    local best, bestD = nil, math.huge
    for _, obj in ipairs(cell:getAll()) do
        local ok, id = pcall(function() return obj.recordId end)
        if ok and id == base then
            local p = obj.position
            local d = (p.x - near[1]) ^ 2 + (p.y - near[2]) ^ 2 + (p.z - near[3]) ^ 2
            if d < bestD then best, bestD = obj, d end
        end
    end
    return best
end

local function describe(player)
    local ok, cur = pcall(core.weather.getCurrent, player.cell)
    local name = '?'
    if ok and cur ~= nil then
        local okn, n = pcall(function() return cur.name end)
        name = okn and tostring(n) or '?'
    end
    local hour = world.mwscript.getGlobalVariables(player).gamehour
    return string.format('cur=%s hour=%.2f', name, hour)
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            start = start or core.getRealTime()
            local t = core.getRealTime() - start
            if step == 0 and t >= 6 then
                step = 1
                world.mwscript.getGlobalVariables(player).gamehour = 12
                local door = nearestDoor(player.cell, DOORBASE, { -128, -1152, 120 })
                if door == nil then print('W2 door not found') else
                    print('W2 activating the front door')
                    door:activateBy(player)
                end
            elseif step == 1 and t >= 14 then
                step = 2
                -- the ground wets and the snow builds over game time: a fast clock, kept around noon
                world.mwscript.getGlobalVariables(player).timescale = 200
                print('W2 outside: ' .. describe(player))
                player:sendEvent('W2Look')
            elseif step == 2 then
                local hour = world.mwscript.getGlobalVariables(player).gamehour
                if hour > 15 or hour < 10 then
                    world.mwscript.getGlobalVariables(player).gamehour = 11
                end
                if shotIndex < #SHOTS and t >= SHOTS[shotIndex + 1] then
                    shotIndex = shotIndex + 1
                    print(string.format('W2 shot %d at %.0f s: %s', shotIndex, t, describe(player)))
                    player:sendEvent('W2Shot')
                end
                if shotIndex >= #SHOTS and t >= SHOTS[#SHOTS] + 4 then
                    step = 3
                    world.mwscript.getGlobalVariables(player).timescale = 30
                    print('W2 done: ' .. describe(player))
                end
            end
        end,
    },
}
