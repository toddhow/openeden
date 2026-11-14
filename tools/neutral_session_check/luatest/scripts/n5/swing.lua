-- tools/neutral_session_check (task 0170, combat stage S5-2): stands the player 190 units from Hulda and takes a picture
-- every SHOT_EVERY seconds of real time from START to END, so that a test swing (run with [extension.skyrimse]
-- combat test swing = Hulda, [Game] neutral combat = true and OPENEDEN_NEUTRAL_COMBAT_TRACE=1) is caught
-- mid-swing. Started by "n5swing" (use at least 2500 frames).
local core = require('openmw.core')
local self = require('openmw.self')

local ACTOR = { 336, -416, 64 } -- Hulda
local DIST = 190
local START, END, SHOT_EVERY = 4, 20, 0.25
local placed, nextShot, shots = nil, nil, 0

return {
    engineHandlers = {
        onFrame = function()
            local t = core.getRealTime()
            if not placed then
                if t < 6 then return end
                placed = t
                local cx, cy = -50 - ACTOR[1], -450 - ACTOR[2]
                local len = math.sqrt(cx * cx + cy * cy)
                local px, py = ACTOR[1] + cx / len * DIST, ACTOR[2] + cy / len * DIST
                local yaw = math.atan2(ACTOR[1] - px, ACTOR[2] - py)
                core.sendGlobalEvent('N4BPlace', { x = px, y = py, z = ACTOR[3] + 2, yaw = yaw })
                nextShot = t + START
                return
            end
            if t >= nextShot and t - placed <= END then
                nextShot = t + SHOT_EVERY
                shots = shots + 1
                require('openmw.debug').takeScreenshot()
                print('N5SWING shot ' .. shots .. ' real ' .. string.format('%.2f', t))
            end
        end,
    },
}
