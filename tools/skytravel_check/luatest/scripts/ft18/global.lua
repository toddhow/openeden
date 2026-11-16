-- tools/skytravel_check (task 0218): walks the player to a marker to discover it and back (teleports on simulation time),
-- logs where the player stands every two seconds (FT18 lines) and, when asked, enables nothing else: the travel itself is
-- done by the real world map window (clicks through OPENEDEN_RMLUI_INPUT_SCRIPT).
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')
local map = require('core.skyrimse.map')
local cfg = require('scripts.ft18.cfg')

-- { time (s after the pack is ready), x, y, yaw }: yaw 0 faces +Y, -pi/2 faces west.
local PLAN = {
    { 3, 30900, -17700, 0 },       -- next to Honningbrew Meadery's marker (30930, -17711): discovers it
    { 6, 25670, -7633, -math.pi / 2 }, -- Whiterun's street, as the map check starts
}
if cfg.noPlan then PLAN = {} end
local GATHER_AT = 20 -- s: with cfg.gather every active actor within 6000 units is put in a ring 300 units from the player
local gathered = false

local t0, nextStep, lastBeat = nil, 1, -10

local function put(player, x, y, yaw)
    local ok, cell = pcall(world.getExteriorCell, math.floor(x / 4096), math.floor(y / 4096), player.cell)
    if not ok or cell == nil then cell = player.cell end
    player:teleport(cell, util.vector3(x, y, -3380), { rotation = util.transform.rotateZ(yaw), onGround = true })
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
                print(string.format('FT18 t=%.1f pos=(%.0f, %.0f, %.0f) cell=%s hour=%.2f', t, p.x, p.y, p.z,
                    tostring(player.cell.worldSpaceId), (core.getGameTime() / 3600) % 24))
            end
            if cfg.gather and not gathered and t >= GATHER_AT then
                gathered = true
                local p, n = player.position, 0
                local cx, cy = math.floor(p.x / 4096), math.floor(p.y / 4096)
                for dx = -1, 1 do
                    for dy = -1, 1 do
                        local ok, cell = pcall(world.getExteriorCell, cx + dx, cy + dy, player.cell)
                        if ok and cell then
                            for _, o in ipairs(cell:getAll()) do
                                local okId, id = pcall(function() return o.recordId end)
                                if okId and id == cfg.gather then
                                    n = n + 1
                                    pcall(function()
                                        o:teleport(player.cell, util.vector3(p.x + 300, p.y + 100 * n, p.z + 20))
                                    end)
                                end
                            end
                        end
                    end
                end
                print('FT18 gathered ' .. n .. ' actors of ' .. cfg.gather)
            end
            local step = PLAN[nextStep]
            if step and t >= step[1] then
                nextStep = nextStep + 1
                put(player, step[2], step[3], step[4])
                print(string.format('FT18 step %d to (%d, %d)', nextStep - 1, step[2], step[3]))
            end
        end,
    },
}
