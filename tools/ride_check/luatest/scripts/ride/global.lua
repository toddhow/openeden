-- tools/ride_check (task 0251): the global half of the riding scenario. Simulation-time steps from cfg.lua:
--   cfg.horse = 'skyrimse:b<base>' (record id of the horse; empty: the nearest actor whose record id is cfg.match),
--   cfg.steps = { { t = 5, do = 'stand', x = , y = , yaw = }, { t = 7, do = 'activate' }, { t = 12, do = 'shot' }, ... }
-- "stand" puts the player at x,y (z from the horse) facing yaw; "activate" activates the horse as the key does;
-- "shot" asks the player script for a screenshot; "log" prints RIDE lines every second.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')

local cfg = require('scripts.ride.cfg')
local t0, nextLog, nextStep = nil, 0, 1
local horse = nil
local seenDoors, seenTypes = {}, {}
-- task 0255: the autopilot. While `auto` is set the player's yaw (the horse's heading) is steered every half second: when the
-- horse covered less than `auto.min` units in that time (a wall, a drop) the heading turns by `auto.turn` radians.
local auto = nil
local autoNext, autoLast = 0, nil
local autoYaw = 0
local waypoints, wpIndex = nil, 1
local lastWp, wpCheck, stuckCount, skew, autoArmedWp = nil, 0, 0, 0, false
local routes, routeIndex, routeAsked = nil, 0, false -- a list of far points: the next one is asked when a route ends
local autoArmed = false -- steering starts once the horse has moved at all

local function findHorse(player)
    local best, bestD = nil, math.huge
    for _, o in ipairs(player.cell:getAll()) do
        local ok, id = pcall(function() return o.recordId end)
        if ok and ((cfg.horse and id == cfg.horse) or (cfg.match and cfg.horse == nil and string.find(id, cfg.match, 1, true))) then
            local d = (o.position - player.position):length()
            if d < bestD then best, bestD = o, d end
        end
    end
    return best
end

local function say(player, t)
    local p = player.position
    local text = string.format('RIDE t=%.1f player %.0f,%.0f,%.0f', t, p.x, p.y, p.z)
    if horse then
        local ok, h = pcall(function() return horse.position end)
        if ok then text = text .. string.format(' | horse %.0f,%.0f,%.0f', h.x, h.y, h.z) end
    end
    print(text)
end

