-- tools/neutral_session_check (task 0197): the global half of "n5npcfight", NPC against NPC in Whiterun street.
-- After 3 s of simulation time it gathers the Whiterun guards of the street (BASES, like n5many), parks the ones it does not
-- use far away and builds one of two scenes (cfg.mode, from check.sh through scripts/n5/npcfightcfg.lua):
--   guards:   COUNT guards on a ring of 130 units round a point DIST east of the player, the one of ATTACKER (a base
--             number) in the middle; the pack setting `combat test attacker` names it, so it attacks the nearest guard
--             and the others come to help (guards protect their allies).
--   follower: the attacker in the middle, the player 150 units west of it facing it, the follower (base FOLLOWER, which
--             `follower test recruit` recruits) 320 units west.
-- Tells the player script where to put the camera (event NpcFightLook). Prints "NPCFIGHT ..." lines.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

local cfg = require('scripts.n5.npcfightcfg')
local BASES = {}
for _, b in ipairs({ 1048812, 1048819, 1082006, 1082008, 1084810, 1084811, 1106623, 1106629, 856058, 856065, 858501,
    862924, 864588, 864591, 864594, 864596, 864598, 864601, 867279, 867280, 867281, 867282, 867284, 867285, 867286,
    888585, 920951, 541905, 542045, 904384, 1048815, 1048817 }) do BASES['skyrimse:b' .. b] = true end

local start, done = nil, false

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
            if player == nil or done then return end
            start = start or core.getSimulationTime()
            if core.getSimulationTime() - start < 3 then return end
            done = true
            pcall(function() world.mwscript.getGlobalVariables(player).gamehour = 12 end)
            local p = player.position
            local list = gather(player)
            print(string.format('NPCFIGHT gathered %d guards, player at %.0f,%.0f,%.0f', #list, p.x, p.y, p.z))
            local function pick(base)
                for i, o in ipairs(list) do
                    if o.recordId == 'skyrimse:b' .. tostring(base) then return i end
                end
                return nil
            end
            local ia = pick(cfg.attacker)
            if ia == nil then print('NPCFIGHT attacker base not in the street'); ia = 1 end
            local dest = {}
            -- the guards that do not take part stand on rings round the player at least 1,950 units apart (a hostile
            -- one sees 1,800 units), the nearest ring 3,400 away
            local rings, ri, rs = { 3400, 5200, 7000, 8200 }, 1, 0
            for i, o in ipairs(list) do
                local r = rings[ri]
                local per = math.floor(2 * math.pi * r / 1950)
                local a = rs / per * 2 * math.pi
                dest[i] = { p.x + math.cos(a) * r, p.y + math.sin(a) * r, p.z, p.x, p.y }
                rs = rs + 1
                if rs >= per and ri < #rings then ri, rs = ri + 1, 0 end
            end
            local gx, gy = p.x + cfg.dist, p.y
            local lookX, lookY = gx, gy
            if cfg.mode == 'follower' then
                local f = pick(cfg.follower)
                if f == nil then print('NPCFIGHT follower base not in the street'); f = (ia == 1) and 2 or 1 end
                dest[ia] = { gx, gy, p.z + 2, gx - 150, gy, keep = true }
                dest[f] = { gx - 320, gy + 40, p.z + 2, gx, gy, keep = true }
                player:teleport(player.cell, util.vector3(gx - 150, gy, p.z + 2),
                    { rotation = util.transform.rotateZ(math.pi / 2) })
                lookX = gx - 150
            else
                dest[ia] = { gx, gy, p.z + 2, gx + 100, gy, keep = true }
                local n = 0
                for i = 1, #list do
                    if i ~= ia and n < cfg.count - 1 then
                        local a = n / math.max(1, cfg.count - 1) * 2 * math.pi
                        dest[i] = { gx + math.cos(a) * 130, gy + math.sin(a) * 130, p.z + 2, gx, gy, keep = true }
                        n = n + 1
                    end
                end
            end
            local used = {}
            for i in pairs(dest) do used[i] = true end
            for i, o in ipairs(list) do
                if dest[i] ~= nil and (dest[i].keep or cfg.parkMode ~= 'disable') then
                    local d = dest[i]
                    put(o, player.cell, d[1], d[2], d[3], d[4], d[5])
                else
                    local ok = pcall(function() o.enabled = false end)
                    if not ok then print('NPCFIGHT could not disable ' .. tostring(o.recordId)) end
                end
            end
            print(string.format('NPCFIGHT scene %s built, look at %.0f,%.0f', cfg.mode, lookX, lookY))
            player:sendEvent('NpcFightLook', { x = lookX, y = lookY, z = p.z })
        end,
    },
}
