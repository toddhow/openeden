-- tools/neutral_session_check (task 0125): the player side of the E2 check. Takes pictures on request, walks forward on
-- request, and logs the player's height against the first thing a ray straight down finds (the physics heightfield), so
-- "stands on the ground" is two numbers; every 60 frames while walking it logs the same.
local self = require('openmw.self')
local nearby = require('openmw.nearby')
local util = require('openmw.util')
local debug = require('openmw.debug')
local core = require('openmw.core')

local walking = false
local frame = 0

local function ray(kind)
    local p = self.object.position
    local hit = nearby.castRay(p + util.vector3(0, 0, 20), p - util.vector3(0, 0, 600),
        { ignore = self.object, collisionType = kind })
    if hit.hit then return hit.hitPos.z end
    return nil
end

local function fmt(z) return z and string.format('%.2f', z) or 'none' end

local function line(tag)
    local p = self.object.position
    local land = ray(nearby.COLLISION_TYPE.HeightMap)
    local objects = ray(nearby.COLLISION_TYPE.World)
    local c = self.object.cell
    -- the height the DRAWN terrain (the neutral storage) has under the player, against the physics heightfield's
    local ok, visual = pcall(function() return core.land.getHeightAt(p, c) end)
    local drawn = ok and visual or nil
    print(string.format('E2 %s drawn=%s drawn_minus_land=%s', tag, fmt(drawn), (drawn and land) and string.format('%.2f', drawn - land) or 'n/a'))
    print(string.format('E2 %s grid=%s,%s pos=%.1f,%.1f,%.2f land=%s above_land=%s objects=%s', tag, tostring(c.gridX), tostring(c.gridY), p.x, p.y, p.z, fmt(land), land and string.format('%.2f', p.z - land) or 'n/a', fmt(objects)))
end

return {
    eventHandlers = {
        E2Shot = function() debug.takeScreenshot() end,
        E2Probe = function() line('probe') end,
        E2Walk = function(on)
            walking = on
            if not on then self.controls.movement = 0 end
            line(on and 'walk-start' or 'walk-end')
        end,
    },
    engineHandlers = {
        onFrame = function()
            frame = frame + 1
            if walking then
                self.controls.movement = 1
                self.controls.run = true
                if frame % 60 == 0 then line('walking') end
            end
        end,
    },
}
