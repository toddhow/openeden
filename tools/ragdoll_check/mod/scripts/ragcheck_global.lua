-- Task 0106 check: after the scene has settled, stand the player 250 units south of the nearest NPC, facing it, then
-- kill that NPC. The player script takes the screenshots.
local world = require('openmw.world')
local core = require('openmw.core')
local types = require('openmw.types')
local util = require('openmw.util')

local SETTLE = 8
local DISTANCE = 450
local KILL_AFTER = 1.5
local startReal = nil
local phase = 0
local target = nil
local killAt = 0

return {
    engineHandlers = {
        onUpdate = function()
            startReal = startReal or core.getSimulationTime()
            local t = core.getSimulationTime() - startReal
            if phase == 0 and t >= SETTLE then
                local player = world.players[1]
                local best, bestDistance = nil, 1e9
                for _, actor in ipairs(world.activeActors) do
                    if actor ~= player and types.NPC.objectIsInstance(actor) then
                        local d = (actor.position - player.position):length()
                        if d < bestDistance then best, bestDistance = actor, d end
                    end
                end
                if best == nil then
                    print('RAG no npc')
                    core.quit()
                    return
                end
                target = best
                local pos = best.position + util.vector3(0, -DISTANCE, 0)
                player:teleport(best.cell, pos, { rotation = util.transform.rotateZ(0) })
                print('RAG target ' .. best.recordId .. ' at ' .. tostring(best.position))
                killAt = t + KILL_AFTER
                phase = 1
            elseif phase == 1 and t >= killAt then
                target:sendEvent('RagKill')
                world.players[1]:sendEvent('RagStart')
                print('RAG killed')
                phase = 2
            end
        end,
    },
}
