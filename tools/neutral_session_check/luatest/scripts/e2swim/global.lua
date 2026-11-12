-- tools/neutral_session_check (task 0129, swimming follow-up): leaves the Bannered Mare into WhiterunWorld, drops the
-- player into the Whiterun lake (squares x 11 to 14, y -1 to 0, water at -5050), lets the player swim, then sends the
-- player east along the water until it leaves it. The player script logs the swim state each half second (E2W swim).
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

local start = nil
local step = 0
local nextAt = 0
local probeAt = nil
local doorBase = 'skyrimse:b152263'

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
                player:sendEvent('SwimGod')
                local door = nearestDoor(player.cell, doorBase, { -128, -1152, 120 })
                if door == nil then print('E2W door not found') else door:activateBy(player) end
            elseif step == 1 and t >= 14 then
                step = 2
                print('E2W swim phase drop into the lake centre')
                player:teleport(player.cell, util.vector3(46000, -2048, 4000), { onGround = true })
                player:sendEvent('SwimTrace', { walk = false })
                step = 3
                nextAt = t + 14
            elseif step == 3 and probeAt and t >= probeAt then
                probeAt = nil
                for _, y in ipairs({ -2048, -6000 }) do player:sendEvent('SwimProbe', { x0 = 40000, y = y, step = 1500, n = 18 }) end
            elseif step == 3 and t >= nextAt then
                step = 4
                print('E2W swim phase move to the east part of the lake, then swim east')
                player:teleport(player.cell, util.vector3(45300, -2048, 4000),
                    { onGround = true, rotation = util.transform.rotateZ(math.pi / 2) })
                player:sendEvent('SwimTrace', { walk = true })
                nextAt = t + 40
            elseif step == 4 and t >= nextAt then
                step = 6
                print('E2W swim phase river: teleport to the river stair and walk south across squares of different water heights')
                player:teleport(player.cell, util.vector3(30720, -26624, 4000), { onGround = true, rotation = util.transform.rotateZ(math.pi) })
                nextAt = t + 40
            elseif step == 6 and t >= nextAt then
                step = 5
                print('E2W swim done')
            end
        end,
    },
}
