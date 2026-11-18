-- tools/neutral_session_check (task 0291, swimming and water): once the world around the start place has loaded, drops the
-- player from above the water at (x, y) of swimcfg.lua and tells the player script to run the swim scenario.
local world = require('openmw.world')
local util = require('openmw.util')
local cfg = require('scripts.swim291.swimcfg')

local frames = 0
local sent = false

return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            frames = frames + 1
            if cfg.probe and not sent and frames == cfg.startFrame - 60 then
                player:sendEvent('Swim291Probe', cfg.probe)
            end
            if not sent and frames >= cfg.startFrame then
                sent = true
                pcall(function() world.mwscript.getGlobalVariables(player).gamehour = 12 end)
                player:sendEvent('Swim291God', {})
                if cfg.keep then
                    print(string.format('S291 start where the player is: %.0f,%.0f,%.0f', player.position.x, player.position.y, player.position.z))
                else
                    print(string.format('S291 drop to %.0f,%.0f,%.0f', cfg.x, cfg.y, cfg.z))
                    player:teleport(player.cell, util.vector3(cfg.x, cfg.y, cfg.z), { rotation = util.transform.rotateZ(cfg.yaw) })
                end
                player:sendEvent('Swim291Go', { water = cfg.water })
            end
        end,
    },
}
