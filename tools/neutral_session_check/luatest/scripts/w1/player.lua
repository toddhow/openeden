-- tools/neutral_session_check (task 0127): the player side of the W1 check. Looks up at the sky on request and takes a
-- picture after a delay.
local debug = require('openmw.debug')
local core = require('openmw.core')
local camera = require('openmw.camera')

local shotAt = nil

return {
    eventHandlers = {
        W1Sky = function()
            camera.setMode(camera.MODE.FirstPerson)
            camera.setPitch(-0.7)
            print('W1 looking up')
        end,
        W1ShotLater = function(delay)
            shotAt = core.getRealTime() + delay
        end,
    },
    engineHandlers = {
        onFrame = function()
            if shotAt ~= nil and core.getRealTime() >= shotAt then
                shotAt = nil
                debug.takeScreenshot()
                print('W1 shot')
            end
        end,
    },
}
