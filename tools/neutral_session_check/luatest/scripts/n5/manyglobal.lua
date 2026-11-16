-- tools/neutral_session_check (task 0192, combat on many actors): the global half of "n5many". After 3 s of simulation time it
-- gathers the Whiterun guards (the Whiterun guard and soldier base records in BASES; Lua cannot tell an actor from a static in a neutral world)
-- of the player's cell and the 24 round it (the whole active grid), parks every one far away (PARK_RADIUS from the player), then puts
-- HOSTILE of them on rings round the player (they are the nearest, so the pack's crowd rule picks them) and 2 * DUELS
-- of them in pairs on a circle of 300 units, (the player script raises the player's health so that the fight can run for minutes).
-- MANY_* values come from check.sh through scripts/n5/manycfg.lua. Prints "MANY ..." lines.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

local cfg = require('scripts.n5.manycfg')
local BASES = {}
for _, b in ipairs({ 1048812, 1048819, 1082006, 1082008, 1084810, 1084811, 1106623, 1106629, 856058, 856065, 858501,
    862924, 864588, 864591, 864594, 864596, 864598, 864601, 867279, 867280, 867281, 867282, 867284, 867285, 867286,
    888585, 920951, 541905, 542045, 904384, 1048815, 1048817 }) do BASES['skyrimse:b' .. b] = true end

local start, done, away = nil, false, false

local function gather(player)
    local found, seen = {}, {}
    local cx, cy = math.floor(player.position.x / 4096), math.floor(player.position.y / 4096)
    for dx = -2, 2 do
        for dy = -2, 2 do
            local ok, cell = pcall(world.getExteriorCell, cx + dx, cy + dy, player.cell)
            if ok and cell ~= nil then
                for _, o in ipairs(cell:getAll()) do
                    local okId, id = pcall(function() return o.recordId end)
                    if okId and BASES[id] and not seen[o.id] then
                        seen[o.id] = true
                        found[#found + 1] = o
                    end
                end
            end
        end
    end
    table.sort(found, function(a, b) return tostring(a.id) < tostring(b.id) end)
    return found
end

local function put(o, cell, x, y, z, faceX, faceY)
    local yaw = math.atan2(faceX - x, faceY - y)
    o:teleport(cell, util.vector3(x, y, z), { rotation = util.transform.rotateZ(yaw) })
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            -- simulation time, not real time: with a fixed frame time the run is then a function of the frame count
            start = start or core.getSimulationTime()
            if done then
                -- MANY_AWAY (units, 0 = never) at MANY_AWAY_AT (simulation seconds): the player walks off west, so the
                -- fighters are far from it (the process levels of task 0154 look at the distance to the player)
                if cfg.away > 0 and not away and core.getSimulationTime() - start >= cfg.awayAt then
                    away = true
                    local q = player.position
                    player:teleport(player.cell, util.vector3(q.x - cfg.away, q.y, q.z + 300), { onGround = true })
                    print(string.format('MANY player moved %.0f units away', cfg.away))
                end
                return
            end
            if core.getSimulationTime() - start < 3 then return end
            done = true
            pcall(function() world.mwscript.getGlobalVariables(player).gamehour = 12 end)
            local p = player.position
            local list = gather(player)
            print(string.format('MANY gathered %d guards round the player at %.0f,%.0f,%.0f', #list, p.x, p.y, p.z))
            -- one teleport per object (a second one in the same frame is refused): destinations first
            local dest = {}
            for i, o in ipairs(list) do
                local a = (i - 1) / math.max(1, #list) * 2 * math.pi
                dest[i] = { p.x + math.cos(a) * cfg.park, p.y + math.sin(a) * cfg.park, p.z, p.x, p.y }
            end
            local n = 0
            -- hostile: rings of 12 (radius 60, 80, 100 ... 140), every one within reach of the player (reach 141 + radius
            -- 22 - 4 = 159); the capsules of neutral actors do not meet each other, so the rings may overlap
            for k = 0, cfg.hostile - 1 do
                n = n + 1
                if list[n] == nil then break end
                local ring, slot = math.floor(k / 12), k % 12
                local r = 60 + 20 * math.min(ring, 4)
                local a = (slot + 0.5 * (ring % 2)) / 12 * 2 * math.pi
                dest[n] = { p.x + math.cos(a) * r, p.y + math.sin(a) * r, p.z + 2, p.x, p.y }
            end
            -- duels: pair d on a circle of 300 units round the player (8 pairs at most), the two 90 apart along the
            -- circle, facing each other
            for d = 0, cfg.duels - 1 do
                if list[n + 2] == nil then break end
                local a = (d % 8) / 8 * 2 * math.pi + math.pi / 8
                local cx, cy = p.x + math.cos(a) * 300, p.y + math.sin(a) * 300
                local tx, ty = -math.sin(a) * 45, math.cos(a) * 45
                dest[n + 1] = { cx - tx, cy - ty, p.z + 2, cx + tx, cy + ty }
                dest[n + 2] = { cx + tx, cy + ty, p.z + 2, cx - tx, cy - ty }
                n = n + 2
            end
            for i, o in ipairs(list) do
                local d = dest[i]
                put(o, player.cell, d[1], d[2], d[3], d[4], d[5])
            end
            print(string.format('MANY arena built: %d hostile, %d duels', cfg.hostile, cfg.duels))
        end,
    },
}
