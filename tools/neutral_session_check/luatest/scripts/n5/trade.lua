-- tools/neutral_session_check (task 0171, combat stage S5-3): stands the player DIST units from Hulda (on the side of the
-- room the swing scenario uses, facing her) and takes a picture every SHOT_EVERY seconds of real time from START to END,
-- while the melee test rules make her attack the player and the player swing at her (SETTINGS_EXTRA: combat test hostile
-- = Hulda, combat test weapon = IronSword, combat test player swing = 2, combat trace = true). Started by "n5trade"
-- (use at least 6000 frames).
local core = require('openmw.core')

local ACTOR = { 336, -416, 64 } -- Hulda
local DIST = 100
local START, END, SHOT_EVERY = 3, 40, 0.5
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
                print('N5TRADE shot ' .. shots .. ' real ' .. string.format('%.2f', t))
            end
        end,
    },
}
