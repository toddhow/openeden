-- Task 0098 move 6 check: prints the disposition of every NPC near the player (types.NPC.getDisposition, which runs the
-- derived disposition rule) with the player's Personality at its own value, then 90, then 20, one line each, for the
-- check script to compare between two executables run with the same --random-seed. Each phase waits some frames so the
-- changed stat is in force.
local core = require('openmw.core')
local types = require('openmw.types')
local self = require('openmw.self')
local nearby = require('openmw.nearby')
local debug = require('openmw.debug')

local startReal = nil
local phase = 0
local phaseFrames = 0
local ownPersonality = nil

local function line(label)
    local npcs = {}
    for _, actor in pairs(nearby.actors) do
        if types.NPC.objectIsInstance(actor) and actor ~= self.object then
            npcs[#npcs + 1] = actor.recordId .. '=' .. types.NPC.getDisposition(actor, self.object)
        end
    end
    table.sort(npcs)
    print('SOCIALCHECK ' .. label .. ' npcs=' .. #npcs .. ' ' .. table.concat(npcs, ';'))
end

return {
    engineHandlers = {
        onFrame = function()
            startReal = startReal or core.getRealTime()
            if phase > 3 or core.getRealTime() - startReal < 4 then return end
            local personality = types.Actor.stats.attributes.personality(self)
            if phase == 0 then
                ownPersonality = personality.base
                line('own personality=' .. ownPersonality)
                personality.base = 90
                phase = 1
                phaseFrames = 0
            elseif phase == 1 then
                phaseFrames = phaseFrames + 1
                if phaseFrames > 5 then
                    line('personality=90')
                    personality.base = 20
                    phase = 2
                    phaseFrames = 0
                end
            elseif phase == 2 then
                phaseFrames = phaseFrames + 1
                if phaseFrames > 5 then
                    line('personality=20')
                    personality.base = ownPersonality
                    phase = 4
                    debug.takeScreenshot()
                end
            end
        end,
    },
}
