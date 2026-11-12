-- Task 0139: traces the player every 0.25 s (position, speed from the position change, on-ground, swimming, controls)
-- while the global step script has switched the trace on.
local self = require('openmw.self')
local types = require('openmw.types')
local core = require('openmw.core')
local on, step, last, lastT = false, 0.1, nil, nil
return {
    eventHandlers = { N4DTrace = function(e) on = e.on; step = e.step or 0.1; last = nil end },
    engineHandlers = {
        onUpdate = function(dt)
            if not on then return end
            local t = core.getRealTime()
            if lastT and t - lastT < step then return end
            local p = self.object.position
            local v = { x = 0, y = 0, z = 0 }
            if last then
                local d = t - lastT
                v = { x = (p.x - last.x) / d, y = (p.y - last.y) / d, z = (p.z - last.z) / d }
            end
            last, lastT = p, t
            local c = self.controls
            print(string.format('N4D trace t=%.2f cell=%s pos=%.1f,%.1f,%.1f vel=%.0f,%.0f,%.0f ground=%s swim=%s ctl=%.2f,%.2f,%s',
                t, tostring(self.object.cell.name), p.x, p.y, p.z, v.x, v.y, v.z, tostring(types.Actor.isOnGround(self.object)),
                tostring(types.Actor.isSwimming(self.object)), c.movement, c.sideMovement, tostring(c.jump)))
        end,
    },
}
