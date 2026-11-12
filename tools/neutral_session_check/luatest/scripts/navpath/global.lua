-- tools/neutral_session_check (task 0134): the timeline of the navmesh check. Real seconds.
--   t=3   position, ring of targets round the start (interior), the scenario queries of the interior
--   then  navmesh debug view on, pictures from three heights
--   then  out of the front door into Whiterun, ring of targets round the door, the scenario queries of the street,
--         navmesh debug view pictures
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

local ok, scen = pcall(require, 'scripts.navpath.scenarios')
if not ok then scen = { interior = {}, street = {} } end

local DOORBASE = 'skyrimse:b152263'
local start = nil
local step = 0
local at = 0
local waiting = false

local function v(t) return util.vector3(t[1], t[2], t[3]) end
local function convert(list)
    local out = {}
    for _, q in ipairs(list) do out[#out + 1] = { name = q.name, from = v(q.from), to = v(q.to), retry = q.retry } end
    return out
end

local function nearestDoor(cell, base, near)
    local best, bestD = nil, math.huge
    for _, obj in ipairs(cell:getAll()) do
        local okId, id = pcall(function() return obj.recordId end)
        if okId and id == base then
            local p = obj.position
            local d = (p.x - near[1]) ^ 2 + (p.y - near[2]) ^ 2 + (p.z - near[3]) ^ 2
            if d < bestD then best, bestD = obj, d end
        end
    end
    return best
end

return {
    eventHandlers = {
        NPDone = function(phase) waiting = false end,
    },
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            start = start or core.getRealTime()
            local t = core.getRealTime() - start
            if t < at or waiting then return end
            if step == 0 then
                step = 1; at = 3
                player:sendEvent('NPPos')
                player:sendEvent('NPRing', { phase = 'interior', radii = { 150, 300, 600 }, retry = 10 }); waiting = true
            elseif step == 1 then
                step = 2; at = t + 1
                player:sendEvent('NPRun', { phase = 'interior-scenario', queries = convert(scen.interior) }); waiting = true
            elseif step == 2 then
                step = 3; at = t + 1
                player:sendEvent('NPScan', { name = 'stair along x at y=-252', x = -1360, y = -252, dx = 1, dy = 0, length = 330, step = 6, zTop = 420 })
                player:sendEvent('NPScan', { name = 'stair across at x=-1200', x = -1200, y = -340, dx = 0, dy = 1, length = 180, step = 6, zTop = 420 })
                player:sendEvent('NPWire')
            elseif step == 3 then
                -- the foot of the stairs, looking up them, navmesh view on
                step = 4; at = t + 3
                player:teleport(player.cell, util.vector3(-1380, -252, 66), { rotation = util.transform.rotateZ(math.pi / 2) })
            elseif step == 4 then
                step = 41; at = t + 1.5
                player:sendEvent('NPLook', 0.25)
                player:sendEvent('NPShot')
            elseif step == 41 then
                step = 42; at = t + 3
                player:teleport(player.cell, util.vector3(-1100, -252, 300), { rotation = util.transform.rotateZ(-math.pi / 2) })
            elseif step == 42 then
                step = 43; at = t + 1.5
                player:sendEvent('NPLook', 0.35)
                player:sendEvent('NPShot')
            elseif step == 43 then
                step = 44; at = t + 1
                player:teleport(player.cell, util.vector3(-256, -304, 70), {})
                player:sendEvent('NPLook', 0.9)
                player:sendEvent('NPShot')
            elseif step == 44 then
                step = 6; at = t + 1.5
                player:sendEvent('NPWire')
                local door = nearestDoor(player.cell, DOORBASE, { -128, -1152, 120 })
                if door == nil then print('NAV door not found') else
                    print('NAV activating the front door')
                    door:activateBy(player)
                end
            elseif step == 6 then
                step = 7; at = t + (scen.fast and 25 or 60)
                -- the exterior needs a while to load and its navmesh to build
            elseif step == 7 then
                -- the porch of the Mare is a pocket against its wall: ring the queries from the street 300 units west
                step = 71; at = t + 20
                player:teleport(player.cell, util.vector3(25379, -7630, -3350), { onGround = true })
            elseif step == 71 then
                step = 8; at = t + 1
                player:sendEvent('NPPos')
                if not scen.fast then
                    player:sendEvent('NPRing', { phase = 'street', radii = { 300, 700 }, retry = 6 }); waiting = true
                end
            elseif step == 8 then
                step = 9; at = t + 1
                player:sendEvent('NPRun', { phase = 'street-scenario', queries = convert(scen.street) }); waiting = true
            elseif step == 9 then
                step = 10; at = t + 1
                player:sendEvent('NPWire')
            elseif step == 10 then
                step = 11; at = t + 1.5
                player:sendEvent('NPLook', 0.6)
                player:sendEvent('NPShot')
            elseif step == 11 then
                step = 12; at = t + 1.5
                player:sendEvent('NPLook', 0.1)
                player:sendEvent('NPShot')
            elseif step == 12 then
                -- from above: the navmesh of the street seen from 1500 units up
                step = 13; at = t + 0.4
                local p = player.position
                player:teleport(player.cell, util.vector3(p.x, p.y, p.z + 1500), {})
                player:sendEvent('NPLook', 1.5)
            elseif step == 13 then
                step = 14; at = t + 0.3
                player:sendEvent('NPShot')
            elseif step == 14 then
                step = 15
                print('NAV done')
            end
        end,
    },
}
