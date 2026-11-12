-- tools/neutral_session_check (task 0115): runs a list of timed steps in the player's cell of a neutral session:
-- screenshots, a report of where the player is, and activating the door of a given base nearest to a given position
-- (the same object:activateBy path the activate key takes). Steps are { at = real seconds, what = 'shot' | 'report' |
-- 'door', base = recordId, near = { x, y, z } }.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

local M = {}

local function nearestObject(cell, base, near)
    local best, bestD = nil, math.huge
    local total = 0
    for _, obj in ipairs(cell:getAll()) do
        local ok, id = pcall(function() return obj.recordId end)
        if ok and id == base then
            total = total + 1
            local p = obj.position
            local d = (p.x - near[1]) ^ 2 + (p.y - near[2]) ^ 2 + (p.z - near[3]) ^ 2
            if d < bestD then best, bestD = obj, d end
        end
    end
    return best, math.sqrt(bestD), total
end

function M.run(steps)
    local start = nil
    local nextStep = 1
    return {
        engineHandlers = {
            onUpdate = function()
                local player = world.players[1]
                if player == nil then return end
                start = start or core.getRealTime()
                local t = core.getRealTime() - start
                local step = steps[nextStep]
                if step == nil or t < step.at then return end
                nextStep = nextStep + 1
                if step.what == 'shot' then
                    player:sendEvent('N4DShot')
                    print('N4D shot at ' .. string.format('%.1f', t))
                elseif step.what == 'report' then
                    local p = player.position
                    print(string.format('N4D report t=%.1f cell=%s pos=%.0f,%.0f,%.0f', t, tostring(player.cell.name), p.x, p.y, p.z))
                elseif step.what == 'tp' then
                    -- Task 0138: puts the player on the ground at a spot of the cell it is in (to stand before a gate).
                    player:teleport(player.cell, util.vector3(step.pos[1], step.pos[2], step.pos[3]), { onGround = true })
                    print('N4D teleported within ' .. tostring(player.cell.name))
                elseif step.what == 'near' then
                    -- Task 0128: lists the objects within 400 units of the player (record id, distance), to find a door's base.
                    local p = player.position
                    for _, obj in ipairs(player.cell:getAll()) do
                        local ok, id = pcall(function() return obj.recordId end)
                        if ok then
                            local d = (obj.position - p):length()
                            if d < 400 then print(string.format('N4D near %s %.0f units at %.0f,%.0f,%.0f', tostring(id), d, obj.position.x, obj.position.y, obj.position.z)) end
                        end
                    end
                elseif step.what == 'door' then
                    local near = step.near
                    if near == 'player' then local pp = player.position; near = { pp.x, pp.y, pp.z } end
                    local door, dist, total = nearestObject(player.cell, step.base, near)
                    if door == nil then
                        print('N4D door not found in ' .. tostring(player.cell.name) .. ' for ' .. step.base)
                    else
                        print(string.format('N4D activating door (%d of this base, %.0f units from the given spot) in %s', total, dist, tostring(player.cell.name)))
                        door:activateBy(player)
                    end
                end
            end,
        },
    }
end

return M
