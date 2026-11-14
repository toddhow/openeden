-- tools/neutral_session_check (task 0127 W-S4): the player side of the W5 check. Aims the first person camera at a
-- direction (yaw, pitch in radians) and takes a picture 2 s later (long enough for the fall and for the sky to settle).
local debug = require('openmw.debug')
local core = require('openmw.core')
local camera = require('openmw.camera')

local shotAt = nil

return {
    eventHandlers = {
        W5Aim = function(data)
            -- the player is put 3000 units up for a clear sky: a fall must not end the game (the menu stops the weather)
            if not debug.isGodMode() then debug.toggleGodMode() end
            camera.setMode(camera.MODE.FirstPerson)
            camera.setYaw(data.yaw)
            camera.setPitch(data.pitch)
            shotAt = core.getRealTime() + 2.5
        end,
    },
    engineHandlers = {
        onFrame = function()
            if shotAt ~= nil and core.getRealTime() >= shotAt then
                shotAt = nil
                debug.takeScreenshot()
                print('W5 shot taken')
            end
        end,
    },
}
