-- tools/neutral_session_check (task 0152): casts physics rays straight down along a line and prints what each hits
-- ("FLOORLINE x y z id"), so the objects on either side of a gap are named. The line: x from LX0 to LX1, y from LY0
-- to LY1 (one of the two ranges is a single value), STEP apart, 8 real seconds after the start.
local core = require('openmw.core')
local nearby = require('openmw.nearby')
local util = require('openmw.util')

local LX0, LX1, LY0, LY1, STEP = -990, -990, -640, -128, 8
local start, done = nil, false
return {
    engineHandlers = {
        onFrame = function()
            if done then return end
            start = start or core.getRealTime()
            if core.getRealTime() - start < 8 then return end
            done = true
            for x = LX0, LX1, STEP do
                for y = LY0, LY1, STEP do
                    local r = nearby.castRay(util.vector3(x, y, 150), util.vector3(x, y, -300),
                        { collisionType = nearby.COLLISION_TYPE.World })
                    if r and r.hit then
                        print(string.format('FLOORLINE %d %d %.1f %s', x, y, r.hitPos.z,
                            r.hitObject and tostring(r.hitObject.recordId) or '?'))
                    else
                        print(string.format('FLOORLINE %d %d MISS', x, y))
                    end
                end
            end
        end,
    },
}
