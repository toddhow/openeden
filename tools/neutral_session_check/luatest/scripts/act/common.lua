-- tools/neutral_session_check (task 0150): stands the player a short way in front of a target, FACING it (first person
-- camera, nothing static), and logs what the engine's own focus query sees there. The activation itself is NOT done
-- here: a synthetic activate key press arrives through the real input path (OPENEDEN_RMLUI_INPUT_SCRIPT), which is the
-- point of the scenario (task 0141's tests all used object:activateBy, which skips the focus ray and the tooltip check).
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

local M = {}

local function nearest(cell, base, near)
    local best, bestD = nil, math.huge
    for _, obj in ipairs(cell:getAll()) do
        local ok, id = pcall(function() return obj.recordId end)
        if ok and type(id) == 'string' and ((base == nil and id:sub(1, 10) == 'skyrimse:b') or id == base) then
            local p = obj.position
            local d = (p.x - near[1]) ^ 2 + (p.y - near[2]) ^ 2 + (p.z - near[3]) ^ 2
            if d < bestD then best, bestD = obj, d end
        end
    end
    return best, math.sqrt(bestD)
end

-- target = { base = recordId or nil, near = {x,y,z}, from = {x,y} (where the player stands relative to the object), dist }
function M.run(target)
    local start, placed = nil, false
    return {
        engineHandlers = {
            onUpdate = function()
                local player = world.players[1]
                if player == nil then return end
                start = start or core.getRealTime()
                local t = core.getRealTime() - start
                if not placed and t >= 6 then
                    placed = true
                    local obj, d = nearest(player.cell, target.base, target.near)
                    if obj == nil then print('N5A target not found'); return end
                    local p = obj.position
                    local cx, cy = target.from[1] - p.x, target.from[2] - p.y
                    local len = math.sqrt(cx * cx + cy * cy)
                    local px, py = p.x + cx / len * target.dist, p.y + cy / len * target.dist
                    local yaw = math.atan2(p.x - px, p.y - py)
                    print(string.format('N5A target %s at %.0f,%.0f,%.0f (%.0f from the spot)', tostring(obj.recordId), p.x, p.y, p.z, d))
                    player:teleport(player.cell, util.vector3(px, py, p.z), { onGround = true, rotation = util.transform.rotateZ(yaw) })
                end
            end,
        },
    }
end

return M
