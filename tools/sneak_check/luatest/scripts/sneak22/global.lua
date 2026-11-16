-- tools/sneak_check (task 0227): the global half. Waits for the pack's sneak namespace, then every half second of
-- simulation time logs the sneak eye and the sneak attack message id (SNEAKCHECK lines) and runs the steps of cfg.steps:
--   { t, 'dump' }                        logs every NPC of the player's cell (record id, position, heading)
--   { t, 'place', x, y, z, heading, pitch }   puts the player there (Lua may place him; the KEYS are real)
--   { t, 'shot' }                         asks the player script for a screenshot
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')
local types = require('openmw.types')
local cfg = require('scripts.sneak22.cfg')
local okSneak, sneak = pcall(require, 'core.skyrimse.sneak')
if not okSneak then
    print('SNEAKCHECK no sneak namespace')
    return {}
end

local nextLog, t0 = 0, nil
local done = {}
return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            t0 = t0 or core.getSimulationTime()
            local t = core.getSimulationTime() - t0
            if t >= nextLog then
                nextLog = t + 0.5
                print(string.format('SNEAKCHECK t=%.1f eye=%d msgid=%d msg=%s pos=%.0f,%.0f', t, sneak.eye(), sneak.message_id(), sneak.message(), player.position.x, player.position.y))
            end
            for i, s in ipairs(cfg.steps) do
                if not done[i] and t >= s[1] then
                    done[i] = true
                    if s[2] == 'dump' then
                        local wanted = { ['skyrimse:b80803'] = 'Hulda', ['skyrimse:b108144'] = 'Mikael', ['skyrimse:b80807'] = 'Brenuin',
                            ['skyrimse:b529333'] = 'Sinmir', ['skyrimse:b80802'] = 'Saadia', ['skyrimse:b50347871'] = 'Lucia' }
                        for _, n in ipairs(player.cell:getAll()) do
                            local name = wanted[tostring(n.recordId)]
                            if name then
                                print(string.format('SNEAKCHECK npc %s at %.0f,%.0f,%.0f yaw %.2f', name, n.position.x, n.position.y, n.position.z, n.rotation:getYaw()))
                            end
                        end
                        print(string.format('SNEAKCHECK player at %.0f,%.0f,%.0f', player.position.x, player.position.y, player.position.z))
                    elseif s[2] == 'place' then
                        player:teleport(player.cell, util.vector3(s[3], s[4], s[5]), { rotation = util.transform.rotateZ(s[6]) })
                        player:sendEvent('SneakLook', s[7] or 0)
                        print(string.format('SNEAKCHECK placed at %.0f,%.0f,%.0f heading %.2f', s[3], s[4], s[5], s[6]))
                    elseif s[2] == 'shot' then
                        player:sendEvent('SneakShot')
                        print(string.format('SNEAKCHECK shot at t=%.1f eye=%d', t, sneak.eye()))
                    end
                end
            end
        end,
    },
}
