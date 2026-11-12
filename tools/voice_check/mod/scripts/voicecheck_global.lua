-- Task 0143 Morrowind-unchanged check: a Morrowind NPC says a vanilla voice line (an mp3 in Data Files/Sound/Vo) through
-- core.sound.say, the same SoundManager::say path the Skyrim voice uses. Prints when the line is active and when it ends.
local core = require('openmw.core')
local types = require('openmw.types')
local world = require('openmw.world')

local FILE = 'Sound/Vo/a/f/Atk_AF001.mp3'
local SETTLE = 6
local startReal = nil
local npc = nil
local said = false
local wasActive = false
local ended = false

local function pickNpc()
    for _, actor in ipairs(world.activeActors) do
        if types.NPC.objectIsInstance(actor) and not types.Player.objectIsInstance(actor) then
            return actor
        end
    end
    return nil
end

return {
    engineHandlers = {
        onUpdate = function()
            startReal = startReal or core.getRealTime()
            local t = core.getRealTime() - startReal - SETTLE
            if t < 0 then return end
            if not said then
                said = true
                npc = pickNpc()
                print('VOICECHECK npc ' .. tostring(npc and npc.recordId))
                if npc then
                    core.sound.say(FILE, npc)
                    print('VOICECHECK said ' .. FILE .. ' at ' .. string.format('%.2f', t))
                end
                return
            end
            if npc and not ended then
                local active = core.sound.isSayActive(npc)
                if active and not wasActive then
                    wasActive = true
                    print('VOICECHECK active at ' .. string.format('%.2f', t))
                elseif wasActive and not active then
                    ended = true
                    print('VOICECHECK ended at ' .. string.format('%.2f', t))
                end
            end
        end,
    },
}
