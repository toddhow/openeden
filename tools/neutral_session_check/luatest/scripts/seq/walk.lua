-- task 0232: a camera in front of the Bannered Mare's upstairs door (wrdoor02 at -256,512,320) taking a picture every
-- SHOT_EVERY seconds of SIMULATION time (so a fixed frame time makes it repeatable) between FROM and TO; prints the
-- simulation time every second so the NACTDOOR lines of the log can be placed. Started by "seqwalk".
local core = require('openmw.core')

local VIEW = { -256, 330, 320 }
local LOOK = { -256, 512 }
local FROM = 64
local TO = 76
local SHOT_EVERY = 0.25
local placed, lastPrint, lastShot = false, -1, -100

return {
    engineHandlers = {
        onFrame = function()
            local t = core.getSimulationTime()
            if not placed then
                if core.getRealTime() < 8 then return end
                placed = true
                local yaw = math.atan2(LOOK[1] - VIEW[1], LOOK[2] - VIEW[2])
                core.sendGlobalEvent('N4BPlace', { x = VIEW[1], y = VIEW[2], z = VIEW[3], yaw = yaw })
                return
            end
            if t - lastPrint >= 1 then
                lastPrint = t
                print(string.format('SEQT %.1f', t))
            end
            if TO > FROM and t >= FROM and t <= TO and t - lastShot >= SHOT_EVERY then
                lastShot = t
                require('openmw.debug').takeScreenshot()
                print(string.format('SEQSHOT %.2f', t))
            end
        end,
    },
}
