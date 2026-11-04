-- Task 0069 stage e review: the global half of the torch scene of tools/mergedbody_check (SCENE=torches). Twenty
-- dressed NPCs, one of each race and sex (the beast races too), stand in four lines of five behind the player, each
-- with a torch in its inventory. The game hour is set by the player script (MBTorchHour): at night every NPC equips its
-- torch in the same frame (Actors::update calls updateEquippedLight for all of them once a second) and by day it puts it
-- away, which is what dusk and dawn do in the game. It is only part of the check, never of the game, and uses the
-- player's own game data.
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
local KIND_ORDER = { 'shirt', 'pants', 'robe', 'skirt', 'lglove', 'rglove', 'shoes', 'cuirass', 'greaves',
    'boots', 'lgauntlet', 'rgauntlet', 'lpauldron', 'rpauldron', 'helmet' }
-- The full set of the armour phase (MBTorchArmour). The beast races wear no boots or helmet.
local FULL_ARMOUR = { Cuirass = 'cuirass', Greaves = 'greaves', Boots = 'boots', LeftGauntlet = 'lgauntlet',
    RightGauntlet = 'rgauntlet', LeftPauldron = 'lpauldron', RightPauldron = 'rpauldron', Helmet = 'helmet' }
local BEAST_ARMOUR = { Cuirass = 'cuirass', Greaves = 'greaves', LeftGauntlet = 'lgauntlet', RightGauntlet = 'rgauntlet',
    LeftPauldron = 'lpauldron', RightPauldron = 'rpauldron' }
local spawned = {} -- { object, beast } of every NPC placed

-- What each NPC wears (slot name, types.Actor.EQUIPMENT_SLOT = kind). The beast races wear no shoes.
local SHIRT_PANTS = { Shirt = 'shirt', Pants = 'pants' }
local ROBE = { Robe = 'robe' }
local ARMOUR = { Cuirass = 'cuirass', Greaves = 'greaves' }
local SHIRT_SKIRT = { Shirt = 'shirt', Skirt = 'skirt' }
local GLOVES_PANTS = { Pants = 'pants', LeftGauntlet = 'lglove', RightGauntlet = 'rglove', Boots = 'shoes' }
local ROW = {
    { race = 'argonian', male = true, slots = SHIRT_PANTS },
    { race = 'argonian', male = false, slots = ROBE },
    { race = 'khajiit', male = true, slots = ARMOUR },
    { race = 'khajiit', male = false, slots = SHIRT_SKIRT },
    { race = 'breton', male = true, slots = GLOVES_PANTS },
    { race = 'breton', male = false, slots = SHIRT_PANTS },
    { race = 'dark elf', male = true, slots = ROBE },
    { race = 'dark elf', male = false, slots = ARMOUR },
    { race = 'high elf', male = true, slots = SHIRT_SKIRT },
    { race = 'high elf', male = false, slots = GLOVES_PANTS },
    { race = 'imperial', male = true, slots = SHIRT_PANTS },
    { race = 'imperial', male = false, slots = ROBE },
    { race = 'nord', male = true, slots = ARMOUR },
    { race = 'nord', male = false, slots = SHIRT_SKIRT },
    { race = 'orc', male = true, slots = GLOVES_PANTS },
    { race = 'orc', male = false, slots = SHIRT_PANTS },
    { race = 'redguard', male = true, slots = ROBE },
    { race = 'redguard', male = false, slots = ARMOUR },
    { race = 'wood elf', male = true, slots = SHIRT_SKIRT },
    { race = 'wood elf', male = false, slots = GLOVES_PANTS },
}
local PER_LINE = 5
local SPACING = 75 -- units between two NPCs of a line
local DISTANCE = 300 -- units behind the player to the first line (in front, Balmora's hill pushed the lines together)
local LINE_GAP = 110 -- units between two lines

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

-- The first carriable light by id whose id names a torch; else the first carriable light.
local function torchId()
    local torch, any = nil, nil
    for _, r in ipairs(types.Light.records) do
        if r.isCarriable then
            if string.find(string.lower(r.id), 'torch', 1, true) and (torch == nil or r.id < torch) then
                torch = r.id
            end
            if any == nil or r.id < any then
                any = r.id
            end
        end
    end
    return torch or any
end

local function npcRecord(race, male)
    for _, record in ipairs(types.NPC.records) do
        -- Never the player's own record (it is a dark elf man in Morrowind.esm).
        if string.lower(tostring(record.race or '')) == race and record.isMale == male and record.id ~= 'player' then
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
        MBTorchHour = function(data)
            world.mwscript.getGlobalVariables().gamehour = data.hour
            print('MBTORCH hour ' .. data.hour)
        end,
        MBTorchSetup = function(data)
            world.mwscript.getGlobalVariables().gamehour = 12
            local player = world.players[1]
            local ids = {}
            for _, kind in ipairs(KIND_ORDER) do
                ids[kind] = resolve(kind)
                print('MBTORCH item ' .. kind .. ' ' .. tostring(ids[kind]))
            end
            local torch = torchId()
            print('MBTORCH torch ' .. tostring(torch))
            -- The player's clothes for the paper doll frame.
            for _, kind in ipairs({ 'shirt', 'pants', 'shoes', 'lglove', 'rglove' }) do
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
                    local line = math.floor((index - 1) / PER_LINE)
                    local column = (index - 1) % PER_LINE - (PER_LINE - 1) / 2
                    local position = data.origin - data.forward * (DISTANCE + line * LINE_GAP)
                        + data.right * (column * SPACING + (line % 2) * SPACING / 2)
                    -- Facing the way the player faces: towards the camera.
                    object:teleport(player.cell, position, util.transform.rotateZ(data.yaw))
                    local names = {}
                    local equipment = {}
                    for slot, kind in pairs(entry.slots) do
                        give(object, ids[kind])
                        equipment[slot] = ids[kind]
                        names[#names + 1] = kind
                    end
                    give(object, torch)
                    table.sort(names)
                    object:addScript('scripts/mbtorch_npc.lua', { equipment = equipment })
                    local beast = entry.race == 'argonian' or entry.race == 'khajiit'
                    spawned[#spawned + 1] = { object = object, beast = beast }
                    print('MBTORCH npc ' .. index .. ' ' .. entry.race .. ' ' .. (entry.male and 'm' or 'f') .. ' '
                        .. tostring(id) .. ' wears ' .. table.concat(names, '+') .. ' and carries a torch')
                else
                    print('MBTORCH no record for ' .. entry.race)
                end
            end
            player:sendEvent('MBTorchItems', { ids = ids })
        end,
        MBTorchArmour = function(data)
            -- Every NPC puts on the full iron set over its clothes (the torch stays in its hand, mbtorch_npc.lua).
            for _, entry in ipairs(spawned) do
                local equipment = {}
                for slot, kind in pairs(entry.beast and BEAST_ARMOUR or FULL_ARMOUR) do
                    give(entry.object, data.ids[kind])
                    equipment[slot] = data.ids[kind]
                end
                entry.object:sendEvent('MBTorchWear', { equipment = equipment })
            end
            print('MBTORCH armour ' .. #spawned)
        end,
    },
}
