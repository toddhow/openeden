-- tools/neutral_session_check (task 0127 W-S2): the player side of the W2 check.
local debug = require('openmw.debug')
local camera = require('openmw.camera')

local shotNext = false

return {
    eventHandlers = {
        W2Look = function()
            camera.setMode(camera.MODE.FirstPerson)
            camera.setPitch(-0.25)
            print('W2 looking ahead')
        end,
        W2Shot = function()
            shotNext = true
        end,
    },
    engineHandlers = {
        onFrame = function()
            if shotNext then
                shotNext = false
                debug.takeScreenshot()
                print('W2 shot taken')
            end
        end,
    },
}
