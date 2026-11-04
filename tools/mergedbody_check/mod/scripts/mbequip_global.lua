-- Task 0069 stage e: the global half of the equipment scene of tools/mergedbody_check (SCENE=equipment). It gives the
-- player one piece of every kind of clothing and armour the scene equips, and places a row of NPCs in front of the
-- player, each carrying one combination: the items enter the NPC's inventory before it enters the world, and NPCs put
-- on what they carry by themselves. It is only part of the check, never of the game, and uses the player's own game
-- data.
local world = require('openmw.world')
local types = require('openmw.types')
local util = require('openmw.util')

-- kind = { record type, its TYPE, the record id preferred (Morrowind.esm) }; without that record the first record of
-- the type by id is taken, so the scene runs on any data.
local KINDS = {
    shirt = { 'Clothing', 'Shirt', 'common_shirt_01' },
    pants = { 'Clothing', 'Pants', 'common_pants_01' },
    robe = { 'Clothing', 'Robe', 'common_robe_01' },
    skirt = { 'Clothing', 'Skirt', 'common_skirt_01' },
    lglove = { 'Clothing', 'LGlove', 'common_glove_left_01' },
    rglove = { 'Clothing', 'RGlove', 'common_glove_right_01' },
    shoes = { 'Clothing', 'Shoes', 'common_shoes_01' },
    cuirass = { 'Armor', 'Cuirass', 'iron_cuirass' },
    greaves = { 'Armor', 'Greaves', 'iron_greaves' },
    boots = { 'Armor', 'Boots', 'iron_boots' },
    lgauntlet = { 'Armor', 'LGauntlet', 'iron_gauntlet_left' },
    rgauntlet = { 'Armor', 'RGauntlet', 'iron_gauntlet_right' },
    lpauldron = { 'Armor', 'LPauldron', 'iron_pauldron_left' },
    rpauldron = { 'Armor', 'RPauldron', 'iron_pauldron_right' },
    helmet = { 'Armor', 'Helmet', 'iron_helmet' },
}
local KIND_ORDER = { 'shirt', 'pants', 'robe', 'skirt', 'lglove', 'rglove', 'shoes', 'cuirass', 'greaves', 'boots',
    'lgauntlet', 'rgauntlet', 'lpauldron', 'rpauldron', 'helmet' }

-- The NPC row: no beast races (they wear no boots or helmets).
local ROW = {
    { race = 'breton', male = true, kinds = {} },
    { race = 'dark elf', male = false, kinds = { 'shirt' } },
    { race = 'nord', male = true, kinds = { 'pants' } },
    { race = 'redguard', male = false, kinds = { 'cuirass', 'greaves' } },
    { race = 'imperial', male = true, kinds = { 'robe' } },
    { race = 'high elf', male = false, kinds = { 'lglove', 'rglove', 'shoes' } },
    { race = 'orc', male = true, kinds = { 'lgauntlet', 'rgauntlet', 'boots' } },
    { race = 'wood elf', male = false, kinds = { 'shirt', 'pants', 'cuirass', 'greaves', 'boots', 'lgauntlet',
        'rgauntlet', 'lpauldron', 'rpauldron', 'helmet' } },
}
local SPACING = 70 -- units between two NPCs
local DISTANCE = 300 -- units in front of the player

local function resolve(kind)
    local spec = KINDS[kind]
    local recordType = types[spec[1]]
    local wanted = recordType.TYPE[spec[2]]
    local ok, record = pcall(recordType.record, spec[3])
    if ok and record and record.type == wanted then
        return record.id
    end
    local first = nil
    for _, r in ipairs(recordType.records) do
        if r.type == wanted and (first == nil or r.id < first) then
            first = r.id
        end
    end
    return first
end

local function npcRecord(race, male)
    for _, record in ipairs(types.NPC.records) do
        if string.lower(tostring(record.race or '')) == race and record.isMale == male then
            return record.id
        end
    end
    return nil
end

local function give(container, id)
    if id then
        world.createObject(id, 1):moveInto(types.Actor.inventory(container))
    end
end

return {
    eventHandlers = {
        MBEquipSetup = function(data)
            local player = world.players[1]
            local ids = {}
            for _, kind in ipairs(KIND_ORDER) do
                ids[kind] = resolve(kind)
                print('MBEQUIP item ' .. kind .. ' ' .. tostring(ids[kind]))
                give(player, ids[kind])
            end
            for index, entry in ipairs(ROW) do
                local id = npcRecord(entry.race, entry.male)
                if id then
                    local object = world.createObject(id, 1)
                    local inventory = types.Actor.inventory(object)
                    for _, item in ipairs(inventory:getAll()) do
                        item:remove()
                    end
                    local names = {}
                    for _, kind in ipairs(entry.kinds) do
                        give(object, ids[kind])
                        names[#names + 1] = kind
                    end
                    local x = (index - (#ROW + 1) / 2) * SPACING
                    local position = data.origin + data.forward * DISTANCE + data.right * x
                    object:teleport(player.cell, position, util.transform.rotateZ(data.yaw + math.pi))
                    print('MBEQUIP npc ' .. index .. ' ' .. entry.race .. ' ' .. (entry.male and 'm' or 'f') .. ' '
                        .. tostring(id) .. ' wears ' .. (#names > 0 and table.concat(names, '+') or 'nothing'))
                else
                    print('MBEQUIP no record for ' .. entry.race)
                end
            end
            player:sendEvent('MBEquipItems', { ids = ids })
        end,
    },
}
