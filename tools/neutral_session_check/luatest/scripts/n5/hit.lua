-- tools/neutral_session_check (task 0171, combat stage S5-3): n5hit stands the player at VIEW looking at LOOK, the open
-- floor at the front of the Bannered Mare where Hulda stands after the pack's go-to test rule (actor test goto = Hulda,
-- 150,-420,66) has walked her there, and takes a picture every SHOT_EVERY seconds of real time from T0 to T1, so that her
-- whole body, feet included, is in view while the melee test rules run (combat test swing / hostile / player swing in
-- SETTINGS_EXTRA). Use at least 4500 frames.
local core = require('openmw.core')

local VIEW = { -60, -180, 72 }
local LOOK = { 144, -398 }
local T0, T1, SHOT_EVERY = 14, 40, 0.25
local placed, nextShot, shots = nil, 0, 0

return {
    engineHandlers = {
        onFrame = function()
            local t = core.getRealTime()
            if not placed then
                if t < 6 then return end
                placed = t
                local yaw = math.atan2(LOOK[1] - VIEW[1], LOOK[2] - VIEW[2])
                core.sendGlobalEvent('N4BPlace', { x = VIEW[1], y = VIEW[2], z = VIEW[3], yaw = yaw })
                nextShot = placed + T0
                return
            end
            if t >= nextShot and t - placed <= T1 then
                nextShot = t + SHOT_EVERY
                shots = shots + 1
                require('openmw.debug').takeScreenshot()
                print('N5HIT shot ' .. shots)
            end
        end,
    },
}
