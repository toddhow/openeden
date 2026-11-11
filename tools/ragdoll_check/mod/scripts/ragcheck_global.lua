-- Task 0106 check: after the scene has settled, stand the player DISTANCE units south of the nearest NPC, facing it, then
-- kill that NPC. The player script takes the screenshots. The scenario (scripts/ragparams.lua, written by check.sh):
--   still: the NPC stands where it is.
--   walk:  the NPC is sent walking north (a Travel package) and killed 4 s later, so the pose at death is a walk.
--   wall:  the NPC is first moved against the nearest wall (found by the NPC's own rays) and then killed.
-- 12 s after the kill (the ragdoll has settled) the corpse's position is printed, the player is stood beside it, the
-- NPC is activated by the player (a dead NPC opens the container window) and the player script reports the UI mode.
local world = require('openmw.world')
local core = require('openmw.core')
local types = require('openmw.types')
local util = require('openmw.util')
local params = require('scripts.ragparams')

local SETTLE = 8
local DISTANCE = params.distance or 450
local KILL_AFTER = 1.5
local startReal = nil
local phase = 0
local target = nil
local killAt = 0
local deathPos = nil
local lastPos, lastTime = nil, nil

local function startKill()
    deathPos = target.position
    target:sendEvent('RagKill')
    world.players[1]:sendEvent('RagStart')
    print('RAG killed at ' .. tostring(deathPos))
end

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
                if params.scenario == 'walk' then
                    best:sendEvent('RagWalk', { destination = best.position + util.vector3(0, 1500, 0) })
                    killAt = t + 4.0
                    phase = 1
                elseif params.scenario == 'wall' then
                    best:sendEvent('RagFindWall')
                    phase = 3
                else
                    killAt = t + KILL_AFTER
                    phase = 1
                end
            elseif phase == 1 then
                -- the target's speed, printed while it walks
                local now = core.getSimulationTime()
                if lastPos == nil then
                    lastPos, lastTime = target.position, now
                elseif now - lastTime >= 0.5 then
                    print(string.format('RAG speed %.1f', (target.position - lastPos):length() / (now - lastTime)))
                    lastPos, lastTime = target.position, now
                end
                if t >= killAt then
                    startKill()
                    killAt = t + 12.0
                    phase = 2
                end
            elseif phase == 5 and t >= killAt then
                -- the wall scenario: the NPC stood against the wall for a moment, now it dies
                startKill()
                killAt = t + 12.0
                phase = 2
            elseif phase == 2 and t >= killAt then
                print('RAG corpse now at ' .. tostring(target.position) .. ' moved '
                    .. string.format('%.1f', (target.position - deathPos):length()))
                local player = world.players[1]
                player:teleport(target.cell, target.position + util.vector3(0, -160, 0),
                    { rotation = util.transform.rotateZ(0) })
                target:activateBy(player)
                player:sendEvent('RagLoot', { npc = target })
                phase = 4
            end
        end,
    },
    eventHandlers = {
        RagWall = function(data)
            if phase ~= 3 then return end
            -- the wall the NPC's own rays found: stand the NPC 3 units off it, then (1 s later) kill it
            local pos = data.hitPos + data.normal * 3
            target:teleport(target.cell, util.vector3(pos.x, pos.y, target.position.z))
            print('RAG wall at ' .. tostring(data.hitPos) .. ' normal ' .. tostring(data.normal))
            killAt = core.getSimulationTime() - startReal + 1.0
            phase = 5
        end,
    },
}
