-- tools/neutral_session_check (task 0135 stage S2): a picture of a neutral actor of the Bannered Mare while the engine's
-- test seam walks it (run the check with OPENEDEN_NEUTRAL_ACTOR_WALK=2,30,0 and OPENEDEN_NEUTRAL_ACTOR_TRACE=1; actor 2
-- stands at about -136, -700 and walks along +X). The player stands DIST units from where the actor starts, on the
-- side of the cell's middle, and a screenshot is taken at SHOTS (seconds after the placement). Started by "n5walk".
local core = require('openmw.core')
local self = require('openmw.self')

local ACTOR = { -136.195, -699.55, 69.1957 }
local DIST = 170
local SHOTS = { 0.8, 2.5, 4.5, 7.0 }
local placed, shot = nil, 0

return {
    engineHandlers = {
        onFrame = function()
            local t = core.getRealTime()
            if not placed then
                if t < 8 then return end
                placed = t
                local cx, cy = -50 - ACTOR[1], -450 - ACTOR[2]
                local len = math.sqrt(cx * cx + cy * cy)
                local px, py = ACTOR[1] + cx / len * DIST, ACTOR[2] + cy / len * DIST
                local yaw = math.atan2(ACTOR[1] + 100 - px, ACTOR[2] - py)
                core.sendGlobalEvent('N4BPlace', { x = px, y = py, z = ACTOR[3] + 2, yaw = yaw })
                return
            end
            if shot < #SHOTS and t - placed >= SHOTS[shot + 1] then
                shot = shot + 1
                require('openmw.debug').takeScreenshot()
                print('N5B walk shot ' .. shot)
            end
        end,
    },
}
