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
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            pcall(function() world.mwscript.getGlobalVariables(player).gamehour = cfg.hour or 13 end)
            local t = core.getSimulationTime()
            if t0 == nil then t0 = t end
            t = t - t0
            if horse == nil or not pcall(function() return horse.position end) then horse = findHorse(player) end
            if t >= nextLog then nextLog = nextLog + 1; say(player, t) end
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
