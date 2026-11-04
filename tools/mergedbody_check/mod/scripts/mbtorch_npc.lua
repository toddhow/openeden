-- Task 0069 stage e review: the NPC half of the torch scene of tools/mergedbody_check (SCENE=torches), attached by
-- mbtorch_global.lua with what the NPC is to wear ({ slot name = record id }). Lua's moveInto never equips, so this puts
-- the clothes on, then stops: the torch is left to the game (Actors::updateEquippedLight), and equipping again later
-- would take it out of the hand. It also drops the AI packages so the lines stand still. MBTorchWear (the armour phase)
-- adds a set of armour to the clothes and keeps whatever is in the left hand.
local self = require('openmw.self')
local types = require('openmw.types')
local I = require('openmw.interfaces')

local wanted = nil
local tries = 0
local MAX_TRIES = 90 -- frames: the items arrive through delayed actions

local function dressed()
    for slot, id in pairs(wanted) do
        local item = types.Actor.getEquipment(self, types.Actor.EQUIPMENT_SLOT[slot])
        if item == nil or item.recordId ~= id then
            return false
        end
    end
    return true
end

return {
    eventHandlers = {
        MBTorchWear = function(data)
            for slot, id in pairs(data.equipment) do
                wanted[slot] = id
            end
            local held = types.Actor.getEquipment(self, types.Actor.EQUIPMENT_SLOT.CarriedLeft)
            wanted.CarriedLeft = held and held.recordId or nil
            tries = 1
        end,
    },
    engineHandlers = {
        onInit = function(data)
            wanted = data and data.equipment or {}
        end,
        onUpdate = function()
            if wanted == nil or tries >= MAX_TRIES then
                return
            end
            tries = tries + 1
            if tries == 1 then
                pcall(function()
                    I.AI.removePackages()
                end)
            end
            if dressed() then
                tries = MAX_TRIES
                return
            end
            local equipment = {}
            for slot, id in pairs(wanted) do
                equipment[types.Actor.EQUIPMENT_SLOT[slot]] = id
            end
            types.Actor.setEquipment(self, equipment)
        end,
    },
}
