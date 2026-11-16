-- tools/neutral_session_check (task 0260, vampirism): runs cfg.plan, a list of steps at simulation seconds:
--   { t = 5, adv = 73 }                     world.advanceTime(73): the game clock moves 73 hours (needs the pack setting rest)
--   { t = 5, hour = 12 }                    sets the game hour and stops the clock (timescale 0)
--   { t = 5, door = 'b152263' }             activates the nearest door with that record id (the Mare's front door)
--   { t = 5, place = 'b108144', dist = 150 } stands the player in front of the actor, facing it
--   { t = 5, activate = 'b108144' }         activates the actor (a vampire feeds on a sleeper)
--   { t = 5, shot = true }                  a screenshot
--   { t = 5, save = true }                  asks the menu script to save
--   { t = 5, say = 'text' }                 a VAMP line in the log
-- cfg is written by check.sh from V_PLAN.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')
local storage = require('openmw.storage')
local cfg = require('scripts.vamp.cfg')

local index = 1
local t0 = nil

local function find(cell, tag)
    for _, o in ipairs(cell:getAll()) do
        if string.find(o.recordId, tag, 1, true) then return o end
    end
    return nil
end

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            local t = core.getSimulationTime()
            t0 = t0 or t
            local step = cfg.plan[index]
            if step == nil or t - t0 < step.t then return end
            index = index + 1
            if step.adv then
                print(string.format('VAMP advancing the clock %.1f hours (game time %.0f)', step.adv, core.getGameTime()))
                world.advanceTime(step.adv)
            end
            if step.hour then
                world.mwscript.getGlobalVariables(player).gamehour = step.hour
                world.mwscript.getGlobalVariables(player).timescale = 0
                print('VAMP hour ' .. tostring(step.hour))
            end
            if step.door then
                local d = find(player.cell, step.door)
                if d then print('VAMP door'); d:activateBy(player) else print('VAMP door not found') end
            end
            if step.place then
                local a = find(player.cell, step.place)
                if a == nil then print('VAMP actor not found ' .. step.place) else
                    local p = a.position
                    local yaw = a.rotation:getYaw()
                    local px, py = p.x + math.sin(yaw) * (step.dist or 150), p.y + math.cos(yaw) * (step.dist or 150)
                    player:teleport(player.cell, util.vector3(px, py, p.z + 2),
                        { rotation = util.transform.rotateZ(math.atan2(p.x - px, p.y - py)) })
                    print(string.format('VAMP placed in front of %s', step.place))
                end
            end
            if step.activate then
                local a = find(player.cell, step.activate)
                if a then print('VAMP activating ' .. step.activate); a:activateBy(player) else print('VAMP activate target not found') end
            end
            if step.shot then
                player:sendEvent('N5Shot')
                print(string.format('VAMP shot at %.1f', t - t0))
            end
            if step.save then
                print('VAMP saving')
                storage.globalSection('N8CRIME'):set('saveNow', true)
            end
            if step.say then print('VAMP ' .. step.say) end
        end,
    },
}
