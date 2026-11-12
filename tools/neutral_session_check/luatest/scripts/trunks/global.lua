-- tools/neutral_session_check (task 0136, skinned trunks): leaves the Bannered Mare and looks for a placed tree whose model has a
-- BSTreeNode (the 35 TREE records of Skyrim.esm below: aspens and pines, whose trunk and branches are skinned shapes), hopping out from
-- the door in rings of 12,288 units; then puts a static camera 500 units from the one with most neighbours, 250 up, and takes four
-- pictures 0.5 s apart (use at least 20000 frames and CHECK_TIMEOUT=600). Real seconds.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

-- The 35 TREE records of Skyrim.esm whose model has a BSTreeNode (the engine's record id is skyrimse:b<decimal form id).
local TREE_IDS = { 980389,980388,980387,980386,980385,980384,980383,980382,980381,980380,758398,758397,758396,758395,758394,483659,483658,444885,444884,436710,381659,381658,381657,381656,381655,376946,376945,376944,376943,376942,332070,326576,307222,100866,77933 }
local isTree = {}
for _, id in ipairs(TREE_IDS) do isTree['skyrimse:b' .. tostring(id)] = true end

local start = nil
local step = 0
local nextAt = 0
local shots = 0
local lookYaw = 0
local nextSearch = 14
local hops = 0
local home = nil
local lookPos = nil
local lookPitch = 0

local function nearestDoor(cell, base, near)
    local best, bestD = nil, math.huge
    for _, obj in ipairs(cell:getAll()) do
        local ok, id = pcall(function() return obj.recordId end)
        if ok and id == base then
            local p = obj.position
            local d = (p.x - near[1]) ^ 2 + (p.y - near[2]) ^ 2 + (p.z - near[3]) ^ 2
            if d < bestD then best, bestD = obj, d end
        end
    end
    return best
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            start = start or core.getRealTime()
            local t = core.getRealTime() - start
            if step == 0 and t >= 1 then
                step = 1
                pcall(function() world.mwscript.getGlobalVariables(player).gamehour = 12 end)
            elseif step == 1 and t >= 6 then
                step = 2
                local door = nearestDoor(player.cell, 'skyrimse:b152263', { -128, -1152, 120 })
                if door == nil then print('TRUNKS door not found') else door:activateBy(player) end
            elseif step == 2 and t >= nextSearch then
                step = 3
                                local trees = {}
                local p = player.position
                for _, obj in ipairs(player.cell:getAll()) do
                    local ok, id = pcall(function() return obj.recordId end)
                    if ok and isTree[id] then trees[#trees + 1] = obj end
                end
                -- the tree with the most other trees within 1200 units (a grove, not a lone town tree), at least 1500 away
                local best, bestN, bestD, count = nil, -1, 0, #trees
                for _, a in ipairs(trees) do
                    local d = (a.position - p):length()
                    if d >= 700 then
                        local n = 0
                        for _, b in ipairs(trees) do
                            if b ~= a and (a.position - b.position):length() < 1200 then n = n + 1 end
                        end
                        if n > bestN then best, bestN, bestD = a, n, d end
                    end
                end
                print(string.format('TRUNKS %d tree references in the cell, the chosen one has %d neighbours and is %.0f units away', count, bestN, bestD))
                if best == nil then
                    -- none in view: hop to the next point of a spiral of rings of 12288 units round the first position
                    home = home or p
                    hops = hops + 1
                    if hops > 48 then print('TRUNKS no tree with a tree node found') step = 9 return end
                    local ring = math.ceil(hops / 8)
                    local k = (hops - 1) % 8
                    local angle = k * math.pi / 4
                    local target = util.vector3(home.x + math.cos(angle) * ring * 12288, home.y + math.sin(angle) * ring * 12288, home.z)
                    print(string.format('TRUNKS none here, hop %d to ring %d direction %d', hops, ring, k))
                    player:teleport(player.cell, target, { onGround = true })
                    step = 2
                    nextSearch = t + 5
                    return
                end
                local tp = best.position
                -- stand on the side of the tree away from its neighbours, with a static camera (it flies through the hill)
                local cx, cy, cn = 0, 0, 0
                for _, o in ipairs(trees) do
                    if o ~= best and (o.position - tp):length() < 1200 then cx, cy, cn = cx + o.position.x, cy + o.position.y, cn + 1 end
                end
                local dir = util.vector3(p.x - tp.x, p.y - tp.y, 0):normalize()
                if cn > 0 then dir = util.vector3(tp.x - cx / cn, tp.y - cy / cn, 0):normalize() end
                local cam = util.vector3(tp.x + dir.x * 500, tp.y + dir.y * 500, tp.z + 250)
                lookYaw = math.atan2(dir.x, -dir.y) -- OpenMW: forward is (-sin yaw, cos yaw)
                lookPos = cam
                lookPitch = math.atan2(250 - 150, 500) -- positive looks down
                print(string.format('TRUNKS camera at %.0f,%.0f,%.0f yaw %.2f pitch %.2f, tree %s at %.0f,%.0f,%.0f', cam.x, cam.y, cam.z, lookYaw, lookPitch, tostring(best.recordId), tp.x, tp.y, tp.z))
                pcall(function() world.mwscript.getGlobalVariables(player).gamehour = 12 end)
                nextAt = t + 1
            elseif step == 3 and t >= nextAt then
                step = 4
                player:sendEvent('TreeLook', { pitch = lookPitch, yaw = lookYaw, pos = lookPos })
                nextAt = t + 2
            elseif step == 4 and t >= nextAt then
                shots = shots + 1
                player:sendEvent('TreeShot', shots)
                nextAt = t + 0.5
                if shots >= 4 then step = 5 end
            end
        end,
    },
}
