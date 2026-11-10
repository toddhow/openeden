-- Task 0098 move 4 check: after the startup script (script.txt, run with --script-run) has been run by the game's
-- script command set, prints what it did to the player, one line the check script greps for.
local core = require('openmw.core')
local types = require('openmw.types')
local self = require('openmw.self')
local debug = require('openmw.debug')

local startReal = nil
local done = false

return {
    engineHandlers = {
        onFrame = function()
            startReal = startReal or core.getRealTime()
            if done or core.getRealTime() - startReal < 4 then return end
            done = true
            local gold = types.Actor.inventory(self):countOf('gold_001')
            local strength = types.Actor.stats.attributes.strength(self).base
            local level = types.Actor.stats.level(self).current
            print(string.format('SCCHECK gold=%d strength=%d level=%d', gold, strength, level))
            debug.takeScreenshot()
        end,
    },
}
