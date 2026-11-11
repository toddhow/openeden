-- tools/neutral_session_check (task 0110): takes a screenshot, turns the player by 60 degrees, waits, and repeats, so
-- one run shows the whole interior. Started only when the check is given "look" as its screenshot argument.
local core = require('openmw.core')
local self = require('openmw.self')
local debug = require('openmw.debug')

local SETTLE = 8   -- real seconds before the first picture (the cell must have drawn)
local SHOTS = 6
local start = nil
local shots = 0
local nextAt = 0
local turning = false

return {
    engineHandlers = {
        onFrame = function()
            start = start or core.getRealTime()
            local t = core.getRealTime() - start - SETTLE
            if turning then
                self.controls.yawChange = 0
                turning = false
            end
            if t < nextAt or shots >= SHOTS then return end
            do
                debug.takeScreenshot()
                shots = shots + 1
                print('N3A4 shot ' .. shots)
                nextAt = t + 1.5
                -- the turn happens at the next due time, 1.5 s later, one frame at a time
                self.controls.yawChange = math.pi / 3
                turning = true
            end
        end,
    },
}
