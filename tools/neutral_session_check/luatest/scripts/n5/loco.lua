-- tools/neutral_session_check (task 0148): pictures every 0.2 s of a neutral actor of the Bannered Mare while the pack's
-- go-to test rule walks it (SETTINGS_EXTRA: "[extension.skyrimse]\nactor test goto = Hulda,150,-420,66" and
-- "[Game]\nneutral actor locomotion = true"; OPENEDEN_NEUTRAL_LOCO_TRACE=1 in the environment). The player stands at VIEW
-- looking at LOOK. Started by "n5loco".
local core = require('openmw.core')

local VIEW = { 60, -250, 72 } -- the player's place: open floor south-west of the bar's end
local LOOK = { 250, -340 } -- what it looks at: the middle of the route
local SHOT_EVERY = 0.2
local SHOTS = 40
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
                print('N5B loco shot ' .. shot)
            end
        end,
    },
}
