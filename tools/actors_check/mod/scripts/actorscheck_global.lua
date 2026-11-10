-- Task 0098 move 8, stage 8c check: reads what the actor classes (NPC and creature) answer, through the public Lua API,
-- for the actors of the loaded content: every Nth NPC and creature record (created next to the player in batches, read
-- one frame later and removed again) and the actors already standing in the player's cell (Balmora). One line per actor.
-- The check script compares the lines between two executables started with the same seed: the one under test (the
-- classes in the Morrowind game pack) and a reference built before the move. Read only apart from the created actors.
local world = require('openmw.world')
local types = require('openmw.types')
local core = require('openmw.core')
local util = require('openmw.util')

local started = nil
local phase = 0
local batches = {}
local current = {}
local batchIndex = 0
local printed = 0

local STEP_NPC = 24
local STEP_CREATURE = 10
local BATCH = 5

local attributes = { 'strength', 'intelligence', 'willpower', 'agility', 'speed', 'endurance', 'personality', 'luck' }
local skills = { 'block', 'armorer', 'mediumarmor', 'heavyarmor', 'bluntweapon', 'longblade', 'axe', 'spear',
    'athletics', 'enchant', 'destruction', 'alteration', 'illusion', 'conjuration', 'mysticism', 'restoration',
    'alchemy', 'unarmored', 'security', 'sneak', 'acrobatics', 'lightarmor', 'shortblade', 'marksman', 'mercantile',
    'speechcraft', 'handtohand' }

local function try(f, ...)
    local ok, v = pcall(f, ...)
    if ok then return tostring(v) end
    return 'ERR'
end

local function fixed(v)
    if type(v) ~= 'number' then return tostring(v) end
    return string.format('%.3f', v)
end

