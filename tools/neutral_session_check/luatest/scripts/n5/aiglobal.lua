-- tools/neutral_session_check (task 0190, combat stage S5-9): the global half of "n5ai". The player stands DIST units east
-- of the target actor (default Hulda, b80803) and is kept turned to face it (a teleport to the same spot with the yaw
-- towards the actor, every frame), so the player's own swings (SETTINGS_EXTRA: combat test player swing = N)
-- land when the actor is in front. A picture is taken every EVERY seconds of simulation time for WINDOW seconds. Prints
-- "N5AI <t> x y z" for the actor's position. cfg is written by check.sh from AI_DIST, AI_EVERY, AI_WINDOW, AI_TARGET.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

local cfg = require('scripts.n5.aicfg')
local START = cfg.start or 0.3
local start, placed, nextShot, nextAim, shots = nil, nil, 0, 0, 0
local px, py, pz

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
            start = start or core.getSimulationTime()
            local t = core.getSimulationTime() - start
            if t < START then return end
            local actor = find(player.cell, cfg.target)
            if actor == nil then return end
            local p = actor.position
            if not placed then
                placed = t
                px, py, pz = p.x + cfg.dist, p.y, p.z + 2
                nextShot = t + 1
            end
            do -- every frame: the yaw then depends only on where the actor is, not on when the pack's clock started
                local yaw = math.atan2(p.x - px, p.y - py)
                player:teleport(player.cell, util.vector3(px, py, pz), { rotation = util.transform.rotateZ(yaw) })
            end
            if t >= nextShot and t - placed <= cfg.window then
                nextShot = t + cfg.every
                shots = shots + 1
                if cfg.cam then
                    player:sendEvent('N5AIShot', { x = p.x + cfg.cam[1], y = p.y + cfg.cam[2], z = p.z + cfg.cam[3],
                        lx = p.x, ly = p.y, lz = p.z + 60 })
                else
                    player:sendEvent('N5Shot')
                end
                print(string.format('N5AI %d t %.2f actor %.1f %.1f %.1f', shots, t, p.x, p.y, p.z))
            end
        end,
    },
}
