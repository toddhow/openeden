-- tools/neutral_session_check (task 0135 stage S4): pictures of a neutral actor of the Bannered Mare while the pack's
-- go-to test rule walks it along a navmesh path (set in SETTINGS_EXTRA: "[extension.skyrimse]\nactor test goto =
-- Hulda,<x>,<y>,<z>", and OPENEDEN_NEUTRAL_ACTOR_TRACE=1 in the environment; the NACTGOTO and NACTWALK lines give the
-- path, the positions and the result). The player stands at VIEW looking at LOOK, and a screenshot is taken every SHOT_EVERY seconds. Started by "n5goto".
local core = require('openmw.core')

local FROM = { 336, -416, 69 } -- Hulda, behind the bar
local TO = { 150, -420, 66 } -- the front of the room; the route goes round the bar and the table at its end
local VIEW = { 60, -250, 72 } -- the player's place: open floor south-west of the bar's end (the camera can end up inside the table nearby: the NPC is in view in the first pictures)
local LOOK = { 250, -340 } -- what it looks at: the middle of the route
local SHOT_EVERY = 1.2
local SHOTS = 9
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
                print('N5B goto shot ' .. shot)
            end
        end,
    },
}
