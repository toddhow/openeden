-- tools/neutral_session_check (task 0202): the global half of the object demo (objectsdemo.sh): a script moves a barrel,
-- fades another out and places a new object; this prints what the ENGINE has afterwards.
--
-- Phases (scripts/n6/phase.lua, written by the driver): "ref" (a long run with no save), "save" (the same, saving once the
-- changes have settled) and "load" (a new process that loads that save). The Papyrus host starts some wall seconds after
-- the game does (its quest data is read on a worker), so nothing here is timed in advance: every 0.1 simulation seconds it
-- looks at every object of the demo base in the player's cell and logs the ones whose enabled state or position changed
-- ("N9OBJ t=<s> <id> enabled=<bool> x,y,z"); a change asks for pictures at +0.1, +0.5, +0.9 and +1.5 s ("N9SHOT"); three
-- seconds after the last change the rays of cfg.rays are cast ("N9RAY", player script), and in "save" the game is saved a
-- second later. In "load" the simulation time carries over from the save and the Papyrus host restores its state some
-- seconds after the start, so the rays are cast cfg.rayAtLoad seconds after the FIRST update (and, with resave.lua
-- returning true, the loaded game is saved again to a second slot). The run ends itself (core.quit) after the rays and
-- the save.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')
local types = require('openmw.types')
local storage = require('openmw.storage')
local phase = require('scripts.n6.phase')
local cfg = require('scripts.n9.cfg')
local resave = require('scripts.n9.resave')

local DEMO = 'skyrimse:b83888128'
local last = {}
local nextLog, placed, pitched, rayed = 0, false, false, false
local changes, lastChange = 0, -1
local pending = {}
local shotsTaken = 0
local quitAt = nil
local t0 = nil

local function key(o)
    return string.format('%s|%.1f,%.1f,%.1f', tostring(o.enabled), o.position.x, o.position.y, o.position.z)
end

local function shot(t, label)
    shotsTaken = shotsTaken + 1
    print(string.format('N9SHOT %d t=%.2f %s', shotsTaken, t, label))
    world.players[1]:sendEvent('N9Shot')
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            local t = core.getSimulationTime()
            if t0 == nil then t0 = t end
            if not placed and t > 1 and phase ~= 'load' then
                placed = true
                player:teleport(player.cell, util.vector3(-128, 0, 170), { rotation = util.transform.rotateZ(0) })
            end
            if not pitched and t > 2 then
                pitched = true
                player:sendEvent('N9Pitch', 0.6)
                pending[#pending + 1] = { t = t + 1.5, label = 'start' }
                if phase == 'load' then
                    pending[#pending + 1] = { t = t + 3, label = 'after the load' }
                    pending[#pending + 1] = { t = t + 5, label = 'after the load' }
                end
            end
            if t >= nextLog then
                nextLog = t + 0.1
                local list = {}
                for _, o in ipairs(player.cell:getAll()) do
                    if not types.Actor.objectIsInstance(o) and tostring(o.recordId) == DEMO then list[#list + 1] = o end
                end
                table.sort(list, function(a, b) return tostring(a.id) < tostring(b.id) end)
                for _, o in ipairs(list) do
                    local id = tostring(o.id)
                    local k = key(o)
                    if last[id] ~= k then
                        local first = last[id] == nil
                        last[id] = k
                        print(string.format('N9OBJ t=%.2f %s enabled=%s %.1f,%.1f,%.1f', t, id, tostring(o.enabled),
                            o.position.x, o.position.y, o.position.z))
                        if t > 3 then
                            changes = changes + 1
                            lastChange = t
                            for _, dt in ipairs({ 0.1, 0.5, 0.9, 1.5 }) do
                                pending[#pending + 1] = { t = t + dt, label = (first and 'new ' or 'change ') .. id }
                            end
                        end
                    end
                end
            end
            table.sort(pending, function(a, b) return a.t < b.t end)
            while #pending > 0 and pending[1].t <= t do
                local p = table.remove(pending, 1)
                shot(t, p.label)
            end
            if not rayed then
                local due = false
                if phase == 'load' then due = t - t0 >= cfg.rayAtLoad
                else due = changes > 0 and t - lastChange >= 3 end
                if due then
                    rayed = true
                    player:sendEvent('N9Rays', cfg.rays)
                    pending[#pending + 1] = { t = t + 0.5, label = 'rays done' }
                    if phase == 'save' or (phase == 'load' and resave) then
                        storage.globalSection('N9'):set('saveNow', t)
                    end
                    quitAt = t + 3
                end
            end
            if quitAt ~= nil and t >= quitAt then
                quitAt = nil
                print('N9 done')
                core.quit()
            end
        end,
    },
}
