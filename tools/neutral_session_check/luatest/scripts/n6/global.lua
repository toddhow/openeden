-- tools/neutral_session_check (task 0177, neutral saves): the global half of the save and load round trip.
--
-- Phase "save" (scripts/n6/phase.lua, written by saveload.sh): 3 real seconds after the start it disables one object of
-- the player's cell, moves two, enables one that the pack starts disabled and deletes one, then (6 s) logs every object
-- of the cell as "N6OBJ <id> <recordId> enabled=<bool> x,y,z" and the player as "N6PLAYER <cell> x,y,z". The menu
-- script saves at 8 s.
-- Phase "load": nothing is changed; the same N6OBJ and N6PLAYER lines are logged at 6 s of the loaded game, so the two
-- runs can be compared line by line.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')
local types = require('openmw.types')
local phase = require('scripts.n6.phase')
local storage = require('openmw.storage')

local start = nil
local changed, reported = false, false

local function sortedObjects(cell)
    local list = {}
    for _, o in ipairs(cell:getAll()) do
        if not types.Actor.objectIsInstance(o) then list[#list + 1] = o end
    end
    table.sort(list, function(a, b) return tostring(a.id) < tostring(b.id) end)
    return list
end

local function report(player)
    local count = 0
    for _, o in ipairs(sortedObjects(player.cell)) do
        local p = o.position
        print(string.format('N6OBJ %s %s T=%s enabled=%s %.2f,%.2f,%.2f', tostring(o.id), tostring(o.recordId), tostring(o.type),
            tostring(o.enabled), p.x, p.y, p.z))
        count = count + 1
    end
    local p = player.position
    print(string.format('N6PLAYER %s %.1f,%.1f,%.1f objects=%d', tostring(player.cell.name), p.x, p.y, p.z, count))
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            start = start or core.getRealTime()
            local t = core.getRealTime() - start
            if phase == 'save' and not changed and t > 3 then
                changed = true
                local list = sortedObjects(player.cell)
                local disabledStart = nil
                local movable = {}
                for _, o in ipairs(list) do
                    if not o.enabled and disabledStart == nil then disabledStart = o end
                    if o.enabled and o.recordId ~= nil and not tostring(o.id):find('player') then
                        movable[#movable + 1] = o
                    end
                end
                if #movable >= 6 then
                    movable[3].enabled = false
                    movable[4]:teleport(player.cell, movable[4].position + util.vector3(0, 0, 64))
                    movable[5]:teleport(player.cell, movable[5].position + util.vector3(30, -20, 0),
                        { rotation = util.transform.rotateZ(1.0) })
                    movable[6]:remove()
                    print('N6CHANGE disabled ' .. tostring(movable[3].id) .. ', moved ' .. tostring(movable[4].id) .. ' and '
                        .. tostring(movable[5].id) .. ', deleted ' .. tostring(movable[6].id))
                end
                if disabledStart ~= nil then
                    disabledStart.enabled = true
                    print('N6CHANGE enabled ' .. tostring(disabledStart.id))
                end
            end
            if phase == 'cycle' and not changed and t > 3 then
                -- the second generation: one object more is moved, once (the flag is part of the second save)
                changed = true
                local section = storage.globalSection('N6')
                if not section:get('cycled') then
                    section:set('cycled', true)
                    for _, o in ipairs(sortedObjects(player.cell)) do
                        if o.enabled and tostring(o.id) == '@0x3fffffff0001f88c' or tostring(o.id) == '@0x3fffffff000206b4' then
                            o:teleport(player.cell, o.position + util.vector3(0, 0, 10))
                            print('N6CHANGE cycle moved ' .. tostring(o.id))
                            break
                        end
                    end
                end
            end
            if not reported and t > 6 then
                reported = true
                report(player)
                storage.globalSection('N6'):set(phase == 'cycle' and 'reportedCycle' or 'reported', true)
            end
        end,
    },
}
