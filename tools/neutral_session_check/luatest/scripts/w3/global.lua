-- tools/neutral_session_check (task 0127, stage W-S3): leaves the Bannered Mare by its front door into WhiterunWorld,
-- then for a list of game hours sets the clock and aims the camera at the sun (the engine's direction for the hour:
-- (cos t, -0.268, sin t) with t = pi (hour - 6) / 14; yaw = atan2(x, y), pitch = -asin(z) as in mwrender/CLAUDE.md).
-- The night hours aim at the direction the sun would have, which is only a guess for where the moons are.
-- Logs "W3 ..." lines. Use at least 4000 frames and a forced weather (OPENEDEN_NEUTRAL_FORCE_WEATHER).
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

local start = nil
local step = 0
local DOORBASE = 'skyrimse:b152263'
local HOURS = { 9.0, 12.0, 15.0, 18.0, 21.0, 23.0, 1.0 }
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

local function sunAim(hour)
    local t = math.pi * (hour - 6) / 14
    local x, y, z = math.cos(t), -0.268, math.sin(t)
    return { yaw = math.atan2(x, y), pitch = -math.asin(math.max(-1, math.min(1, z))) }
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
                if door == nil then print('W3 door not found') else
                    print('W3 activating the front door')
                    door:activateBy(player)
                end
            elseif step == 1 and t >= 14 then
                step = 2
                nextAt = t
            elseif step == 2 and t >= nextAt then
                index = index + 1
                if index > #HOURS then
                    step = 3
                    print('W3 done')
                    return
                end
                local hour = HOURS[index]
                world.mwscript.getGlobalVariables(player).gamehour = hour
                -- Up in the air (the player falls about 1.5 s before the picture), so no roof or wall is in the way.
                if step == 2 and index == 1 then ground = player.position end
                player:teleport(player.cell, ground + util.vector3(0, 0, 3000), { rotation = player.rotation })
                local aim = sunAim(hour)
                print(string.format('W3 hour %.1f', hour))
                player:sendEvent('W3Aim', aim)
                nextAt = t + 5
            end
        end,
    },
}
