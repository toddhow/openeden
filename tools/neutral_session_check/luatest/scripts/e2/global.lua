-- tools/neutral_session_check (task 0125, Skyrim exteriors stage E2): leaves the Bannered Mare through its front door into
-- WhiterunWorld, takes pictures, then hops the player east across cell borders so that the grid streams. Real seconds.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

local start = nil
local step = 0
local hops = 0
local hourSet = false
local probeAt = nil

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

local function report(tag, t, player)
    local p = player.position
    local c = player.cell
    print(string.format('E2 %s t=%.1f cell=%s exterior=%s grid=%s,%s pos=%.0f,%.0f,%.0f', tag, t, tostring(c.name), tostring(c.isExterior), tostring(c.gridX), tostring(c.gridY), p.x, p.y, p.z))
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            start = start or core.getRealTime()
            local t = core.getRealTime() - start
            if probeAt and t >= probeAt then
                probeAt = nil
                player:sendEvent('E2Probe')
            end
            if not hourSet and t >= 1 then
                hourSet = true
                local ok, err = pcall(function() world.mwscript.getGlobalVariables(player).gamehour = 12 end)
                print('E2 hour set to noon: ' .. tostring(ok) .. ' ' .. tostring(err))
            end
            if step == 0 and t >= 6 then
                step = 1
                report('inside', t, player)
                local door = nearestDoor(player.cell, 'skyrimse:b152263', { -128, -1152, 120 })
                if door == nil then print('E2 door not found') else
                    print('E2 activating the front door')
                    door:activateBy(player)
                end
            elseif step == 1 and t >= 14 then
                step = 2
                report('outside', t, player)
                player:sendEvent('E2Shot')
                player:sendEvent('E2Probe')
            elseif step == 2 and t >= 18 then
                step = 3
                player:sendEvent('E2Walk', true)
            elseif step == 3 and t >= 26 then
                step = 4
                player:sendEvent('E2Walk', false)
                report('walked', t, player)
                player:sendEvent('E2Shot')
            elseif step >= 4 and step < 12 and t >= 26 + (step - 3) * 7 then
                -- hop east by one cell (4096 units) at a time, onto the ground
                step = step + 1
                hops = hops + 1
                local p = player.position
                local dx = (hops <= 4) and 4096 or -4096
                local target = util.vector3(p.x + dx, p.y, p.z)
                print(string.format('E2 hop %d to %.0f,%.0f,%.0f', hops, target.x, target.y, target.z))
                player:teleport(player.cell, target, { onGround = true })
                probeAt = t + 4 -- after it has settled onto the ground
                if hops % 2 == 0 then player:sendEvent('E2Shot') end
            elseif step == 12 and t >= 26 + 9 * 7 then
                step = 13
                report('end', t, player)
                player:sendEvent('E2Shot')
            end
        end,
    },
}
