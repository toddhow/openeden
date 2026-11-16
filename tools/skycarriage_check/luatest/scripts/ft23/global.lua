-- tools/skycarriage_check (task 0223): puts the player next to the Whiterun carriage driver (CarriageDriverWhiterun,
-- placed at (17590, -12412, -4745), facing him) once the pack has read its data, and logs where the player stands every
-- two seconds (FT23 lines). The conversation, the choice of a destination and the arrival are the real game's: the real input
-- script (Space, clicks) opens the dialogue and the carriage script pays and travels. `cfg.noPlan` leaves the player where
-- the session starts (the map-indoors run).
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')
local map = require('core.skyrimse.map')
local cfg = require('scripts.ft23.cfg')

local DRIVER = 'skyrimse:b79465' -- CarriageDriverWhiterun's base record
local DAY_AT, FIND_AT = 1, 20 -- s: the clock moves to the morning (the driver works by day), then the player goes to him
local t0, lastBeat, advanced, placed, discovered = nil, -10, false, false, false

local function put(player, x, y, z, yaw)
    local ok, cell = pcall(world.getExteriorCell, math.floor(x / 4096), math.floor(y / 4096), player.cell)
    if not ok or cell == nil then cell = player.cell end
    player:teleport(cell, util.vector3(x, y, z), { rotation = util.transform.rotateZ(yaw), onGround = true })
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil or not map.ready() then return end
            t0 = t0 or core.getSimulationTime()
            local t = core.getSimulationTime() - t0
            if t - lastBeat >= 2 then
                lastBeat = t
                local p = player.position
                print(string.format('FT23 t=%.1f pos=(%.0f, %.0f, %.0f) cell=%s name=%s id=%s hour=%.2f', t, p.x, p.y, p.z,
                    tostring(player.cell.worldSpaceId), tostring(player.cell.name), tostring(player.cell.id),
                    (core.getGameTime() / 3600) % 24))
            end
            if cfg.discover and not discovered and t >= 2 then
                discovered = true -- the map's Mutate function: marks the markers round a place as found (the indoors run)
                map.visit(cfg.discover[1], cfg.discover[2], cfg.discover[3])
                print(string.format('FT23 discovered round (%d, %d) of world %d', cfg.discover[2], cfg.discover[3],
                    cfg.discover[1]))
            end
            if not cfg.noPlan and not advanced and t >= DAY_AT then
                advanced = true
                world.advanceTime(cfg.advance)
                print(string.format('FT23 clock advanced by %d hours', cfg.advance))
            end
            if not cfg.noPlan and not placed and t >= FIND_AT then
                -- neutral actors are not Lua actors: look through the cells round the player's
                local pp = player.position
                local cx, cy = math.floor(pp.x / 4096), math.floor(pp.y / 4096)
                for dx = -1, 1 do
                    for dy = -1, 1 do
                        local okCell, cell = pcall(world.getExteriorCell, cx + dx, cy + dy, player.cell)
                        if okCell and cell and not placed then
                            for _, o in ipairs(cell:getAll()) do
                                local okId, id = pcall(function() return o.recordId end)
                                local dd = o.position - pp
                                if math.sqrt(dd.x * dd.x + dd.y * dd.y) < 700 then
                                    print(string.format('FT23 near %s type=%s (%.0f, %.0f, %.0f)', tostring(id),
                                        tostring(o.type), o.position.x, o.position.y, o.position.z))
                                end
                                if okId and id == DRIVER then
                                    placed = true
                                    local d = o.position
                                    -- 130 units west of him, facing him (yaw 0 faces +Y, clockwise)
                                    put(player, d.x - 130, d.y, d.z + 20, math.pi / 2)
                                    print(string.format('FT23 driver at (%.0f, %.0f, %.0f); player placed beside him', d.x,
                                        d.y, d.z))
                                    break
                                end
                            end
                        end
                    end
                end
                if not placed then
                    placed = true
                    put(player, cfg.x, cfg.y, cfg.z, cfg.yaw)
                    print('FT23 driver not active; placed at the default spot')
                end
            end
        end,
    },
}
