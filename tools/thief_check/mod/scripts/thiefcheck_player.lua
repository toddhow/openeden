-- Task 0098 move 5 check: prints what the progression rules produced in this game, one line per kind, for the check
-- script to compare between two executables run with the same --random-seed:
--   * the player's spells (the player autocalc, run when a new game is built),
--   * the spells of every NPC near the player (the NPC autocalc, run when an NPC's stats are made),
--   * that the first creature levelled lists give something at level 10 (the levelled list rule).
local core = require('openmw.core')
local types = require('openmw.types')
local self = require('openmw.self')
local nearby = require('openmw.nearby')
local debug = require('openmw.debug')

local startReal = nil
local done = false

local function spellIds(actor)
    local ids = {}
    for _, spell in pairs(types.Actor.spells(actor)) do
        ids[#ids + 1] = spell.id
    end
    table.sort(ids)
    return table.concat(ids, ',')
end

return {
    engineHandlers = {
        onFrame = function()
            startReal = startReal or core.getRealTime()
            if done or core.getRealTime() - startReal < 4 then return end
            done = true
            print('THIEFCHECK player spells=' .. spellIds(self))
            local npcs = {}
            for _, actor in pairs(nearby.actors) do
                if types.NPC.objectIsInstance(actor) and actor ~= self.object then
                    npcs[#npcs + 1] = actor.recordId .. ':' .. spellIds(actor)
                end
            end
            table.sort(npcs)
            print('THIEFCHECK npcs=' .. #npcs .. ' ' .. table.concat(npcs, ';'))
            local drawn = 0
            local nonempty = 0
            local lists = types.LevelledCreature and types.LevelledCreature.records or {}
            local count = 0
            for _, record in pairs(lists) do
                count = count + 1
                if count > 40 then break end
                drawn = drawn + 1
                if record:getRandomId(10) ~= '' then nonempty = nonempty + 1 end
            end
            -- the world's generator is not the same in two runs (other systems draw from it), so the picks themselves
            -- cannot be compared; the unit tests pin them. Here: the rule runs, and most draws give something.
            print('THIEFCHECK levelled=' .. drawn .. ' mostly_nonempty=' .. tostring(nonempty * 2 > drawn))
            debug.takeScreenshot()
        end,
    },
}
