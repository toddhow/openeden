-- tools/neutral_session_check (task 0212, creatures): "n5wolf" is "n5ai" with daylight (game hour 12, time frozen) and a
-- camera that looks at the middle between the player and the wolf from the side (cam = { side, up } in aicfg.lua is
-- the side offset in y and the height). The player stands DIST units east of the target and faces it every frame. A
-- picture every EVERY seconds for WINDOW seconds; prints "N5WOLF <n> t <t> actor x y z".
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

local cfg = require('scripts.n5.aicfg')
local start, placed, nextShot, shots, hourSet = nil, nil, 0, 0, false
local px, py, pz
local lastP

local function find(cell, tag)
    for _, o in ipairs(cell:getAll()) do
        if string.find(o.recordId, tag, 1, true) then return o end
    end
    return nil
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            if not hourSet then
                hourSet = true
                world.mwscript.getGlobalVariables(player).gamehour = 12
                world.mwscript.getGlobalVariables(player).timescale = 0
            end
            start = start or core.getSimulationTime()
            local t = core.getSimulationTime() - start
            if t < cfg.start then return end
            local actor = find(player.cell, cfg.target)
            -- task 0280: a creature that walks across a cell border leaves the player's cell (neutral actors are not
            -- in world.activeActors): from then on the last place it was seen stands for it
            if actor == nil and lastP == nil then return end
            local p = actor and actor.position or lastP
            lastP = p
            if not placed then
                placed = t
                px, py, pz = p.x + cfg.dist, p.y, p.z + 2
                nextShot = t + 1
            end
            local yaw = math.atan2(p.x - px, p.y - py)
            player:teleport(player.cell, util.vector3(px, py, pz), { rotation = util.transform.rotateZ(yaw) })
            if t >= nextShot and t - placed <= cfg.window then
                nextShot = t + cfg.every
                shots = shots + 1
                local mx, my = (p.x + px) / 2, (p.y + py) / 2
                local side = cfg.cam and cfg.cam[2] or -260
                local up = cfg.cam and cfg.cam[3] or 110
                player:sendEvent('N5AIShot', { x = mx, y = my + side, z = p.z + up, lx = mx, ly = my, lz = p.z + 40 })
                print(string.format('N5WOLF %d t %.2f actor %.1f %.1f %.1f', shots, t, p.x, p.y, p.z))
            end
        end,
    },
}
