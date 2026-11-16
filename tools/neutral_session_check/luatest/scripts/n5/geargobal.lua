-- tools/neutral_session_check (task 0214, real weapons): the global half of "n5gear". After START seconds of simulation time it
-- looks through the exterior cells round the player for the actor whose record id is cfg.target (or tries to create it from
-- cfg.create), puts it cfg.dist units east of the player, turns both to face each other (every frame), and takes a picture
-- every cfg.every seconds from a static camera beside the pair (cfg.cam = x, y, z relative to the player). Prints "N5GEAR"
-- lines. cfg is written by check.sh from GEAR_*.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

local cfg = require('scripts.n5.gearcfg')
local start, actor, nextShot, shots, tried = nil, nil, 0, 0, false

local function find(player)
    local cx, cy = math.floor(player.position.x / 4096), math.floor(player.position.y / 4096)
    for dx = -2, 2 do
        for dy = -2, 2 do
            local ok, cell = pcall(world.getExteriorCell, cx + dx, cy + dy, player.cell)
            if ok and cell ~= nil then
                for _, o in ipairs(cell:getAll()) do
                    local okId, id = pcall(function() return o.recordId end)
                    if okId and id == cfg.target then return o end
                end
            end
        end
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
            if t < cfg.start then return end
            if actor == nil then
                if tried then return end
                tried = true
                if cfg.create ~= '' then
                    local ok, o = pcall(world.createObject, cfg.create)
                    print('N5GEAR create ' .. cfg.create .. ' ' .. tostring(ok) .. ' ' .. tostring(o))
                    if ok then actor = o end
                else
                    actor = find(player)
                    print('N5GEAR found ' .. tostring(actor ~= nil) .. ' ' .. tostring(actor and actor.id))
                end
                if actor == nil then return end
                local p = player.position
                actor:teleport(player.cell, util.vector3(p.x + cfg.dist, p.y, p.z + 2),
                    { rotation = util.transform.rotateZ(-math.pi / 2) })
                nextShot = t + 1
                return
            end
            local p, a = player.position, actor.position
            player:teleport(player.cell, p, { rotation = util.transform.rotateZ(math.atan2(a.x - p.x, a.y - p.y)) })
            if t >= nextShot and shots < cfg.maxShots then
                nextShot = t + cfg.every
                shots = shots + 1
                player:sendEvent('N5AIShot', { x = p.x + cfg.cam[1], y = p.y + cfg.cam[2], z = p.z + cfg.cam[3],
                    lx = (p.x + a.x) / 2, ly = (p.y + a.y) / 2, lz = p.z + 60 })
                print(string.format('N5GEAR %d t %.2f player %.0f %.0f actor %.0f %.0f', shots, t, p.x, p.y, a.x, a.y))
            end
        end,
    },
}
