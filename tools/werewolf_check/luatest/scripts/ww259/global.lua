-- tools/werewolf_check (task 0259): stands the player a short way from Hulda, facing her, at the update number the config
-- names (a count of updates, not a clock, so the runs are equal), and prints where everybody stands every 300 updates.
local world = require('openmw.world')
local util = require('openmw.util')
local cfg = require('scripts.ww259.cfg')

local updates, placed = 0, false

local function hulda(cell)
    for _, obj in ipairs(cell:getAll()) do
        local ok, id = pcall(function() return obj.recordId end)
        if ok and id == cfg.hulda then return obj end
    end
    return nil
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            updates = updates + 1
            if placed or updates < cfg.placeAt then return end
            local target = hulda(player.cell)
            if target == nil then
                if updates % 300 == 0 then print('WW259 Hulda not found') end
                return
            end
            placed = true
            local p = target.position
            local px, py = p.x + cfg.dist, p.y
            local yaw = math.atan2(p.x - px, p.y - py)
            print(string.format('WW259 placed at update %d: Hulda at %.0f,%.0f,%.0f, player to %.0f,%.0f', updates, p.x, p.y, p.z, px, py))
            player:teleport(player.cell, util.vector3(px, py, p.z), { onGround = true, rotation = util.transform.rotateZ(yaw) })
        end,
    },
}
