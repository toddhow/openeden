-- tools/neutral_session_check (task 0142 follow-up): logs the player's position every half real second for 14 s, so a start that
-- throws the player away shows in the log ("POSLOG t=1.0 x,y,z").
local core = require('openmw.core')
local world = require('openmw.world')

local start, last = nil, -1
return {
    engineHandlers = {
        onUpdate = function()
            local player = world.players[1]
            if player == nil then return end
            start = start or core.getRealTime()
            local t = core.getRealTime() - start
            if t - last >= 0.5 and t <= 14 then
                last = t
                local p = player.position
                print(string.format('POSLOG t=%.1f %.0f,%.0f,%.0f', t, p.x, p.y, p.z))
            end
        end,
    },
}
