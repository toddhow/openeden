-- tools/skyhouses_check (task 0261): the global half. cfg.plan is a list of { update, what, ... }: 'talk' (activates the
-- target actor in the player's cell), 'cell name x y z' (moves the player into a cell), 'go x y z' (inside the cell),
-- 'door x y radius' (the nearest object of the door type), 'furn x y radius [text]' (activates the nearest object whose
-- record id contains the text, default cfg.furn; neutral objects are named skyrimse:b<decimal base id>), 'ids text',
-- 'trace tag' (gold, the Breezehome key, the cell), 'save'. Every line starts with CMP and holds no wall clock.
local core = require('openmw.core')
local world = require('openmw.world')
local types = require('openmw.types')
local util = require('openmw.util')
local storage = require('openmw.storage')
local cfg = require('scripts.cmp.cfg')
local okInv, inv = pcall(require, 'core.skyrimse.inventory')

local updates = 0
local faceBase, faceUntil = nil, 0
local target = nil

local function find(cell)
    for _, obj in ipairs(cell:getAll()) do
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
        key = inv.count('player', '00093b08')
    end
    print(string.format('CMP %s u=%d cell=%s grid=%s,%s player=%.0f,%.0f,%.0f gold=%d breezehomekey=%d', tag, updates,
        tostring(player.cell.name), tostring(player.cell.gridX), tostring(player.cell.gridY), p.x, p.y, p.z, gold, key))
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
            -- 'face base until': from this update the player is turned to the nearest actor of that base every 30 updates
            if faceBase ~= nil and updates <= faceUntil and updates % 30 == 0 then
                local best, bestD = nil, math.huge
                for _, o in ipairs(player.cell:getAll()) do
                    local ok, id = pcall(function() return o.recordId end)
                    if ok and id ~= nil and tostring(id):match(':b(%d+)$') == tostring(faceBase) then
                        local d = (o.position - player.position):length()
                        if d < bestD then best, bestD = o, d end
                    end
                end
                if best ~= nil and bestD > 1 then
                    local dx, dy = best.position.x - player.position.x, best.position.y - player.position.y
                    player:teleport(player.cell, player.position, { rotation = util.transform.rotateZ(math.atan2(dx, dy)) })
                    if updates % 300 == 0 then
                        print(string.format('CMP face %s at %.0f,%.0f dist %.0f', tostring(faceBase), best.position.x, best.position.y, bestD))
                    end
                end
            end
            for _, step in ipairs(cfg.plan) do
                if step[1] == updates and step[2] == 'face' then
                    faceBase, faceUntil = step[3], step[4]
                elseif step[1] == updates then
                    if step[2] == 'talk' then
                        trace('talk')
                        if step[3] ~= nil then
                            -- a base id (decimal) of this step; the nearest object of that base in the cell
                            local best, bestD = nil, math.huge
                            -- task 0333: an exterior actor a script moved stays an object of the cell it came from, so the
                            -- neighbour cells are searched too
                            local cells = { player.cell }
                            if player.cell.isExterior then
                                for dx = -1, 1 do
                                    for dy = -1, 1 do
                                        if dx ~= 0 or dy ~= 0 then
                                            local okc, other = pcall(world.getExteriorCell, player.cell.gridX + dx, player.cell.gridY + dy, player.cell)
                                            if okc and other ~= nil then cells[#cells + 1] = other end
                                        end
                                    end
                                end
                            end
                            for _, c in ipairs(cells) do
                                for _, o in ipairs(c:getAll()) do
                                    local ok, id = pcall(function() return o.recordId end)
                                    if ok and id ~= nil and tostring(id):match(':b(%d+)$') == tostring(step[3]) then
                                        local d = (o.position - player.position):length()
                                        if d < bestD then best, bestD = o, d end
                                    end
                                end
                            end
                            target = best
                        elseif target == nil or not target:isValid() then
                            target = find(player.cell)
                        end
                        if target ~= nil then print('CMP target ' .. tostring(target.recordId)) target:activateBy(player) else print('CMP target not found') end
                    elseif step[2] == 'cell' then
                        trace('cell ' .. step[3])
                        player:teleport(step[3], util.vector3(step[4], step[5], step[6]),
                            { rotation = util.transform.rotateZ(step[7] or 0) })
                    elseif step[2] == 'go' then
                        trace('go')
                        local gz = step[5]
                        if gz == 'land' then
                            local okh, h = pcall(function() return core.land.getHeightAt(util.vector3(step[3], step[4], 0), player.cell) end)
                            gz = (okh and h or 0) + 120
                            print(string.format('CMP land %.0f,%.0f = %s', step[3], step[4], tostring(h)))
                        end
                        player:teleport(player.cell, util.vector3(step[3], step[4], gz),
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
                                print(string.format('CMP doorlist %s at %.0f,%.0f,%.0f', tostring(o.recordId), o.position.x, o.position.y, o.position.z))
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
                    elseif step[2] == 'count' then
                        local n = 0
                        for _ in ipairs(player.cell:getAll()) do n = n + 1 end
                        trace('count ' .. tostring(n) .. ' objects')
                    elseif step[2] == 'ids' then
                        -- every object of the cell whose record id contains step[3] (case blind), with its position
                        for _, o in ipairs(player.cell:getAll()) do
                            local id = tostring(o.recordId)
                            if string.find(string.lower(id), string.lower(step[3]), 1, true) ~= nil then
                                print(string.format('CMP id %s at %.0f,%.0f,%.0f', id, o.position.x, o.position.y, o.position.z))
                            end
                        end
                    elseif step[2] == 'hour' then
                        pcall(function() world.mwscript.getGlobalVariables(world.players[1]).gamehour = step[3] end)
                        trace('hour ' .. tostring(step[3]))
                    elseif step[2] == 'near' then
                        -- every actor within step[3] units, the neighbour cells included (task 0333)
                        local cells = { player.cell }
                        if player.cell.isExterior then
                            for dx = -1, 1 do
                                for dy = -1, 1 do
                                    if dx ~= 0 or dy ~= 0 then
                                        local okc, other = pcall(world.getExteriorCell, player.cell.gridX + dx, player.cell.gridY + dy, player.cell)
                                        if okc and other ~= nil then cells[#cells + 1] = other end
                                    end
                                end
                            end
                        end
                        for _, c in ipairs(cells) do
                            local okn, all = pcall(function() return c:getAll() end)
                            print(string.format('CMP cell %s,%s objects %d', tostring(c.gridX), tostring(c.gridY), okn and #all or -1))
                            for _, o in ipairs(c:getAll()) do
                                local bid = tostring(o.recordId):match(':b(%d+)$')
                                local wanted = setmetatable({}, { __index = function() return true end })
                                if bid ~= nil and wanted[bid] and (o.position - player.position):length() <= step[3] then
                                    print(string.format('CMP near %s at %.0f,%.0f,%.0f dist %.0f', tostring(o.recordId), o.position.x, o.position.y, o.position.z, (o.position - player.position):length()))
                                end
                            end
                        end
                    elseif step[2] == 'trace' then
                        trace(step[3] or 'note')
                    elseif step[2] == 'save' then
                        trace('save')
                        storage.globalSection('CMP'):set('saveNow', true)
                    end
                end
            end
        end,
    },
}
