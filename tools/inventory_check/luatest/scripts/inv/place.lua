-- tools/inventory_check (task 0182): stands the player a short way in front of a container of the Bannered Mare, FACING
-- it, at the update number the config names (a frame count, not a clock, so the runs are equal), and logs where it
-- stands. The activation, the clicks and the screenshots come from OPENEDEN_RMLUI_INPUT_SCRIPT through the real input
-- path. Config: scripts/inv/cfg.lua written by check.sh: { base, near = {x,y,z}, from = {x,y}, dist, frame }.
local world = require('openmw.world')
local util = require('openmw.util')
local storage = require('openmw.storage')
local cfg = require('scripts.inv.cfg')
local phase = require('scripts.inv.phase')
local okInv, inv = pcall(require, 'core.skyrimse.inventory')

local function nearest(cell, base, near)
    local best, bestD = nil, math.huge
    for _, obj in ipairs(cell:getAll()) do
        local ok, id = pcall(function() return obj.recordId end)
        if ok and id == base then
            local p = obj.position
            local d = (p.x - near[1]) ^ 2 + (p.y - near[2]) ^ 2 + (p.z - near[3]) ^ 2
            if d < bestD then best, bestD = obj, d end
        end
    end
    return best, math.sqrt(bestD)
end

local updates, placed = 0, false
return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            updates = updates + 1
            if phase == 'save' and updates == cfg.saveAt then
                print(string.format('N6INV state boots player=%d container=%d revision=%d', inv.count('player', '00086993'), inv.count('000c8fcd', '00086993'), inv.revision()))
                storage.globalSection('N6INV'):set('saveNow', true)
            end
            if phase == 'load' then
                if updates == 300 then
                    print(string.format('N6INV loaded state boots player=%d container=%d gold=%d known=%s revision=%d', inv.count('player', '00086993'), inv.count('000c8fcd', '00086993'), inv.count('player', '0000000f'), tostring(inv.known('000c8fcd')), inv.revision()))
                end
                return
            end
            if placed or updates < cfg.frame then return end
            placed = true
            local obj, d = nearest(player.cell, cfg.base, cfg.near)
            if obj == nil then print('N6INV target not found'); return end
            local p = obj.position
            local cx, cy = cfg.from[1] - p.x, cfg.from[2] - p.y
            local len = math.sqrt(cx * cx + cy * cy)
            local px, py = p.x + cx / len * cfg.dist, p.y + cy / len * cfg.dist
            local yaw = math.atan2(p.x - px, p.y - py)
            print(string.format('N6INV target %s at %.0f,%.0f,%.0f (%.0f from the spot), placed at update %d', tostring(obj.recordId), p.x, p.y, p.z, d, updates))
            player:teleport(player.cell, util.vector3(px, py, p.z), { onGround = true, rotation = util.transform.rotateZ(yaw) })
        end,
    },
}
