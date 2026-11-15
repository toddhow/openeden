-- tools/neutral_session_check (task 0181): the global half of the Papyrus demo (papyrusdemo.sh).
--
-- Phases (scripts/n6/phase.lua, written by the driver): "ref" (a long run with no save), "save" (the same, saving once at
-- the simulation time cfg.saveAt) and "load" (a new process that loads that save). Every 0.5 simulation seconds it logs
-- the demo placements (base record b83888128) whose enabled state changed ("N7DEMO t=<s> <id> enabled=<bool>") and every
-- cfg.shotEvery simulation seconds it asks the player script for a picture ("N7SHOT <k> t=<s>"). In ref and save the
-- player is put at the first Mare spot facing the placements, 3 seconds in.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')
local types = require('openmw.types')
local storage = require('openmw.storage')
local phase = require('scripts.n6.phase')
local cfg = require('scripts.n7.cfg')

local DEMO = 'skyrimse:b83888128'
local lastState = {}
local nextLog, shots, placed, pitched = 0, 0, false, false
local nextShot = cfg.shotEvery

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            local t = core.getSimulationTime()
            if not placed and t > 3 and phase ~= 'load' then
                placed = true
                player:teleport(player.cell, util.vector3(-128, 0, 170), { rotation = util.transform.rotateZ(0) })
            end
            if not pitched and t > 4 then
                pitched = true
                player:sendEvent('N7Pitch', 0.6)
            end
            if phase == 'save' and t >= cfg.saveAt then storage.globalSection('N7'):set('saveNow', true) end
            if t >= nextLog then
                nextLog = t + 0.5
                local list = {}
                for _, o in ipairs(player.cell:getAll()) do
                    if not types.Actor.objectIsInstance(o) and tostring(o.recordId) == DEMO then list[#list + 1] = o end
                end
                table.sort(list, function(a, b) return tostring(a.id) < tostring(b.id) end)
                for _, o in ipairs(list) do
                    local id = tostring(o.id)
                    if lastState[id] ~= o.enabled then
                        lastState[id] = o.enabled
                        print(string.format('N7DEMO t=%.1f %s enabled=%s %.0f,%.0f,%.0f player %.0f,%.0f,%.0f', t, id, tostring(o.enabled), o.position.x, o.position.y, o.position.z, player.position.x, player.position.y, player.position.z))
                    end
                end
            end
            if t >= nextShot and t > 6 then
                nextShot = nextShot + cfg.shotEvery
                shots = shots + 1
                player:sendEvent('N7Shot')
                print(string.format('N7SHOT %d t=%.1f', shots, t))
            end
        end,
    },
}
