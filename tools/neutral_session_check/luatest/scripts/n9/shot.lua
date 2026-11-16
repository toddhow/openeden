-- tools/neutral_session_check (task 0202): the player half of the object demo. Sets the pitch, takes a picture when the
-- global script asks and casts the rays it names: "N9RAY <name> x,y -> hit z <z> object <record id>" per ray, straight down
-- from 300 above the first floor level to 120 below it, against the world's collision.
local debug = require('openmw.debug')
local camera = require('openmw.camera')
local nearby = require('openmw.nearby')
local util = require('openmw.util')

return {
    eventHandlers = {
        N9Pitch = function(pitch)
            camera.setMode(camera.MODE.FirstPerson)
            camera.setPitch(pitch)
        end,
        N9Shot = function()
            debug.takeScreenshot()
        end,
        N9Rays = function(rays)
            for _, r in ipairs(rays) do
                local from = util.vector3(r.x, r.y, 300)
                local to = util.vector3(r.x, r.y, -120)
                local res = nearby.castRay(from, to, { collisionType = nearby.COLLISION_TYPE.World })
                if res.hit then
                    print(string.format('N9RAY %s %.0f,%.0f -> hit z %.1f object %s', r.name, r.x, r.y, res.hitPos.z,
                        res.hitObject and tostring(res.hitObject.recordId) or 'none'))
                else
                    print(string.format('N9RAY %s %.0f,%.0f -> no hit', r.name, r.x, r.y))
                end
            end
        end,
    },
}
