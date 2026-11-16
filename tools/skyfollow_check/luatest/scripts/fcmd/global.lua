-- tools/skyfollow_check (task 0239): the global half. Every FCMD_EVERY updates it logs where the player and the target
-- actor stand (FCMD lines, simulation time and distance, no wall clock), starts the conversation with the target at the
-- updates the plan names ("talk"), and moves the player (teleport inside the cell) at the others ("go x y z"). The
-- clicks and screenshots come from OPENEDEN_RMLUI_INPUT_SCRIPT. The plan is cfg.plan, a list of { update, what, ... }.
local core = require('openmw.core')
local world = require('openmw.world')
local types = require('openmw.types')
local util = require('openmw.util')
local storage = require('openmw.storage')
local cfg = require('scripts.fcmd.cfg')

local updates = 0
local target = nil

local function find(cell)
    for _, obj in ipairs(cell:getAll()) do
        local ok, id = pcall(function() return obj.recordId end)
        if ok and id == cfg.target then return obj end
    end
    return nil
end

local function line(tag)
    local player = world.players[1]
    local p = player.position
    local text = string.format('FCMD %s u=%d t=%.2f player=%.0f,%.0f,%.0f', tag, updates, core.getSimulationTime(), p.x, p.y, p.z)
    if target ~= nil and target:isValid() then
        local a = target.position
        local dx, dy = a.x - p.x, a.y - p.y
        text = text .. string.format(' actor=%.0f,%.0f,%.0f dist=%.0f', a.x, a.y, a.z, math.sqrt(dx * dx + dy * dy))
    end
    print(text)
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            updates = updates + 1
            if target == nil or not target:isValid() then target = find(player.cell) end
            if updates % cfg.every == 0 and updates >= cfg.from then line('sample') end
            for _, step in ipairs(cfg.plan) do
                if step[1] == updates then
                    if step[2] == 'talk' then
                        line('talk')
                        if target ~= nil then target:activateBy(player) end
                    elseif step[2] == 'go' then
                        line('go')
                        player:teleport(player.cell, util.vector3(step[3], step[4], step[5]),
                            { rotation = util.transform.rotateZ(step[6] or 0) })
                    elseif step[2] == 'rel' then
                        -- move the player by (dx, dy) on the ground: the land height there (or his own z) plus a little
                        line('rel')
                        local p = player.position
                        local nx, ny = p.x + step[3], p.y + step[4]
                        local z = p.z
                        local ok, h = pcall(function() return core.land.getHeightAt(util.vector3(nx, ny, 0), player.cell) end)
                        if ok and h ~= nil then z = h + 8 end
                        player:teleport(player.cell, util.vector3(nx, ny, z), { rotation = util.transform.rotateZ(step[5] or 0) })
                    elseif step[2] == 'doors' then
                        for _, o in ipairs(player.cell:getAll()) do
                            local ok, isDoor = pcall(function() return types.Door.objectIsInstance(o) end)
                            if ok and isDoor then
                                print(string.format('FCMD door %s at %.0f,%.0f,%.0f', tostring(o.recordId), o.position.x, o.position.y, o.position.z))
                            end
                        end
                    elseif step[2] == 'open' then
                        local best, bestD = nil, 1e12
                        for _, o in ipairs(player.cell:getAll()) do
                            if tostring(o.recordId) == step[3] then
                                local dx, dy = o.position.x - step[4], o.position.y - step[5]
                                local d = dx * dx + dy * dy
                                if d < bestD then best, bestD = o, d end
                            end
                        end
                        if best ~= nil then
                            line(string.format('open %s at %.0f,%.0f', step[3], best.position.x, best.position.y))
                            best:activateBy(player)
                        end
                    elseif step[2] == 'save' then
                        line('save')
                        storage.globalSection('FCMD'):set('saveNow', true)
                    elseif step[2] == 'note' then
                        line('note ' .. tostring(step[3]))
                    end
                end
            end
        end,
    },
}
