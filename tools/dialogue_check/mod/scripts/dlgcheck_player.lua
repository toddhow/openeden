-- Task 0085 move 3 stage 3c check: starts a conversation with a nearby NPC through the Lua UI interface (the greeting,
-- the topic list and the journal run through the journal and dialogue manager the Morrowind game pack made) and takes a screenshot of the dialogue window and of the journal.
local core = require('openmw.core')
local types = require('openmw.types')
local self = require('openmw.self')
local nearby = require('openmw.nearby')
local I = require('openmw.interfaces')
local debug = require('openmw.debug')

local SETTLE = 8
local startReal = nil
local step = 0
local stepStart = 0
local npc = nil

local function pickNpc()
    for _, actor in ipairs(nearby.actors) do
        if actor ~= self.object and types.NPC.objectIsInstance(actor) and types.Actor.stats.dynamic.health(actor).current > 0 then
            return actor
        end
    end
    return nil
end

local function shot(name)
    debug.takeScreenshot()
    print('DLG shot ' .. name)
end

return {
    engineHandlers = {
        onFrame = function()
            startReal = startReal or core.getRealTime()
            local t = core.getRealTime() - startReal - SETTLE
            if t < 0 then return end
            local dt = t - stepStart
            if step == 0 then
                debug.toggleGodMode()
                npc = pickNpc()
                print('DLG npc ' .. tostring(npc and npc.recordId))
                step = 1
                stepStart = t
                I.UI.setMode('Dialogue', { target = npc })
                print('DLG open dialogue')
            elseif step == 1 then
                if dt >= 3.0 then
                    shot('dialogue')
                    step = 2
                    stepStart = t
                end
            elseif step == 2 then
                if dt >= 2.0 then
                    I.UI.setMode()
                    I.UI.setMode('Journal')
                    print('DLG open journal')
                    step = 3
                    stepStart = t
                end
            elseif step == 3 then
                if dt >= 3.0 then
                    shot('journal')
                    step = 4
                    stepStart = t
                end
            elseif dt >= 1.5 then
                print('DLG done')
                core.quit()
            end
        end,
    },
}
