-- tools/neutral_session_check (task 0133, far terrain tiles): leaves the Bannered Mare by its front door into
-- WhiterunWorld, then visits places of the water map (a lake at 5100 below zero, a river that steps down by cells) and
-- takes pictures from a static camera over each. Real seconds. The places are grid squares of Tamriel's land as
-- WhiterunWorld borrows it; heights come from the pack (log lines "NWATER").
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

local start = nil
local step = 0
local doorBase = 'skyrimse:b152263'

-- { name, teleport x, y, camera x, y, z, yaw, pitch }  (task 0133: the far field from Whiterun's plain; yaw 0 looks north)
local places = {
    { 'north-level', 25670, -7633, 25670, -7633, 0, 0, 0.0 },
    { 'north-down', 25670, -7633, 25670, -7633, 0, 0, 0.12 },
    { 'east-level', 25670, -7633, 25670, -7633, 0, 1.5708, 0.0 },
    { 'west-level', 25670, -7633, 25670, -7633, 0, -1.5708, 0.0 },
    { 'south-level', 25670, -7633, 25670, -7633, 0, 3.1416, 0.0 },
    { 'high-north', 25670, -7633, 25670, -7633, 9000, 0, 0.35 },
    { 'high-east', 25670, -7633, 25670, -7633, 9000, 1.5708, 0.35 },
}
local index = 0
local nextAt = 0

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
                pcall(function() world.mwscript.getGlobalVariables(player).gamehour = 12 end)
                player:sendEvent('WaterGod')
                local door = nearestDoor(player.cell, doorBase, { -128, -1152, 120 })
                if door == nil then print('LV door not found') else door:activateBy(player) end
            elseif step == 1 and t >= 14 then
                step = 2
                local c = player.cell
                print(string.format('LV outside exterior=%s grid=%s,%s', tostring(c.isExterior), tostring(c.gridX), tostring(c.gridY)))
                nextAt = t + 1
            elseif step == 2 and t >= nextAt then
                index = index + 1
                local p = places[index]
                if p == nil then step = 9; print('LV done'); return end
                print(string.format('LV place %s', p[1]))
                player:teleport(player.cell, util.vector3(p[2], p[3], 4000), { onGround = true })
                step = 3
                nextAt = t + 12 -- the cells stream in
            elseif step == 3 and t >= nextAt then
                local p = places[index]
                player:sendEvent('WaterCamera', { x = p[4], y = p[5], z = p[6], yaw = p[7], pitch = p[8] })
                step = 4
                nextAt = t + 1.5
            elseif step == 4 and t >= nextAt then
                local p = places[index]
                local c = player.cell
                print(string.format('LV shot %s cell=%s,%s', p[1], tostring(c.gridX), tostring(c.gridY)))
                player:sendEvent('WaterShot')
                step = 2
                nextAt = t + 2
            end
        end,
    },
}
