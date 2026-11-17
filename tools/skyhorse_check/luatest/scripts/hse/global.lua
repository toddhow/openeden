-- tools/skyhorse_check (task 0278): a copy of tools/skyhouses_check's global half with a 'mount' step. (task 0261): the global half. cfg.plan is a list of { update, what, ... }: 'talk' (activates the
-- target actor in the player's cell), 'cell name x y z' (moves the player into a cell), 'go x y z' (inside the cell),
-- 'door x y radius' (the nearest object of the door type), 'furn x y radius [text]' (activates the nearest object whose
-- record id contains the text, default cfg.furn; neutral objects are named skyrimse:b<decimal base id>), 'ids text',
-- 'trace tag' (gold, the Breezehome key, the cell), 'save'. Every line starts with HSE and holds no wall clock.
local core = require('openmw.core')
local world = require('openmw.world')
local types = require('openmw.types')
local util = require('openmw.util')
local storage = require('openmw.storage')
local cfg = require('scripts.hse.cfg')
local okInv, inv = pcall(require, 'core.skyrimse.inventory')

local updates = 0
local target = nil

local function find(cell)
    for _, obj in ipairs(cell:getAll()) do
        local ok, id = pcall(function() return obj.recordId end)
        if ok and id == cfg.target then return obj end
    end
    -- task 0278: an exterior's actors are in world.activeActors, not in the cell's list
    for _, obj in ipairs(world.activeActors) do
        local ok, id = pcall(function() return obj.recordId end)
        if ok and id == cfg.target then return obj end
    end
    return nil
end

local function trace(tag)
    local player = world.players[1]
    local p = player.position
    local gold, key = -1, -1
    if okInv and inv.ready() then
        gold = inv.count('player', inv.gold_item())
        key = inv.count('player', cfg.key or '00093b08')
    end
    print(string.format('HSE %s ws=%s u=%d cell=%s player=%.0f,%.0f,%.0f gold=%d key=%d', tag, tostring(player.cell.worldSpaceId), updates,
        tostring(player.cell.name), p.x, p.y, p.z, gold, key))
end

local function nearest(player, x, y, radius, pred)
    local best, bestD = nil, radius * radius
    for _, o in ipairs(player.cell:getAll()) do
        local ok, hit = pcall(pred, o)
        if ok and hit then
            local dx, dy = o.position.x - x, o.position.y - y
            local d = dx * dx + dy * dy
            if d <= bestD then best, bestD = o, d end
        end
    end
    return best
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            updates = updates + 1
            if updates % cfg.every == 0 and updates >= cfg.from then trace('sample') end
            for _, step in ipairs(cfg.plan) do
                if step[1] == updates then
                    if step[2] == 'talk' then
                        trace('talk')
                        if target == nil or not target:isValid() then target = find(player.cell) end
                        if target ~= nil then target:activateBy(player) end
                    elseif step[2] == 'cell' then
                        trace('cell ' .. step[3])
                        player:teleport(step[3], util.vector3(step[4], step[5], step[6]),
                            { rotation = util.transform.rotateZ(step[7] or 0) })
                    elseif step[2] == 'ext' then
                        -- task 0278: moves the player into an exterior cell (grid x y of the worldspace cfg.world) at x y z
                        trace('ext')
                        local ok, cell = pcall(world.getExteriorCell, step[3], step[4], cfg.world)
                        if ok and cell ~= nil then
                            player:teleport(cell, util.vector3(step[5], step[6], step[7]), { rotation = util.transform.rotateZ(step[8] or 0) })
                        else
                            print('HSE ext failed ' .. tostring(cell))
                        end
                    elseif step[2] == 'go' then
                        trace('go')
                        player:teleport(player.cell, util.vector3(step[3], step[4], step[5]),
                            { rotation = util.transform.rotateZ(step[6] or 0) })
                    elseif step[2] == 'door' then
                        local d = nearest(player, step[3], step[4], step[5], function(o) return types.Door.objectIsInstance(o) end)
                        if d ~= nil then
                            trace(string.format('door %s at %.0f,%.0f,%.0f', tostring(d.recordId), d.position.x, d.position.y, d.position.z))
                            d:activateBy(player)
                        else
                            trace('door none')
                        end
                    elseif step[2] == 'doors' then
                        for _, o in ipairs(player.cell:getAll()) do
                            local ok, isDoor = pcall(function() return types.Door.objectIsInstance(o) end)
                            if ok and isDoor then
                                print(string.format('HSE doorlist %s at %.0f,%.0f,%.0f', tostring(o.recordId), o.position.x, o.position.y, o.position.z))
                            end
                        end
                    elseif step[2] == 'furn' then
                        local pattern = step[6] or cfg.furn
                        local f = nearest(player, step[3], step[4], step[5], function(o) return string.find(tostring(o.recordId), pattern, 1, true) ~= nil end)
                        if f ~= nil then
                            trace(string.format('furn %s at %.0f,%.0f,%.0f', tostring(f.recordId), f.position.x, f.position.y, f.position.z))
                            f:activateBy(player)
                        else
                            trace('furn none')
                        end
                    elseif step[2] == 'mount' then
                        -- activates the nearest object of the record id cfg.horse (the stable's horse) as the player does
                        local h = nil
                        for _, a in ipairs(player.cell:getAll()) do
                            if tostring(a.recordId) == cfg.horse then h = a end
                        end
                        if h ~= nil then
                            trace(string.format('mount %s at %.0f,%.0f,%.0f', tostring(h.recordId), h.position.x, h.position.y, h.position.z))
                            h:activateBy(player)
                        else
                            trace('mount none')
                        end
                    elseif step[2] == 'count' then
                        local n = 0
                        for _ in ipairs(player.cell:getAll()) do n = n + 1 end
                        trace('count ' .. tostring(n) .. ' objects')
                    elseif step[2] == 'actors' then
                        for _, o in ipairs(world.activeActors) do
                            local id = tostring(o.recordId)
                            if string.find(string.lower(id), string.lower(step[3]), 1, true) ~= nil then
                                print(string.format('HSE actor %s at %.0f,%.0f,%.0f', id, o.position.x, o.position.y, o.position.z))
                            end
                        end
                    elseif step[2] == 'ids' then
                        -- every object of the cell whose record id contains step[3] (case blind), with its position
                        for _, o in ipairs(player.cell:getAll()) do
                            local id = tostring(o.recordId)
                            if string.find(string.lower(id), string.lower(step[3]), 1, true) ~= nil then
                                print(string.format('HSE id %s at %.0f,%.0f,%.0f', id, o.position.x, o.position.y, o.position.z))
                            end
                        end
                    elseif step[2] == 'trace' then
                        trace(step[3] or 'note')
                    elseif step[2] == 'save' then
                        trace('save')
                        storage.globalSection('HSE'):set('saveNow', true)
                    end
                end
            end
        end,
    },
}
