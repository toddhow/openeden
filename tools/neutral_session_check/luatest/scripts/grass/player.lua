local camera = require('openmw.camera')
local debug = require('openmw.debug')

return {
    eventHandlers = {
        GLook = function(e)
            camera.setMode(camera.MODE.FirstPerson)
            camera.setYaw(e.yaw)
            camera.setPitch(e.pitch)
        end,
        GShot = function() debug.takeScreenshot() end,
    },
}
