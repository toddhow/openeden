-- Task 0042: holds the camera still, a little above the horizon, so that the sky fills the picture. A static camera at
-- the player; used by the `look` option of tools/benchmark.sh (volumetric clouds).
local camera = require('openmw.camera')
local self = require('openmw.self')
local util = require('openmw.util')

local lookUp = -0.35 -- radians above the horizon (the API's pitch is negative upwards)
local lookHeight = 150 -- game units above the player's feet

return {
    engineHandlers = {
        onFrame = function()
            camera.setMode(camera.MODE.Static)
            camera.setStaticPosition(self.position + util.vector3(0, 0, lookHeight))
            camera.setPitch(lookUp)
            camera.setYaw(0.5)
        end,
    },
}
