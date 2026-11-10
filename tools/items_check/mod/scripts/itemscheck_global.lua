-- Task 0098 move 8, stage 8b check: reads what the item classes (armor, weapon, clothing, book, ingredient, potion,
-- miscellaneous, light, lockpick, probe, repair, apparatus) answer for EVERY item record of the loaded content, through
-- the public Lua API, and prints one line per record (plus the items lying in the player's cell and a few equipped
-- ones). The items are created in the player's inventory first, so that the instance data (condition, charge, soul)
-- is read too. The check script compares the lines between two executables started with the same seed: the one under
-- test (classes in the Morrowind game pack) and a reference built before the move. Read only apart from the created
-- items.
local world = require('openmw.world')
local types = require('openmw.types')
local core = require('openmw.core')

local started = nil
local phase = 0

local kinds = { 'Armor', 'Weapon', 'Clothing', 'Book', 'Ingredient', 'Potion', 'Miscellaneous', 'Light', 'Lockpick',
    'Probe', 'Repair', 'Apparatus' }
local fields = { 'id', 'name', 'model', 'icon', 'mwscript', 'weight', 'value', 'health', 'enchant', 'enchantCapacity',
    'baseArmor', 'type', 'quality', 'maxCondition', 'uses', 'time', 'radius', 'color', 'isCarriable', 'isKey',
    'isOffByDefault', 'isFire', 'isFlicker', 'isFlickerSlow', 'isNegative', 'isPulse', 'isPulseSlow', 'isDynamic',
    'isScroll', 'skill', 'speed', 'reach', 'chopMinDamage', 'chopMaxDamage', 'slashMinDamage', 'slashMaxDamage',
    'thrustMinDamage', 'thrustMaxDamage', 'isMagical', 'isSilver', 'sound' }

local function try(f, ...)
    local ok, v = pcall(f, ...)
    if ok then return tostring(v) end
    return 'ERR'
end

local function effectsOf(rec)
    local out = {}
    local ok, effects = pcall(function() return rec.effects end)
    if ok and effects then
        for _, e in ipairs(effects) do
            out[#out + 1] = try(function()
                return tostring(e.id) .. '/' .. tostring(e.affectedSkill) .. '/' .. tostring(e.affectedAttribute)
                    .. '/' .. tostring(e.magnitudeMin) .. '/' .. tostring(e.magnitudeMax) .. '/' .. tostring(e.duration)
            end)
        end
    end
    return table.concat(out, ';')
end

local function describe(kind, rec, obj)
    local parts = {}
    for _, f in ipairs(fields) do
        local v = try(function() return rec[f] end)
        if v ~= 'ERR' and v ~= 'nil' then parts[#parts + 1] = f .. '=' .. v end
    end
    local fx = effectsOf(rec)
    if fx ~= '' then parts[#parts + 1] = 'fx=' .. fx end
    if obj then
        parts[#parts + 1] = 'cond=' .. try(function() return types.Item.itemData(obj).condition end)
        parts[#parts + 1] = 'charge=' .. try(function() return types.Item.itemData(obj).enchantmentCharge end)
        parts[#parts + 1] = 'soul=' .. try(function() return types.Item.itemData(obj).soul end)
        parts[#parts + 1] = 'restock=' .. try(function() return types.Item.isRestocking(obj) end)
    end
    return string.format('ITCHECK %s %s', kind, table.concat(parts, ' '))
end

local firstOfType = {}

return {
    engineHandlers = {
        onUpdate = function()
            started = started or core.getRealTime()
            if core.getRealTime() - started < 4 then return end
            local player = world.players[1]
            if phase == 0 then
                phase = 1
                local lines = {}
                local inv = types.Actor.inventory(player)
                for _, kind in ipairs(kinds) do
                    local T = types[kind]
                    local count = 0
                    local recs = {}
                    for _, rec in ipairs(T.records) do recs[#recs + 1] = rec end
                    table.sort(recs, function(a, b) return a.id < b.id end)
                    for _, rec in ipairs(recs) do
                        local ok, obj = pcall(function()
                            local o = world.createObject(rec.id, 1)
                            o:moveInto(inv)
                            return o
                        end)
                        lines[#lines + 1] = describe(kind, rec, ok and obj or nil)
                        if ok and obj then
                            local t = try(function() return rec.type end)
                            if t == 'ERR' then t = 'nil' end
                            local key = kind .. ':' .. t
                            firstOfType[key] = firstOfType[key] or obj
                        end
                        count = count + 1
                    end
                    lines[#lines + 1] = 'ITCHECK count ' .. kind .. ' ' .. count
                end
                for _, l in ipairs(lines) do print(l) end
                -- the items lying in the player's cell
                local cellLines = {}
                for _, kind in ipairs(kinds) do
                    local ok, objs = pcall(function() return player.cell:getAll(types[kind]) end)
                    if ok then
                        for _, o in ipairs(objs) do
                            cellLines[#cellLines + 1] = string.format('ITCHECK cell %s %s @%.0f,%.0f,%.0f', kind,
                                o.recordId, o.position.x, o.position.y, o.position.z)
                        end
                    end
                end
                table.sort(cellLines)
                for _, l in ipairs(cellLines) do print(l) end
                print('ITCHECK cell count ' .. #cellLines)
                -- use one item of each kind the actor can wear or consume, through the engine's own use action (the
                -- class's use() and canBeEquipped()); the result is read back next frame
                local want = {
                    'Armor:' .. tostring(types.Armor.TYPE.Helmet), 'Armor:' .. tostring(types.Armor.TYPE.Cuirass),
                    'Armor:' .. tostring(types.Armor.TYPE.Boots), 'Armor:' .. tostring(types.Armor.TYPE.Shield),
                    'Armor:' .. tostring(types.Armor.TYPE.LBracer),
                    'Weapon:' .. tostring(types.Weapon.TYPE.LongBladeOneHand),
                    'Weapon:' .. tostring(types.Weapon.TYPE.AxeTwoHand),
                    'Clothing:' .. tostring(types.Clothing.TYPE.Shirt), 'Clothing:' .. tostring(types.Clothing.TYPE.Ring),
                    'Clothing:' .. tostring(types.Clothing.TYPE.Robe),
                    'Light:nil', 'Lockpick:nil', 'Probe:nil', 'Potion:nil', 'Ingredient:nil',
                }
                for _, key in ipairs(want) do
                    local obj = firstOfType[key]
                    if obj then
                        print('ITCHECK use ' .. key .. ' ' .. obj.recordId .. ' '
                            .. try(function() world._runStandardUseAction(obj, player, false) return 'ok' end))
                    else
                        print('ITCHECK use ' .. key .. ' none')
                    end
                end
            elseif phase == 1 then
                phase = 2
                local eq = {}
                local ok, equipment = pcall(types.Actor.getEquipment, player)
                if ok then
                    for slot, obj in pairs(equipment) do
                        eq[#eq + 1] = tostring(slot) .. '=' .. obj.recordId
                    end
                end
                table.sort(eq)
                print('ITCHECK equipment ' .. table.concat(eq, ','))
                print('ITCHECK encumbrance ' .. try(types.Actor.getEncumbrance, player))
                print('ITCHECK inventory ' .. try(function() return #types.Actor.inventory(player):getAll() end))
                print('ITCHECK armorrating ' .. try(types.Actor.getArmorRating, player))
                print('ITCHECK done')
            end
        end,
    },
}
