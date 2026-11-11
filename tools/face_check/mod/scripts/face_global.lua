-- Task 0071 check: after the scene has settled, stand the player in front of the nearest NPC, facing it at close range,
-- turn the NPC to face the player and make it say the voice line (scripts/faceparams.lua, written by check.sh).
local world = require('openmw.world')
local core = require('openmw.core')
local types = require('openmw.types')
local util = require('openmw.util')
local params = require('scripts.faceparams')

local SETTLE = 6
local startReal = nil
local phase = 0

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
                    print('FACE no npc')
                    core.quit()
                    return
                end
                -- the NPC stays where it is, turned toward -Y; the player stands in front of it, looking along +Y
                best:teleport(best.cell, best.position, { rotation = util.transform.rotateZ(math.pi) })
                local pos = best.position + util.vector3(0, -(params.distance or 90), 0)
                player:teleport(best.cell, pos, { rotation = util.transform.rotateZ(0) })
                print('FACE target ' .. best.recordId .. ' at ' .. tostring(best.position))
                phase = 1
                startReal = core.getSimulationTime()
                t = 0
                params.sayAt = 1.0
                params.target = best
            elseif phase == 1 and t >= params.sayAt then
                if params.voice ~= 'none' then
                    core.sound.say(params.voice, params.target, 'test line')
                    print('FACE say ' .. params.voice)
                end
                world.players[1]:sendEvent('FaceStart')
                phase = 2
            end
        end,
    },
}
