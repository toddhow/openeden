-- tools/neutral_session_check (task 0222): the global half of "pbody". Sets the hour of the day and stops the clock once, so
-- the light is the same in every run (cfg.hour; nil leaves the game's own).
local core = require('openmw.core')
local world = require('openmw.world')

local cfg = require('scripts.pbody.cfg')
local done = false

return {
    engineHandlers = {
        onUpdate = function()
            if done or cfg.hour == nil then return end
            local player = world.players[1]
            if player == nil then return end
            done = true
            world.mwscript.getGlobalVariables(player).gamehour = cfg.hour
            world.mwscript.getGlobalVariables(player).timescale = 0
            print('PBODY hour ' .. cfg.hour)
        end,
    },
}
