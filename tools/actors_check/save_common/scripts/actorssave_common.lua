-- Task 0098 move 8, stage 8c save check (global script of tools/actors_check/save_check.sh).
--
-- The script runs in two modes, chosen by which copy of the mod is loaded:
--   make: after the new game has been running for a few seconds, change the state of the actors standing in the player's
--         cell (health, fatigue, level, a skill, items in the inventory, a spell), print one line per actor
--         ("ACSAVE ...") and let the menu script save the game.
--   load: after a saved game has been loaded, print the same lines without changing anything.
-- The check script compares the lines of every run: a saved game must give back exactly what was saved, whichever
-- executable (old or new) wrote it and whichever read it. Only static fields are printed (no position, no time).
local world = require('openmw.world')
local util = require('openmw.util')
local types = require('openmw.types')
local core = require('openmw.core')

local started = nil
local done = false
local wait = 0

local attributes = { 'strength', 'intelligence', 'willpower', 'agility', 'speed', 'endurance', 'personality', 'luck' }

local function try(f, ...)
    local ok, v = pcall(f, ...)
    if ok then return tostring(v) end
    return 'ERR'
end

local function fixed(v)
    if type(v) ~= 'number' then return tostring(v) end
    return string.format('%.2f', v)
end

local function describe(kind, obj)
    local A = types.Actor
    local parts = {}
    for _, d in ipairs({ 'health', 'magicka', 'fatigue' }) do
        parts[#parts + 1] = d .. '=' .. try(function()
            local s = A.stats.dynamic[d](obj)
            -- health does not regenerate; the current magicka and fatigue drift with the time that has passed
            if d == 'health' then return fixed(s.base) .. '/' .. fixed(s.current) end
            return fixed(s.base)
        end)
    end
    parts[#parts + 1] = 'level=' .. try(function() return A.stats.level(obj).current end)
    local at = {}
    for _, a in ipairs(attributes) do
        at[#at + 1] = try(function() return fixed(A.stats.attributes[a](obj).base) end)
    end
    parts[#parts + 1] = 'attr=' .. table.concat(at, ',')
    if kind == 'NPC' then
        parts[#parts + 1] = 'sneak=' .. try(function() return fixed(types.NPC.stats.skills.sneak(obj).base) end)
        parts[#parts + 1] = 'blade=' .. try(function() return fixed(types.NPC.stats.skills.longblade(obj).base) end)
        parts[#parts + 1] = 'rep=' .. try(function() return types.NPC.getFactions(obj)[1] or '' end)
    end
    parts[#parts + 1] = 'dead=' .. try(A.isDead, obj)
    parts[#parts + 1] = 'inv=' .. try(function()
        local counts = {}
        for _, item in ipairs(A.inventory(obj):getAll()) do
            counts[item.recordId] = (counts[item.recordId] or 0) + item.count
        end
        local out = {}
        for id, n in pairs(counts) do out[#out + 1] = id .. 'x' .. n end
        table.sort(out)
        return table.concat(out, ',')
    end)
    parts[#parts + 1] = 'equip=' .. try(function()
        local out = {}
        for slot, item in pairs(A.getEquipment(obj)) do out[#out + 1] = slot .. ':' .. item.recordId end
        table.sort(out)
        return table.concat(out, ',')
    end)
    parts[#parts + 1] = 'spells=' .. try(function()
        local out = {}
        for _, spell in pairs(A.spells(obj)) do out[#out + 1] = spell.id end
        table.sort(out)
        return table.concat(out, ',')
    end)
    return string.format('ACSAVE %s %s %s', kind, obj.recordId, table.concat(parts, ' '))
end

local function actorsOf(player)
    local out = {}
    for _, kind in ipairs({ 'NPC', 'Creature' }) do
        local ok, objs = pcall(function() return player.cell:getAll(types[kind]) end)
        if ok then
            for _, obj in ipairs(objs) do
                if obj.recordId ~= 'player' then out[#out + 1] = { kind, obj } end
            end
        end
    end
    table.sort(out, function(a, b)
        if a[2].recordId ~= b[2].recordId then return a[2].recordId < b[2].recordId end
        return a[2].id < b[2].id
    end)
    return out
end

local function change(kind, obj, index)
    local A = types.Actor
    pcall(function()
        local h = A.stats.dynamic.health(obj)
        h.current = h.current - (5 + index)
    end)
    pcall(function()
        local f = A.stats.dynamic.fatigue(obj)
        f.current = f.current - 3 * index
    end)
    pcall(function() A.stats.level(obj).current = A.stats.level(obj).current + 1 + index % 3 end)
    if kind == 'NPC' then
        pcall(function() types.NPC.stats.skills.sneak(obj).base = 41 + index end)
        pcall(function() types.NPC.stats.skills.longblade(obj).base = 22 + 2 * index end)
    end
    pcall(function() world.createObject('gold_001', 30 + index):moveInto(A.inventory(obj)) end)
    pcall(function() world.createObject('misc_com_bottle_01', 1 + index % 4):moveInto(A.inventory(obj)) end)
    if index % 2 == 0 then
        pcall(function() A.spells(obj):add('flame') end)
    end
end

local function spawn(player)
    -- a few more actors next to the player: every 190th NPC record and every 70th creature record, in record order
    local function pick(records, step)
        local sorted = {}
        for _, r in ipairs(records) do sorted[#sorted + 1] = r end
        table.sort(sorted, function(a, b) return a.id < b.id end)
        local out = {}
        for i = 1, #sorted, step do out[#out + 1] = sorted[i].id end
        return out
    end
    local ids = {}
    for _, id in ipairs(pick(types.NPC.records, 190)) do ids[#ids + 1] = id end
    for _, id in ipairs(pick(types.Creature.records, 70)) do ids[#ids + 1] = id end
    for k, id in ipairs(ids) do
        pcall(function()
            local o = world.createObject(id, 1)
            o:teleport(player.cell, player.position + util.vector3(250 + 60 * (k % 6), 250 + 60 * math.floor(k / 6), 0))
        end)
    end
end

return function(mode)
local MAKE = (mode == 'make')
return {
    engineHandlers = {
        onUpdate = function(dt)
            if done then return end
            started = started or core.getRealTime()
            if core.getRealTime() - started < (MAKE and 6 or 4) then return end
            local player = world.players[1]
            if MAKE and wait < 3 then
                wait = wait + 1
                if wait == 1 then spawn(player) end -- the actors appear over the next frames
                return
            end
            local actors = actorsOf(player)
            if MAKE and wait == 3 then
                for i, e in ipairs(actors) do change(e[1], e[2], i) end
                wait = 4
                return -- the changes are applied by the next update
            end
            local lines = {}
            for _, e in ipairs(actors) do lines[#lines + 1] = describe(e[1], e[2]) end
            for _, l in ipairs(lines) do print(l) end
            print('ACSAVE count ' .. #lines)
            done = true
        end,
    },
}
end
