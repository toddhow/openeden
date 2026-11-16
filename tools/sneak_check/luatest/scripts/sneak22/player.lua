-- tools/sneak_check (task 0227): the player half: screenshots and the camera pitch on request.
local debug = require('openmw.debug')
local camera = require('openmw.camera')
return {
    eventHandlers = {
        SneakShot = function() debug.takeScreenshot() end,
        SneakLook = function(pitch) camera.setMode(camera.MODE.FirstPerson); camera.setPitch(pitch) end,
    },
}
