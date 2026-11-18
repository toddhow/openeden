-- tools/skydungeon_check (task 0289): the global half. cfg.plan is a list of { seconds, what, ... } run once each when the
-- simulation time since the first update reaches the seconds: 'go x y z yaw', 'act x y z radius [record id]' (activates the
-- object nearest to the point, of that record when one is given, as the player's key does), 'shot cx cy cz tx ty tz' (asks the player script for a picture),
-- 'trace tag'. Every line starts with DNG and holds no wall clock.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')
local cfg = require('scripts.dng.cfg')

local t0, r0 = nil, nil
local done = {}

local function nearest(player, x, y, z, radius, record)
    local best, bestD = nil, radius * radius
    for _, o in ipairs(player.cell:getAll()) do
        local ok, p = pcall(function() return o.position end)
        local okId, id = pcall(function() return tostring(o.recordId) end)
        if ok and p and okId and (record == nil or id == record) then
            local dx, dy, dz = p.x - x, p.y - y, p.z - z
            local d = dx * dx + dy * dy + dz * dz
            if d <= bestD then best, bestD = o, d end
        end
    end
    return best
end

local function trace(tag, player)
    local p = player.position
    print(string.format('DNG %s cell=%s player=%.0f,%.0f,%.0f', tag, tostring(player.cell.name), p.x, p.y, p.z))
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            -- the plan starts once the pack's scripts are attached (a few real seconds after the cell is built); it then
            -- runs on simulation time
            r0 = r0 or core.getRealTime()
            if t0 == nil and core.getRealTime() - r0 >= 8 then t0 = core.getSimulationTime() end
            if t0 == nil then return end
            local t = core.getSimulationTime() - t0
            for index, step in ipairs(cfg.plan) do
                if not done[index] and t >= step[1] then
                    done[index] = true
                    local what = step[2]
                    if what == 'go' then
                        trace('go', player)
                        player:teleport(player.cell, util.vector3(step[3], step[4], step[5]),
                            { rotation = util.transform.rotateZ(step[6] or 0) })
                    elseif what == 'act' then
                        local o = nearest(player, step[3], step[4], step[5], step[6], step[7])
                        if o ~= nil then
                            print(string.format('DNG act %s at %.0f,%.0f,%.0f', tostring(o.recordId), o.position.x,
                                o.position.y, o.position.z))
                            o:activateBy(player)
                        else
                            print(string.format('DNG act none near %.0f,%.0f,%.0f', step[3], step[4], step[5]))
                        end
                    elseif what == 'shot' then
                        player:sendEvent('DNGShot', { from = util.vector3(step[3], step[4], step[5]),
                            to = util.vector3(step[6], step[7], step[8]) })
                    elseif what == 'trace' then
                        trace(step[3] or 'trace', player)
                    end
                end
            end
        end,
    },
}