local function describe(kind, obj, tag)
    local T = types[kind]
    local A = types.Actor
    local parts = {}
    local function add(name, f, ...) parts[#parts + 1] = name .. '=' .. try(f, ...) end
    add('rec', function()
        local r = T.record(obj)
        return (r.name or '') .. '|' .. (r.model or '') .. '|' .. (r.mwscript or '') .. '|' .. tostring(r.baseGold)
    end)
    if kind == 'NPC' then
        add('npc', function()
            local r = T.record(obj)
            return tostring(r.race) .. '|' .. tostring(r.class) .. '|' .. tostring(r.isMale) .. '|' .. tostring(r.hair)
                .. '|' .. tostring(r.head)
        end)
        add('wolf', T.isWerewolf, obj)
        add('faction', function()
            local out = {}
            for _, f in ipairs(T.getFactions(obj)) do out[#out + 1] = f .. ':' .. T.getFactionRank(obj, f) end
            return table.concat(out, ',')
        end)
        local sk = {}
        for _, s in ipairs(skills) do
            sk[#sk + 1] = try(function() return fixed(T.stats.skills[s](obj).base) end)
        end
        parts[#parts + 1] = 'skills=' .. table.concat(sk, ',')
    else
        add('crea', function()
            local r = T.record(obj)
            return tostring(r.combatSkill) .. '|' .. tostring(r.magicSkill) .. '|' .. tostring(r.stealthSkill)
                .. '|' .. tostring(r.soulValue) .. '|' .. tostring(r.type) .. '|' .. tostring(r.canFly)
                .. '|' .. tostring(r.canSwim) .. '|' .. tostring(r.canWalk) .. '|' .. tostring(r.isBiped)
                .. '|' .. tostring(r.isEssential) .. '|' .. tostring(r.isRespawning)
        end)
    end
    local at = {}
    for _, a in ipairs(attributes) do
        at[#at + 1] = try(function() return fixed(A.stats.attributes[a](obj).base) end)
    end
    parts[#parts + 1] = 'attr=' .. table.concat(at, ',')
    -- (a few creatures with abilities show different magicka and fatigue bases from one run to the next in the same
    -- executable, so those two are printed for NPCs only)
    for _, d in ipairs(kind == 'NPC' and { 'health', 'magicka', 'fatigue' } or { 'health' }) do
        add(d, function()
            local s = A.stats.dynamic[d](obj)
            -- the current value and the modifier are left out: they change with the time that has passed (regeneration, drains
            -- of the creatures' abilities)
            return fixed(s.base)
        end)
    end
    add('level', function() return A.stats.level(obj).current end)
    add('cap', A.getCapacity, obj)
    add('walk', A.getWalkSpeed, obj)
    add('run', A.getRunSpeed, obj)
    add('dead', A.isDead, obj)
    -- the gold, the item count and the encumbrance depend on the random draws of the inventory fill, which differ with
    -- the timing of the run, so they are not printed
    add('equip', function()
        local out = {}
        for slot, item in pairs(A.getEquipment(obj)) do out[#out + 1] = slot .. ':' .. item.recordId end
        table.sort(out)
        return table.concat(out, ',')
    end)
    add('scale', function() return fixed(obj.scale) end)
    return string.format('ACCHECK %s %s %s %s', kind, obj.recordId, tag, table.concat(parts, ' '))
end

local function everyNth(records, step)
    local sorted = {}
    for _, r in ipairs(records) do sorted[#sorted + 1] = r end
    table.sort(sorted, function(a, b) return a.id < b.id end)
    local out = {}
    for i = 1, #sorted, step do out[#out + 1] = sorted[i] end
    return out
end

local function emit(lines)
    for _, l in ipairs(lines) do print(l) end
    printed = printed + #lines
end

return {
    engineHandlers = {
        onUpdate = function()
            started = started or core.getRealTime()
            if core.getRealTime() - started < 4 then return end
            local player = world.players[1]
            if phase == 0 then
                -- the actors standing in the cell
                local lines = {}
                for _, kind in ipairs({ 'NPC', 'Creature' }) do
                    local ok, objs = pcall(function() return player.cell:getAll(types[kind]) end)
                    if ok then
                        for _, obj in ipairs(objs) do
                            if obj.recordId ~= 'player' then lines[#lines + 1] = describe(kind, obj, 'cell') end
                        end
                    end
                end
                table.sort(lines)
                emit(lines)
                -- the records to create
                local list = {}
                for _, r in ipairs(everyNth(types.NPC.records, STEP_NPC)) do list[#list + 1] = { 'NPC', r.id } end
                for _, r in ipairs(everyNth(types.Creature.records, STEP_CREATURE)) do list[#list + 1] = { 'Creature', r.id } end
                for i = 1, #list, BATCH do
                    local b = {}
                    for j = i, math.min(i + BATCH - 1, #list) do b[#b + 1] = list[j] end
                    batches[#batches + 1] = b
                end
                print('ACCHECK plan ' .. #list .. ' records in ' .. #batches .. ' batches')
                phase = 1
            elseif phase == 1 then
                -- create a batch next to the player
                batchIndex = batchIndex + 1
                if batchIndex > #batches then
                    print('ACCHECK count ' .. printed)
                    phase = 3
                    return
                end
                current = {}
                for k, entry in ipairs(batches[batchIndex]) do
                    local ok, obj = pcall(function()
                        local o = world.createObject(entry[2], 1)
                        o:teleport(player.cell, player.position + util.vector3(300 + 40 * (k % 8), 300 + 40 * math.floor(k / 8), 0))
                        return o
                    end)
                    if ok then current[#current + 1] = { entry[1], obj } else print('ACCHECK failed-create ' .. entry[2]) end
                end
                phase = 2
            elseif phase == 2 then
                -- empty the inventories (their random fill differs with the timing of the run and would change the
                -- load-dependent values); the removal is applied at the end of the frame
                for _, e in ipairs(current) do
                    pcall(function()
                        for _, item in ipairs(types.Actor.inventory(e[2]):getAll()) do item:remove() end
                    end)
                end
                phase = 4
            elseif phase == 4 then
                local lines = {}
                for _, e in ipairs(current) do
                    local kind, obj = e[1], e[2]
                    lines[#lines + 1] = describe(kind, obj, 'made')
                    pcall(function() obj:remove() end)
                end
                table.sort(lines)
                emit(lines)
                current = {}
                phase = 1
            end
        end,
    },
}
