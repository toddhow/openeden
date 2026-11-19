local camera = require('openmw.camera')
local debug = require('openmw.debug')

return {
    eventHandlers = {
        SkyGiLook = function(e)
            camera.setMode(camera.MODE.FirstPerson)
            camera.setYaw(e.yaw)
            camera.setPitch(e.pitch)
        end,
        SkyGiShot = function() debug.takeScreenshot() end,
    },
}
