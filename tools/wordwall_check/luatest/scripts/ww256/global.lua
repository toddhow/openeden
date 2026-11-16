-- tools/wordwall_check (task 0256): stands the player before a word wall (cfg.pos, facing cfg.look), then prints the
-- shout state and the messages the pack announced every second. In the "run" phase it saves at cfg.saveAt seconds.
local core = require('openmw.core')
local world = require('openmw.world')
local util = require('openmw.util')
local storage = require('openmw.storage')
local cfg = require('scripts.ww256.cfg')
local shouts = require('core.skyrimse.shouts')

local t0, last, placed, saved, shown = nil, -10, false, false, 0
local soulAt, unlockAt, soulGiven, shouted = nil, nil, false, false

return {
    engineHandlers = {
        onUpdate = function()
            if not shouts.ready() then return end
            t0 = t0 or core.getSimulationTime()
            local t = core.getSimulationTime() - t0
            local player = world.players[1]
            if player == nil then return end
            if not placed and t >= cfg.placeAt and cfg.pos then
                placed = true
                local dx, dy = cfg.look[1] - cfg.pos[1], cfg.look[2] - cfg.pos[2]
                local heading = math.atan2(dx, dy)
                print(string.format('WW256 placing the player at %.0f %.0f %.0f heading %.3f', cfg.pos[1], cfg.pos[2], cfg.pos[3], heading))
                player:teleport(player.cell, util.vector3(cfg.pos[1], cfg.pos[2], cfg.pos[3]), { rotation = util.transform.rotateZ(heading) })
            end
            local count = shouts.notification_count()
            while shown < count do
                shown = shown + 1
                print('WW256 message ' .. shown .. ': ' .. shouts.notification(shown))
                storage.globalSection('WW256'):set('learnedAt', core.getSimulationTime())
                if cfg.unlock and cfg.unlock ~= '' and not soulAt then soulAt = t + 2 end
            end
            -- the learned word is unlocked with a dragon soul (there is no shout menu: the script plays the player)
            if soulAt and not soulGiven and t >= soulAt then
                soulGiven = true
                print('WW256 giving one dragon soul and unlocking ' .. cfg.unlock)
                shouts.add_souls(1)
                unlockAt = t + 1
            end
            if unlockAt and t >= unlockAt then
                unlockAt = nil
                shouts.unlock_word(cfg.unlock)
            end
            if not shouted and shouts.cooldown() > 0 then
                shouted = true
                storage.globalSection('WW256'):set('shoutedAt', core.getSimulationTime())
            end
            if t - last >= 1 then
                last = t
                local p = player.position
                print(string.format('WW256 t=%.1f at %.0f %.0f %.0f unlocked=%d souls=%d cooldown=%.2f', t, p.x, p.y, p.z,
                    shouts.unlocked_count(), shouts.souls(), shouts.cooldown()))
            end
            if cfg.phase == 'run' and cfg.saveAt > 0 and t >= cfg.saveAt and not saved then
                saved = true
                storage.globalSection('WW256'):set('saveNow', true)
            end
        end,
    },
}
