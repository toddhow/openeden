-- tools/neutral_session_check (task 0355 stage g): for each spot of scripts.l55g.spots casts a ray straight down from
-- the spot's own height and prints where it lands (L55GPROBE), so a camera can be put at a sensible height above the floor.
local core = require('openmw.core')
local nearby = require('openmw.nearby')
local util = require('openmw.util')

local ok, spots = pcall(require, 'scripts.l55g.spots')
local start, done = nil, false

return {
    engineHandlers = {
        onFrame = function()
            if done or not ok then return end
            start = start or core.getRealTime()
            if core.getRealTime() - start < 5 then return end
            done = true
            for i, s in ipairs(spots) do
                local from = util.vector3(s[1], s[2], s[3])
                local r = nearby.castRay(from, from - util.vector3(0, 0, 3000), { collisionType = nearby.COLLISION_TYPE.World })
                print(string.format('L55GPROBE %d down from %.0f,%.0f,%.0f hits z %s', i, s[1], s[2], s[3],
                    r and r.hit and string.format('%.0f', r.hitPos.z) or 'nothing'))
            end
        end,
    },
}
