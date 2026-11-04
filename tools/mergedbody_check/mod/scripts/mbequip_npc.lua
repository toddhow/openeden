-- Task 0069 stage e: the NPC half of the equipment scene of tools/mergedbody_check (SCENE=equipment), attached by
-- mbequip_global.lua to each NPC of the row with what it is to wear ({ slot name = record id }). Items that Lua moves
-- into an inventory are never put on by the NPC itself (objectbindings.cpp, moveInto: no auto-equip), so this script
-- equips them, and drops the NPC's AI packages so the row stands still for its frame.
local self = require('openmw.self')
local types = require('openmw.types')
local I = require('openmw.interfaces')

local wanted = nil
local tries = 0
local MAX_TRIES = 120 -- frames: the items arrive through delayed actions

return {
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
            local equipment = {}
            for slot, id in pairs(wanted) do
                equipment[types.Actor.EQUIPMENT_SLOT[slot]] = id
            end
            types.Actor.setEquipment(self, equipment)
        end,
    },
}
