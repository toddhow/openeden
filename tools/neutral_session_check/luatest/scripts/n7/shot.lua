-- tools/neutral_session_check (task 0181): the player half of the Papyrus demo. Sets the pitch and takes a picture when
-- the global script asks (an event carries the picture's number, which is logged with the simulation time).
local debug = require('openmw.debug')
local camera = require('openmw.camera')

return {
    eventHandlers = {
        N7Pitch = function(pitch)
            camera.setMode(camera.MODE.FirstPerson)
            camera.setPitch(pitch)
        end,
        N7Shot = function()
            debug.takeScreenshot()
        end,
    },
}
