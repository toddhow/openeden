-- tools/neutral_session_check (task 0133, Skyrim tree sway): leaves the Bannered Mare by its front door, finds the nearest
-- placed tree (a base form of Skyrim.esm's TREE group) in WhiterunWorld, puts a static camera 800 units from it, 600 up, looking down at it,
-- and takes four pictures 0.5, 1.0, 1.5 and 2.0 real seconds after the first (use at least 2500 frames). Real seconds.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

-- The 154 TREE records of Skyrim.esm (form ids; the engine's record id is skyrimse:b<decimal form id>).
local TREE_IDS = { 1087254,1008831,980389,980388,980387,980386,980385,980384,980383,980382,980381,980380,955942,955941,955940,955939,955938,287191,287177,144913,925390,925389,913429,844034,844033,803766,803760,570820,438712,391898,113469,779238,779233,779232,773960,773959,773951,773950,773949,773948,773946,773940,773939,773287,773286,773285,773284,773283,773281,768339,768338,768337,768336,768335,768334,768333,768332,768331,768330,768329,768328,768327,758398,758397,758396,758395,758394,758264,758256,757587,756343,756342,756341,756340,756339,756338,756337,756336,756335,756334,756333,756332,756331,756330,756329,756328,756327,756326,756325,756324,756322,756313,756311,750530,750528,750526,750524,750522,750520,725464,725463,725462,725461,725460,725459,703845,700048,700047,700046,700045,700043,700041,700039,700037,700035,700033,700031,700027,700026,700025,700021,700020,700019,684841,684829,684828,684819,684818,645797,645796,645795,645794,645793,645792,483659,483658,444885,444884,436710,381659,381658,381657,381656,381655,376946,376945,376944,376943,376942,332070,326576,307222,100866,77933 }
local isTree = {}
for _, id in ipairs(TREE_IDS) do isTree['skyrimse:b' .. tostring(id)] = true end

local start = nil
local step = 0
local nextAt = 0
local shots = 0
local lookYaw = 0
local nextSearch = 14
local hops = 0
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
                if door == nil then print('TREES door not found') else door:activateBy(player) end
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
                    if d >= 1500 then
                        local n = 0
                        for _, b in ipairs(trees) do
                            if b ~= a and (a.position - b.position):length() < 1200 then n = n + 1 end
                        end
                        if n > bestN then best, bestN, bestD = a, n, d end
                    end
                end
                print(string.format('TREES %d tree references in the cell, the chosen one has %d neighbours and is %.0f units away', count, bestN, bestD))
                if best == nil then
                    -- none in this square: hop east (then west) one square at a time and look again
                    hops = hops + 1
                    if hops > 14 then print('TREES no tall tree found') step = 9 return end
                    local dx = (hops <= 7) and 4096 or -4096
                    if hops == 8 then dx = -4096 * 8 end
                    print(string.format('TREES none in this square, hop %d (%.0f)', hops, dx))
                    player:teleport(player.cell, util.vector3(p.x + dx, p.y, p.z), { onGround = true })
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
                local cam = util.vector3(tp.x + dir.x * 800, tp.y + dir.y * 800, tp.z + 600)
                lookYaw = math.atan2(dir.x, -dir.y) -- OpenMW: forward is (-sin yaw, cos yaw)
                lookPos = cam
                lookPitch = math.atan2(600 - 150, 800) -- positive looks down
                print(string.format('TREES camera at %.0f,%.0f,%.0f yaw %.2f pitch %.2f, tree %s at %.0f,%.0f,%.0f', cam.x, cam.y, cam.z, lookYaw, lookPitch, tostring(best.recordId), tp.x, tp.y, tp.z))
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
