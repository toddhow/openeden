-- tools/neutral_session_check (task 0127, stage W-S4): leaves the Bannered Mare by its front door into WhiterunWorld, then
-- walks a plan (scripts/w5/plan.lua: one entry per picture: game hour, yaw, pitch in radians, waiting seconds) and takes a
-- picture at each. The moons' directions come from the engine's "Neutral weather: moon" lines of an earlier run (the
-- plan holds them as numbers). With `timescale` 0 the hour stays where the plan put it. Logs "W5 ..." lines.
-- Use a forced weather (OPENEDEN_NEUTRAL_FORCE_WEATHER) and at least 4000 frames (more for the storm scenario).
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')
local plan = require('scripts.w5.plan')

local DOORBASE = 'skyrimse:b152263'
local start = nil
local step = 0
local index = 0
local nextAt = 0
local ground = nil

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
                if door == nil then print('W5 door not found') else
                    print('W5 activating the front door')
                    door:activateBy(player)
                end
            elseif step == 1 and t >= 14 then
                step = 2
                nextAt = t
                world.mwscript.getGlobalVariables(player).timescale = plan.timescale or 0
            elseif step == 2 and t >= nextAt then
                index = index + 1
                if index > #plan.shots then
                    step = 3
                    print('W5 done')
                    return
                end
                local shot = plan.shots[index]
                world.mwscript.getGlobalVariables(player).gamehour = shot.hour
                if index == 1 then ground = player.position end
                -- up in the air so that no roof or wall is in the way
                player:teleport(player.cell, ground + util.vector3(0, 0, plan.height or 3000), { rotation = player.rotation })
                print(string.format('W5 shot %d hour %.2f yaw %.3f pitch %.3f', index, shot.hour, shot.yaw, shot.pitch))
                player:sendEvent('W5Aim', { yaw = shot.yaw, pitch = shot.pitch })
                nextAt = t + (shot.wait or 6)
            end
        end,
    },
}
