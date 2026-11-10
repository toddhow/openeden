-- Task 0098 move 8, stage 8a check: reads what the world-object classes (door, container, activator, static) answer
-- for every such object in the loaded cells around the player (Balmora) through the public Lua API and prints one
-- line per object. The check script compares the lines between two executables started with the same seed: the one
-- under test (classes in the Morrowind game pack) and a reference built before the move. Read only.
local world = require('openmw.world')
local types = require('openmw.types')
local core = require('openmw.core')

local started = nil
local done = false

local function try(f, ...)
    local ok, v = pcall(f, ...)
    if ok then return tostring(v) end
    return 'ERR'
end

local function describe(kind, obj)
    local pos = obj.position
    local head = string.format('WOCHECK %s %s @%.0f,%.0f,%.0f', kind, obj.recordId, pos.x, pos.y, pos.z)
    local parts = {}
    local T = types[kind]
    if T then
        local rec = try(function() local r = T.record(obj) return (r.name or '') .. '|' .. (r.model or '') .. '|' .. (r.mwscript or '') end)
        parts[#parts + 1] = 'rec=' .. rec
    end
    if kind == 'Door' then
        parts[#parts + 1] = 'tele=' .. try(T.isTeleport, obj)
        parts[#parts + 1] = 'dest=' .. try(function() return T.destCell(obj).name end)
    end
    if kind == 'Door' or kind == 'Container' then
        parts[#parts + 1] = 'locked=' .. try(types.Lockable.isLocked, obj)
        parts[#parts + 1] = 'level=' .. try(types.Lockable.getLockLevel, obj)
        parts[#parts + 1] = 'key=' .. try(function() local k = types.Lockable.getKeyRecord(obj) return k and k.id or '' end)
        parts[#parts + 1] = 'trap=' .. try(function() local k = types.Lockable.getTrapSpell(obj) return k and k.id or '' end)
    end
    if kind == 'Container' then
        parts[#parts + 1] = 'cap=' .. try(T.getCapacity, obj)
        parts[#parts + 1] = 'enc=' .. try(T.getEncumbrance, obj)
        parts[#parts + 1] = 'items=' .. try(function() return #T.inventory(obj):getAll() end)
    end
    return head .. ' ' .. table.concat(parts, ' ')
end

return {
    engineHandlers = {
        onUpdate = function()
            started = started or core.getRealTime()
            if done or core.getRealTime() - started < 4 then return end
            done = true
            local player = world.players[1]
            local lines = {}
            for _, kind in ipairs({ 'Door', 'Container', 'Activator', 'Static' }) do
                local ok, objs = pcall(function() return player.cell:getAll(types[kind]) end)
                if ok then
                    for _, obj in ipairs(objs) do lines[#lines + 1] = describe(kind, obj) end
                end
            end
            table.sort(lines)
            for _, l in ipairs(lines) do print(l) end
            print('WOCHECK count ' .. #lines)
        end,
    },
}
