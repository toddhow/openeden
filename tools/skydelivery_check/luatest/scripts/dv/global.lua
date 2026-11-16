-- tools/skydelivery_check (task 0242): the global half. The player stands cfg.dist units east of the first actor (cfg.tags[1],
-- Hulda) facing it, and is kept there (a teleport every frame); the other actors of cfg.tags stand at cfg.offsets from the first
-- one's spot, held there by a teleport every frame, so an explosion finds them where the log says. In mode 'rune' only Hulda is
-- placed, cfg.parkAway units west of the player, and held until cfg.release seconds, after which she is left alone (a hostile
-- Hulda walks to the player, over the rune). Pictures every cfg.every seconds between cfg.shotFrom and cfg.shotTo. Prints DV lines.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')
local cfg = require('scripts.dv.cfg')

local function find(cell, tag)
    for _, o in ipairs(cell:getAll()) do
        local ok, id = pcall(function() return o.recordId end)
        if ok and id ~= nil and string.find(id, tag, 1, true) then return o end
    end
    return nil
end

local start, placed, nextShot, shots = nil, nil, 0, 0
local spot, px, py, pz
local actors = {}
local releasedLogged = false
local lastSecond = -1

local function faceYaw(fx, fy, tx, ty) return math.atan2(tx - fx, ty - fy) end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            start = start or core.getSimulationTime()
            local t = core.getSimulationTime() - start
            if t < cfg.start then return end
            local first = find(player.cell, cfg.tags[1])
            if first == nil then return end
            if not placed then
                placed = t
                spot = first.position
                px, py, pz = spot.x + cfg.dist, spot.y, spot.z + 2
                for i, tag in ipairs(cfg.tags) do
                    if cfg.mode ~= 'rune' or i == 1 then
                        local a = find(player.cell, tag)
                        if a ~= nil then
                            actors[#actors + 1] = { obj = a, dx = cfg.offsets[i][1], dy = cfg.offsets[i][2], first = (i == 1) }
                        end
                    end
                end
                if cfg.mode == 'rune' and actors[1] ~= nil then
                    actors[1].dx = cfg.dist - cfg.parkAway -- west of the player, parkAway units from him
                end
                print(string.format('DV placed t %.2f spot %.0f %.0f %.0f player %.0f %.0f actors %d', t, spot.x, spot.y, spot.z, px, py, #actors))
            end
            if math.floor(t) ~= lastSecond then
                lastSecond = math.floor(t)
                if lastSecond % 5 == 0 then print(string.format('DV t %d', lastSecond)) end
            end
            local yaw = faceYaw(px, py, spot.x, spot.y)
            player:teleport(player.cell, util.vector3(px, py, pz), { rotation = util.transform.rotateZ(yaw) })
            local released = cfg.release > 0 and t >= cfg.release
            for _, a in ipairs(actors) do
                if not (released and a.first) then
                    local tx, ty = spot.x + a.dx, spot.y + a.dy
                    a.obj:teleport(player.cell, util.vector3(tx, ty, spot.z), { rotation = util.transform.rotateZ(faceYaw(tx, ty, px, py)) })
                end
            end
            if released and not releasedLogged then
                releasedLogged = true
                print(string.format('DV released at t %.2f', t))
            end
            if cfg.shotTo > 0 and t >= cfg.shotFrom and t <= cfg.shotTo and t >= nextShot then
                nextShot = t + cfg.every
                shots = shots + 1
                player:sendEvent('DVShot')
                local h = actors[1] and actors[1].obj.position
                print(string.format('DV shot %d t %.2f first actor %.0f %.0f %.0f', shots, t, h and h.x or 0, h and h.y or 0, h and h.z or 0))
            end
        end,
    },
}
