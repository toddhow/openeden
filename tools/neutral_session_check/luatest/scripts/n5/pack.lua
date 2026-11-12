-- tools/neutral_session_check (task 0135 stage S5): pictures of the Bannered Mare while its NPCs run their AI packages
-- (set in SETTINGS_EXTRA: "[extension.skyrimse]\nactor packages = true" and OPENEDEN_NEUTRAL_ACTOR_TRACE=1 in the
-- environment; the NACTWALK lines give each actor's position every 30 frames, NACTGOTO the goals and results).
-- OPENEDEN_NEUTRAL_ACTOR_HOUR pins the hour of the day. The player stands at VIEW looking at LOOK; a screenshot is taken
-- every SHOT_EVERY seconds. Started by "n5pack" (use at least 6000 frames).
local core = require('openmw.core')

local VIEW = { 60, -250, 72 }
local LOOK = { 200, -450 }
local SHOT_EVERY = 12
local SHOTS = 8
local placed, shot = nil, 0

return {
    engineHandlers = {
        onFrame = function()
            local t = core.getRealTime()
            if not placed then
                if t < 8 then return end
                placed = t
                local yaw = math.atan2(LOOK[1] - VIEW[1], LOOK[2] - VIEW[2])
                core.sendGlobalEvent('N4BPlace', { x = VIEW[1], y = VIEW[2], z = VIEW[3], yaw = yaw })
                return
            end
            if shot < SHOTS and t - placed >= 0.5 + shot * SHOT_EVERY then
                shot = shot + 1
                require('openmw.debug').takeScreenshot()
                print('N5B pack shot ' .. shot)
            end
        end,
    },
}