return {
    eventHandlers = {
        RideWaypoints = function(data) waypoints = data.pts; wpIndex = 1; routeAsked = false end,
    },
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            pcall(function() world.mwscript.getGlobalVariables(player).gamehour = cfg.hour or 13 end)
            local t = core.getSimulationTime()
            if t0 == nil then t0 = t end
            t = t - t0
            if horse == nil or not pcall(function() return horse.position end) then horse = findHorse(player) end
            if t >= nextLog then
                nextLog = nextLog + 1
                say(player, t)
                if cfg.doorscan then
                    -- RIDEDOOR lines: each door of the player's cell once, with its distance from the horse
                    local types = require('openmw.types')
                    for _, d in ipairs(player.cell:getAll()) do
                        local oky, ty = pcall(function() return tostring(d.type) end)
                        if oky and not seenTypes[ty] then
                            seenTypes[ty] = true
                            print(string.format('RIDETYPE %s example %s', ty, d.recordId))
                        end
                        if oky and string.find(string.lower(ty), 'door', 1, true) and not seenDoors[d.id] then
                            seenDoors[d.id] = true
                            local okt, tp = pcall(function() return types.Door.isTeleport(d) end)
                            print(string.format('RIDEDOOR %s id %s at %.0f,%.0f,%.0f teleport %s t=%.0f', d.recordId, tostring(d.id), d.position.x, d.position.y, d.position.z, tostring(okt and tp), t))
                        end
                    end
                end
            end
            if routes and horse and (waypoints == nil or wpIndex > #waypoints) and not routeAsked and routeIndex < #routes then
                routeIndex = routeIndex + 1
                routeAsked = true
                local r = routes[routeIndex]
                player:sendEvent('RideRoute', { x = r[1], y = r[2], z = r[3] or player.position.z })
            end
            if waypoints and horse and wpIndex <= #waypoints then
                -- steer along the route: aim the heading (the player's yaw) at the next waypoint; one is reached within 250
                local okh, h = pcall(function() return horse.position end)
                if okh then
                    local w = waypoints[wpIndex]
                    local dx, dy = w.x - h.x, w.y - h.y
                    if dx * dx + dy * dy < 90 * 90 then
                        wpIndex = wpIndex + 1
                        w = waypoints[wpIndex]
                    end
                    if w then
                        -- a horse that did not move in the last half second tries a heading beside the aim, alternating
                        local moved = lastWp and math.sqrt((h.x - lastWp.x) ^ 2 + (h.y - lastWp.y) ^ 2) or 999
                        if t >= wpCheck then
                            wpCheck = t + 0.5
                            if lastWp ~= nil and moved < 40 and autoArmedWp then
                                stuckCount = stuckCount + 1
                                skew = ((stuckCount % 2 == 0) and 1 or -1) * 0.35 * math.min(6, 1 + math.floor(stuckCount / 2))
                                print(string.format('RIDE stuck t=%.1f at %.0f,%.0f skew %.2f', t, h.x, h.y, skew))
                            elseif moved >= 150 then
                                skew = 0
                            end
                            if moved >= 40 then autoArmedWp = true end
                            lastWp = h
                        end
                        local yaw = math.atan2(w.x - h.x, w.y - h.y) + skew
                        player:teleport(player.cell, player.position, { rotation = util.transform.rotateZ(yaw) })
                    end
                end
            end
            if auto and horse and t >= autoNext then
                local ok, h = pcall(function() return horse.position end)
                if ok then
                    if autoLast then
                        local d = math.sqrt((h.x - autoLast.x) ^ 2 + (h.y - autoLast.y) ^ 2)
                        if d >= auto.min then autoArmed = true end
                        if autoArmed and d < auto.min then
                            autoYaw = autoYaw + auto.turn
                            player:teleport(player.cell, player.position, { rotation = util.transform.rotateZ(autoYaw) })
                            print(string.format('RIDE steer t=%.1f moved %.0f yaw %.2f', t, d, autoYaw))
                        end
                    end
                    autoLast = h
                end
                autoNext = t + 0.5
            end
            local step = cfg.steps and cfg.steps[nextStep]
            if step and t >= step.t then
                nextStep = nextStep + 1
                if step['do'] == 'stand' then
                    local z = horse and horse.position.z or player.position.z
                    player:teleport(player.cell, util.vector3(step.x, step.y, z), { rotation = util.transform.rotateZ(step.yaw or 0) })
                elseif step['do'] == 'beside' and horse then
                    -- the player stands step.dx, step.dy from the horse, looking at it (or along step.yaw)
                    local h = horse.position
                    local x, y = h.x + (step.dx or 0), h.y + (step.dy or 0)
                    local yaw = step.yaw or math.atan2(h.x - x, h.y - y)
                    player:teleport(player.cell, util.vector3(x, y, h.z), { rotation = util.transform.rotateZ(yaw) })
                elseif step['do'] == 'activate' then
                    if horse then horse:activateBy(player) else print('RIDE no horse to activate') end
                elseif step['do'] == 'doors' then
                    -- RIDEDOOR lines: every teleport door of the loaded cells (to find a road's end)
                    local types = require('openmw.types')
                    for gx = 2, 9 do for gy = -8, 1 do
                      local okc, cell = pcall(function() return world.getExteriorCell(gx, gy) end)
                      if not okc then print('RIDEDOOR cell error ' .. tostring(cell)) end
                      if okc and cell then
                        for _, d in ipairs(cell:getAll()) do
                            local okd, tp = pcall(function() return types.Door.objectIsInstance(d) end)
                            if okd and tp then
                                local near = horse and (d.position - horse.position):length() or -1
                                print(string.format('RIDEDOOR %s %.0f,%.0f,%.0f dist %.0f', d.recordId, d.position.x, d.position.y, d.position.z, near))
                            end
                        end
                      end
                    end end
                elseif step['do'] == 'save' then
                    require('openmw.storage').globalSection('RIDESAVE'):set('saveNow', true)
                    print('RIDE save asked at ' .. string.format('%.1f', t) .. ' player ' .. tostring(player.position))
                elseif step['do'] == 'waypoints' then
                    -- a fixed route (x, y pairs): no navigator query at run time, so a run depends on simulation time only
                    waypoints = {}
                    for _, p in ipairs(step.list) do waypoints[#waypoints + 1] = { x = p[1], y = p[2] } end
                    wpIndex = 1
                elseif step['do'] == 'go' then
                    player:sendEvent('RideGo', step.on ~= false)
                elseif step['do'] == 'routes' then
                    routes = step.list
                elseif step['do'] == 'route' then
                    player:sendEvent('RideRoute', { x = step.x, y = step.y, z = step.z or player.position.z })
                elseif step['do'] == 'auto' then
                    auto = { min = step.min or 100, turn = step.turn or 0.8 }
                    autoYaw = step.yaw or 0
                    autoNext = t + 0.5
                elseif step['do'] == 'run' then
                    player:sendEvent('RideRun', step.on and true or false)
                elseif step['do'] == 'shot' then
                    player:sendEvent('RideShot', step.name or 'shot')
                elseif step['do'] == 'list' then
                    for _, o in ipairs(world.activeActors) do
                        local ok2, id = pcall(function() return o.recordId end)
                        if ok2 and (step.only == nil or id == step.only) then
                            print(string.format('RIDELIST %s %.0f,%.0f,%.0f', id, o.position.x, o.position.y, o.position.z))
                        end
                    end
                end
            end
        end,
    },
}
