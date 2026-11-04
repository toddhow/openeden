-- Task 0069 stage e: the global half of the equipment scene of tools/mergedbody_check (SCENE=equipment). It gives the
-- player one piece of every kind of clothing and armour the scene equips, and places a row of NPCs behind the player,
-- each wearing one combination: its own items leave the inventory before it enters the world, the scene's items are
-- moved in, and mbequip_npc.lua puts them on. It is only part of the check, never of the game, and uses the player's
-- own game data.
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

-- The NPC row: no beast races (they wear no boots or helmets). slots: slot name (types.Actor.EQUIPMENT_SLOT) = kind.
local ROW = {
    { race = 'breton', male = true, slots = {} },
    { race = 'dark elf', male = false, slots = { Shirt = 'shirt' } },
    { race = 'nord', male = true, slots = { Pants = 'pants' } },
    { race = 'redguard', male = false, slots = { Cuirass = 'cuirass', Greaves = 'greaves' } },
    { race = 'imperial', male = true, slots = { Robe = 'robe' } },
    { race = 'high elf', male = false, slots = { LeftGauntlet = 'lglove', RightGauntlet = 'rglove', Boots = 'shoes' } },
    { race = 'orc', male = true, slots = { LeftGauntlet = 'lgauntlet', RightGauntlet = 'rgauntlet', Boots = 'boots' } },
    { race = 'wood elf', male = false, slots = { Shirt = 'shirt', Pants = 'pants', Cuirass = 'cuirass',
        Greaves = 'greaves', Boots = 'boots', LeftGauntlet = 'lgauntlet', RightGauntlet = 'rgauntlet',
        LeftPauldron = 'lpauldron', RightPauldron = 'rpauldron', Helmet = 'helmet' } },
}
local SPACING = 70 -- units between two NPCs of a line
local DISTANCE = 320 -- units behind the player (the player's own frames look at it from the front)
local LINE_GAP = 130 -- units between the two lines of four (one line of eight ran into the hill at Balmora's start)

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
                    local line = math.floor((index - 1) / 4)
                    local x = -(((index - 1) % 4) + 1 + line / 2) * SPACING
                    local position = data.origin - data.forward * (DISTANCE + line * LINE_GAP) + data.right * x
                    -- Facing the way the player faces: towards the player's back and the row camera.
                    object:teleport(player.cell, position, util.transform.rotateZ(data.yaw))
                    -- An NPC never puts on what Lua moves into its inventory: mbequip_npc.lua equips it.
                    local names = {}
                    local equipment = {}
                    for slot, kind in pairs(entry.slots) do
                        give(object, ids[kind])
                        equipment[slot] = ids[kind]
                        names[#names + 1] = kind
                    end
                    table.sort(names)
                    object:addScript('scripts/mbequip_npc.lua', { equipment = equipment })
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
